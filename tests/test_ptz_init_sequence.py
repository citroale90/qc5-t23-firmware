"""PTZ initialization must preserve the original grouped register writes."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class PtzInitialization(unittest.TestCase):
    def test_original_trace_and_fault_policy(self):
        with tempfile.TemporaryDirectory(prefix='qc5-ptz-init-') as tmp:
            binary = Path(tmp)/'test-init'
            subprocess.run(['cc','-std=c11','-O1','-UNDEBUG','-Wall','-Wextra',
                            '-Wno-unused-function','-I',str(ROOT/'native/drivers'),
                            str(ROOT/'tests/ptz_init_sequence_driver.c'),
                            '-o',str(binary)],check=True,capture_output=True)
            output = subprocess.check_output([str(binary)],text=True)
        steps = [[int(v) for v in line.split()[1:]] for line in output.splitlines()
                 if line.startswith('STEP ')]
        original = json.loads((ROOT/'tests/fixtures/ptz-original-init.json').read_text())
        self.assertEqual(steps[1],[0x16,0,50])  # explicit bridge-off safety
        self.assertEqual(steps[:1]+steps[2:],original['steps'])
        self.assertIn('PASS initialization success and 23 injected transfer faults',output)

if __name__ == '__main__':
    unittest.main()
