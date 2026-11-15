import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class AppModeNativeTests(unittest.TestCase):
    def test_home_picks_the_first_new_connection(self):
        root = Path(__file__).resolve().parents[1] / 'firmware/components'
        with tempfile.TemporaryDirectory(prefix='coffee-app-') as temp:
            executable = str(Path(temp) / 'test_app_mode')
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root / 'app_mode/include'), str(root / 'app_mode/app_mode.c'),
                            str(root / 'app_mode/test/test_app_mode.c'), '-o', executable],
                           check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([executable], check=True, capture_output=True, text=True, timeout=10)
            self.assertIn('5 app mode scenarios passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
