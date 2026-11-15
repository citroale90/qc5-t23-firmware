from pathlib import Path
import subprocess,tempfile,unittest
R=Path(__file__).resolve().parents[1]
class DayNightAuto(unittest.TestCase):
 def test_manual_override_auto_reentry_and_ambient_policy(self):
  with tempfile.TemporaryDirectory() as d:
   binary=Path(d)/'daynight'
   subprocess.run(['cc','-O1','-g','-UNDEBUG','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',str(R/'native/media/native-integrated/native-continuous'),str(R/'tests/daynight_auto_driver.c'),'-o',str(binary)],check=True,capture_output=True)
   p=subprocess.run([str(binary)],capture_output=True,text=True,check=True);self.assertIn('DAYNIGHT_AUTO_PASS',p.stdout)
if __name__=='__main__':unittest.main()
