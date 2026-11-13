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

    def test_ssh_publication_recovers_from_interrupted_helper(self):
        result = self.compile_and_run('ssh', 'test-key-file.c')
        self.assertIn('stale_temp=1', result)
        self.assertIn('symlink_target_preserved=1', result)
        self.assertIn('interrupted_short_writes=1', result)
        self.assertIn('atomic_failure=1', result)
        self.assertIn('mode_0600=1', result)

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

    def test_standalone_driver_sources_match_byte_identical_rebuild_receipt(self):
        manifest = json.loads((NATIVE / 'drivers/SOURCE-MANIFEST.json').read_text())
        proof = json.loads((NATIVE / 'drivers/R43-compile-verification.json').read_text())
        self.assertTrue(proof['all_seven_identical_to_deployed'])
        self.assertFalse(proof['module_install_or_load_attempted'])
        modules = [m for c in proof['components'] for m in c['modules']]
        self.assertEqual(len(modules), 7)
        combined = json.loads((NATIVE / 'drivers/R43-all-driver-verification.json').read_text())
        self.assertEqual(len(combined['modules']), 9)
        self.assertTrue(all(m['matches_deployed_bytes'] for m in combined['modules']))
        radio = json.loads((NATIVE / 'radio/SOURCE-MANIFEST.json').read_text())
        self.assertEqual(len(radio['files']), 121)
        self.assertFalse(radio['radio_firmware_bin_files_included'])
        self.assertEqual(hashlib.sha256((NATIVE / 'radio/qc5-r43-aic8800.patch').read_bytes()).hexdigest(), radio['patch_sha256'])
        self.assertTrue(all(m['matches_deployed_bytes'] for m in modules))
        for component in manifest['components']:
            for row in component['files']:
                path = NATIVE / 'drivers' / component['component'] / row['path']
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), row['sha256'])
        self.assertEqual(hashlib.sha256((NATIVE / 'kernel/qc5-7.3.1.config').read_bytes()).hexdigest(),
                         proof['kernel_config_sha256'])



if __name__ == '__main__':
    unittest.main()
