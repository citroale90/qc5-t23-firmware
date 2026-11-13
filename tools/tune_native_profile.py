#!/usr/bin/env python3
"""Build-time GC1084 reference-weight policy; no device, DMA or flash access.

Input is the existing private 66,364-byte native MDNS profile, not a firmware
image. Calibration data is supplied locally and is never included here.
"""
# SPDX-License-Identifier: GPL-3.0-only
import argparse
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'native/media/video-gc1084/mdns-offsets.h'
SIZE = 66364
CURVES = {0x7948 + 16*g + 4*w for g in range(4) for w in range(4)}
CURVES |= {0x7988 + 4*w for w in range(4)}
MASKS = {r: 0xffffffff for r in CURVES}
MASKS.update({0x799c: 0x00ff00ff, 0x79a0: 0xff,
              0x7a6c: 0x00ff00ff, 0x7a70: 0xff})


def checksum(raw):
    value = 2166136261
    for byte in raw:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def offsets():
    text = HEADER.read_text()
    result = []
    # Init and gain sequences deliberately repeat trigger/enable ports.
    for name, count, unique in [('initial', 170, 164), ('patch', 161, 160), ('read', 164, 164)]:
        body = re.search(r'qc5_mdns_' + name + r'_offsets\[[^]]+\]=\{(.*?)\};', text, re.S)
        if not body:
            raise ValueError('Missing native offset table')
        rows = [int(v, 16) for v in re.findall(r'0x[0-9a-f]+', body[1])]
        if len(rows) != count or len(set(rows)) != unique or not MASKS.keys() <= set(rows):
            raise ValueError('Unsupported native offset layout')
        result.append(rows)
    return result


def limit_word(reg, value, ceiling):
    mask = MASKS.get(reg, 0)
    for shift in range(0, 32, 8):
        if mask & (255 << shift):
            byte = min((value >> shift) & 255, ceiling)
            value = (value & ~(255 << shift)) | (byte << shift)
    # Original Y/C F/B/R minimums must remain usable after the max clamp.
    if reg in (0x799c, 0x7a6c):
        fmax, fmin = value & 255, (value >> 8) & 255
        bmax, bmin = (value >> 16) & 255, (value >> 24) & 255
        if fmin > fmax or bmin > bmax:
            raise ValueError('Reference ceiling is below a calibrated minimum')
    elif reg in (0x79a0, 0x7a70) and ((value >> 8) & 255) > (value & 255):
        raise ValueError('Reference ceiling is below a calibrated minimum')
    return value


def transform(raw, ceiling=192):
    if type(ceiling) is not int or not 0 <= ceiling <= 255:
        raise ValueError('Reference ceiling must be an integer in0..255')
    if len(raw) != SIZE:
        raise ValueError('Expected one native66,364-byte profile')
    words = list(struct.unpack('<16591I', raw))
    if not words[-1] or checksum(raw[:-4]) != words[-1]:
        raise ValueError('Invalid native profile checksum')
    initial, patch, read = offsets()
    sections = [(bank*170, initial) for bank in range(2)]
    for bank in range(2):
        for gain in range(25):
            sections.extend([(340+(bank*25+gain)*161, patch),
                             (8390+(bank*25+gain)*164, read)])
    for start, regs in sections:
        for n, reg in enumerate(regs):
            words[start+n] = limit_word(reg, words[start+n], ceiling)
    body = struct.pack('<16590I', *words[:-1])
    digest = checksum(body)
    if not digest:
        raise ValueError('Zero checksum is not a valid native profile')
    return body + struct.pack('<I', digest)


def render_header(raw):
    if len(raw) != SIZE:
        raise ValueError('Invalid native profile')
    digest = struct.unpack_from('<I', raw, SIZE-4)[0]
    if not digest or checksum(raw[:-4]) != digest:
        raise ValueError('Invalid native profile')
    words = struct.unpack('<16591I', raw)
    def row(values):
        return '{' + ','.join(str(v)+'u' for v in values) + '}'
    initial = ','.join(row(words[b*170:(b+1)*170]) for b in range(2))
    patch = ','.join('{' + ','.join(row(words[340+(b*25+g)*161:340+(b*25+g+1)*161])
                                  for g in range(25)) + '}' for b in range(2))
    expected = ','.join('{' + ','.join(row(words[8390+(b*25+g)*164:8390+(b*25+g+1)*164])
                                     for g in range(25)) + '}' for b in range(2))
    return ('/* Locally derived private GC1084 calibration; reference policy applied at build time. */\n'
            'static const struct qc5_mdns_profile gc1084_mdns={.initial={'+initial+
            '},.patch={'+patch+'},.expected={'+expected+'},.checksum='+str(words[-1])+'u};\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--ceiling', type=int, default=192)
    parser.add_argument('--format', choices=['binary', 'c'], default='binary')
    args = parser.parse_args()
    with args.input.open('rb') as source:
        result = transform(source.read(SIZE+1), args.ceiling)
    data = render_header(result).encode() if args.format == 'c' else result
    with args.output.open('xb') as dest:
        args.output.chmod(0o600)
        dest.write(data)


if __name__ == '__main__':
    main()
