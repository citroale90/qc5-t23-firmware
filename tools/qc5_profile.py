"""Layout and immutable fingerprints for QC5_YA_CODE_V82."""

import hashlib
import re
import struct

BOARD = 'QC5_YA_CODE_V82-T23-GC1084-AIC8800DC-USB'
BOOT_CODE_SIZE = 0x3c000
BOOT_CODE_SHA = 'edb9b4a93a2112d63fa1242031a4089393d19d91d143b8a86d91fee16630f65b'
ERASE = 0x8000
HEADER_SIZE = 512
PARTITIONS = [
    ('boot', 0x0, 0x40000, None),
    ('kernel', 0x40000, 0x158000, 'da9edc55709742183d6431febeffe930408e5919ce9a302d86ca9baa56b307fc'),
    ('root', 0x198000, 0x360000, 'feebc6f945a887fdc045ebf7d593d57e254107aafa46b3221e7d44e514e08b8b'),
    ('system', 0x4f8000, 0x298000, 'ea45d6015bf5a2315154e1bfb6eb55f4c2e067ffe3d059fa2f6d90c8961b8346'),
    ('www', 0x790000, 0x30000, 'db894df2a0fc3be05aa197afae3836871d77bfe500d9c69788a3fb47ed21a889'),
    ('param', 0x7c0000, 0x40000, None),
]
LAYOUT = ''.join(f'mtd{i}:{size:08x}:{ERASE:08x}:{name}\n'
                 for i, (name, _, size, _) in enumerate(PARTITIONS)) + 'mtd6:00800000:00008000:all\n'


def sha(data):
    return hashlib.sha256(data).digest()


def encode_text(text, length):
    if not re.fullmatch(r'[A-Za-z0-9._-]{1,63}', text):
        raise ValueError('Etichetta versione non valida')
    return text.encode('ascii').ljust(length, b'\0')


def squash_check(image, size):
    if len(image) != size or len(image) < 96:
        raise ValueError('Dimensione partizione non valida')
    if (struct.unpack_from('<I', image)[0], struct.unpack_from('<I', image, 12)[0],
        struct.unpack_from('<H', image, 20)[0], struct.unpack_from('<HH', image, 28)) != (
            0x73717368, 65536, 4, (4, 0)):
        raise ValueError('Formato SquashFS diverso dal profilo QC5')
    if not 96 <= struct.unpack_from('<Q', image, 40)[0] <= size:
        raise ValueError('Lunghezza SquashFS non valida')
