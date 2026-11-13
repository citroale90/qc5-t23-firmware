"""Source archive validation, with synthetic fixtures and no camera access."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


class RadioSources(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='qc5-source-archive-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.metadata = self.root / 'native/radio'
        self.metadata.mkdir(parents=True)
        spec = importlib.util.spec_from_file_location('qc5_source_test', ROOT / 'tools/prepare_aic_source.py')
        self.program = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.program)
        self.program.ROOT = self.root
        self.archive = self.root / 'source.tar.gz'
        self.output = self.root / 'prepared'

    def fixture(self, symlink=False):
        before, after = b'int x = 1;\n', b'int x = 2;\n'
        with tarfile.open(self.archive, 'w:gz') as tar:
            member = tarfile.TarInfo('upstream/drivers/a.c')
            if symlink:
                member.type = tarfile.SYMTYPE
                member.linkname = '../../../outside'
                tar.addfile(member)
            else:
                member.size = len(before)
                tar.addfile(member, io.BytesIO(before))
            blob = tarfile.TarInfo('upstream/fw/opaque.bin')
            blob.size = 4
            tar.addfile(blob, io.BytesIO(b'BLOB'))
        change = b'--- a/a.c\n+++ b/a.c\n@@ -1 +1 @@\n-int x = 1;\n+int x = 2;\n'
        (self.metadata / 'qc5-r43-aic8800.patch').write_bytes(change)
        sha = lambda b: hashlib.sha256(b).hexdigest()
        self.manifest = {'archive_sha256':sha(self.archive.read_bytes()),
            'patch_sha256':sha(change),'archive_driver_prefix':'upstream/drivers/',
            'files':[{'path':'a.c','upstream_sha256':sha(before),'sha256':sha(after)}]}
        self.save()

    def save(self):
        (self.metadata / 'SOURCE-MANIFEST.json').write_text(json.dumps(self.manifest))

    def run_prepare(self):
        with patch.object(sys, 'argv', ['prepare', '--archive', str(self.archive),
                                       '--output', str(self.output)]):
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.program.main()

    @unittest.skipUnless(shutil.which('patch'), 'patch program required')
    def test_reconstructs_only_pinned_source_and_excludes_radio_binary(self):
        self.fixture()
        self.run_prepare()
        self.assertEqual((self.output / 'a.c').read_bytes(), b'int x = 2;\n')
        self.assertFalse((self.output / 'fw').exists())
        self.assertEqual([p.name for p in self.output.iterdir()], ['a.c'])

    def test_corrupt_archive_is_rejected_before_output_creation(self):
        self.fixture()
        self.archive.write_bytes(b'corrupt')
        with self.assertRaises(SystemExit):self.run_prepare()
        self.assertFalse(self.output.exists())

    def test_existing_output_is_preserved(self):
        self.fixture()
        self.output.mkdir()
        marker = self.output / 'owner-data'
        marker.write_bytes(b'preserve')
        with self.assertRaises(SystemExit):self.run_prepare()
        self.assertEqual(marker.read_bytes(), b'preserve')

    def test_archive_source_symlink_is_rejected(self):
        self.fixture(symlink=True)
        with self.assertRaises(ValueError):self.run_prepare()
        self.assertFalse((self.output / 'a.c').exists())

    def test_manifest_traversal_is_rejected(self):
        self.fixture()
        self.manifest['files'][0]['path'] = '../outside'
        self.save()
        with self.assertRaises(ValueError):self.run_prepare()
        self.assertFalse((self.root / 'outside').exists())


if __name__ == '__main__':
    unittest.main()
