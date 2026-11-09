import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class TraceNativeTests(unittest.TestCase):
    def test_probe_conversion_buffer_and_csv(self):
        root = Path(__file__).resolve().parents[1] / 'firmware/components'
        with tempfile.TemporaryDirectory(prefix='coffee-trace-') as temp:
            executable = str(Path(temp) / 'test_trace')
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root / 'brew_trace/include'), str(root / 'brew_trace/brew_trace.c'),
                            str(root / 'brew_trace/test/test_trace.c'), '-lm', '-o', executable],
                           check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([executable], check=True, capture_output=True, text=True, timeout=10)
            self.assertIn('trace tests passed', result.stdout)
