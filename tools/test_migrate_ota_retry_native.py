"""Exercise esptool 4.11.0's REAL per-file retry loop without any device.

Source: esptool/loader.py ESPLoader.WRITE_FLASH_ATTEMPTS (default 2), and
esptool/cmds.py write_flash: original_image, per-attempt flash[_defl]_begin,
SerialException recovery (close/open/connect/run_stub), and original_image replay.
Only loader I/O is fake: write_flash and the migration's ESPTOOL_ADAPTER are not.
Missing/wrong esptool is a test FAILURE, never a skip. Install esptool==4.11.0.
"""
import contextlib
import hashlib
import io
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zlib

import esptool
from esptool import cmds, loader as loader_module
from esptool.loader import ESPLoader
from esptool.targets.esp32s3 import ESP32S3ROM
import serial
from serial import SerialException

from migrate_ota import ESPTOOL_ADAPTER


class FakePort:
    def __init__(self, events):
        self.events = events

    def close(self):
        self.events.append("port.close")

    def open(self):
        self.events.append("port.open")


class PartialTransferLoader(ESP32S3ROM):
    """No superclass init, serial port, registers, device, or actual resets.

    First block is acknowledged; second block raises once. A default-policy
    reconnect then succeeds, exposing actual upstream replay of block zero.
    Inherit WRITE_FLASH_ATTEMPTS rather than shadowing the adapter's setting.
    """
    FLASH_WRITE_SIZE = 256
    IS_STUB = True

    def __init__(self):
        self.secure_download_mode = False
        self.events = []
        self.blocks = []
        self.accepted = []
        self.failed = False
        self.error = SerialException("injected link loss after acknowledged first block")
        self._port = FakePort(self.events)

    def get_secure_boot_enabled(self):
        return False

    def flash_id(self):
        return 0x1840EF  # 16 MiB, consumed by real detect_flash_size().

    def flash_begin(self, size, address, **kwargs):
        self.events.append("flash.begin")
        return (size + self.FLASH_WRITE_SIZE - 1) // self.FLASH_WRITE_SIZE

    def flash_defl_begin(self, size, compressed_size, address):
        self.events.append("flash.defl_begin")
        return (compressed_size + self.FLASH_WRITE_SIZE - 1) // self.FLASH_WRITE_SIZE

    def flash_block(self, block, seq, **kwargs):
        self.blocks.append((seq, bytes(block)))
        if seq == 1 and not self.failed:
            self.failed = True
            raise self.error
        self.accepted.append((seq, bytes(block)))

    flash_defl_block = flash_block

    def connect(self, *args, **kwargs):
        self.events.append("connect")  # Native recovery calls this reset-capable API.

    def run_stub(self, *args, **kwargs):
        self.events.append("run_stub")
        self.IS_STUB = True
        return self

    def hard_reset(self, *args, **kwargs):
        self.events.append("hard_reset")

    def soft_reset(self, *args, **kwargs):
        self.events.append("soft_reset")

    def flash_finish(self, *args, **kwargs):
        self.events.append("finish")

    flash_defl_finish = flash_finish

    def flash_md5sum(self, address, size):
        self.events.append("verify")
        return hashlib.md5(PAYLOAD).hexdigest()


# Incompressible, reproducible, multiple blocks in BOTH transfer modes.
PAYLOAD = b"".join(hashlib.sha256(str(i).encode()).digest() for i in range(128))


