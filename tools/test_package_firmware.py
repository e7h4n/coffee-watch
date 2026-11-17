import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

import package_firmware as p


def image(version="v0.1.42", project=p.PROJECT, idf="v5.5.2", app=True,
          chip=9, flash=4, secure=0, payload_size=512):
    header = bytearray(24)
    header[0:4] = bytes([0xe9, 1, 2, flash << 4 | 0xf])
    struct.pack_into("<H", header, 12, chip)
    header[17:19] = b"\x63\x00"
    header[23] = 1
    payload = bytearray(payload_size)
    if app:
        struct.pack_into("<II", payload, 0, 0xabcd5432, secure)
        for offset, value in ((16, version), (48, project), (112, idf)):
            encoded = value.encode()
            payload[offset:offset + len(encoded)] = encoded
    data = header + struct.pack("<II", 0x3c000020, len(payload)) + payload
    checksum = 0xef
    for byte in payload:
        checksum ^= byte
    data += b"\0" * ((-len(data) - 1) % 16) + bytes([checksum])
    return bytes(data + hashlib.sha256(data).digest())


def table(entries):
    data = b"".join(struct.pack("<HBBII16sI", 0x50aa, typ, sub, off, size, name.encode(), flags)
                    for name, typ, sub, off, size, flags in entries)
    data += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(data).digest()
    return data.ljust(0xc00, b"\xff")


def build_fixture(root, **app_options):
    root.mkdir()
    (root / "config").mkdir()
    (root / "bootloader").mkdir()
    (root / "partition_table").mkdir()
    config = {"IDF_TARGET": "esp32s3", "ESPTOOLPY_FLASHSIZE": "16MB",
              "BOOTLOADER_APP_ROLLBACK_ENABLE": True, "PARTITION_TABLE_CUSTOM": True,
              "PARTITION_TABLE_OFFSET": 0x8000, "BOOTLOADER_OFFSET_IN_FLASH": 0,
              "ESP_TASK_WDT_EN": True,
              "ESP_TASK_WDT_INIT": True, "ESP_TASK_WDT_PANIC": True,
              "ESP_TASK_WDT_TIMEOUT_S": 20, "MBEDTLS_CERTIFICATE_BUNDLE": True,
              "MBEDTLS_HAVE_TIME_DATE": True,
              "MBEDTLS_EXTERNAL_MEM_ALLOC": True, "MBEDTLS_HARDWARE_AES": False,
              "SPIRAM_TRY_ALLOCATE_WIFI_LWIP": True, "ESP_WIFI_STATIC_TX_BUFFER": True,
              "ESP_WIFI_STATIC_TX_BUFFER_NUM": 4}
    (root / "config/sdkconfig.json").write_text(json.dumps(config))
    (root / (p.PROJECT + ".bin")).write_bytes(image(**app_options))
    (root / "bootloader/bootloader.bin").write_bytes(image(app=False))
    (root / "partition_table/partition-table.bin").write_bytes(table(p.LAYOUT))
    (root / "ota_data_initial.bin").write_bytes(b"\xff" * 0x2000)
    return config


class PackageTests(unittest.TestCase):
    def test_deterministic_strict_manifest_and_separate_usb(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_fixture(root / "build")
            one, two = root / "one", root / "two"
            manifest = p.package(root / "build", one, "v0.1.42")
            p.package(root / "build", two, "v0.1.42")
            self.assertEqual(set(manifest), {"schema", "product", "chip", "partition_schema", "version", "tag", "bin", "size", "sha256"})
            self.assertEqual(manifest["bin"], "coffee-watch-v0.1.42.bin")
            self.assertEqual(len(list(one.iterdir())), 4)
            for path in one.iterdir():
                self.assertEqual(path.read_bytes(), (two / path.name).read_bytes())
            for line in (one / "SHA256SUMS").read_text().splitlines():
                digest, name = line.split("  ")
                self.assertEqual(digest, p.sha((one / name).read_bytes()))
            with zipfile.ZipFile(one / "coffee-watch-v0.1.42-usb-migration.zip") as archive:
                self.assertEqual(set(archive.namelist()), {manifest["bin"], "migration.json", "USB-ONLY.txt", "bootloader.bin", "partition-table.bin", "ota_data_initial.bin"})

    def test_release_version_canonical_and_compiled(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_fixture(root / "build")
            for version in ("dev", "v0.1.042", "v0.1.43", "v0.1.42-dirty", "v0.1.42\n", "v0.1.4294967296", "v0.1." + "9" * 100):
                with self.subTest(version=version), self.assertRaises(ValueError):
                    p.package(root / "build", root / "out", version)

    def test_invalid_images(self):
        candidates = [image(chip=0), image(flash=2), image(project="other"),
                      image(idf="v5.5.1"), image(secure=1), image()[:-1],
                      image() + b"\xff" * 16, b"\xff" * 100,
                      image(payload_size=p.SLOT_SIZE), image(version="x" * 32)]
        corrupt = bytearray(image())
        corrupt[100] ^= 1
        candidates.append(bytes(corrupt))
        digest_corrupt = bytearray(image())
        digest_corrupt[-1] ^= 1
        candidates.append(bytes(digest_corrupt))
        for data in candidates:
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                p.image_info(data)

    def test_table_checksum_flags_exact_layout(self):
        self.assertEqual(p.partition_entries(table(p.LAYOUT)), p.LAYOUT)
        bad = bytearray(table(p.LAYOUT))
        bad[10] ^= 1
        with self.assertRaises(ValueError):
            p.partition_entries(bad)
        with self.assertRaises(ValueError):
            p.partition_entries(table(p.LAYOUT)[:-1])
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_fixture(root / "build")
            (root / "build/partition_table/partition-table.bin").write_bytes(table(p.LEGACY))
            with self.assertRaises(ValueError):
                p.package(root / "build", root / "out", "v0.1.42")

    def test_security_and_watchdog_configuration(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            config = build_fixture(root / "build")
            for key, value in [("BOOTLOADER_APP_ROLLBACK_ENABLE", False), ("ESPTOOLPY_FLASHSIZE", "8MB"),
                               ("ESP_TASK_WDT_PANIC", False), ("ESP_TASK_WDT_TIMEOUT_S", 5),
                               ("SECURE_BOOT", True), ("SECURE_FLASH_ENC_ENABLED", True),
                               ("BOOTLOADER_APP_ANTI_ROLLBACK", True), ("EFUSE_VIRTUAL", True),
                               ("BOOTLOADER_SKIP_VALIDATE_ALWAYS", True),
                               ("MBEDTLS_HAVE_TIME_DATE", False),
                               ("MBEDTLS_EXTERNAL_MEM_ALLOC", False), ("MBEDTLS_HARDWARE_AES", True),
                               ("SPIRAM_TRY_ALLOCATE_WIFI_LWIP", False), ("ESP_WIFI_STATIC_TX_BUFFER", False),
                               ("ESP_WIFI_STATIC_TX_BUFFER_NUM", 8)]:
                with self.subTest(key=key), self.assertRaises(ValueError):
                    p.check_config(dict(config, **{key: value}))

    def test_erased_otadata_required_and_output_never_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_fixture(root / "build")
            p.package(root / "build", root / "out", "v0.1.42")
            with self.assertRaises(ValueError):
                p.package(root / "build", root / "out", "v0.1.42")
            (root / "build/ota_data_initial.bin").write_bytes(b"\0" * 0x2000)
            with self.assertRaises(ValueError):
                p.package(root / "build", root / "other", "v0.1.42")


if __name__ == "__main__":
    unittest.main()
