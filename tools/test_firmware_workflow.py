"""Execute the actual publish shell with a fake gh; no GitHub writes/network."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]
MOCK_GH = r'''#!/usr/bin/env python3
import json, os, sys
args = sys.argv[1:]
with open(os.environ['GH_STUB_LOG'], 'a') as stream:
    stream.write(json.dumps(args) + '\n')
case = os.environ['GH_STUB_CASE']
if args == ['api', 'repos/test/repo', '--jq', '.private']:
    if case == 'lookup-failure': raise SystemExit(1)
    print('true' if case == 'private' else 'false')
elif 'repos/test/repo/git/refs' in args:
    if case in ('tag-only-wrong-commit', 'tag-only-same-commit', 'annotated-tag', 'draft-release', 'existing-release', 'tag-create-failure'):
        raise SystemExit(1)
    assert 'ref=refs/tags/v0.1.42' in args
    assert 'sha=' + os.environ['COMMIT'] in args
    print('wrong' if case == 'tag-return-mismatch' else os.environ['COMMIT'])
elif args[:2] == ['release', 'create']:
    assert '--verify-tag' in args and '--draft' in args and '--latest=false' in args
    if case == 'upload-failure': raise SystemExit(1)
elif args[:2] == ['release', 'view']:
    print('17')
elif args[:3] == ['api', '--method', 'PATCH']:
    assert 'repos/test/repo/releases/17' in args
    assert 'draft=false' in args and 'prerelease=false' in args and 'make_latest=legacy' in args
    if case == 'publish-failure': raise SystemExit(1)
else:
    raise SystemExit('unexpected gh command: ' + repr(args))
'''


class FirmwareWorkflowTests(unittest.TestCase):
    def test_publication_atomic_tag_target_and_fail_closed(self):
        source = (ROOT / '.github/workflows/firmware.yml').read_text()
        script = textwrap.dedent(source.split('name: Publish immutable run-number release', 1)[1].split('run: |\n', 1)[1])
        cases = ('new', 'private', 'lookup-failure', 'tag-only-wrong-commit', 'tag-only-same-commit',
                 'annotated-tag', 'draft-release', 'existing-release', 'tag-create-failure',
                 'tag-return-mismatch', 'upload-failure', 'publish-failure')
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / 'bin').mkdir()
                gh = root / 'bin/gh'
                gh.write_text(MOCK_GH)
                gh.chmod(0o700)
                dist = root / 'dist'
                dist.mkdir()
                files = {'coffee-watch-v0.1.42.bin': b'app-only', 'latest.json': b'{}',
                         'coffee-watch-v0.1.42-usb-migration.zip': b'usb-only'}
                for name, data in files.items():
                    (dist / name).write_bytes(data)
                (dist / 'SHA256SUMS').write_text(''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n' for name, data in files.items()))
                log = root / 'gh.log'
                env = dict(os.environ, PATH=str(root / 'bin') + os.pathsep + os.environ['PATH'],
                           GH_STUB_LOG=str(log), GH_STUB_CASE=case, GH_REPO='test/repo',
                           VERSION='v0.1.42', COMMIT='a' * 40, GH_TOKEN='TEST-NOT-A-TOKEN')
                result = subprocess.run(['bash', '-c', script], cwd=root, env=env,
                                        capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode == 0, case == 'new', result.stdout + result.stderr)
                calls = [json.loads(line) for line in log.read_text().splitlines()]
                create = [call for call in calls if call[:2] == ['release', 'create']]
                publish = [call for call in calls if call[:3] == ['api', '--method', 'PATCH']]
                if case in ('new', 'upload-failure', 'publish-failure'):
                    self.assertEqual(len(create), 1)
                else:
                    self.assertEqual(create, [])
                    self.assertEqual(publish, [])
                if case == 'upload-failure':
                    self.assertEqual(publish, [])

    def test_ci_has_zero_skip_dependencies_and_pr_publication_gate(self):
        source = (ROOT / '.github/workflows/firmware.yml').read_text()
        self.assertNotIn('\n  pull_request_target:', source.split('permissions:', 1)[0])
        self.assertIn("if: github.event_name == 'push' && github.ref == 'refs/heads/main'", source)
        self.assertIn('contents: read', source.split('jobs:', 1)[0])
        native = source.split('  native:', 1)[1].split('  build:', 1)[0]
        self.assertNotIn('libssl-dev', native)
        self.assertIn('30aaf64524299d3bde422ca9a2848090d1bc5d0f', native)
        self.assertIn('components/json/cJSON components/mbedtls/mbedtls', native)
        self.assertIn('IDF_PATH=', native)
        self.assertIn('esptool==4.11.0', native)
        self.assertIn('result.wasSuccessful() and not result.skipped', native)


if __name__ == '__main__':
    unittest.main()
