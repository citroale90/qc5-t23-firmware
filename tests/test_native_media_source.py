"""Public media utilities: actual C behavior, provenance and nonce generation."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeMediaSource(unittest.TestCase):
    def test_annexb_roundtrip_chunk_seams_and_bounds(self):
        with tempfile.TemporaryDirectory(prefix='qc5-annexb-') as tmp:
            binary = Path(tmp) / 'test-annexb'
            subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-UNDEBUG',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            str(ROOT/'native/media/video-gc1084/test-annexb.c'),
                            '-o', str(binary)], check=True, capture_output=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True,
                                    text=True, timeout=15).stdout
            self.assertIn('625 vectors, 3125 seams', result)

    def test_x264_helpers_have_the_pinned_bytes_and_explicit_license(self):
        directory = ROOT/'native/media/openimp'
        manifest = json.loads((directory/'SOURCE-MANIFEST.json').read_text())
        self.assertEqual(manifest['license'], 'GPL-2.0-or-later')
        self.assertEqual(len(manifest['files']), 11)
        notice = (directory/'NOTICE').read_bytes()
        self.assertEqual(hashlib.sha256(notice).hexdigest(), manifest['notice_sha256'])
        self.assertIn(b'GPL-2.0-or-later: src/t30/h264enc/', notice)
        for relative, record in manifest['files'].items():
            self.assertEqual(hashlib.sha256((directory/relative).read_bytes()).hexdigest(), record['sha256'])

    def test_build_nonce_is_private_fresh_and_never_overwrites(self):
        spec = importlib.util.spec_from_file_location('nonce', ROOT/'tools/generate_native_nonce.py')
        module = importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(prefix='qc5-nonce-') as tmp:
            first,second = Path(tmp)/'first.h',Path(tmp)/'second.h'
            module.generate(first);module.generate(second)
            a,b = first.read_bytes(),second.read_bytes()
            self.assertNotEqual(a,b)
            values = re.search(rb'\[16\]=\{([0-9,]+)\}', a).group(1).split(b',')
            self.assertEqual(len(values), 16)
            self.assertTrue(all(0 <= int(v) <= 255 for v in values))
            self.assertEqual(first.stat().st_mode & 0o777, 0o600)
            with self.assertRaises(FileExistsError):module.generate(first)
            alias = Path(tmp)/'alias.h';alias.symlink_to(first)
            with self.assertRaises(FileExistsError):module.generate(alias)
            self.assertEqual(first.read_bytes(),a)


if __name__ == '__main__':
    unittest.main()
