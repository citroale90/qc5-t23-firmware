"""Exercise six-phase homing, faults, Stop and strict post-home jogs offline."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
ACTUATORS=ROOT/'native/media/native-integrated/native-features/native-actuators'

class StartupCalibration(unittest.TestCase):
    def test_homing_and_failure_policy(self):
        with tempfile.TemporaryDirectory(prefix='qc5-home-') as tmp:
            binary=Path(tmp)/'test-homing'
            subprocess.run(['cc','-std=c11','-O1','-g','-UNDEBUG',
                            '-fsanitize=address,undefined','-fno-sanitize-recover=all',
                            '-I',str(ROOT/'tests/include'),'-I',str(ACTUATORS),
                            str(ROOT/'tests/ptz_startup_calibration_driver.c'),
                            '-o',str(binary)],check=True,capture_output=True)
            output=subprocess.check_output([str(binary)],text=True,timeout=15)
        self.assertIn('checks=1900',output)
        self.assertIn('Stop at every phase, exact jogs',output)

if __name__=='__main__':unittest.main()
