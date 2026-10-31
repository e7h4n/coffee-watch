import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class FlowNativeTests(unittest.TestCase):
    def test_complete_flow_and_event_boundaries(self):
        root = Path(__file__).resolve().parents[1] / 'firmware/components'
        with tempfile.TemporaryDirectory(prefix='coffee-flow-') as temp:
            executable = str(Path(temp) / 'test_flow')
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root / 'coffee_flow/include'), '-I', str(root / 'bookoo/include'),
                            '-I', str(root / 'difluid_rs/include'), str(root / 'coffee_flow/coffee_flow.c'),
                            str(root / 'bookoo/bookoo.c'), str(root / 'coffee_flow/test/test_flow.c'),
                            '-lm', '-o', executable], check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([executable], check=True, capture_output=True, text=True, timeout=10)
            self.assertIn('10 full-flow scenarios passed', result.stdout)
