import unittest
from difluid_rs import crc16, decode_frame, parse_measurement, unpack_frame

FRAME = bytes.fromhex('DADACC2C58EB5F5C63B6DBB88E3C3DB1877018986404E3484EE773654DA9606419A13BFAFAF999174D9F47D90C699AC50F260EF1070A0A')
PLAIN = bytes.fromhex('0103120027000082bfb23b82bfb23b012e72b64100000000bb9faa3f71f635437e91ab4486023eaddd3b015086676335')


class RsTests(unittest.TestCase):
    def test_real_capture_matches_device_display(self):
        result = decode_frame(FRAME)
        self.assertEqual(result['kind'], 'measurement')
        self.assertEqual(round(result['tds_percent'], 2), 0.01)
        self.assertEqual(round(result['temperature_c'], 1), 22.8)
        self.assertEqual(round(result['refraction'], 5), 1.33300)
        self.assertEqual(result['count'], 1)

    def test_live_stopwatch_capture_matches_second_display(self):
        packet = bytes.fromhex('DADAD061EAC1A68FB7E1EDA2667C7AA506B029F6D1F3B5EAB461B117E208A179719BB061A98C18AFCBDF566760C876C4FBF9052D0A0A')
        result = decode_frame(packet)
        self.assertEqual(result['tds_percent'], 0.0)
        self.assertEqual(round(result['temperature_c'], 1), 23.1)
        self.assertEqual(round(result['refraction'], 5), 1.33296)
        self.assertEqual(result['kind'], 'measurement')

    def test_transport_rejects_corruption_and_incomplete_data(self):
        for bad in (FRAME[:-1], FRAME[:8]+b'\0'+FRAME[9:], b'\xda\xda\xfa\x03\x0a\x0a'):
            with self.subTest(frame=bad), self.assertRaises(ValueError):
                unpack_frame(bad)

    def test_inner_crc_and_exact_result_route_required(self):
        for bad in (PLAIN[:10]+b'\0'+PLAIN[11:], b'\x02'+PLAIN[1:], PLAIN[:20]):
            with self.subTest(plain=bad), self.assertRaises(ValueError):
                parse_measurement(bad)

    def test_error_result_is_not_success(self):
        for offset in (6, 20):
            plain = bytearray(PLAIN)
            plain[offset] = 1
            plain[45:47] = crc16(plain[:45]).to_bytes(2, 'little')
            self.assertEqual(parse_measurement(plain)['kind'], 'measurement_error')
