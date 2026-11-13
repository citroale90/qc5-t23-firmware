"""Exercise the real startup control flow with synthetic keys and command stubs."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

STUB = r'''#!/usr/bin/python3
import json,os,shutil,sys
from pathlib import Path
root=Path(os.environ['QC5_FIXTURE']);a=sys.argv[1:];role=Path(sys.argv[0]).name
with (root/'calls').open('a') as f:f.write(json.dumps([role]+a)+'\n')
key=root/'ram/hostkey'
if role=='flash':
 if a==['load']:
  code=int(os.environ['LOAD_RESULT'])
  if code==0:key.write_bytes(b'F'*83)
  sys.exit(code)
 if a==['store']:sys.exit(int(os.environ.get('STORE_RESULT','0')))
 raise AssertionError(a)
if role=='dropbear':
 assert a[0]=='dropbearkey',a
 p=Path(a[a.index('-f')+1])
 if '-y' in a:sys.exit(0 if p.is_file() and p.stat().st_size==83 else 1)
 assert a[1:4]==['-t','ed25519','-f'],a
 p.write_bytes(b'G'*83);sys.exit(0)
if role=='busybox':
 cmd=a.pop(0)
 if cmd in ['--install','grep']:sys.exit(0)
 if cmd=='mkdir':
  for v in a:
   if not v.startswith('-') and str(root) in v:Path(v).mkdir(mode=0o700,parents=True,exist_ok=True)
  sys.exit(0)
 if cmd=='rm':
  for v in a:
   if v.startswith('-'):continue
   p=Path(v);assert p.is_relative_to(root),p
   if p.exists():p.unlink()
  sys.exit(0)
 if cmd=='stat':print(Path(a[-1]).stat().st_size);sys.exit(0)
 if cmd=='cp':
  assert all(Path(v).is_relative_to(root) for v in a),a
  shutil.copyfile(*a);sys.exit(0)
 raise AssertionError(cmd)
raise AssertionError(role)
'''


class SSHStartup(unittest.TestCase):
    def run_start(self, load, store=0, ram=None, sd=None):
        with tempfile.TemporaryDirectory(prefix='qc5-ssh-start-') as folder:
            root = Path(folder)
            (root/'ram').mkdir(mode=0o700)
            (root/'sd').mkdir(mode=0o700)
            if ram is not None:(root/'ram/hostkey').write_bytes(ram)
            if sd is not None:(root/'sd/ssh-hostkey').write_bytes(sd)
            for name in ['busybox','flash','dropbear']:
                p=root/name;p.write_text(STUB);p.chmod(0o700)
            script=(ROOT/'native/ssh/ssh-start.sh').read_text()
            script=script.replace('/bin/busybox-full',str(root/'busybox'))
            script=script.replace('/bin/qc5-ssh-key',str(root/'flash'))
            script=script.replace('/bin/dropbearmulti',str(root/'dropbear'))
            script=script.replace('/run/qc5-ssh',str(root/'ram'))
            script=script.replace('/mnt/sda0/qc5-state',str(root/'sd'))
            path=root/'start.sh';path.write_text(script)
            result=subprocess.run(['/bin/sh',str(path),'--prepare-key'],
                env=dict(os.environ,QC5_FIXTURE=str(root),LOAD_RESULT=str(load),
                         STORE_RESULT=str(store)),capture_output=True,text=True,timeout=5)
            calls=[json.loads(line) for line in (root/'calls').read_text().splitlines()]
            key=(root/'ram/hostkey').read_bytes() if (root/'ram/hostkey').is_file() else None
            source=(root/'ram/identity-source').read_text().strip() if (root/'ram/identity-source').is_file() else None
            return result,key,source,calls

    def no_generation(self, calls):
        self.assertFalse(any(c[0]=='dropbear' and '-t' in c for c in calls))

    def test_flash_identity_is_preserved(self):
        result,key,source,calls=self.run_start(0)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(key,b'F'*83);self.assertEqual(source,'flash')
        self.no_generation(calls)

    def test_blank_flash_creates_and_persists_one_identity(self):
        result,key,source,calls=self.run_start(2)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(key,b'G'*83);self.assertEqual(source,'flash')

    def test_corrupt_flash_does_not_generate_another_identity(self):
        result,key,source,calls=self.run_start(1)
        self.assertNotEqual(result.returncode,0)
        self.assertIsNone(key);self.no_generation(calls)

    def test_unavailable_flash_does_not_import_an_unbound_SD_key(self):
        result,key,source,calls=self.run_start(67,store=1,sd=b'S'*83)
        self.assertNotEqual(result.returncode,0)
        self.assertIsNone(key);self.no_generation(calls)

    def test_blank_camera_does_not_inherit_another_camera_SD_identity(self):
        result,key,source,calls=self.run_start(2,sd=b'S'*83)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(key,b'G'*83)

    def test_initial_persistence_failure_is_not_advertised_as_permanent(self):
        result,key,source,calls=self.run_start(2,store=1)
        self.assertNotEqual(result.returncode,0)
        self.assertNotEqual(source,'flash')
        self.assertIsNone(key)

    def test_valid_RAM_identity_survives_a_flash_read_error(self):
        result,key,source,calls=self.run_start(1,store=1,ram=b'R'*83)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(key,b'R'*83);self.no_generation(calls)

    def test_replica_repair_failure_does_not_discard_verified_flash_key(self):
        result,key,source,calls=self.run_start(0,store=1)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(key,b'F'*83);self.assertEqual(source,'flash')


if __name__=='__main__':unittest.main()
