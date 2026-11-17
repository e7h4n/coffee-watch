#!/usr/bin/env python3
"""Host-native actual firmware_update.c runtime tests; no IDF/network/hardware.
Requires a C compiler and OpenSSL libcrypto. Builds only in a temporary directory.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class UpdateRuntimeTest(unittest.TestCase):
    def test_actual_updater(self):
        base = ROOT / "tools/test_update_runtime"
        with tempfile.TemporaryDirectory(prefix="cw-update-runtime-") as tmp:
            exe = Path(tmp) / "runtime"
            # This sandbox ships libcrypto.so.3 but not the development symlink.
            crypto = os.environ.get("CW_OPENSSL_LIB", "-l:libcrypto.so.3")
            cmd = [os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                   "-g", "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
                   "-I" + str(base / "stubs"),
                   "-I" + str(ROOT / "firmware/components/update_manifest/include"),
                   str(base / "runtime.c"),
                   str(ROOT / "firmware/components/update_manifest/update_manifest.c"),
                   crypto, "-o", str(exe)]
            subprocess.run(cmd, check=True, timeout=60)
            args = [str(exe)]
            if os.environ.get("CW_REPRO_PREFIX_DEADLINE"):
                args.append("--repro-prefix-deadline")
            elif os.environ.get("CW_REPRO_EXTRA_OTADATA"):
                args.append("--repro-extra-otadata")
            sanitizers = {**os.environ,
                          "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                          "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
            subprocess.run(args, check=True, timeout=30, env=sanitizers)
            # Compile the same actual updater with stale TLS configuration.
            # HAVE_TIME alone must not enable HTTPS certificate-date bypass.
            subprocess.run(cmd + ["-DCW_NO_TIME_DATE"], check=True, timeout=60)
            subprocess.run([str(exe)], check=True, timeout=30, env=sanitizers)

if __name__ == "__main__":
    unittest.main()
