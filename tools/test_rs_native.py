"""Actual C streaming decoder + real AES (CommonCrypto/macOS, mbedTLS/Linux)."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class RsNativeTests(unittest.TestCase):
    def test_streaming_real_aes(self):
        root = Path(__file__).resolve().parents[1] / 'firmware/components/difluid_rs'
        with tempfile.TemporaryDirectory(prefix='coffee-rs-') as temp:
            executable = str(Path(temp) / 'test_rs')
            crypto = []
            if sys.platform != 'darwin':
                candidates = [Path(os.environ['IDF_PATH'])] if os.environ.get('IDF_PATH') else []
                candidates.append(Path('/tmp/idf/esp-idf'))
                mbed = next((p / 'components/mbedtls/mbedtls' for p in candidates
                             if (p / 'components/mbedtls/mbedtls/library/aes.c').is_file()), None)
                if mbed is None:
                    self.skipTest('native AES dependency unavailable: set IDF_PATH to an ESP-IDF source checkout')
                (Path(temp) / 'native_config.h').write_text('#define MBEDTLS_AES_C\n#define MBEDTLS_CIPHER_MODE_CBC\n')
                crypto = ['-DMBEDTLS_CONFIG_FILE="native_config.h"', '-I', temp,
                          '-I', str(mbed / 'include'), str(mbed / 'library/aes.c'),
                          str(mbed / 'library/platform_util.c')]
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-Wno-unused-parameter', '-g', '-I', str(root / 'include'),
                            str(root / 'difluid_rs.c'), str(root / 'test/test_rs.c'),
                            '-o', executable] + crypto, check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([executable], check=True, capture_output=True, text=True, timeout=10)
            self.assertIn('8 RS stream scenarios passed', result.stdout)
