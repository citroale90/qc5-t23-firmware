"""Read-only AIC8800DC firmware inventory. Does not load code into a radio."""
# SPDX-License-Identifier: GPL-3.0-only
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import struct

MAX_BYTES = 1024 * 1024


def parse_table(data):
    if len(data) < 128 or (len(data)-128) % 8:
        raise ValueError('Patch table needs 128 metadata bytes and complete 8-byte pairs')
    address = struct.unpack_from('<I', data)[0]
    description = data[4:128].split(b'\0', 1)[0].decode('ascii', errors='replace')
    pairs = list(struct.iter_unpack('<II', data[128:]))
    return address, description, pairs


def read_regular(path):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    with os.fdopen(fd, 'rb') as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or not 0 < info.st_size <= MAX_BYTES:
            raise ValueError('Expected a nonempty regular firmware file <=1 MiB')
        data = stream.read(MAX_BYTES+1)
        if len(data) != info.st_size:
            raise ValueError('Firmware file changed size during read')
        return data


def inventory(directory):
    result = []
    for variant in ('u02', 'h_u02'):
        for kind, base in (('patch', 0x180000), ('calib', 0x130000), ('patch_tbl', None)):
            name = 'fmacfw_' + kind + '_8800dc_' + variant + '.bin'
            data = read_regular(directory / name)
            row = {'name':name, 'bytes':len(data), 'sha256':hashlib.sha256(data).hexdigest()}
            if base is not None:
                row.update(load_address=hex(base), loaded_end_exclusive=hex(base+len(data)))
                if kind == 'calib':
                    row.update(driver_start_address='0x130009', thumb_instruction_address='0x130008')
            else:
                address, description, pairs = parse_table(data)
                row.update(metadata_address=hex(address), description=description, pairs=len(pairs),
                           target_64KiB_windows=sorted({hex(a & 0xffff0000) for a, _ in pairs}))
            result.append(row)
    return {'camera_accessed':False, 'radio_code_executed':False,
            'binary_payloads_included':False, 'files':result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, help='Local lib/firmware directory')
    args = parser.parse_args()
    print(json.dumps(inventory(args.directory), indent=2))


if __name__ == '__main__':main()
