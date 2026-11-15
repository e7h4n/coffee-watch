import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class RoastColorNativeTests(unittest.TestCase):
    def test_meter_and_calibration(self):
        root = Path(__file__).resolve().parents[1] / 'firmware/components'
        with tempfile.TemporaryDirectory(prefix='coffee-color-') as temp:
            executable = str(Path(temp) / 'test_roast_color')
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root / 'roast_color/include'), str(root / 'roast_color/roast_color.c'),
                            str(root / 'roast_color/test/test_roast_color.c'), '-lm', '-o', executable],
                           check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([executable], check=True, capture_output=True, text=True, timeout=10)
            self.assertIn('7 roast color scenarios passed', result.stdout)

    def test_max30101_registers_and_fifo(self):
        root = Path(__file__).resolve().parents[1] / 'firmware/components'
        with tempfile.TemporaryDirectory(prefix='coffee-max30101-') as temp:
            executable = str(Path(temp) / 'test_max30101')
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root / 'max30101/include'), str(root / 'max30101/max30101.c'),
                            str(root / 'max30101/test/test_max30101.c'), '-o', executable],
                           check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([executable], check=True, capture_output=True, text=True, timeout=10)
            self.assertIn('5 max30101 scenarios passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
