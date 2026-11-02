"""Compile actual driver with deterministic SPI/time; no hardware access."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
def function(source, name):
    start = source.index('void ' + name + '(')
    pos = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]

with tempfile.TemporaryDirectory(prefix='fmdab_audio_') as directory:
    exe = Path(directory) / 'audio.exe'
    subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                    '-DFMDAB_HOST_TEST', '-Wno-error=switch',
                    '-I' + str(ROOT / 'tests/radio_shims'), '-I' + str(ROOT / 'tests/host_shims'),
                    '-I' + str(ROOT / 'src'), str(ROOT / 'tests/test_audio_activation.cpp'),
                    str(ROOT / 'src/si4684.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    app = Path(directory) / 'app.cpp'
    source = (ROOT / 'src/RadioDAB.ino').read_text(encoding='utf-8')
    body = '\n'.join(function(source, name) for name in ['finishBandTransition', 'handleRadioEvents'])
    app.write_text((ROOT / 'tests/test_app_audio_boot.cpp').read_text().replace('// PRODUCTION_FUNCTIONS', body))
    subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                    str(app), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
