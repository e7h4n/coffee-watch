"""Compile production brew_backup.c against a crash/failure-injectable NVS mock.

No ESP device, network, external services or third-party Python packages needed.
Run: python3 tools/test_brew_backup_native.py
"""
import ctypes as C
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'tools/fixtures/brew_backup'


class BrewBackupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='cw-backup-')
        library = Path(cls.temp.name) / 'backup.so'
        command = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                   '-Werror', '-pedantic', '-shared', '-fPIC', '-g',
                   '-I', str(FIXTURE)]
        for include in (ROOT / 'firmware/components').glob('*/include'):
            command += ['-I', str(include)]
        subprocess.run(command + [str(FIXTURE / 'backend.c'), '-lm', '-o', str(library)],
                       check=True, capture_output=True, text=True)
        cls.lib = C.CDLL(str(library))
        cls.lib.fixture_save.argtypes = [C.c_char_p, C.c_uint, C.c_int64, C.c_uint]
        cls.lib.fixture_save.restype = C.c_bool
        cls.lib.brew_backup_json.argtypes = [C.c_uint, C.c_char_p, C.c_size_t]
        cls.lib.brew_backup_json.restype = C.c_int
        cls.lib.mock_fail.argtypes = [C.c_uint, C.c_uint]
        cls.lib.mock_corrupt.argtypes = [C.c_uint]
        cls.lib.mock_immediate.argtypes = [C.c_uint]
        cls.lib.mock_bank_revision.argtypes = [C.c_uint]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.lib.mock_reset()

    def save(self, identity='brew-1', revision=1, utc=0, mode=0):
        return self.lib.fixture_save(identity.encode(), revision, utc, mode)

    def export(self, index=0):
        out = C.create_string_buffer(2048)
        n = self.lib.brew_backup_json(index, out, len(out))
        if n < 0:
            self.assertEqual(out.value, b'')
            return None
        self.assertEqual(n, len(out.value))
        return json.loads(out.value, parse_constant=lambda s: self.fail(s))

    def test_reboot_unknown_utc_and_actual_summary(self):
        self.assertIsNone(self.export())
        self.assertTrue(self.save())
        self.lib.mock_reboot()
        data = self.export()
        self.assertEqual(data['source'], 'local_backup')
        self.assertFalse(data['trace_samples_persisted'])
        self.assertIsNone(data['probe_trace'])
        self.assertIsNone(data['captured_utc_seconds'])
        self.assertEqual(data['captured_monotonic_us'], 123456)
        self.assertEqual(data['id'], 'brew-1')
        self.assertEqual(data['revision'], 1)
        self.assertEqual(data['status'], 'complete')
        expected = {'brewer': 'AeroPress', 'dose_g': 15, 'water_ml': 240,
                    'beverage_g': 200, 'empty_server_g': 100, 'elapsed_ms': 180000,
                    'brew_ratio': 16, 'tds_percent': 1.4, 'extraction_percent': 22.4,
                    'sample_temperature_c': 25.5, 'refractive_index': 1.335,
                    'scales': 2, 'drain_start_ms': 120000, 'peak_out_flow_gps': 9.1,
                    'extraction_formula': 'immersion'}
        for key, value in expected.items():
            self.assertEqual(data[key], value, key)
        self.assertNotIn('created_at', data)
        self.assertNotIn('uploaded', json.dumps(data))
        self.assertNotIn('XXXXX', json.dumps(data))
        self.assertEqual(self.lib.mock_erases(), 0)

    def test_revisions_do_not_rotate_or_invent_capture(self):
        self.assertTrue(self.save('first', utc=1700000000, mode=7))
        self.assertIsNone(self.export()['tds_percent'])
        self.assertEqual(self.export()['status'], 'awaiting_tds')
        self.assertTrue(self.save('second'))
        self.assertTrue(self.save('first', revision=2, utc=1800000000))
        self.lib.mock_reboot()
        self.assertEqual(self.export()['id'], 'second')
        self.assertEqual(self.export(1)['revision'], 2)
        self.assertEqual(self.export(1)['captured_utc_seconds'], 1700000000)
        before = self.lib.mock_writes()
        self.assertFalse(self.save('first', revision=1))
        self.assertFalse(self.save('first', revision=2))
        self.assertEqual(self.lib.mock_writes(), before)
        self.assertTrue(self.save('second', revision=2, utc=1800000000))
        self.assertIsNone(self.export()['captured_utc_seconds'])
        self.assertIsNone(self.export(2))

    def test_ring_bound_and_stable_order_across_reboots(self):
        for i in range(24):
            self.assertTrue(self.save(f'brew-{i}'))
            self.lib.mock_reboot()
            expected = [f'brew-{j}' for j in range(i, max(-1, i-8), -1)]
            self.assertEqual([self.export(k)['id'] for k in range(len(expected))], expected)
        self.assertIsNone(self.export(8))
        self.assertTrue(self.save('brew-19', revision=3))
        self.assertEqual(self.export(4)['revision'], 3)
        self.assertEqual(self.export(0)['id'], 'brew-23')

    def test_failures_preserve_all_last_committed_records_without_erase(self):
        for op, nth in [(1, 1), (2, 1), (3, 1), (4, 1), (5, 1), (6, 1), (6, 2)]:
            for update in [False, True]:
                with self.subTest(op=op, nth=nth, update=update):
                    self.lib.mock_reset()
                    for i in range(8):
                        self.assertTrue(self.save(f'brew-{i}'))
                    before = [self.export(k) for k in range(8)]
                    self.lib.mock_fail(op, nth)
                    self.assertFalse(self.save('brew-7' if update else 'new', revision=2))
                    self.lib.mock_reboot()
                    self.assertEqual([self.export(k) for k in range(8)], before)
                    self.assertEqual(self.lib.mock_erases(), 0)
                    self.assertTrue(self.save('recovery'))
                    self.assertEqual(self.export()['id'], 'recovery')

    def test_interrupted_first_save_is_not_visible(self):
        for op, nth in [(4, 1), (5, 1), (6, 1), (6, 2)]:
            self.lib.mock_reset()
            self.lib.mock_fail(op, nth)
            self.assertFalse(self.save())
            self.lib.mock_reboot()
            self.assertIsNone(self.export())
            self.assertTrue(self.save('retry'))

    def test_nvs_immediate_write_and_ambiguous_commit(self):
        # ESP-IDF may write during set, before commit. Never falsely return true;
        # head commit failure may expose the candidate, but old bank stays intact.
        for nth in [1, 2]:
            self.lib.mock_reset()
            self.assertTrue(self.save())
            self.lib.mock_immediate(1)
            self.lib.mock_fail(6, nth)
            self.assertFalse(self.save(revision=2))
            self.lib.mock_reboot()
            self.assertEqual(self.export()['revision'], 1 if nth == 1 else 2)
            self.assertIn(1, [self.lib.mock_bank_revision(0), self.lib.mock_bank_revision(1)])
            self.assertEqual(self.lib.mock_erases(), 0)

    def test_invalid_input_and_nonfinite_values_do_not_write(self):
        for mode in [1, 2, 3, 4, 5, 6, 8, 10, 11]:
            self.assertFalse(self.save(mode=mode), mode)
        for identity in ['', 'bad"id', 'bad_id', 'bad/id']:
            self.assertFalse(self.save(identity))
        self.assertFalse(self.save(revision=0))
        self.assertFalse(self.save(utc=-1))
        self.assertFalse(self.save(utc=253402300800))
        self.assertEqual(self.lib.mock_writes(), 0)
        self.assertTrue(self.save(mode=9))
        self.assertEqual(self.export()['scales'], 1)
        self.assertIsNone(self.export()['drain_start_ms'])
        self.assertIsNone(self.export()['peak_out_flow_gps'])

    def test_malformed_storage_fails_closed_and_is_not_overwritten(self):
        for mode in range(14):
            with self.subTest(mode=mode):
                self.lib.mock_reset()
                self.assertTrue(self.save())
                self.lib.mock_corrupt(mode)
                self.lib.mock_reboot()
                self.assertIsNone(self.export())
                before = self.lib.mock_writes()
                self.assertFalse(self.save('new'))
                self.assertEqual(self.lib.mock_writes(), before)
                self.assertEqual(self.lib.mock_erases(), 0)

    def test_export_capacity_and_read_errors(self):
        self.assertTrue(self.save())
        for cap in [0, 1, 8, 64]:
            out = C.create_string_buffer(b'sentinel', 64)
            self.assertEqual(self.lib.brew_backup_json(0, out, cap), -1)
            if cap:
                self.assertEqual(out.value, b'')
        self.assertEqual(self.lib.brew_backup_json(0, None, 2048), -1)
        for op in [1, 2, 3]:
            self.lib.mock_fail(op, 1)
            self.assertIsNone(self.export())
            self.lib.mock_reboot()
            self.assertIsNotNone(self.export())


if __name__ == '__main__':
    unittest.main(verbosity=2)
