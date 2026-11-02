"""Host regression runner; all binaries/generated fixtures go to TEMP.

Adam7 independent encoder adapted from SI4684-FMDAB-Receiver 4a4b2d2,
tests/test_epg_png_adam7.py (GPLv3). No Pillow/EPG/system zlib dependency.
The harness links the exact PNGdec 1.1.6 inflate sources used by firmware.
"""
from pathlib import Path
import argparse
import random
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
PASSES = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
          (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
MAGIC = b'\x89PNG\r\n\x1a\n'


def chunk(tag, data):
    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))


def chunks(data):
    pos = 8
    while pos < len(data):
        length = struct.unpack('>I', data[pos:pos + 4])[0]
        yield data[pos + 4:pos + 8], data[pos + 8:pos + 8 + length]
        pos += length + 12


def assemble(parts):
    return MAGIC + b''.join(chunk(tag, data) for tag, data in parts)


def paeth(a, b, c):
    p = a + b - c
    distances = [abs(p - a), abs(p - b), abs(p - c)]
    return [a, b, c][distances.index(min(distances))]


def encode(width, height, kind, depth, extra=b'', bad_filter=False, interlace=True,
           palette_alpha=True):
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[kind]
    maximum = (1 << depth) - 1
    palette = [((i * 71) % 256, (i * 127) % 256, (i * 193) % 256)
               for i in range(min(1 << depth, 37))]
    alpha = [(i * 97) % 256 if palette_alpha else 255 for i in range(len(palette))]
    samples, rgb = [], []
    for y in range(height):
        for x in range(width):
            # Large images deliberately compress below the real 50 KiB limit.
            sx, sy = (x // 16, y // 16) if width > 64 else (x, y)
            values = [((sx * 17 + sy * 31 + c * 43) ^ (sx * sy * 7)) & maximum
                      for c in range(channels)]
            if kind == 3:
                values[0] %= len(palette)
            samples.append(values)
            eight = lambda n: n >> 8 if depth == 16 else n if depth == 8 else n * 255 // maximum
            if kind == 3:
                r, g, b = palette[values[0]]
                a = alpha[values[0]]
            elif kind in (0, 4):
                r = g = b = eight(values[0])
                a = eight(values[1]) if kind == 4 else (0 if values[0] == 0 else 255)
            else:
                r, g, b = map(eight, values[:3])
                a = eight(values[3]) if kind == 6 else (0 if values[:3] == [0, 0, 0] else 255)
            r, g, b = [(v * a + 127) // 255 for v in (r, g, b)]
            rgb.append((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3))
    raw = bytearray()
    row_number = 0
    for x0, y0, dx, dy in PASSES if interlace else [(0, 0, 1, 1)]:
        if x0 >= width or y0 >= height:
            continue
        previous = None
        for y in range(y0, height, dy):
            values = [v for x in range(x0, width, dx) for v in samples[y * width + x]]
            if depth >= 8:
                line = b''.join(v.to_bytes(depth // 8, 'big') for v in values)
            else:
                line = bytearray((len(values) * depth + 7) // 8)
                for index, value in enumerate(values):
                    line[index * depth // 8] |= value << (8 - depth - index * depth % 8)
            if previous is None:
                previous = bytes(len(line))
            stride = max(1, (channels * depth + 7) // 8)
            filter_kind = row_number % 5
            row_number += 1
            raw.append(5 if bad_filter else filter_kind)
            for index, value in enumerate(line):
                a = line[index - stride] if index >= stride else 0
                b = previous[index]
                c = previous[index - stride] if index >= stride else 0
                predictor = [0, a, b, (a + b) // 2, paeth(a, b, c)][filter_kind]
                raw.append((value - predictor) & 255)
            previous = line
    parts = [(b'IHDR', struct.pack('>IIBBBBB', width, height, depth, kind, 0, 0, int(interlace)))]
    if kind == 3:
        parts += [(b'PLTE', bytes(v for color in palette for v in color)), (b'tRNS', bytes(alpha))]
    elif kind == 0:
        parts += [(b'tRNS', b'\0\0')]
    elif kind == 2:
        parts += [(b'tRNS', b'\0' * 6)]
    compressed = zlib.compress(bytes(raw) + extra)
    step = 1 if width * height < 10 else 997
    parts += [(b'IDAT', compressed[i:i + step]) for i in range(0, len(compressed), step)]
    parts += [(b'IEND', b'')]
    return assemble(parts), struct.pack('<II', width, height) + struct.pack('<' + 'H' * len(rgb), *rgb)


def fixtures(directory):
    paths = []
    def save(name, data, expected=None):
        path = directory / name
        path.write_bytes(data)
        if expected is not None:
            Path(str(path) + '.expected').write_bytes(expected)
        paths.append(path)
    for kind, depths in [(0, [1, 2, 4, 8, 16]), (2, [8, 16]),
                         (3, [1, 2, 4, 8]), (4, [8, 16]), (6, [8, 16])]:
        for depth in depths:
            for width, height in [(1, 1), (3, 7), (17, 13), (319, 127), (320, 240), (320, 320)]:
                data, expected = encode(width, height, kind, depth)
                assert len(data) <= 51200
                save(f'{kind}_{depth}_{width}_{height}.png', data, expected)
    base, expected = encode(17, 13, 6, 8)
    parts = list(chunks(base))
    idat = b''.join(data for tag, data in parts if tag == b'IDAT')
    header = parts[0][1]
    def compressed_change(data):
        return assemble([(b'IHDR', header), (b'IDAT', data), (b'IEND', b'')])
    invalid = [base[:-1], base[:32], base[:29] + bytes([base[29] ^ 1]) + base[30:],
               encode(17, 13, 6, 8, extra=b'X')[0], encode(17, 13, 6, 8, bad_filter=True)[0],
               compressed_change(idat[:-1] + bytes([idat[-1] ^ 1])),
               compressed_change(idat + b'junk'),
               compressed_change(zlib.compress(zlib.decompress(idat)[:-1])),
               assemble(parts[::-1]), assemble([parts[0], parts[0]] + parts[1:]),
               assemble([(b'IHDR', header), (b'NOPE', b''), *parts[1:]]),
               assemble([(b'IHDR', header), (b'abcd', b''), *parts[1:]]),
               compressed_change(idat[:len(idat)//2]),
               MAGIC + b'\xff\xff\xff\xff' + base[12:],
               assemble([(b'IHDR', header), (b'IDAT', idat[:3]), (b'tEXt', b'a'),
                         (b'IDAT', idat[3:]), (b'IEND', b'')])]
    for offset, value in [(0, 0xFFFFFFFF), (0, 0), (0, 321), (4, 321)]:
        changed = bytearray(header)
        changed[offset:offset + 4] = struct.pack('>I', value)
        invalid.append(assemble([(b'IHDR', bytes(changed)), *parts[1:]]))
    for offset, value in [(8, 3), (9, 1), (10, 1), (11, 1), (12, 0), (12, 2)]:
        changed = bytearray(header)
        changed[offset] = value
        invalid.append(assemble([(b'IHDR', bytes(changed)), *parts[1:]]))
    indexed, _ = encode(17, 13, 3, 8)
    indexed_parts = list(chunks(indexed))
    invalid += [assemble([(tag, data) for tag, data in indexed_parts if tag != b'PLTE']),
                assemble([(tag, b'\0\0\0' if tag == b'PLTE' else data)
                          for tag, data in indexed_parts if tag != b'tRNS'])]
    for length in [51200, 51201]:
        padding = length - len(base) - 12
        padded = assemble([*parts[:-1], (b'tEXt', b'X' * padding), parts[-1]])
        assert len(padded) == length
        if length == 51200:
            save('limit.png', padded, expected)
        else:
            invalid.append(padded)
    save('empty_idat.png', assemble([parts[0], (b'IDAT', b''),
         *parts[1:-1], (b'IDAT', b''), parts[-1]]), expected)
    for length in range(0, len(base), max(1, len(base)//40)):
        invalid.append(base[:length])
    rng = random.Random(4684)
    for _ in range(128):
        changed = bytearray(base)
        at = rng.randrange(8, len(changed))
        changed[at] ^= 1 << rng.randrange(8)
        invalid.append(changed)
    for index, data in enumerate(invalid):
        save(f'{index}.bad.png', data)
    return paths


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--cc', default='gcc')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    lib = ROOT / '.pio/libdeps/FMDABRadio/PNGdec/src'
    if not lib.is_dir():
        parser.error('Run pio run first to install PNGdec 1.1.6.')
    with tempfile.TemporaryDirectory(prefix='fmdab_sls_') as temp:
        directory = Path(temp)
        flags = ['-D__LINUX__', '-I' + str(lib)]
        sanitizers = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
        objects = []
        for name in ['adler32', 'crc32', 'infback', 'inffast', 'inflate', 'inftrees', 'zutil']:
            obj = directory / (name + '.o')
            # PNGdec uses the Arduino unsigned-int alias in one C signature.
            subprocess.run([args.cc, *flags, '-Duint=unsigned', *sanitizers,
                            '-c', str(lib / (name + '.c')), '-o', str(obj)], check=True)
            objects.append(str(obj))
        common = [args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic', *flags, *sanitizers]
        executable = directory / 'adam7.exe'
        subprocess.run([*common, str(ROOT / 'tests/test_png_adam7.cpp'),
                        str(ROOT / 'src/PngAdam7.cpp'), *objects, '-o', str(executable)], check=True)
        paths = fixtures(directory)
        subprocess.run([str(executable), *map(str, paths)], check=True, cwd=ROOT)
        # The unchanged noninterlaced path still uses PNGdec, not Adam7.
        ordinary = []
        # PNGdec's existing RGB565 converter has no grayscale 2/4-bit cases;
        # that pre-existing limitation is documented, not counted as PASS.
        for kind, depth in [(0, 1), (0, 8), (2, 8), (3, 1), (3, 2),
                            (3, 4), (3, 8), (4, 8), (6, 8)]:
            # PNGdec also ignores tRNS alpha in packed 1/2/4-bit palette
            # RGB565 conversion. Exercise opaque palette there, alpha at 8bit.
            data, expected = encode(17, 13, kind, depth, interlace=False,
                                    palette_alpha=depth >= 8)
            path = directory / f'standard_{kind}_{depth}.png'
            path.write_bytes(data)
            Path(str(path) + '.expected').write_bytes(expected)
            ordinary.append(path)
        png_object = directory / 'PNGdec.o'
        subprocess.run([args.cxx, '-std=c++11', *flags, *sanitizers, '-c',
                        str(lib / 'PNGdec.cpp'), '-o', str(png_object)], check=True)
        executable = directory / 'standard_png.exe'
        subprocess.run([*common, str(ROOT / 'tests/test_png_standard.cpp'),
                        str(png_object), *objects, '-o', str(executable)], check=True)
        subprocess.run([str(executable), *map(str, ordinary)], check=True)
        executable = directory / 'slideshow_render.exe'
        subprocess.run([*common, '-I' + str(ROOT / 'tests/host_shims'),
                        str(ROOT / 'tests/test_slideshow_render.cpp'),
                        str(ROOT / 'src/Slideshow.cpp'), str(ROOT / 'src/PngAdam7.cpp'),
                        str(ROOT / 'src/JPEGdecoder.cpp'), str(png_object), *objects,
                        '-o', str(executable)], check=True)
        subprocess.run([str(executable), str(directory / '6_8_320_320.png'),
                        str(directory / 'standard_6_8.png')], check=True, cwd=ROOT)
        for name in ['radio_scheduler', 'fm_features', 'dab_service_switch', 'mot_assembly', 'jpeg']:
            sources = [str(ROOT / f'tests/test_{name}.cpp')]
            if name == 'jpeg':
                sources += [str(ROOT / 'src/JPEGdecoder.cpp')]
            executable = directory / (name + '.exe')
            subprocess.run([*common, *sources, '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True, cwd=ROOT)
            print('PASS', name, flush=True)


if __name__ == '__main__':
    main()
