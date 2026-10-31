import unittest
from difluid_frame import decode_frame


class DiFluidFrameTests(unittest.TestCase):
    def test_official_identity_examples(self):
        self.assertEqual(decode_frame(bytes.fromhex("DFDF0001084446542D52313032B7"))["value"], "DFT-R102")
        self.assertEqual(decode_frame(bytes.fromhex("DFDF00020454303033AB"))["value"], "T003")

    def test_measurement_packet_is_not_promoted_to_final_result(self):
        result = decode_frame(bytes.fromhex("DFDF03000702004F0002093D61"))
        self.assertEqual(result["kind"], "uninterpreted")
        self.assertNotIn("tds", result)

    def test_corrupt_or_incomplete_envelopes_rejected(self):
        good = bytes.fromhex("DFDF00020454303033AB")
        for bad in (b"", good[:-1], good+b"\0", good[:-1]+b"\0", b"\0\0"+good[2:]):
            with self.subTest(packet=bad), self.assertRaises(ValueError):
                decode_frame(bad)
