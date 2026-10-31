"""Compile and exercise the firmware adapter with captured packets on the host."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class NativeAdapterTests(unittest.TestCase):
    def test_native_scenarios(self):
        component = Path(__file__).resolve().parents[1] / "firmware/components/bookoo"
        with tempfile.TemporaryDirectory(prefix="coffee-bookoo-") as directory:
            executable = Path(directory) / "test_bookoo"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-g", "-I", str(component / "include"),
                            str(component / "bookoo.c"), str(component / "test/test_bookoo.c"),
                            "-o", str(executable)], check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([str(executable)], check=True, capture_output=True, text=True, timeout=10)
            self.assertIn("7 scenario tests passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
