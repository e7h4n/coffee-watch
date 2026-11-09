import os
from pathlib import Path
import subprocess
import tempfile
import unittest

class DropboxPacketTests(unittest.TestCase):
    def test_native_dropbox_packet_validation(self):
        root = Path(__file__).resolve().parents[1] / 'firmware/components/dropbox_config'
        with tempfile.TemporaryDirectory(prefix='coffee-dropbox-') as temp:
            executable = str(Path(temp) / 'test_dropbox')
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root / 'include'), str(root / 'dropbox_packet.c'),
                            str(root / 'test/test_dropbox_packet.c'), '-o', executable], check=True,
                           capture_output=True, text=True, timeout=60)
            subprocess.run([executable], check=True, capture_output=True, text=True, timeout=10)
