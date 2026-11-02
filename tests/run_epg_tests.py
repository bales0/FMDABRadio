"""EPG L0 replay and L1 discovery with host radio shims; no hardware access."""
from pathlib import Path
import argparse
import base64
import hashlib
import subprocess
import tempfile
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='fmdab_epg_') as temporary:
        output = Path(temporary)
        fixture = output / 'synthetic.bin'
        fixture.write_bytes(base64.b64decode((ROOT / 'tests/fixtures/epg/synthetic_logo_1_groups_v2.b64').read_text()))
        assert hashlib.sha256(fixture.read_bytes()).hexdigest() == '52e6f81385ed6edb6d2246aa889e0833762fdb9a5d881648699f1c3c7126e6c8'
        for name in ['core', 'errors', 'cache', 'multitid', 'contract']:
            exe = output / (name + '.exe')
            subprocess.run([args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                            '-DEPG_ENABLE=1', '-I' + str(ROOT / 'src'),
                            str(ROOT / f'tests/test_epg_{name}.cpp'),
                            str(ROOT / 'src/epg_core.cpp'), '-o', str(exe)], check=True)
            subprocess.run([str(exe), *([str(fixture)] if name == 'multitid' else [])], check=True)
        for enabled in [1, 0]:
            exe = output / f'discovery_{enabled}.exe'
            subprocess.run([args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        f'-DEPG_DISCOVERY_ENABLE={enabled}',
                        '-DFMDAB_HOST_TEST', '-Wno-error=switch',
                        '-I' + str(ROOT / 'tests/radio_shims'),
                        '-I' + str(ROOT / 'tests/host_shims'), '-I' + str(ROOT / 'src'),
                        str(ROOT / 'tests/test_epg_discovery.cpp'), str(ROOT / 'src/si4684.cpp'),
                        '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
        print('EPG L0 + L1 host suites PASS (no hardware access)', flush=True)


if __name__ == '__main__':
    main()
