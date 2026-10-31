"""Exercise the actual C streaming decoder with real AES on macOS."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == 'darwin', 'native crypto harness uses macOS CommonCrypto')
class RsNativeTests(unittest.TestCase):
    def test_streaming_real_aes(self):
        root = Path(__file__).resolve().parents[1] / 'firmware/components/difluid_rs'
        with tempfile.TemporaryDirectory(prefix='coffee-rs-') as temp:
            executable = str(Path(temp) / 'test_rs')
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-Wno-unused-parameter', '-g', '-I', str(root / 'include'),
                            str(root / 'difluid_rs.c'), str(root / 'test/test_rs.c'),
                            '-o', executable], check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([executable], check=True, capture_output=True, text=True, timeout=10)
            self.assertIn('8 RS stream scenarios passed', result.stdout)
