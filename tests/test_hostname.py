"""Storage upgrade and owner failure tests use synthetic credentials only."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class HostnameTests(unittest.TestCase):
    def test_flash_banks_and_owner(self):
        with tempfile.TemporaryDirectory() as folder:
            for name in ['test-hostname-banks','test-hostname-owner']:
                with self.subTest(program=name):
                    binary=Path(folder)/name
                    args=['cc','-O1','-g','-UNDEBUG','-DQC5_HOST_TEST','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',str(ROOT/'native/common'),'-I',str(ROOT/'native/wifi'),str(ROOT/'native/wifi'/(name+'.c'))]
                    if name.endswith('owner'):args +=[str(ROOT/'native/common/qc5_wifi_store.c'),str(ROOT/'native/common/qc5_network_policy.c')]
                    subprocess.run([*args,'-o',str(binary)],check=True,capture_output=True)
                    p=subprocess.run([str(binary)],check=True,capture_output=True,text=True)
                    self.assertIn('PASS',p.stdout)
if __name__=='__main__':unittest.main()
