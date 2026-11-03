"""Host-test production Ui.ino render bodies, not a duplicate implementation."""
from pathlib import Path
import argparse
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    start = next(source.index(prefix + name + '(') for prefix in ['void ', 'uint16_t ', 'bool ', 'String ', 'uint32_t ']
                 if prefix + name + '(' in source)
    opening = source.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    source = (ROOT / 'src/Ui.ino').read_text(encoding='utf-8')
    names = ['invalidateListeningPaint', 'renderCachedUiText', 'uiSignalColor',
             'uiRssiColor', 'uiFicColor', 'renderUiHeader', 'renderUiStationName',
             'pushUiTextStrip', 'drawUiTextStrip', 'renderUiTextArea', 'renderUiSignal', 'renderUiStatusStatic']
    body = '\n'.join(function(source, name) for name in names)
    harness = (ROOT / 'tests/test_ui_paint.cpp').read_text(encoding='utf-8').replace('// PRODUCTION_FUNCTIONS', body)
    # Include resolves relative to the generated TEMP file, so use an explicit source include.
    harness = harness.replace('#include "../src/UiTextCache.h"', '#include "UiTextCache.h"')
    with tempfile.TemporaryDirectory(prefix='fmdab_ui_') as temporary:
        directory = Path(temporary)
        path = directory / 'ui.cpp'
        path.write_text(harness, encoding='utf-8')
        exe = directory / 'ui.exe'
        subprocess.run([args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-I' + str(ROOT / 'src'), str(path), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        info = (ROOT / 'src/EpgUi.ino').read_text(encoding='utf-8')
        body = '\n'.join(function(source, name) for name in ['nextUtf8Codepoint', 'utf8Slice'])
        body += '\n' + '\n'.join(function(info, name) for name in [
            'isInfoView', 'infoLine', 'invalidateInfoRows', 'epgTimeText', 'renderInfoView',
            'handleInfoButton', 'serviceInfoView'])
        body += '\n' + '\n'.join(function(source, name) for name in [
            'renderMenu', 'renderCurrentUiView', 'applyUiBacklightSettings', 'adjustMenu', 'openSettingsMenu',
            'closeSettingsMenu', 'serviceMenuModeCommit', 'handleUiButtonEvent'])
        path.write_text((ROOT / 'tests/test_ui_epg.cpp').read_text(encoding='utf-8').replace('// PRODUCTION_FUNCTIONS', body), encoding='utf-8')
        subprocess.run([args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-DFMDAB_HOST_TEST', '-DEPG_ENABLE=1', '-DINPUT=0',
                        '-I' + str(ROOT / 'tests/radio_shims'), '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'tests'),
                        str(path), str(ROOT / 'src/Controls.cpp'), str(ROOT / 'src/epg_live.cpp'), str(ROOT / 'src/epg_core.cpp'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        subprocess.run([args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-DINPUT=0', '-I' + str(ROOT / 'tests/radio_shims'), '-I' + str(ROOT / 'src'),
                        str(ROOT / 'tests/test_band_controls.cpp'), str(ROOT / 'src/Controls.cpp'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        body = '\n'.join(function(source, name) for name in ['renderSlideshowScreen', 'serviceUiSlideshow'])
        path.write_text((ROOT / 'tests/test_ui_slideshow.cpp').read_text(encoding='utf-8').replace('// PRODUCTION_FUNCTIONS', body), encoding='utf-8')
        subprocess.run([args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-DFMDAB_HOST_TEST', '-Wno-error=switch',
                        '-I' + str(ROOT / 'tests/radio_shims'), '-I' + str(ROOT / 'tests/host_shims'),
                        '-I' + str(ROOT / 'src'), str(path), str(ROOT / 'src/si4684.cpp'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    assert 'invalidateListeningPaint();' in (ROOT / 'src/Display.ino').read_text()
    print('LCD cache and invalidation contract PASS', flush=True)


if __name__ == '__main__':
    main()
