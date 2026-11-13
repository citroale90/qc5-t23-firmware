"""Boot-input checks: an outer kernel checksum cannot detect a nested initrd."""
import importlib.util
import lzma
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('qc5_flash_builder',ROOT/'native/boot/build-flash-kernel.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

def cpio(entries):
    out=bytearray()
    for name,mode,body in [*entries,('TRAILER!!!',0,b'')]:
        encoded=name.encode()+b'\0'
        fields=[1,mode,0,0,1,0,len(body),0,0,0,0,len(encoded),0]
        out+=b'070701'+''.join('%08x'%n for n in fields).encode()+encoded
        out+=bytes(-len(out)%4);out+=body;out+=bytes(-len(out)%4)
    return bytes(out)

class BootArchiveTest(unittest.TestCase):
    def valid(self):return cpio([('init',0o100500,b'\x7fELFtest-bootstrap')])
    def test_valid_single_layer(self):
        result=module.validate_initramfs(lzma.compress(self.valid()))
        self.assertTrue(result['single_XZ_to_newc']);self.assertEqual(result['entries'],1)
    def test_nested_xz_is_not_a_bootable_cpio(self):
        with self.assertRaises(ValueError):module.validate_initramfs(lzma.compress(lzma.compress(self.valid())))
    def test_truncated_body_or_trailer(self):
        for data in [self.valid()[:125],self.valid()[:-12]]:
            with self.subTest(length=len(data)),self.assertRaises(ValueError):module.validate_initramfs(lzma.compress(data))
    def test_missing_or_non_executable_init(self):
        for entries in [[],[('init',0o100400,b'\x7fELFtest')],[('init',0o120777,b'\x7fELFtest')]]:
            with self.subTest(entries=entries),self.assertRaises(ValueError):module.validate_initramfs(lzma.compress(cpio(entries)))
    def test_duplicate_and_unsafe_paths(self):
        for entries in [[('init',0o100500,b'\x7fELF'),('init',0o100500,b'\x7fELF')],[('../init',0o100500,b'\x7fELF')]]:
            with self.subTest(entries=entries),self.assertRaises(ValueError):module.validate_initramfs(lzma.compress(cpio(entries)))

if __name__=='__main__':unittest.main()
