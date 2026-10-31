"""Fetch the exact official board drivers; no runtime network dependency."""
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parent / 'components'
DRIVERS = {'M5GFX': ('0.2.19', '53a7184601f3667b030ba141c58b87ce2acfaa2a'),
           'M5IOE1': ('1.0.8', '37db04861687858b6748f5dbd1a84439a6635c46'),
           'M5PM1': ('1.0.6', '8f1f1a60b3040088cd0ca2b9eb9d024ccc4d907f')}
for name, (tag, commit) in DRIVERS.items():
    target = ROOT / name
    if not target.exists():
        subprocess.run(['git', 'clone', '--depth', '1', '--branch', tag,
                        f'https://github.com/m5stack/{name}.git', str(target)], check=True)
    actual = subprocess.check_output(['git', '-C', str(target), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != commit:
        raise SystemExit(f'{name}: expected {commit}, got {actual}')
    if name != 'M5GFX':
        # Use the driver's documented native i2c_master_bus_handle_t overload,
        # sharing one bus with touch. The demo has no i2c_bus/M5Unified dependency.
        (target / 'CMakeLists.txt').write_text(
            f'idf_component_register(SRCS "src/{name}.cpp" INCLUDE_DIRS "src" REQUIRES driver M5GFX)\n')
