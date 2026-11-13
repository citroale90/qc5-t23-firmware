"""Exercise the exported flash-record code on synthetic, in-memory banks."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / 'native'


class NativePersistence(unittest.TestCase):
    def compile_and_run(self, folder, filename, include=()):
        with tempfile.TemporaryDirectory(prefix='qc5-native-records-') as tmp:
            binary = Path(tmp) / 'test-records'
            subprocess.run([
                'cc', '-std=gnu11', '-O1', '-g', '-UNDEBUG',
                '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                '-I', str(NATIVE / 'common'),
                *[arg for path in include for arg in ('-I', str(path))],
                str(NATIVE / folder / filename), '-o', str(binary),
            ], check=True, capture_output=True)
            return subprocess.run([str(binary)], check=True, capture_output=True,
                                  text=True, timeout=15).stdout

    def test_ssh_identity_survives_torn_writes_without_rotation(self):
        result = self.compile_and_run('ssh', 'test-key.c')
        self.assertIn('redundant_banks=1', result)
        self.assertIn('same_identity_no_writes_1000boots=1', result)
        self.assertIn('repair_and_513_partial_write_boundaries=1', result)

    def test_wifi_profile_survives_every_partial_record_write(self):
        result = self.compile_and_run('wifi', 'test-banks.c')
        self.assertIn('512 byte-boundary power cuts', result)

    def test_controller_recovery_does_not_remove_live_network(self):
        result = self.compile_and_run('wifi', 'test-owned-reset.c')
        self.assertTrue(result.strip())

    def test_exported_sources_match_manifest(self):
        data = json.loads((NATIVE / 'SOURCE-MANIFEST.json').read_text())
        self.assertFalse(data['firmware_blobs_and_flash_dumps_included'])
        self.assertFalse(data['turnkey_native_build'])
        for relative, record in data['files'].items():
            with self.subTest(path=relative):
                path = NATIVE / relative
                self.assertFalse(path.is_symlink())
                self.assertTrue(path.resolve().is_relative_to(NATIVE.resolve()))
                self.assertFalse(any('private' in v.lower() for v in Path(relative).parts))
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),
                                 record['sha256'])



if __name__ == '__main__':
    unittest.main()
