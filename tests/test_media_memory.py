"""Metadata reset must not copy payload capacity or expose stale frame bytes."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class MediaMemory(unittest.TestCase):
    def test_large_frame_pages_are_released_without_changing_frame_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(__file__).resolve().parents[1];binary=str(Path(folder)/'media-trim')
            subprocess.run(['cc','-Os','-Wall','-Wextra','-Werror',str(root/'tests/media_memory_driver.c'),str(root/'src/qc5_mp4.c'),str(root/'src/qc5_wifi_store.c'),'-o',binary],check=True)
            memory=Path(folder)/'meminfo';memory.write_text('MemAvailable: 4096 kB\n')
            result=subprocess.run([binary,'--trim-check'],env=dict(os.environ,QC5_TEST_MEMINFO=str(memory)),capture_output=True,text=True,timeout=5)
            self.assertEqual(result.returncode,0,result.stderr)
            report=json.loads(result.stdout)
            self.assertEqual(report['frames_exact'],3)
            self.assertEqual(report['maximum_frame_bytes'],512*1024)
            if os.uname().sysname=='Linux':self.assertGreaterEqual(report['rss_reclaimed_kib'],384)

    def test_frame_growth_preserves_reserve_and_rejects_missing_memory_sample(self):
        with tempfile.TemporaryDirectory()as folder:
            root=Path(__file__).resolve().parents[1];binary=str(Path(folder)/'media-growth')
            subprocess.run(['cc','-Os','-Wall','-Wextra','-Werror',str(root/'tests/media_memory_driver.c'),str(root/'src/qc5_mp4.c'),str(root/'src/qc5_wifi_store.c'),'-o',binary],check=True)
            result=subprocess.run([binary,'--growth-tests',str(Path(folder)/'meminfo')],capture_output=True,text=True,timeout=5)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('6 video growth reserve checks passed',result.stdout)
    def test_payload_reuse_keeps_frames_and_size_limits_correct(self):
        with tempfile.TemporaryDirectory() as folder:
            supplied=os.environ.get('QC5_MEDIA_MEMORY_TEST_COMMAND')
            if supplied:
                command=json.loads(supplied)
                if not isinstance(command,list) or not command or any(not isinstance(x,str) or not x for x in command):
                    raise ValueError('QC5_MEDIA_MEMORY_TEST_COMMAND must be a nonempty JSON argv list')
            else:
                root=Path(__file__).resolve().parents[1];binary=str(Path(folder)/'media-memory')
                subprocess.run(['cc','-Os','-Wall','-Wextra','-Werror',str(root/'tests/media_memory_driver.c'),str(root/'src/qc5_mp4.c'),str(root/'src/qc5_wifi_store.c'),'-o',binary],check=True)
                command=[binary]
            memory=Path(folder)/'meminfo';memory.write_text('MemAvailable: 4096 kB\n')
            result=subprocess.run(command,env=dict(os.environ,QC5_TEST_MEMINFO=str(memory)),capture_output=True,text=True,timeout=5)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('4 media buffer reuse and size checks passed',result.stdout)
