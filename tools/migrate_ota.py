#!/usr/bin/env python3
"""Fail-closed, USB-only schema-1 -> schema-2 migration. Default: inspect only.

No erase_flash, eFuse, encryption, signing or reset operations. See spec/firmware-update.md.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import hmac
import json
import os
from pathlib import Path
import re
import secrets
import subprocess
import sys
import tempfile
import zipfile

from package_firmware import (FLASH_SIZE, LEGACY, LAYOUT, SLOT_SIZE, VERSION,
                              image_info, partition_entries, sha)


def strict_json(raw):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate JSON key: " + key)
            result[key] = value
        return result
    return json.loads(raw, object_pairs_hook=unique)


def load_bundle(path, digest):
    raw = path.read_bytes()
    if not digest or not re.fullmatch(r"[0-9a-f]{64}", digest) or sha(raw) != digest:
        raise ValueError("supply independently verified --bundle-sha256; bundle mismatch")
    if len(raw) > 8 * 1024 * 1024:
        raise ValueError("oversized bundle")
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)) or any(i.file_size > SLOT_SIZE for i in archive.infolist()):
            raise ValueError("duplicate/oversized ZIP entry")
        meta = strict_json(archive.read("migration.json"))
        manifest = meta["manifest"]
        version = manifest["version"]
        app_name = "coffee-watch-" + version + ".bin"
        expected_manifest = {"schema": 1, "product": "coffee-watch", "chip": "esp32s3",
                             "partition_schema": 2, "version": version, "tag": version,
                             "bin": app_name, "size": manifest["size"], "sha256": manifest["sha256"]}
        if (manifest != expected_manifest or not VERSION.fullmatch(version)
                or int(version.rsplit(".", 1)[1]) > 0xffffffff
                or type(manifest["schema"]) is not int or type(manifest["partition_schema"]) is not int
                or type(manifest["size"]) is not int or not 0 < manifest["size"] <= SLOT_SIZE
                or not re.fullmatch(r"[0-9a-f]{64}", manifest["sha256"])):
            raise ValueError("invalid strict manifest")
        expected = {"bootloader.bin", "partition-table.bin", "ota_data_initial.bin", app_name}
        if set(names) != expected | {"migration.json", "USB-ONLY.txt"} or set(meta["files"]) != expected:
            raise ValueError("unexpected USB bundle files")
        if (set(meta) != {"schema", "purpose", "manifest", "idf", "flash_size", "rollback", "files"}
                or type(meta["schema"]) is not int or meta["schema"] != 1
                or type(meta["flash_size"]) is not int or meta["purpose"] != "USB-only-initial-migration"
                or meta["idf"] != "v5.5.2" or meta["flash_size"] != FLASH_SIZE or meta["rollback"] is not True):
            raise ValueError("unsupported USB bundle configuration")
        files = {name: archive.read(name) for name in expected}
    for name, data in files.items():
        if meta["files"][name] != {"size": len(data), "sha256": sha(data)}:
            raise ValueError("bundle file hash/size mismatch: " + name)
    app = files[app_name]
    if len(app) != manifest["size"] or sha(app) != manifest["sha256"] or image_info(app)["version"] != version:
        raise ValueError("app manifest/descriptor mismatch")
    image_info(files["bootloader.bin"], app=False)
    if len(files["bootloader.bin"]) > 0x8000 or partition_entries(files["partition-table.bin"]) != LAYOUT:
        raise ValueError("invalid bootloader/table")
    if files["ota_data_initial.bin"] != b"\xff" * 0x2000:
        raise ValueError("otadata must be erased")
    return meta, [(0, "bootloader.bin", files["bootloader.bin"]),
                  (0x8000, "partition-table.bin", files["partition-table.bin"]),
                  (0x10000, app_name, app),
                  (0x810000, "ota_data_initial.bin", files["ota_data_initial.bin"])]


def trimmed_image(data):
    # Determine the original unsigned image's exact length from segment records.
    if data[:1] != b"\xe9" or not 1 <= data[1] <= 16:
        raise ValueError("legacy app missing")
    import struct
    pos = 24
    for _ in range(data[1]):
        if pos + 8 > len(data):
            raise ValueError("legacy image truncated")
        size = struct.unpack_from("<I", data, pos + 4)[0]
        pos += 8 + size
        if pos > SLOT_SIZE:
            raise ValueError("legacy image exceeds slot")
    end = ((pos + 16) & ~15) + 32
    return data[:end]


def compatible(flash, writes=None):
    if len(flash) != FLASH_SIZE:
        raise ValueError("readback must be exactly 16MiB")
    if partition_entries(flash[0x8000:0x8c00]) != LEGACY:
        raise ValueError("existing table is not the exact supported legacy factory layout")
    old_app = trimmed_image(flash[0x10000:0x410000])
    info = image_info(old_app)
    old_boot = trimmed_image(flash[:0x8000])
    image_info(old_boot, app=False)
    if writes:
        new_boot, new_app = writes[0][2], writes[2][2]
        for old, new in ((old_boot, new_boot), (old_app, new_app)):
            if old[2:4] != new[2:4] or old[14:19] != new[14:19]:
                raise ValueError("flash mode/frequency or silicon revision requirements differ")
        if old_app[208:213] != new_app[208:213]:
            raise ValueError("eFuse block revision / MMU page-size requirements differ")
    if flash[0x410000:] != b"\xff" * (FLASH_SIZE - 0x410000):
        raise ValueError("new OTA/data region is non-erased; preserve it and stop")
    return info


def private_write(path, data):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    if path.read_bytes() != data:
        raise ValueError("backup readback failed")


def backup(root, flash, observation):
    if len(flash) != FLASH_SIZE:
        raise ValueError("cannot back up incomplete flash")
    root.mkdir(mode=0o700, parents=True, exist_ok=True)
    if root.is_symlink() or root.stat().st_uid != os.getuid() or root.stat().st_mode & 0o077:
        raise ValueError("backup root must be an owned, private (0700), non-symlink directory")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    directory = root / (stamp + "-" + secrets.token_hex(4))
    directory.mkdir(mode=0o700)
    contents = {"flash-16MiB.bin": flash, "nvs-private.bin": flash[0x9000:0xf000],
                "phy-private.bin": flash[0xf000:0x10000]}
    for name, data in contents.items():
        private_write(directory / name, data)
    receipt = {"schema": 1, "observed_at": stamp, "observation": observation,
               "files": {name: {"size": len(data), "sha256": sha(data)} for name, data in contents.items()}}
    payload = (json.dumps(receipt, sort_keys=True, indent=2) + "\n").encode()
    # Local authentication, NOT publisher authentication. Keep key separately/offline.
    key_path = root / (directory.name + ".receipt-key")
    key = secrets.token_bytes(32)
    private_write(key_path, key)
    private_write(directory / "receipt.json", payload)
    private_write(directory / "receipt.hmac", (hmac.new(key, payload, hashlib.sha256).hexdigest() + "\n").encode())
    verify_backup(directory, key_path)
    # Persist directory entries too, before authorizing potentially destructive writes.
    for path in (directory, root):
        fd = os.open(path, os.O_RDONLY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)
    return directory, key_path


def verify_backup(directory, key_path):
    payload = (directory / "receipt.json").read_bytes()
    key = key_path.read_bytes()
    signature = (directory / "receipt.hmac").read_text().strip()
    if len(key) != 32 or not hmac.compare_digest(signature, hmac.new(key, payload, hashlib.sha256).hexdigest()):
        raise ValueError("backup receipt authentication failed")
    receipt = json.loads(payload)
    sizes = {"flash-16MiB.bin": FLASH_SIZE, "nvs-private.bin": 0x6000, "phy-private.bin": 0x1000}
    if set(receipt["files"]) != set(sizes):
        raise ValueError("invalid backup receipt files")
    for name, size in sizes.items():
        data = (directory / name).read_bytes()
        if len(data) != size or receipt["files"][name] != {"size": size, "sha256": sha(data)}:
            raise ValueError("backup hash/size mismatch")
    flash = (directory / "flash-16MiB.bin").read_bytes()
    if (flash[0x9000:0xf000] != (directory / "nvs-private.bin").read_bytes()
            or flash[0xf000:0x10000] != (directory / "phy-private.bin").read_bytes()):
        raise ValueError("private backups do not match full flash")
    return receipt


# esptool 4.11 defaults to TWO per-file write attempts after SerialException,
# potentially reconnecting/resetting/replaying a partially completed write.
# Pin and disable that behavior inside EVERY subprocess before serial access.
ESPTOOL_ADAPTER = '''
import sys
import esptool
if getattr(esptool, "__version__", None) != "4.11.0":
    raise SystemExit("require exactly esptool 4.11.0; no serial access attempted")
from esptool import loader
loader.ESPLoader.WRITE_FLASH_ATTEMPTS = 1
loader.WRITE_BLOCK_ATTEMPTS = 1
if sys.argv[1:] == ["--cw-version-check"]:
    print("esptool 4.11.0; WRITE_FLASH_ATTEMPTS=1")
else:
    esptool._main()
'''


class Esptool:
    def __init__(self, port, baud=460800):
        adapter = [sys.executable, "-c", ESPTOOL_ADAPTER]
        subprocess.run(adapter + ["--cw-version-check"], check=True, text=True,
                       capture_output=True, timeout=30)
        self.prefix = adapter + ["--chip", "esp32s3", "--port", port,
                                 "--baud", str(baud),
                                 "--before", "default_reset", "--after", "no_reset"]

    def run(self, *args):
        result = subprocess.run(self.prefix + list(args), check=True, text=True, capture_output=True, timeout=1800)
        return result.stdout


def security_check(flash_id, security):
    if not re.search(r"^Detected flash size: 16MB\s*$", flash_id, re.M):
        raise ValueError("observed flash is not 16MB")
    if (not re.search(r"^Flags: 0x00000000 \(0b0\)\s*$", security, re.M)
            or not re.search(r"^Secure Boot: Disabled\s*$", security, re.M)
            or not re.search(r"^Flash Encryption: Disabled\s*$", security, re.M)):
        raise ValueError("security configuration unknown or unsupported; no writes")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--baud", type=int, choices=(115200, 230400, 460800, 921600), default=460800)
    parser.add_argument("--offline-flash", type=Path, help="inspect a full dump, never writes hardware")
    parser.add_argument("--bundle", type=Path)
    parser.add_argument("--bundle-sha256")
    parser.add_argument("--backup-root", type=Path, default=Path("private-migration-backups"))
    parser.add_argument("--backup-only", action="store_true", help="save raw evidence without compatibility approval")
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--verify-backup", type=Path)
    parser.add_argument("--receipt-key", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.verify_backup:
            if not args.receipt_key or args.apply:
                raise ValueError("verification requires --receipt-key and forbids --apply")
            verify_backup(args.verify_backup, args.receipt_key)
            print("Backup hashes and local HMAC receipt verified.")
            return 0
        if args.apply and (args.offline_flash or args.backup_only):
            raise ValueError("--apply cannot be offline or backup-only")
        if bool(args.port) == bool(args.offline_flash):
            raise ValueError("choose exactly one --port or --offline-flash")
        if not args.backup_only and not args.bundle:
            raise ValueError("--bundle required (or choose --backup-only)")
        bundle = load_bundle(args.bundle, args.bundle_sha256) if args.bundle else None
        with tempfile.TemporaryDirectory(prefix="cw-migrate-") as scratch:
            scratch = Path(scratch)
            observation = {"source": "offline-unverified"}
            if args.offline_flash:
                flash = args.offline_flash.read_bytes()
            else:
                tool = Esptool(args.port, args.baud)
                fid, security = tool.run("flash_id"), tool.run("get_security_info")
                observation = {"source": "USB", "flash_id": fid, "security_info": security}
                tool.run("read_flash", "0x0", hex(FLASH_SIZE), str(scratch / "before.bin"))
                flash = (scratch / "before.bin").read_bytes()
            if bundle:
                observation["candidate"] = {"bundle_sha256": args.bundle_sha256,
                                            "version": bundle[0]["manifest"]["version"],
                                            "partition_schema": 2, "target_entries": LAYOUT}
            # Bind decoded observed layout to the authenticated local receipt,
            # even when unsupported. Backup-only is always available for evidence.
            try:
                observation["partition_entries"] = partition_entries(flash[0x8000:0x8c00])
            except (ValueError, UnicodeError):
                observation["partition_entries"] = "unrecognized"
            directory, key = backup(args.backup_root, flash, observation)
            print(f"Verified private backup: {directory}\nLocal authentication key (keep separately): {key}")
            if args.backup_only:
                print("Backup only; compatibility NOT approved; no writes.")
                return 0
            info = compatible(flash, bundle[1])
            if not args.offline_flash:
                security_check(fid, security)
            print("Legacy layout and app compatible: " + json.dumps(info))
            if not args.apply:
                print("DRY RUN: no writes. Offline inspection does not prove live security/flash configuration.")
                return 0
            verify_backup(directory, key)  # Immediately before any write.
            writes = []
            expected = bytearray(flash)
            for offset, name, data in bundle[1]:
                path = scratch / name
                path.write_bytes(data)
                writes += [hex(offset), str(path)]
                # esptool erases complete 4KiB sectors, including final-sector padding.
                padded = data + b"\xff" * ((-len(data)) % 0x1000)
                expected[offset:offset + len(padded)] = padded
            # Never retry this operation automatically. The bootloader remains in USB download mode.
            tool.run("write_flash", "--flash_size", "keep", "--flash_mode", "keep", "--flash_freq", "keep", *writes)
            tool.run("read_flash", "0x0", hex(FLASH_SIZE), str(scratch / "after.bin"))
            after = (scratch / "after.bin").read_bytes()
            if after != bytes(expected):
                raise ValueError("post-write full-flash comparison failed; DO NOT BOOT; USB recovery required")
            private_write(directory / "applied.json", (json.dumps({"bundle_sha256": args.bundle_sha256,
                          "version": bundle[0]["manifest"]["version"], "flash_sha256": sha(after)}) + "\n").encode())
            print("Verified all 16MiB, including unchanged NVS/PHY/storage/ota1. Manually reset to boot ota_0.")
            return 0
    except (ValueError, OSError, KeyError, TypeError, UnicodeError, zipfile.BadZipFile,
            subprocess.SubprocessError) as exc:
        print(f"REFUSED/STOPPED: {exc}. No automatic retry. See USB recovery documentation.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
