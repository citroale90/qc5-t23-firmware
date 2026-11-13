"""Run actual candidate loader with a synthetic transport, never a radio."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RadioLoader(unittest.TestCase):
    def test_malformed_lengths_io_errors_and_transport_order(self):
        with tempfile.TemporaryDirectory(prefix='qc5-loader-') as tmp:
            binary = Path(tmp)/'test'
            subprocess.run(['cc','-O1','-g','-UNDEBUG','-fsanitize=address,undefined',
                '-fno-sanitize-recover=all',str(ROOT/'native/radio/hardening/test-patch-loader.c'),
                '-o',str(binary)],check=True,capture_output=True)
            output = subprocess.check_output([str(binary)],text=True,timeout=15)
            self.assertIn('514 file lengths',output)
            self.assertIn('load/header/pair errors and cleanup verified',output)

    def test_audit_overlay_is_pinned_and_keeps_120_baseline_inputs(self):
        folder = ROOT/'native/radio'
        base = json.loads((folder/'SOURCE-MANIFEST.json').read_text())
        audit = json.loads((folder/'hardening/SOURCE-MANIFEST.json').read_text())
        self.assertFalse(audit['hardware_tested'])
        self.assertFalse(audit['radio_firmware_changed'])
        self.assertEqual(audit['base_patch_sha256'],base['patch_sha256'])
        digest = lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
        self.assertEqual(digest(folder/'hardening/bounded-patch-table.patch'),audit['patch_sha256'])
        self.assertEqual(digest(folder/'hardening/patch-table-load.inc'),audit['function_source_sha256'])
        before = {r['path']:r['sha256'] for r in base['files']}
        after = {r['path']:r['sha256'] for r in audit['files']}
        self.assertEqual(set(before),set(after))
        self.assertEqual([p for p in before if before[p]!=after[p]],
                         ['aic8800_fdrv/aicwf_compat_8800dc.c'])


if __name__ == '__main__':unittest.main()
