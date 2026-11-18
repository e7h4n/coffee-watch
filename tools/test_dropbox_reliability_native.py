"""Fault injection against current production Dropbox helpers (no network/hardware).

Optional prerequisite: an ESP-IDF source checkout, selected by IDF_PATH or an
available local /tmp/idf/esp-idf development cache, for its actual cJSON and
portable mbedTLS SHA256 sources. Missing optional sources explicitly skip this
test; CI/release verification should provide IDF_PATH and require a non-skipped run. Only the HTTP, clock,
credential refresh and RTOS/UI boundaries are replaced; CSV/JSON serializers,
upload response validation, metadata reconciliation, recovery and the retained
unsaved predicate run unchanged. The gate harness separately exercises actual
maintenance admission with a confirmed cached trace and unconfirmed attempts.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


class DropboxReliabilityTests(unittest.TestCase):
    def test_production_http_setup_and_byte_logging(self):
        root = Path(__file__).resolve().parents[1]
        src = (root / 'firmware/demo/main/dropbox_setup.c').read_text()
        start = src.index('static int http(')
        with tempfile.TemporaryDirectory(prefix='cw-dropbox-http-') as tmp:
            tmp = Path(tmp)
            (tmp / 'production_http.inc').write_text(src[start:src.index('\n}', start) + 2])
            cmd = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                   '-I', str(tmp), str(root / 'tools/dropbox_http_harness.c'), '-o', str(tmp / 'test')]
            if os.environ.get('CW_SANITIZE'):
                cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            subprocess.run(cmd, check=True, capture_output=True, text=True, timeout=30)
            result = subprocess.run([str(tmp / 'test')], check=True, capture_output=True, text=True, timeout=20)
            print(result.stdout, end='')

    def test_production_recovery_and_diagnostic(self):
        root = Path(__file__).resolve().parents[1]
        candidates = [Path(os.environ['IDF_PATH'])] if os.environ.get('IDF_PATH') else []
        candidates.append(Path('/tmp/idf/esp-idf'))  # optional development cache, never required
        idf = next((p for p in candidates
                    if (p / 'components/json/cJSON/cJSON.c').is_file()
                    and (p / 'components/mbedtls/mbedtls/library/sha256.c').is_file()), None)
        if idf is None:
            self.skipTest('optional ESP-IDF source dependency unavailable: set IDF_PATH '
                          'to a checkout containing cJSON and portable mbedTLS SHA256 sources')
        src = (root / 'firmware/demo/main/dropbox_setup.c').read_text()
        def function(name):
            match = re.search(r'^static [^\n]*\b' + name + r'\(', src, re.M)
            if not match:
                raise AssertionError('Missing production helper: ' + name)
            return src[match.start():src.index('\n}', match.start()) + 2]
        state = src[src.index('static struct {\n    char id[64]'):src.index('static public_t view;')]
        enum = re.search(r'typedef enum \{UPLOAD_OK[^\n]+', src).group()
        helpers = ['text_field', 'content_hash', 'upload', 'metadata',
                   'reliable_upload', 'record_status', 'upload_trace',
                   'upload_record', 'run_diagnostic', 'retained_unsaved']
        remote_type = re.search(r'typedef struct \{char hash\[65\][^\n]+', src).group()
        with tempfile.TemporaryDirectory(prefix='cw-dropbox-reliability-') as tmp:
            tmp = Path(tmp)
            (tmp / 'production.inc').write_text(enum + '\n' + state + '\n' + remote_type + '\n' + '\n'.join(map(function, helpers)))
            (tmp / 'native_config.h').write_text('#define MBEDTLS_SHA256_C\n')
            components = root / 'firmware/components'
            mbed = idf / 'components/mbedtls/mbedtls'
            cmd = [os.environ.get('CC', 'cc'), '-std=c11', '-D_POSIX_C_SOURCE=200809L',
                   '-Wall', '-Wextra', '-Werror', '-g',
                   '-DMBEDTLS_CONFIG_FILE="native_config.h"', '-I', str(tmp),
                   '-I', str(idf / 'components/json/cJSON'), '-I', str(mbed / 'include'),
                   '-I', str(root / 'firmware/demo/main')]
            for part in ['brew_record', 'brew_trace', 'coffee_flow', 'bookoo', 'difluid_rs', 'dropbox_config', 'roast_color']:
                cmd += ['-I', str(components / part / 'include')]
            cmd += [str(root / 'tools/dropbox_reliability_harness.c'),
                    str(idf / 'components/json/cJSON/cJSON.c'), str(mbed / 'library/sha256.c')]
            for part, files in [('brew_record', ['brew_record']), ('brew_trace', ['brew_trace']),
                                ('coffee_flow', ['coffee_flow', 'pour', 'duo']), ('bookoo', ['bookoo'])]:
                cmd += [str(components / part / (f + '.c')) for f in files]
            cmd += ['-lm', '-o', str(tmp / 'test')]
            # Opt-in sanitizer checks work on native hosts with ASan support.
            if os.environ.get('CW_SANITIZE'):
                cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(tmp / 'test')], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout, end='')
            self.assertIn('all Dropbox reliability tests passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
