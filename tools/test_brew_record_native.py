import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

class BrewRecordTests(unittest.TestCase):
    def test_sensor_events_emit_two_revisions_of_one_valid_json_record(self):
        root = Path(__file__).resolve().parents[1] / 'firmware/components'
        with tempfile.TemporaryDirectory(prefix='coffee-record-') as temp:
            binary = str(Path(temp) / 'test_record')
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root / 'brew_record/include'), '-I', str(root / 'coffee_flow/include'),
                            '-I', str(root / 'bookoo/include'), '-I', str(root / 'difluid_rs/include'),
                            str(root / 'brew_record/brew_record.c'), str(root / 'coffee_flow/coffee_flow.c'),
                            str(root / 'bookoo/bookoo.c'), str(root / 'brew_record/test/test_brew_record.c'),
                            '-lm', '-o', binary], check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([binary], check=True, capture_output=True, text=True, timeout=10)
            draft, final = map(json.loads, filter(None, result.stdout.splitlines()))
            self.assertEqual(draft['id'], final['id'])
            self.assertEqual(draft['created_at'], final['created_at'])
            self.assertIsNone(draft['tds_percent'])
            self.assertEqual(final['tds_percent'], 0.9)
            self.assertEqual(final['brewer'], 'Switch')
            self.assertEqual(final['elapsed_ms'], 180000)
            self.assertEqual(final['beverage_g'], 196.5)
