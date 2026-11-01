import os
from pathlib import Path
import subprocess
import tempfile
import unittest

class WifiPacketTests(unittest.TestCase):
    def test_native_wifi_packet_validation(self):
        root = Path(__file__).resolve().parents[1] / 'firmware/components/wifi_config'
        with tempfile.TemporaryDirectory(prefix='coffee-wifi-') as temp:
            executable = str(Path(temp) / 'test_wifi')
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root / 'include'), str(root / 'wifi_packet.c'),
                            str(root / 'test/test_wifi_packet.c'), '-o', executable], check=True,
                           capture_output=True, text=True, timeout=60)
            subprocess.run([executable], check=True, capture_output=True, text=True, timeout=10)
