"""Synthetic protocol tests; these are not device compatibility evidence."""
import functools
import operator
import json
import subprocess
import sys
import tempfile
from pathlib import Path
import unittest
from ble_replay import decode_bookoo


def packet(kind, data):
    body = bytes([3, kind]) + bytes(data)
    body += bytes(19 - len(body))
    return body + bytes([functools.reduce(operator.xor, body)])


class DecodeTests(unittest.TestCase):
    def test_physical_v401_dose_capture(self):
        # FF11 notification, compared with the scale before dose-save.
        result = decode_bookoo(bytes.fromhex("030F2B00091A0000000000000000000000000034"))
        self.assertEqual(result["dose_g"], 23.3)

    def test_physical_v401_two_pour_stop_capture(self):
        # FF12 notification; device showed 0'29, 40.4g, 1:1.7.
        result = decode_bookoo(bytes.fromhex("030D000071AC2B000FC82B00AA000000000000BE"))
        self.assertEqual(result["state_raw"], 0)
        self.assertEqual(result["elapsed_ms"], 29100)
        self.assertEqual(result["weight_g"], 40.4)
        self.assertEqual(result["settlement_magnitude"], 1.7)

    def test_weight_24_bit_time_and_grams_even_in_ounce_display(self):
        result = decode_bookoo(packet(0x0B, [1, 2, 3, 2, 0, 0, 0x30, 0x39, 1, 0, 250, 85]))
        self.assertEqual(result["elapsed_ms"], 0x010203)
        self.assertEqual(result["weight_magnitude_g"], 123.45)
        self.assertEqual(result["display_unit_raw"], 2)
        self.assertEqual(result["flow_magnitude_g_s"], 2.5)
        self.assertEqual(result["battery_percent"], 85)

    def test_unknown_sign_is_not_guessed(self):
        result = decode_bookoo(packet(0x0F, [0xFF, 0, 5, 220]))
        self.assertEqual(result["dose_magnitude_g"], 15)
        self.assertEqual(result["dose_sign_raw"], 0xFF)
        self.assertNotIn("dose_g", result)

    def test_ascii_signs(self):
        for sign, expected in [(0x2B, 15), (0x2D, -15)]:
            result = decode_bookoo(packet(0x0F, [sign, 0, 5, 220]))
            self.assertEqual(result["dose_g"], expected)

    def test_stop_event_keeps_settlement_separate(self):
        result = decode_bookoo(packet(0x0D, [0, 0, 0xEA, 0x60, 0, 0, 0x75, 0x30, 0, 7, 208]))
        self.assertEqual(result["state_raw"], 0)
        self.assertEqual(result["elapsed_ms"], 60000)
        self.assertEqual(result["weight_magnitude_g"], 300)
        self.assertEqual(result["settlement_magnitude"], 20)
        self.assertNotIn("added_water_g", result)

    def test_bad_checksum_rejected(self):
        valid = bytearray(packet(0x0B, []))
        valid[8] ^= 1
        with self.assertRaisesRegex(ValueError, "XOR"):
            decode_bookoo(valid)

    def test_partial_and_oversized_packets_rejected(self):
        valid = packet(0x0F, [])
        for data in (valid[:-1], valid + b"\0", b"", b"\xDF" * 20):
            with self.subTest(data=data), self.assertRaises(ValueError):
                decode_bookoo(data)

    def test_unknown_type_does_not_become_measurement(self):
        self.assertEqual(decode_bookoo(packet(0xFF, []))["kind"], "unknown")

    def test_replay_filters_reused_connection_and_other_device(self):
        raw = packet(0x0F, [0, 0, 5, 220]).hex()
        records = [{"source": "device", "line": "I boot log"}]
        for conn, epoch, handle in [(0, 1, 17), (0, 2, 17), (1, 1, 17), (0, 1, 18)]:
            record = {"event": "rx", "us": 123,
                      "detail": f"conn={conn} epoch={epoch} handle={handle} hex={raw}"}
            records.append({"source": "device", "line": json.dumps(record)})
        records.append({"source": "device", "line": json.dumps({"event": "boot", "us": 0})})
        records.append({"source": "device", "line": json.dumps({"event": "rx", "us": 123,
                        "detail": f"conn=0 epoch=1 handle=17 hex={raw}"})})
        with tempfile.TemporaryDirectory() as directory:
            capture = Path(directory) / "capture.jsonl"
            capture.write_text("\n".join(json.dumps(r) for r in records))
            result = subprocess.run([sys.executable, str(Path(__file__).with_name("ble_replay.py")),
                                     str(capture), "--conn", "0", "--epoch", "1", "--handle", "17"],
                                    capture_output=True, text=True, check=True)
        decoded = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(decoded), 1)
        self.assertEqual(decoded[0]["dose_magnitude_g"], 15)


if __name__ == "__main__":
    unittest.main()
