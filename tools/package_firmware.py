#!/usr/bin/env python3
"""Deterministic schema-2 OTA assets and a separate, never-OTA USB bundle."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zipfile

FLASH_SIZE = 16 * 1024 * 1024
SLOT_SIZE = 4 * 1024 * 1024
PROJECT = "coffee_watch_demo"
VERSION = re.compile(r"v0\.1\.(0|[1-9][0-9]{0,9})\Z")
LEGACY = [("nvs", 1, 2, 0x9000, 0x6000, 0),
          ("phy_init", 1, 1, 0xf000, 0x1000, 0),
          ("factory", 0, 0, 0x10000, SLOT_SIZE, 0)]
LAYOUT = LEGACY[:2] + [("ota_0", 0, 0x10, 0x10000, SLOT_SIZE, 0),
          ("ota_1", 0, 0x11, 0x410000, SLOT_SIZE, 0),
          ("otadata", 1, 0, 0x810000, 0x2000, 0),
          ("storage", 1, 0x82, 0x812000, 0x7ee000, 0)]

def sha(data):
    return hashlib.sha256(data).hexdigest()

def partition_entries(data):
    """Require IDF's MD5-protected table, flags and exact entries checked by caller."""
    if len(data) != 0xc00:
        raise ValueError("partition table must be exactly 0xc00 bytes")
    result = []
    for pos in range(0, len(data), 32):
        row = data[pos:pos + 32]
        if row[:2] == b"\xeb\xeb":
            if row[2:16] != b"\xff" * 14 or row[16:] != hashlib.md5(data[:pos]).digest():
                raise ValueError("partition table MD5 mismatch")
            if data[pos + 32:] != b"\xff" * (len(data) - pos - 32):
                raise ValueError("unexpected partition table trailer")
            return result
        magic, typ, sub, offset, size, name, flags = struct.unpack("<HBBII16sI", row)
        if magic != 0x50aa:
            raise ValueError("missing partition entry or MD5")
        result.append((name.rstrip(b"\0").decode("ascii"), typ, sub, offset, size, flags))
    raise ValueError("missing table checksum")

def image_info(data, app=True):
    """Validate complete unsigned ESP32-S3 image, checksum and appended SHA256."""
    if len(data) < 24 or data[0] != 0xe9 or not 1 <= data[1] <= 16:
        raise ValueError("invalid ESP image")
    if struct.unpack_from("<H", data, 12)[0] != 9:
        raise ValueError("image is not esp32s3")
    if data[3] >> 4 != 4:  # esptool size code 4 = 16 MiB
        raise ValueError("image header must declare 16MiB flash")
    if data[23] != 1:
        raise ValueError("appended image SHA256 required")
    pos, checksum = 24, 0xef
    for _ in range(data[1]):
        if pos + 8 > len(data):
            raise ValueError("truncated segment header")
        _, size = struct.unpack_from("<II", data, pos)
        pos += 8
        if pos + size > len(data):
            raise ValueError("truncated segment")
        for byte in data[pos:pos + size]:
            checksum ^= byte
        pos += size
    end = (pos + 16) & ~15
    if end + 32 != len(data) or data[end - 1] != checksum:
        raise ValueError("invalid checksum, length, or signed/merged image")
    if hashlib.sha256(data[:end]).digest() != data[end:]:
        raise ValueError("embedded image SHA256 mismatch")
    if not app:
        return {}
    if len(data) > SLOT_SIZE or len(data) < 288:
        raise ValueError("app size outside slot limits")
    if struct.unpack_from("<I", data, 28)[0] < 256:
        raise ValueError("first segment does not contain complete app descriptor")
    if struct.unpack_from("<I", data, 32)[0] != 0xabcd5432:
        raise ValueError("missing embedded app descriptor")
    if struct.unpack_from("<I", data, 36)[0] != 0:
        raise ValueError("secure_version unsupported for initial migration")
    def text(offset):
        field = data[offset:offset + 32]
        if b"\0" not in field:
            raise ValueError("unterminated app descriptor field")
        return field.split(b"\0", 1)[0].decode("ascii")
    info = {"version": text(48), "project": text(80), "idf": text(144)}
    if info["project"] != PROJECT or info["idf"] != "v5.5.2":
        raise ValueError("unrecognized app project or IDF version (require v5.5.2)")
    return info

