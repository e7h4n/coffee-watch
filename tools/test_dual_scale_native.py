import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1] / 'firmware/components'


def run(name, sources, includes, expected):
    with tempfile.TemporaryDirectory(prefix='coffee-dual-') as temp:
        executable = str(Path(temp) / name)
        args = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror']
        for include in includes:
            args += ['-I', str(ROOT / include)]
        subprocess.run(args + [str(ROOT / s) for s in sources] + ['-lm', '-o', executable],
                       check=True, capture_output=True, text=True, timeout=60)
        result = subprocess.run([executable], check=True, capture_output=True, text=True, timeout=20)
        return expected in result.stdout


class DualScaleNativeTests(unittest.TestCase):
    def test_two_scale_brew_logic(self):
        self.assertTrue(run('test_duo', ['coffee_flow/duo.c', 'coffee_flow/pour.c', 'coffee_flow/test/test_duo.c'],
                            ['coffee_flow/include'], '16 duo scenarios passed'))

    def test_ck811_frames(self):
        self.assertTrue(run('test_ck811', ['ck811/ck811.c', 'ck811/test/test_ck811.c'], ['ck811/include'],
                            'ck811 tests passed'))


if __name__ == '__main__':
    unittest.main()
