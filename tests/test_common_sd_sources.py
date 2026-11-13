"""Integrity of the separately qualified first-install source snapshot."""
from pathlib import Path
import hashlib,json,unittest
COMMON=Path(__file__).resolve().parents[1]/'native/installer/common'
class CommonSDSources(unittest.TestCase):
 def test_snapshot_hashes_and_binary_rebuild_receipt(self):
  manifest=json.loads((COMMON/'SOURCE-MANIFEST.json').read_text())
  self.assertFalse(manifest['binary_payload_and_factory_dumps_published'])
  self.assertFalse(manifest['per_unit_data_published'])
  self.assertEqual(manifest['production_preparer_compile_definition'],'QC5_PREPARE_ONLY')
  for name,wanted in manifest['files'].items():
   with self.subTest(name=name):
    self.assertEqual(Path(name).name,name);p=COMMON/name;self.assertFalse(p.is_symlink());self.assertEqual(hashlib.sha256(p.read_bytes()).hexdigest(),wanted)
  proof=json.loads((COMMON/'REBUILD-VERIFICATION.json').read_text());self.assertTrue(proof['passed']);self.assertTrue(proof['public_builder_recreates_shipped_preparer_and_ENV_module_byte_identically'])
 def test_validation_keeps_physical_boundary_explicit(self):
  proof=json.loads((COMMON/'VALIDATION.json').read_text());self.assertTrue(proof['passed_offline']);self.assertFalse(proof['current_camera_accessed']);self.assertTrue(proof['physical_twin_installation_still_pending']);self.assertGreaterEqual(proof['generic_C_cases'],24);self.assertGreaterEqual(proof['PC_preparation_and_reuse_cases'],13)
if __name__=='__main__':unittest.main()
