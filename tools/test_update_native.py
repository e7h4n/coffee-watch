"""No ESP-IDF or third-party Python dependencies: exercise the real C helpers."""
import ctypes as C
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / 'firmware/components/update_manifest'
FIXTURES = Path(__file__).resolve().parent / 'test_update_fixtures'


class Manifest(C.Structure):
    _fields_ = [('version', C.c_char * 32), ('bin', C.c_char * 80),
                ('sha256', C.c_uint8 * 32), ('size', C.c_uint32), ('build', C.c_uint32)]


class UpdateHelpersTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='coffee-update-')
        lib = Path(cls.temp.name) / 'update.so'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-pedantic', '-Wall',
                        '-Wextra', '-Werror', '-shared', '-fPIC', '-O2',
                        '-I', str(COMPONENT / 'include'), str(COMPONENT / 'update_manifest.c'),
                        '-o', str(lib)], check=True, capture_output=True, text=True, timeout=60)
        cls.lib = C.CDLL(str(lib))
        cls.lib.um_parse.argtypes = [C.c_char_p, C.c_size_t, C.POINTER(Manifest)]
        cls.lib.um_version_build.argtypes = [C.c_char_p, C.POINTER(C.c_uint32)]
        cls.lib.um_content_length.argtypes = [C.c_char_p, C.c_size_t, C.POINTER(C.c_uint32)]
        cls.lib.um_running_version.argtypes = [C.c_char_p, C.POINTER(C.c_uint32), C.POINTER(C.c_int)]
        cls.lib.um_resolve_url.argtypes = [C.c_char_p, C.c_char_p, C.c_void_p, C.c_size_t]
        cls.lib.um_firmware_url.argtypes = [C.POINTER(Manifest), C.c_void_p, C.c_size_t]
        cls.valid = (FIXTURES / 'valid-schema1.json').read_bytes()
        cls.data = json.loads(cls.valid)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def parse(self, raw, valid=True):
        if isinstance(raw, dict):
            raw = json.dumps(raw, separators=(',', ':')).encode()
        out = Manifest()
        C.memset(C.byref(out), 0xA5, C.sizeof(out))
        before = bytes(out)
        rc = self.lib.um_parse(raw, len(raw), C.byref(out))
        self.assertEqual(rc, 0 if valid else -1, raw[:160])
        if not valid:
            self.assertEqual(bytes(out), before, 'error must leave output unchanged')
        return out

    def test_manifest_fixture_and_immutable_url(self):
        m = self.parse(self.valid)
        self.assertEqual(m.version, b'v0.1.42')
        self.assertEqual(m.bin, b'coffee-watch-v0.1.42.bin')
        self.assertEqual(m.build, 42)
        self.assertEqual(m.size, 123456)
        self.assertEqual(bytes(m.sha256), bytes.fromhex(self.data['sha256']))
        out = C.create_string_buffer(256)
        self.assertEqual(self.lib.um_firmware_url(C.byref(m), out, len(out)), 0)
        expected = b'https://github.com/e7h4n/coffee-watch/releases/download/v0.1.42/coffee-watch-v0.1.42.bin'
        self.assertEqual(out.value, expected)
        self.assertEqual(self.lib.um_firmware_url(C.byref(m), out, len(expected)), -1)
        m.build += 1
        self.assertEqual(self.lib.um_firmware_url(C.byref(m), out, len(out)), -1)
        m.build -= 1
        m.bin = b'../../evil.bin'
        self.assertEqual(self.lib.um_firmware_url(C.byref(m), out, len(out)), -1)

    def test_fail_closed_schema_types_and_prerequisites(self):
        for name in ('unsafe-prerequisite.json', 'duplicate-key.json'):
            self.parse((FIXTURES / name).read_bytes(), False)
        for key in self.data:
            data = dict(self.data)
            del data[key]
            self.parse(data, False)
            self.parse(dict(self.data, **{key: None}), False)
            # Every duplicate is invalid, even when its value is identical.
            raw = json.dumps(self.data).encode()
            self.parse(raw[:-1] + b',' + json.dumps(key).encode() + b':' +
                       json.dumps(self.data[key]).encode() + b'}', False)
        for key in ('unknown', 'bootloader', 'min_bootloader', 'min_bootloader_version',
                    'requires', 'prerequisites', 'min_partition_schema', 'url'):
            self.parse(dict(self.data, **{key: 3}), False)
        for key, values in {
            'schema': [0, 2, '1', True, 1.0],
            'partition_schema': [1, 3, '2', 2.0],
            'product': ['coffee_watch', 'Coffee-watch'],
            'chip': ['esp32', 'ESP32S3'],
            'tag': ['v0.1.43', 'latest'],
            'bin': ['coffee-watch-v0.1.43.bin', '../coffee-watch-v0.1.42.bin'],
            'size': [0, -1, 4194305, 4294967296, '42', 1.1, True],
            'sha256': ['', '0' * 63, '0' * 65, 'g' * 64],
        }.items():
            for value in values:
                with self.subTest(key=key, value=value):
                    self.parse(dict(self.data, **{key: value}), False)
        for size in (1, 4194304):
            self.parse(dict(self.data, size=size))
        self.parse(dict(self.data, sha256='F' * 64))

    def test_json_boundaries_and_malformed(self):
        raw = json.dumps(self.data, separators=(',', ':')).encode()
        self.parse(raw + b' ' * (4096 - len(raw)))
        self.parse(raw + b' ' * (4097 - len(raw)), False)
        for n in range(len(raw)):
            self.parse(raw[:n], False)
        for extra in (b'\x00', b'{}', b'garbage', b'\v', b'\xff'):
            self.parse(raw + extra, False)
        for bad in (b'[]', b'{}', b'null', b'\xef\xbb\xbf' + raw,
                    raw[:-1] + b',}', raw.replace(b'"size":123456', b'"size":01'),
                    raw.replace(b'"size":123456', b'"size":1e2'),
                    raw.replace(b'coffee-watch', b'coffee\\u002dwatch'),
                    raw.replace(b'coffee-watch', b'coffee\x00watch'),
                    raw.replace(b'coffee-watch', b'coffee\nwatch')):
            self.parse(bad, False)
        # Reordered fields and ordinary JSON whitespace remain valid.
        self.parse(dict(reversed(list(self.data.items()))))
        self.parse(b' \r\n\t' + raw + b' \r\n\t')

    def test_canonical_versions(self):
        for text, value in [('v0.1.0', 0), ('v0.1.1', 1), ('v0.1.4294967295', 4294967295)]:
            out = C.c_uint32(7)
            self.assertEqual(self.lib.um_version_build(text.encode(), C.byref(out)), 0)
            self.assertEqual(out.value, value)
            self.parse(dict(self.data, version=text, tag=text, bin=f'coffee-watch-{text}.bin'))
        for text in ['', 'v0.1.', '0.1.42', 'V0.1.42', 'v0.2.42', 'v0.1.01',
                     'v0.1.+1', 'v0.1.-1', 'v0.1.1a', 'v0.1.1\n',
                     'v0.1.4294967296', 'v0.1.0000000000000000000000000000000']:
            out = C.c_uint32(7)
            self.assertEqual(self.lib.um_version_build(text.encode(), C.byref(out)), -1, text)
            self.assertEqual(out.value, 7)
            self.parse(dict(self.data, version=text, tag=text, bin=f'coffee-watch-{text}.bin'), False)

    def resolve(self, location, expected=None, base='https://github.com/a/b/file?old=1', cap=4097):
        out = C.create_string_buffer(b'unchanged', 4097)
        before = bytes(out)
        rc = self.lib.um_resolve_url(base.encode(), location.encode(), out, cap)
        self.assertEqual(rc, 0 if expected is not None else -1, (base, location[:120]))
        if expected is not None:
            self.assertEqual(out.value.decode(), expected)
        else:
            self.assertEqual(bytes(out), before)

    def test_redirect_resolution_and_signed_queries(self):
        fixture = json.loads((FIXTURES / 'signed-asset-redirect.json').read_text())
        self.resolve(fixture['location'], fixture['location'], base=fixture['base'])
        for loc, expected in {
            '/x': 'https://github.com/x',
            'next.bin': 'https://github.com/a/b/next.bin',
            '../next.bin': 'https://github.com/a/next.bin',
            './next.bin': 'https://github.com/a/b/next.bin',
            '../../next.bin': 'https://github.com/next.bin',
            '.': 'https://github.com/a/b/',
            '..': 'https://github.com/a/',
            '/a/./b/../c?sig=A%2FB%3D': 'https://github.com/a/c?sig=A%2FB%3D',
            '?sig=A%2FB%3D&x=1': 'https://github.com/a/b/file?sig=A%2FB%3D&x=1',
            '//release-assets.githubusercontent.com/id?sig=A%2FB%3D':
                'https://release-assets.githubusercontent.com/id?sig=A%2FB%3D',
            'https://release-assets.githubusercontent.com/id?sig=A%2FB%3D':
                'https://release-assets.githubusercontent.com/id?sig=A%2FB%3D',
        }.items():
            self.resolve(loc, expected)
        self.resolve('next', 'https://github.com/next', base='https://github.com')
        self.resolve('?q=1', 'https://github.com?q=1', base='https://github.com?old=2')
        long_url = 'https://release-assets.githubusercontent.com/id?sig=' + 'a' * 3500
        self.resolve(long_url, long_url)
        exact = 'https://github.com/?s=' + 'a' * (4096 - len('https://github.com/?s='))
        self.resolve(exact, exact)
        self.resolve(exact + 'a')
        self.resolve('/x', cap=len('https://github.com/x'))
        self.resolve('/x', 'https://github.com/x', cap=len('https://github.com/x') + 1)
        self.resolve('/x', cap=0)

    def test_redirect_rejects_unsafe_forms(self):
        for loc in ['', 'http://github.com/a', 'ftp://github.com/a',
                    'https://evil.com/a', '//evil.com/a',
                    'https://github.com.evil.com/a', 'https://github.com@evil.com/a',
                    'https://evil@github.com/a', 'https://github.com:443/a',
                    'https://github.com./a', 'https://GITHUB.com/a',
                    'https://github.com\\@evil.com/a', 'https://github.com/a#frag',
                    'https:///github.com/a', 'https:github.com/a',
                    '/a\r\nHeader:value', '/a b', '/é', '/%00', '/%0d', '/%5c',
                    '/%2e%2e/a', '/a%2fb', '/a%', '/a%GG', '/a?sig=%0A',
                    '../../../escape', '/../escape', '/a/../../escape']:
            with self.subTest(location=loc):
                self.resolve(loc)
        for base in ['http://github.com/a', 'https://evil.com/a',
                     'https://github.com:443/a', 'https://github.com/a#bad']:
            self.resolve('https://github.com/safe', base=base)

    def test_running_version_baseline(self):
        for text, build, development in [
            ('v0.1.0', 0, 0), ('v0.1.42', 42, 0), ('v0.1.4294967295', 4294967295, 0),
            ('dev', 0, 1), ('abcdef0', 0, 1), ('0123456789abcdef', 0, 1),
            ('a' * 40, 0, 1), ('abcdef0-dirty', 0, 1), ('f' * 40 + '-dirty', 0, 1),
        ]:
            b, d = C.c_uint32(77), C.c_int(88)
            self.assertEqual(self.lib.um_running_version(text.encode(), C.byref(b), C.byref(d)), 0, text)
            self.assertEqual((b.value, d.value), (build, development))
        for text in ['', 'DEV', 'dev-dirty', 'unknown', 'v0.1.01', 'v0.1.4294967296',
                     'v0.1.42-dirty', 'abcdef', 'A' * 7, 'g' * 7, 'a' * 41,
                     'a' * 41 + '-dirty', 'abcdef0-DIRTY', 'abcdef0-dirtyx',
                     'abcdef0\n', 'release-v0.1.42', '1.0']:
            b, d = C.c_uint32(77), C.c_int(88)
            self.assertEqual(self.lib.um_running_version(text.encode(), C.byref(b), C.byref(d)), -1, text)
            self.assertEqual((b.value, d.value), (77, 88))
        b, d = C.c_uint32(77), C.c_int(88)
        self.assertEqual(self.lib.um_running_version(None, C.byref(b), C.byref(d)), -1)
        self.assertEqual(self.lib.um_running_version(b'dev', None, C.byref(d)), -1)
        self.assertEqual(self.lib.um_running_version(b'dev', C.byref(b), None), -1)

    def test_content_length(self):
        for raw, expected in [(b'0', 0), (b'123456', 123456), (b'4294967295', 4294967295),
                              (b'001', 1)]:
            out = C.c_uint32(77)
            self.assertEqual(self.lib.um_content_length(raw, len(raw), C.byref(out)), 0)
            self.assertEqual(out.value, expected)
        for raw in (b'', b'4294967296', b'-1', b'+1', b'1.0', b'1e2', b' 1', b'1 ',
                    b'1,1', b'1\r\n', b'1\x00', b'00000000000'):
            out = C.c_uint32(77)
            self.assertEqual(self.lib.um_content_length(raw, len(raw), C.byref(out)), -1)
            self.assertEqual(out.value, 77)

    def test_native_robustness_harness(self):
        executable = Path(self.temp.name) / 'test_update_manifest'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-pedantic', '-Wall',
                        '-Wextra', '-Werror', '-O2', '-I', str(COMPONENT / 'include'),
                        str(COMPONENT / 'update_manifest.c'),
                        str(COMPONENT / 'test/test_update_manifest.c'), '-o', str(executable)],
                       check=True, capture_output=True, text=True, timeout=60)
        subprocess.run([str(executable)], check=True, capture_output=True, text=True, timeout=30)

    def test_null_arguments(self):
        out = Manifest()
        n = C.c_uint32()
        buf = C.create_string_buffer(256)
        self.assertEqual(self.lib.um_parse(None, 1, C.byref(out)), -1)
        self.assertEqual(self.lib.um_parse(self.valid, len(self.valid), None), -1)
        self.assertEqual(self.lib.um_version_build(None, C.byref(n)), -1)
        self.assertEqual(self.lib.um_version_build(b'v0.1.1', None), -1)
        self.assertEqual(self.lib.um_content_length(None, 1, C.byref(n)), -1)
        self.assertEqual(self.lib.um_content_length(b'1', 1, None), -1)
        self.assertEqual(self.lib.um_firmware_url(None, buf, len(buf)), -1)
        self.assertEqual(self.lib.um_firmware_url(C.byref(out), None, 256), -1)
        self.assertEqual(self.lib.um_resolve_url(None, b'/x', buf, len(buf)), -1)
        self.assertEqual(self.lib.um_resolve_url(b'https://github.com', None, buf, len(buf)), -1)
        self.assertEqual(self.lib.um_resolve_url(b'https://github.com', b'/x', None, 256), -1)


if __name__ == '__main__':
    unittest.main()
