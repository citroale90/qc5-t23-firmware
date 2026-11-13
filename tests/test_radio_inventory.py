"""Offline inspection is bounded and never treats a firmware file as a program."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('qc5_aic_inventory', ROOT/'tools/inspect_aic_firmware.py')
inventory = importlib.util.module_from_spec(spec);spec.loader.exec_module(inventory)


class RadioInventory(unittest.TestCase):
    def test_metadata_and_pairs_are_separate_and_NUL_is_not_required(self):
        data = struct.pack('<I',0x187c00)+b'A'*124+struct.pack('<II',0x180004,123)
        address,description,pairs = inventory.parse_table(data)
        self.assertEqual(address,0x187c00);self.assertEqual(description,'A'*124)
        self.assertEqual(pairs,[(0x180004,123)])

    def test_every_malformed_length_is_rejected(self):
        for size in range(514):
            if size >= 128 and (size-128)%8 == 0:
                inventory.parse_table(bytes(size))
            else:
                with self.assertRaises(ValueError):inventory.parse_table(bytes(size))

    def test_file_reader_refuses_links_empty_and_oversized_inputs(self):
        with tempfile.TemporaryDirectory(prefix='qc5-radio-inventory-') as tmp:
            target=Path(tmp)/'data';target.write_bytes(b'synthetic')
            self.assertEqual(inventory.read_regular(target),b'synthetic')
            link=Path(tmp)/'alias';link.symlink_to(target)
            with self.assertRaises(OSError):inventory.read_regular(link)
            target.write_bytes(b'')
            with self.assertRaises(ValueError):inventory.read_regular(target)
            with target.open('wb') as stream:stream.truncate(inventory.MAX_BYTES+1)
            with self.assertRaises(ValueError):inventory.read_regular(target)


if __name__ == '__main__':unittest.main()
