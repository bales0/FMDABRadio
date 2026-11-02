"""Bound Adafruit's initR/commonInit clock too, without editing libdeps.

Only the default-frequency line is replaced in a generated build-local copy.
All Adafruit command tables, CS handling and notices remain unchanged.
Fail closed if the upstream line changes, rather than silently boot at 32MHz.
"""
from pathlib import Path


def configured_source(source, config_header, library_header):
    marker = '#define SPI_DEFAULT_FREQ 32000000'
    if source.count(marker) != 1:
        raise RuntimeError('Adafruit ST77xx default frequency changed; audit initR before building')
    return source.replace('#include "Adafruit_ST77xx.h"',
                          f'#include "{library_header}"', 1).replace(
        marker, f'#include "{config_header}"\n#define SPI_DEFAULT_FREQ board_bus::TFT_SPI_HZ', 1)


def install(build_env):
    def replace(env, node):
        original = Path(node.srcnode().get_abspath())
        root = Path(env.subst('$PROJECT_DIR'))
        output = Path(env.subst('$BUILD_DIR')) / 'configured_st77xx' / original.name
        source = configured_source(original.read_text(encoding='utf-8'),
                                   (root / 'src/BusConfig.h').as_posix(),
                                   original.with_suffix('.h').as_posix())
        output.parent.mkdir(parents=True, exist_ok=True)
        if not output.exists() or output.read_text(encoding='utf-8') != source:
            output.write_text(source, encoding='utf-8')
        return env.File(str(output))
    build_env.AddBuildMiddleware(replace, '*Adafruit_ST77xx.cpp')


try:
    Import
except NameError:
    pass  # importable by the host regression test, without SCons installed
else:
    Import('env')
    install(env)
