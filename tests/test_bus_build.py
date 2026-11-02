"""Check build-local Adafruit adaptation and reject overclock configurations."""
import importlib.util
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def check(compiler, directory, common):
    spec = importlib.util.spec_from_file_location('st77xx_clock', ROOT / 'scripts/st77xx_clock.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    source = ROOT / '.pio/libdeps/FMDABRadio/Adafruit ST7735 and ST7789 Library/Adafruit_ST77xx.cpp'
    original = source.read_text(encoding='utf-8')
    configured = module.configured_source(original, 'BusConfig.h', 'Adafruit_ST77xx.h')
    assert '#define SPI_DEFAULT_FREQ board_bus::TFT_SPI_HZ' in configured
    assert '#define SPI_DEFAULT_FREQ 32000000' not in configured
    assert 'MIT license' in configured
    assert original.replace('#define SPI_DEFAULT_FREQ 32000000',
                            '#include "BusConfig.h"\n#define SPI_DEFAULT_FREQ board_bus::TFT_SPI_HZ') == configured
    try:
        module.configured_source(original.replace('32000000', '40000000'), 'BusConfig.h', 'Adafruit_ST77xx.h')
    except RuntimeError:
        pass
    else:
        raise AssertionError('upstream drift must fail closed')
    assert source.read_text(encoding='utf-8') == original
    eeprom = (ROOT / 'src/eeprom.ino').read_text(encoding='utf-8')
    assert eeprom.count('Wire.setClock(') == 1
    assert 'Wire.setClock(board_bus::EEPROM_I2C_HZ)' in eeprom
    driver = (ROOT / 'src/si4684.cpp').read_text(encoding='utf-8')
    assert 'SLS_COLLECTION_TIMEOUT_MS' not in driver
    assert 'collection timeout:' not in driver
    function = driver.split('void DAB::task() {', 1)[1].split('bool DAB::ready()', 1)[0]
    assert 'resetSlideshowAssembler(' not in function
    for override in ['FMDAB_TFT_SPI_HZ=32000000UL', 'FMDAB_TFT_SPI_HZ=40000000UL',
                     'FMDAB_RADIO_SPI_HZ=20000000UL', 'FMDAB_EEPROM_I2C_HZ=1000000UL']:
        result = subprocess.run([*common, '-D' + override, '-c', str(ROOT / 'tests/test_bus_config.cpp'),
                                 '-o', str(directory / 'forbidden.o')], capture_output=True, text=True)
        assert result.returncode != 0 and 'overclock forbidden' in result.stderr
    exe = directory / 'bus_fallback.exe'
    subprocess.run([*common, '-DFMDAB_TFT_SPI_HZ=8000000UL', '-DFMDAB_RADIO_SPI_HZ=2000000UL',
                    str(ROOT / 'tests/test_bus_config.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    print('Bus build PASS: initR default, lower-clock override, overclock compile rejection, no idle reset', flush=True)
