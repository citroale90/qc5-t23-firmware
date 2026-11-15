"""Regress the actual extract-to-card layout and generated integrity chain."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('sd_builder',ROOT/'native/installer/common/build_sd_package.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
sha=lambda b:hashlib.sha256(b).hexdigest()

class DirectSDPackage(unittest.TestCase):
    def fixtures(self,root):
        base=root/'base';p=base/'qc5-installer';p.mkdir(parents=True)
        files={'native.img':bytes(0x750000),'prepare':b'OLD_PREPARER',
               'env-template.ko':b'ENV','Q73BOOT.IMG':b'NEW_LOADER',
               'busybox':b'BUSYBOX','run.sh':b'OLD_WRONG_PINS',
               'manifest.json':b'{"release":"OLD","files":{}}',
               'config.txt.example':b'SSID=\nPWD=\n','RIPRISTINO.txt':b'old guide'}
        for name,body in files.items():(p/name).write_bytes(body)
        for name,body in {'qc5-run':b'OLD_ENTRY','NOTICE.txt':b'notice',
                          'VERIFICA-HARDWARE.json':b'{}'}.items():(base/name).write_bytes(body)
        paths=[f for f in base.rglob('*') if f.is_file()]
        (base/'PACCHETTO.json').write_text(json.dumps({'files':{str(f.relative_to(base)):{'bytes':f.stat().st_size,'sha256':sha(f.read_bytes())} for f in paths}}))
        compiled=root/'compiled';compiled.mkdir()
        (compiled/'prepare').write_bytes(b'NEW_PREPARER');(compiled/'env-template.ko').write_bytes(b'ENV')
        receipt={'native_image_sha256':sha(files['native.img']),
                 'production_preparer_readonly':True,
                 'files':{n:sha((compiled/n).read_bytes()) for n in ['prepare','env-template.ko']}}
        (compiled/'build.json').write_text(json.dumps(receipt))
        return base,compiled

    def test_extract_alone_is_ready_and_preserves_existing_owner_files(self):
        with tempfile.TemporaryDirectory(prefix='qc5-extract-') as tmp:
            r=Path(tmp);base,compiled=self.fixtures(r);out=r/'release'
            report=builder.assemble(base,compiled,out,'QC5-7.3.1-R47-SD',None)
            sd=r/'SD';sd.mkdir();(sd/'config.txt').write_bytes(b'KEEP_WIFI')
            backup=sd/'qc5-backups/unit-example';backup.mkdir(parents=True)
            (backup/'original-1.bin').write_bytes(b'KEEP_BACKUP')
            with zipfile.ZipFile(report['archive']) as z:
                names=z.namelist();self.assertEqual(names[-1],builder.TRIGGER)
                self.assertIn('qc5-run',names);self.assertNotIn('config.txt',names)
                self.assertFalse(any(n.startswith(('__MACOSX/','QC5-R47-SD/')) or '/._' in n for n in names))
                z.extractall(sd)  # deliberately no preparation program
            self.assertEqual((sd/'qc5-installer/AZIONE.txt').read_bytes(),b'INSTALLA\n')
            self.assertEqual((sd/builder.TRIGGER).read_bytes(),b'')
            self.assertEqual((sd/'config.txt').read_bytes(),b'KEEP_WIFI')
            self.assertEqual((backup/'original-1.bin').read_bytes(),b'KEEP_BACKUP')
            run=(sd/'qc5-installer/run.sh').read_text();entry=(sd/'qc5-run').read_text()
            for file in ['prepare','env-template.ko','Q73BOOT.IMG','busybox']:
                self.assertIn(sha((sd/'qc5-installer'/file).read_bytes()),run)
            self.assertIn(hashlib.md5((sd/'qc5-installer/run.sh').read_bytes()).hexdigest(),entry)
            self.assertIn(hashlib.md5((sd/'qc5-installer/busybox').read_bytes()).hexdigest(),entry)
            self.assertNotIn('OLD_WRONG_PINS',run);self.assertNotIn('@RUN_MD5@',entry)
            m=json.loads((sd/'qc5-installer/manifest.json').read_text())
            self.assertEqual(m['cold_entry_sha256'],sha((sd/'qc5-run').read_bytes()))
            self.assertFalse(m['PC_script_required_for_factory_install'])
            self.assertTrue(m['native_update_requires_own_backup_and_preparation_tool'])

    def test_preparer_for_another_payload_is_refused_before_output(self):
        with tempfile.TemporaryDirectory(prefix='qc5-pins-') as tmp:
            r=Path(tmp);base,compiled=self.fixtures(r);p=compiled/'build.json'
            receipt=json.loads(p.read_text());receipt['native_image_sha256']='ff'*32;p.write_text(json.dumps(receipt))
            with self.assertRaisesRegex(ValueError,'payload do not match'):
                builder.assemble(base,compiled,r/'out','TEST',None)
            self.assertFalse((r/'out').exists())

    def test_changed_compiled_binary_and_damaged_base_are_refused(self):
        for what in ['prepare','base','symlink']:
            with self.subTest(what=what),tempfile.TemporaryDirectory(prefix='qc5-chain-') as tmp:
                r=Path(tmp);base,compiled=self.fixtures(r)
                if what=='prepare':(compiled/'prepare').write_bytes(b'TAMPER')
                elif what=='base':(base/'qc5-installer/busybox').write_bytes(b'TAMPER')
                else:
                    p=base/'qc5-installer/busybox';p.unlink();p.symlink_to(compiled/'prepare')
                with self.assertRaises(ValueError):builder.assemble(base,compiled,r/'out','TEST',None)
                self.assertFalse((r/'out').exists())

if __name__=='__main__':unittest.main()