class NativeRetryTests(unittest.TestCase):
    def setUp(self):
        self.assertEqual(esptool.__version__, "4.11.0", "requires exact audited esptool")
        self.assertEqual(ESPLoader.WRITE_FLASH_ATTEMPTS, 2, "upstream baseline changed")

    def exercise(self, policy, compressed):
        loader = PartialTransferLoader()
        image = io.BytesIO(PAYLOAD)
        image.name = "transport-test.bin"
        args = SimpleNamespace(
            compress=None if compressed else False, no_compress=not compressed,
            no_stub=False, force=True, encrypt=False, encrypt_files=None,
            flash_size="keep", flash_mode="keep", flash_freq="keep",
            erase_all=False, addr_filename=[(0x10000, image)],
        )
        receipts = []
        output = io.StringIO()

        def operation():
            self.assertEqual(loader.WRITE_FLASH_ATTEMPTS, 1 if policy != "default" else 2)
            cmds.write_flash(loader, args)  # REAL installed upstream implementation.
            receipts.append("operation succeeded")  # Unreachable on uncertain writes.

        # Never instantiate a real serial transport, even if the harness regresses.
        with contextlib.ExitStack() as stack:
            stack.enter_context(patch.object(serial, "Serial", side_effect=AssertionError("device access")))
            stack.enter_context(patch.object(serial, "serial_for_url", side_effect=AssertionError("device access")))
            stack.enter_context(patch.object(ESPLoader, "__init__", side_effect=AssertionError("real loader init")))
            stack.enter_context(patch.object(cmds.time, "sleep"))
            stack.enter_context(patch.object(ESPLoader, "WRITE_FLASH_ATTEMPTS", 2))
            stack.enter_context(patch.object(loader_module, "WRITE_BLOCK_ATTEMPTS", 3))
            stack.enter_context(contextlib.redirect_stdout(output))
            if policy == "controlled":
                ESPLoader.WRITE_FLASH_ATTEMPTS = 1
            if policy == "adapter":
                # Execute the exact production adapter's non-version-check branch.
                # Replace only CLI/device discovery, not the write implementation.
                stack.enter_context(patch.object(esptool, "_main", side_effect=operation))
                stack.enter_context(patch("sys.argv", ["adapter", "write_flash"]))
                invoke = lambda: exec(ESPTOOL_ADAPTER, {"__name__": "__main__"})
            else:
                invoke = operation
            if policy == "default":
                invoke()
            else:
                with self.assertRaises(SerialException) as raised:
                    invoke()
                self.assertIs(raised.exception, loader.error)

        begin = "flash.defl_begin" if compressed else "flash.begin"
        if policy == "default":
            self.assertEqual(loader.events, [begin, "port.close", "port.open", "connect",
                                             "run_stub", begin, "finish", "verify"])
            self.assertEqual([seq for seq, _ in loader.blocks[:3]], [0, 1, 0])
            self.assertEqual(loader.blocks[0], loader.blocks[2], "retry must replay original block zero")
            replay = b"".join(block for _, block in loader.blocks[2:])
            self.assertEqual(zlib.decompress(replay) if compressed else replay, PAYLOAD)
            self.assertEqual(receipts, ["operation succeeded"])
            self.assertIn("Lost connection, retrying", output.getvalue())
            self.assertIn("Hash of data verified.", output.getvalue())
        else:
            # Exact event list rules out finish/verify, reopen, reconnect, stub
            # upload, either reset API, or a second begin/replay of any kind.
            self.assertEqual(loader.events, [begin])
            self.assertEqual([seq for seq, _ in loader.blocks], [0, 1])
            self.assertEqual(loader.accepted, [loader.blocks[0]])
            self.assertEqual(receipts, [])
            for success in ("Lost connection, retrying", "Wrote ", "Hash of data verified.", "Leaving..."):
                self.assertNotIn(success, output.getvalue())
        self.assertTrue(loader.failed, "must reach injected partial-transfer failure")
        self.assertEqual(ESPLoader.WRITE_FLASH_ATTEMPTS, 2, "test leaked global policy")

    def test_production_adapter_disables_real_block_retries(self):
        # Actual low-level upstream methods also retry ambiguous FatalError ACKs.
        # Verify the adapter disables both compressed and raw block replay.
        for compressed in (False, True):
            with self.subTest(compressed=compressed):
                fake = PartialTransferLoader()
                attempts = []
                error = esptool.FatalError("injected lost block acknowledgement")
                def check(*args, **kwargs):
                    attempts.append(args)
                    raise error
                fake.check_command = check
                method = ESPLoader.flash_defl_block if compressed else ESPLoader.flash_block
                def operation():
                    self.assertEqual(loader_module.WRITE_BLOCK_ATTEMPTS, 1)
                    with self.assertRaises(esptool.FatalError) as raised:
                        method(fake, b"payload", 0)
                    self.assertIs(raised.exception, error)
                with patch.object(loader_module, "WRITE_BLOCK_ATTEMPTS", 3), \
                     patch.object(ESPLoader, "WRITE_FLASH_ATTEMPTS", 2), \
                     patch.object(esptool, "_main", side_effect=operation), \
                     patch("sys.argv", ["adapter", "write_flash"]):
                    exec(ESPTOOL_ADAPTER, {"__name__": "__main__"})
                self.assertEqual(len(attempts), 1)
                self.assertEqual(fake.events, [])

    def test_upstream_default_two_attempts_reconnects_and_replays(self):
        for compressed in (True, False):
            with self.subTest(compressed=compressed):
                self.exercise("default", compressed)

    def test_controlled_one_attempt_propagates_without_recovery(self):
        for compressed in (True, False):
            with self.subTest(compressed=compressed):
                self.exercise("controlled", compressed)

    def test_production_adapter_stops_partial_write_without_success(self):
        for compressed in (True, False):
            with self.subTest(compressed=compressed):
                self.exercise("adapter", compressed)


if __name__ == "__main__":
    unittest.main()
