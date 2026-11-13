"""Public synthetic vectors contain no factory calibration data."""
import importlib.util
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('quality', ROOT/'tools/tune_native_profile.py')
QUALITY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(QUALITY)


def fixture():
    words = [0] * 16591
    initial, patch, read = QUALITY.offsets()
    sections = [(b*170, initial) for b in range(2)]
    for b in range(2):
        for g in range(25):
            sections.extend([(340+(b*25+g)*161, patch), (8390+(b*25+g)*164, read)])
    for start, regs in sections:
        for n, reg in enumerate(regs):
            value = ((start+n)*2654435761) & 0xffffffff
            if reg in QUALITY.CURVES:
                value = 0xfaf0c8a0
            elif reg in (0x799c, 0x7a6c):
                value = 0x32fa14ff
            elif reg in (0x79a0, 0x7a70):
                value = 0x003200ff
            words[start+n] = value
    raw = struct.pack('<16590I', *words[:-1])
    return raw + struct.pack('<I', QUALITY.checksum(raw)), sections


class QualityProfile(unittest.TestCase):
    def test_all_states_keep_spatial_bits_and_cap_all_reference_paths(self):
        original, sections = fixture()
        changed = QUALITY.transform(original)
        before = struct.unpack('<16591I', original)
        after = struct.unpack('<16591I', changed)
        self.assertEqual(len(sections), 102)
        for start, regs in sections:
            for n, reg in enumerate(regs):
                old, new = before[start+n], after[start+n]
                self.assertEqual((old ^ new) & ~QUALITY.MASKS.get(reg, 0), 0)
                if reg in QUALITY.CURVES:
                    self.assertEqual(new, 0xc0c0c0a0)
                elif reg in (0x799c, 0x7a6c):
                    self.assertEqual(new, 0x32c014c0)
                elif reg in (0x79a0, 0x7a70):
                    self.assertEqual(new, 0x003200c0)
        self.assertEqual(after[-1], QUALITY.checksum(changed[:-4]))
        self.assertEqual(QUALITY.transform(changed), changed)

    def test_full_ceiling_preserves_input_and_header_roundtrip(self):
        raw, _ = fixture()
        self.assertEqual(QUALITY.transform(raw, 255), raw)
        data = QUALITY.transform(raw)
        values = [int(v) for v in re.findall(r'(\d+)u', QUALITY.render_header(data))]
        self.assertEqual(struct.pack('<16591I', *values), data)

    def test_invalid_input_or_incompatible_minimum_is_rejected(self):
        raw, _ = fixture()
        for data in (b'', raw[:-1], raw+b'\0', bytes([raw[0]^1])+raw[1:]):
            with self.assertRaises(ValueError):
                QUALITY.transform(data)
        for value in (-1, 256, True, 1.5):
            with self.assertRaises(ValueError):
                QUALITY.transform(raw, value)
        with self.assertRaises(ValueError):
            QUALITY.transform(raw, 49)

    def test_cli_does_not_overwrite_and_creates_private_output(self):
        raw, _ = fixture()
        with tempfile.TemporaryDirectory(prefix='qc5-quality-') as tmp:
            source, dest = Path(tmp)/'input', Path(tmp)/'output'
            source.write_bytes(raw)
            command = [sys.executable, str(ROOT/'tools/tune_native_profile.py'), str(source), str(dest)]
            subprocess.run(command, check=True, capture_output=True)
            self.assertEqual(dest.read_bytes(), QUALITY.transform(raw))
            if os.name != 'nt':
                self.assertEqual(dest.stat().st_mode & 0o777, 0o600)
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(source.read_bytes(), raw)
            self.assertEqual(dest.read_bytes(), QUALITY.transform(raw))

    def test_zero_result_checksum_cannot_enter_the_firmware(self):
        raw, _ = fixture()
        old_digest = struct.unpack_from('<I', raw, len(raw)-4)[0]
        with patch.object(QUALITY, 'checksum', side_effect=[old_digest, 0]):
            with self.assertRaises(ValueError):
                QUALITY.transform(raw)
        with patch.object(QUALITY, 'checksum', return_value=0):
            with self.assertRaises(ValueError):
                QUALITY.render_header(raw[:-4]+bytes(4))


if __name__ == '__main__':
    unittest.main()