def check_config(config):
    required = {"IDF_TARGET": "esp32s3", "ESPTOOLPY_FLASHSIZE": "16MB",
                "BOOTLOADER_APP_ROLLBACK_ENABLE": True,
                "PARTITION_TABLE_CUSTOM": True,
                "PARTITION_TABLE_OFFSET": 0x8000, "BOOTLOADER_OFFSET_IN_FLASH": 0,
                "ESP_TASK_WDT_EN": True, "ESP_TASK_WDT_INIT": True,
                "ESP_TASK_WDT_PANIC": True, "ESP_TASK_WDT_TIMEOUT_S": 20,
                "MBEDTLS_CERTIFICATE_BUNDLE": True, "MBEDTLS_HAVE_TIME_DATE": True,
                "MBEDTLS_EXTERNAL_MEM_ALLOC": True, "MBEDTLS_HARDWARE_AES": False,
                "SPIRAM_TRY_ALLOCATE_WIFI_LWIP": True, "ESP_WIFI_STATIC_TX_BUFFER": True,
                "ESP_WIFI_STATIC_TX_BUFFER_NUM": 4}
    for key, value in required.items():
        if config.get(key) != value:
            raise ValueError("incompatible sdkconfig: " + key)
    for key in ("SECURE_BOOT", "SECURE_BOOT_V2_ENABLED", "SECURE_FLASH_ENC_ENABLED",
                "BOOTLOADER_APP_ANTI_ROLLBACK", "EFUSE_VIRTUAL",
                "SECURE_SIGNED_APPS_NO_SECURE_BOOT", "SECURE_SIGNED_APPS",
                "BOOTLOADER_SKIP_VALIDATE_ALWAYS", "BOOTLOADER_SKIP_VALIDATE_ON_POWER_ON",
                "BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP"):
        if config.get(key):
            raise ValueError("unsupported sdkconfig: " + key)

def package(build, out, version):
    if not VERSION.fullmatch(version) or int(version.rsplit(".", 1)[1]) > 0xffffffff:
        raise ValueError("version must be canonical v0.1.<N>")
    check_config(json.loads((build / "config/sdkconfig.json").read_text()))
    app = (build / (PROJECT + ".bin")).read_bytes()
    if image_info(app)["version"] != version:
        raise ValueError("compiled PROJECT_VER differs from release tag")
    boot = (build / "bootloader/bootloader.bin").read_bytes()
    image_info(boot, app=False)
    if len(boot) > 0x8000:
        raise ValueError("bootloader overlaps table")
    table = (build / "partition_table/partition-table.bin").read_bytes()
    if partition_entries(table) != LAYOUT:
        raise ValueError("partition schema is not exactly 2")
    initial = (build / "ota_data_initial.bin").read_bytes()
    if initial != b"\xff" * 0x2000:
        raise ValueError("initial otadata must be erased")
    name = "coffee-watch-" + version + ".bin"
    manifest = {"schema": 1, "product": "coffee-watch", "chip": "esp32s3",
                "partition_schema": 2, "version": version, "tag": version,
                "bin": name, "size": len(app), "sha256": sha(app)}
    assets = {name: app, "latest.json": (json.dumps(manifest, indent=2) + "\n").encode()}
    files = {"bootloader.bin": boot, "partition-table.bin": table,
             name: app, "ota_data_initial.bin": initial}
    metadata = {"schema": 1, "purpose": "USB-only-initial-migration",
                "manifest": manifest, "idf": "v5.5.2", "flash_size": FLASH_SIZE,
                "rollback": True, "files": {k: {"size": len(v), "sha256": sha(v)} for k, v in files.items()}}
    files["migration.json"] = (json.dumps(metadata, indent=2) + "\n").encode()
    files["USB-ONLY.txt"] = b"Not an OTA image. Use tools/migrate_ota.py and spec/firmware-update.md. No merged binary. Migration is not atomic.\n"
    import io
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", zipfile.ZIP_DEFLATED) as archive:
        for filename, content in sorted(files.items()):
            info = zipfile.ZipInfo(filename, (2024, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, content)
    assets["coffee-watch-" + version + "-usb-migration.zip"] = stream.getvalue()
    assets["SHA256SUMS"] = "".join(f"{sha(data)}  {filename}\n" for filename, data in sorted(assets.items())).encode()
    out.mkdir(parents=True, exist_ok=True)
    if any(out.iterdir()):
        raise ValueError("output directory must be empty")
    for filename, content in assets.items():
        (out / filename).write_bytes(content)
    return manifest

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(package(args.build_dir, args.out, args.version), indent=2))
    except (ValueError, OSError, UnicodeError) as exc:
        parser.exit(1, f"refused: {exc}\n")

if __name__ == "__main__":
    main()
