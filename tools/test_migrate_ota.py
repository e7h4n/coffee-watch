import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

import migrate_ota as m
import package_firmware as p
from test_package_firmware import build_fixture, image, table

# A separate Python process implements a fake esptool. No serial package or device
# is reachable in these tests. The stub models full-sector erase on write_flash.
STUB = r'''
import json, os
from pathlib import Path
import sys
args = sys.argv[1:]
with open(os.environ['CW_STUB_LOG'], 'a') as log:
    log.write(json.dumps(args) + '\n')
commands = {'flash_id', 'get_security_info', 'read_flash', 'write_flash'}
command = next((a for a in args if a in commands), None)
if command is None:
    raise SystemExit('unexpected esptool command')
args = args[args.index(command)+1:]
flash = Path(os.environ['CW_STUB_FLASH'])
if command == 'flash_id':
    print('Detected flash size: ' + os.environ.get('CW_STUB_SIZE', '16MB'))
elif command == 'get_security_info':
    print(os.environ.get('CW_STUB_SECURITY', 'Flags: 0x00000000 (0b0)\nSecure Boot: Disabled\nFlash Encryption: Disabled'))
elif command == 'read_flash':
    offset, size = int(args[0], 0), int(args[1], 0)
    Path(args[2]).write_bytes(flash.read_bytes()[offset:offset+size])
elif command == 'write_flash':
    from .loader import ESPLoader, WRITE_BLOCK_ATTEMPTS
    assert ESPLoader.WRITE_FLASH_ATTEMPTS == 1 and WRITE_BLOCK_ATTEMPTS == 1, 'unsafe built-in retry policy'
    if args[:6] != ['--flash_size', 'keep', '--flash_mode', 'keep', '--flash_freq', 'keep']:
        raise SystemExit('unsafe header rewrite')
    args = args[6:]
    data = bytearray(flash.read_bytes())
    for address, filename in zip(args[0::2], args[1::2]):
        offset, content = int(address, 0), Path(filename).read_bytes()
        length = (len(content) + 4095) // 4096 * 4096
        data[offset:offset+length] = b'\xff' * length
        data[offset:offset+len(content)] = content
    flash.write_bytes(data)
    if os.environ.get('CW_STUB_FAIL_WRITE'):
        raise SystemExit('simulated uncertain power loss after write')
'''


def legacy_flash():
    flash = bytearray(b"\xff" * p.FLASH_SIZE)
    flash[:len(image(app=False))] = image(app=False)
    flash[0x8000:0x8c00] = table(p.LEGACY)
    flash[0x9000:0xf000] = bytes(range(256)) * 96
    flash[0xf000:0x10000] = bytes(range(256)) * 16
    app = image(version="old-local")
    flash[0x10000:0x10000 + len(app)] = app
    return bytes(flash)


class MigrationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        build_fixture(self.root / "build")
        p.package(self.root / "build", self.root / "dist", "v0.1.42")
        self.bundle = self.root / "dist/coffee-watch-v0.1.42-usb-migration.zip"
        self.digest = p.sha(self.bundle.read_bytes())
        self.flash = self.root / "flash.bin"
        self.original = legacy_flash()
        self.flash.write_bytes(self.original)
        self.log = self.root / "commands.jsonl"
        stub = self.root / "esptool"
        stub.mkdir()
        (stub / "loader.py").write_text('WRITE_BLOCK_ATTEMPTS = 3\nclass ESPLoader:\n    WRITE_FLASH_ATTEMPTS = 2\n')
        import textwrap
        (stub / "__init__.py").write_text(
            'import os\nfrom .loader import ESPLoader\n'
            '__version__ = os.environ.get("CW_STUB_VERSION", "4.11.0")\n'
            'def _main():\n' + textwrap.indent(STUB, '    '))
        self.env = dict(os.environ, PYTHONPATH=str(self.root), CW_STUB_FLASH=str(self.flash), CW_STUB_LOG=str(self.log))

    def run_tool(self, *extra, expected=0, env=None):
        command = [sys.executable, str(Path(m.__file__)), "--port", "STUB-NOT-A-DEVICE",
                   "--bundle", str(self.bundle), "--bundle-sha256", self.digest,
                   "--backup-root", str(self.root / "backups"), *extra]
        result = subprocess.run(command, env=env or self.env, text=True, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def commands(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def assert_no_writes(self):
        self.assertFalse(any("write_flash" in cmd or "erase_flash" in cmd for cmd in self.commands()))
        self.assertEqual(self.flash.read_bytes(), self.original)

    def test_default_inspect_is_read_only_and_private_backups_verified(self):
        result = self.run_tool()
        self.assertIn("DRY RUN", result.stdout)
        self.assert_no_writes()
        receipt = next((self.root / "backups").glob("*/receipt.json"))
        key = next((self.root / "backups").glob("*.receipt-key"))
        evidence = m.verify_backup(receipt.parent, key)
        self.assertEqual(evidence["observation"]["partition_entries"], [list(e) for e in p.LEGACY])
        self.assertEqual(receipt.parent.stat().st_mode & 0o777, 0o700)
        for path in receipt.parent.iterdir():
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        self.assertEqual(key.stat().st_mode & 0o777, 0o600)
        subprocess.run([sys.executable, m.__file__, "--verify-backup", str(receipt.parent), "--receipt-key", str(key)], check=True, capture_output=True)
        receipt.write_text(receipt.read_text().replace("USB", "EVIL"))
        with self.assertRaises(ValueError):
            m.verify_backup(receipt.parent, key)

    def test_apply_only_four_regions_preserves_entire_rest_and_no_reset(self):
        self.run_tool("--apply")
        commands = self.commands()
        writes = [cmd for cmd in commands if "write_flash" in cmd]
        self.assertEqual(len(writes), 1)
        args = writes[0][writes[0].index("write_flash") + 7:]
        self.assertEqual(args[0::2], ["0x0", "0x8000", "0x10000", "0x810000"])
        self.assertTrue(all(cmd[cmd.index("--after") + 1] == "no_reset" for cmd in commands))
        self.assertFalse(any("erase_flash" in cmd for cmd in commands))
        after = self.flash.read_bytes()
        self.assertEqual(after[0x9000:0x10000], self.original[0x9000:0x10000])
        self.assertEqual(after[0x410000:0x810000], self.original[0x410000:0x810000])
        self.assertEqual(after[0x812000:], self.original[0x812000:])
        self.assertEqual(after[0x810000:0x812000], b"\xff" * 0x2000)
        self.assertEqual(p.partition_entries(after[0x8000:0x8c00]), p.LAYOUT)
        applied = next((self.root / "backups").glob("*/applied.json"))
        self.assertEqual(json.loads(applied.read_text())["flash_sha256"], p.sha(after))
        # A retry on schema-2 is not a second first migration.
        self.run_tool("--apply", expected=1)
        self.assertEqual(len([cmd for cmd in self.commands() if "write_flash" in cmd]), 1)

    def test_wrong_flash_security_unknown_or_enabled_refused_after_backup(self):
        for updates in ({"CW_STUB_SIZE": "8MB"}, {"CW_STUB_SECURITY": "unknown"},
                        {"CW_STUB_SECURITY": "Flags: 0x00000001 (0b1)\nSecure Boot: Disabled\nFlash Encryption: Disabled"}):
            with self.subTest(updates=updates):
                self.run_tool("--apply", expected=1, env=dict(self.env, **updates))
                self.assert_no_writes()
        self.assertTrue(list((self.root / "backups").glob("*/flash-16MiB.bin")))

    def test_incompatible_layout_and_non_erased_region(self):
        cases = []
        wrong = bytearray(self.original)
        entries = list(p.LEGACY)
        entries[0] = ("nvs", 1, 2, 0x9000, 0x5000, 0)
        wrong[0x8000:0x8c00] = table(entries)
        cases.append(wrong)
        for offset in (0x410000, 0x810000, 0x812000, p.FLASH_SIZE - 1):
            data = bytearray(self.original)
            data[offset] = 0
            cases.append(data)
        for data in cases:
            with self.subTest(hash=p.sha(data)), self.assertRaises(ValueError):
                m.compatible(bytes(data))
        self.flash.write_bytes(cases[-1])
        self.run_tool("--apply", expected=1)
        self.assertFalse(any("write_flash" in cmd for cmd in self.commands()))

    def test_legacy_app_descriptor_and_flash_config_fail_closed(self):
        for options in ({"chip": 0}, {"project": "other"}, {"idf": "v5.5.1"}, {"flash": 2}, {"secure": 1}):
            data = bytearray(self.original)
            app = image(**options)
            data[0x10000:0x10000 + len(app)] = app
            with self.subTest(options=options), self.assertRaises(ValueError):
                m.compatible(bytes(data))
        data = bytearray(self.original)
        data[0x10000 + 100] ^= 1
        with self.assertRaises(ValueError):
            m.compatible(bytes(data))
        with self.assertRaises(ValueError):
            m.compatible(self.original[:-1])

    def test_backup_only_accepts_unrecognized_data_but_never_apply(self):
        unknown = b"\x00" * p.FLASH_SIZE
        self.flash.write_bytes(unknown)
        self.run_tool("--backup-only")
        self.assertEqual(self.flash.read_bytes(), unknown)
        self.run_tool("--backup-only", "--apply", expected=1)
        self.assertFalse(any("write_flash" in cmd for cmd in self.commands()))

    def test_uncertain_write_never_retried_and_backup_remains(self):
        result = self.run_tool("--apply", expected=1, env=dict(self.env, CW_STUB_FAIL_WRITE="1"))
        self.assertIn("No automatic retry", result.stderr)
        self.assertEqual(sum("write_flash" in cmd for cmd in self.commands()), 1)
        backup = next((self.root / "backups").glob("*/flash-16MiB.bin"))
        self.assertEqual(backup.read_bytes(), self.original)
        self.assertFalse(list((self.root / "backups").glob("*/applied.json")))

    def test_wrong_esptool_version_refuses_before_serial_access(self):
        for version in ("4.13.0", "5.0.0", "unknown"):
            with self.subTest(version=version):
                self.run_tool("--apply", expected=1, env=dict(self.env, CW_STUB_VERSION=version))
                self.assertEqual(self.commands(), [])
                self.assertFalse((self.root / "backups").exists())

    def test_bad_bundle_hash_refuses_before_any_esptool_command(self):
        self.digest = "0" * 64
        self.run_tool("--apply", expected=1)
        self.assertEqual(self.commands(), [])

    def test_bundle_internal_hash_manifest_and_zip_validation(self):
        with zipfile.ZipFile(self.bundle) as archive:
            files = {name: archive.read(name) for name in archive.namelist()}
        for change in ("hash", "size", "tag", "extra", "chip", "bin"):
            modified = dict(files)
            meta = json.loads(files["migration.json"])
            if change == "hash":
                modified["bootloader.bin"] += b"x"
            elif change == "size":
                meta["manifest"]["size"] = p.SLOT_SIZE + 1
            elif change == "extra":
                meta["manifest"]["url"] = "https://evil.invalid/merged.bin"
            else:
                meta["manifest"][change] = "wrong"
            modified["migration.json"] = json.dumps(meta).encode()
            path = self.root / (change + ".zip")
            with zipfile.ZipFile(path, "w") as archive:
                for name, data in modified.items():
                    archive.writestr(name, data)
            with self.subTest(change=change), self.assertRaises(ValueError):
                m.load_bundle(path, p.sha(path.read_bytes()))

    def test_strict_json_duplicate_keys_and_boolean_schema(self):
        with self.assertRaises(ValueError):
            m.strict_json('{"schema":1,"schema":1}')
        with zipfile.ZipFile(self.bundle) as archive:
            files = {name: archive.read(name) for name in archive.namelist()}
        meta = json.loads(files["migration.json"])
        meta["manifest"]["schema"] = True
        files["migration.json"] = json.dumps(meta).encode()
        path = self.root / "boolean.zip"
        with zipfile.ZipFile(path, "w") as archive:
            for name, data in files.items():
                archive.writestr(name, data)
        with self.assertRaises(ValueError):
            m.load_bundle(path, p.sha(path.read_bytes()))

    def test_backup_root_must_be_private_before_any_write(self):
        root = self.root / "backups"
        root.mkdir(mode=0o755)
        root.chmod(0o755)
        self.run_tool("--apply", expected=1)
        self.assert_no_writes()

    def test_candidate_hardware_requirements_must_match(self):
        _, writes = m.load_bundle(self.bundle, self.digest)
        for index, offset in ((0, 2), (0, 14), (2, 3), (2, 208), (2, 212)):
            changed = list(writes)
            address, name, binary = changed[index]
            data = bytearray(binary)
            data[offset] ^= 1
            changed[index] = (address, name, bytes(data))
            with self.subTest(index=index, offset=offset), self.assertRaises(ValueError):
                m.compatible(self.original, changed)

    def test_backup_corruption_detected(self):
        directory, key = m.backup(self.root / "separate", self.original, {})
        (directory / "nvs-private.bin").write_bytes(b"\0" * 0x6000)
        with self.assertRaises(ValueError):
            m.verify_backup(directory, key)

    def test_offline_inspection_and_apply_forbidden(self):
        base = [sys.executable, m.__file__, "--offline-flash", str(self.flash), "--bundle", str(self.bundle),
                "--bundle-sha256", self.digest, "--backup-root", str(self.root / "offline")]
        result = subprocess.run(base, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run(base + ["--apply"], text=True, capture_output=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(self.commands(), [])


if __name__ == "__main__":
    unittest.main()
