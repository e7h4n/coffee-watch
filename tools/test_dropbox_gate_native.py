"""Deterministic interleavings against extracted production admission/queue code.

No ESP-IDF checkout, network, firmware build, or hardware required. The harness
uses real public record/command types and extracts the complete worker too.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


class DropboxGateTests(unittest.TestCase):
    def test_production_gate(self):
        root = Path(__file__).resolve().parents[1]
        src = (root / 'firmware/demo/main/dropbox_setup.c').read_text()

        def function(name):
            match = re.search(r'^(?:static )?[^\n]*\b' + name + r'\([^;\n]*\) \{', src, re.M)
            self.assertIsNotNone(match, name)
            # Production functions have top-level closing braces on their own line.
            line = src[match.start():src.find('\n', match.start())]
            if line.endswith('}'):
                return line
            return src[match.start():src.index('\n}', match.start()) + 2]

        state = src[src.index('static struct {\n    char id[64]'):src.index('static public_t view;')]
        queued = re.search(r'^typedef struct \{cw_dropbox_command_t[^\n]+', src, re.M).group()
        gates = src[src.index('static QueueHandle_t commands;'):src.index('static char response[')]
        names = ['dropbox_setup_submit', 'dropbox_setup_record', 'dropbox_setup_color',
                 'dropbox_setup_init', 'dropbox_task', 'upload_color']
        # Also check the actual cleanup/counter ordering, not a reimplementation.
        self.assertLess(src.index('mbedtls_platform_zeroize(&item,sizeof item);wipe_session();'),
                        src.index('finish_work();\n    }\n}\nvoid dropbox_setup_init'))
        with tempfile.TemporaryDirectory(prefix='cw-dropbox-gate-') as temp:
            temp = Path(temp)
            (temp / 'production_state.inc').write_text(queued + '\n' + state + '\n' + gates)
            (temp / 'production_functions.inc').write_text('\n'.join(function(n) for n in names))
            cmd = [os.environ.get('CC', 'cc'), '-std=c11', '-D_POSIX_C_SOURCE=200809L',
                   '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-g', '-I', str(temp),
                   '-I', str(root / 'firmware/demo/main')]
            for include in (root / 'firmware/components').glob('*/include'):
                cmd += ['-I', str(include)]
            cmd += [str(root / 'tools/dropbox_gate_harness.c'), '-o', str(temp / 'test')]
            if os.environ.get('CW_SANITIZE'):
                cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(temp / 'test')], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('all Dropbox gate tests passed', result.stdout)
            print(result.stdout, end='')


if __name__ == '__main__':
    unittest.main()
