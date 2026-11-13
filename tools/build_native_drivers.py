"""Compile the exported GPL modules locally. Never install or load a module."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
COMPONENTS = ('ptz', 'illumination', 'sd', 'msc-clock', 'flash')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel-tree', type=Path, required=True)
    parser.add_argument('--kernel-build', type=Path, required=True)
    parser.add_argument('--cross-compile', default='mipsel-linux-gnu-')
    parser.add_argument('--arch', default='mips')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/native-drivers')
    parser.add_argument('--component', action='append', choices=COMPONENTS)
    args = parser.parse_args()
    tree, build, output = (p.resolve() for p in
                           (args.kernel_tree, args.kernel_build, args.output))
    if not (tree / 'Makefile').is_file() or not (build / '.config').is_file():
        parser.error('A prepared kernel source/build tree is required')
    if output.exists():
        parser.error('Use a fresh output directory; existing files are preserved')
    if output.is_relative_to(tree) or output.is_relative_to(build):
        parser.error('Output must be outside the kernel source/build tree')
    output.mkdir(parents=True)
    manifest = json.loads((ROOT / 'native/drivers/SOURCE-MANIFEST.json').read_text())
    exports = json.loads((ROOT / 'native/SOURCE-MANIFEST.json').read_text())
    env = dict(os.environ, ARCH=args.arch, CROSS_COMPILE=args.cross_compile,
               KBUILD_BUILD_USER='qc5', KBUILD_BUILD_HOST='kernel-lab',
               KBUILD_BUILD_TIMESTAMP='2026-11-04 00:00:00 +0000')
    results = []
    for component in args.component or COMPONENTS:
        destination = output / component
        destination.mkdir()
        if component == 'flash':
            source = ROOT / 'native/modules'
            rows = [{'path':Path(p).name,'sha256':r['sha256']}
                    for p,r in exports['files'].items() if p.startswith('modules/')]
            wanted = manifest['flash_modules']
        else:
            record = next(r for r in manifest['components'] if r['component']==component)
            source = ROOT / 'native/drivers' / component
            rows = record['files']
            wanted = {record['module']:record['deployed_binary_sha256']}
        for row in rows:
            path = source / row['path']
            if path.is_symlink() or digest(path) != row['sha256']:
                raise ValueError('Source changed: '+str(path))
            shutil.copyfile(path, destination / row['path'])
        shutil.copyfile(source / 'Makefile', destination / 'Makefile')
        command = ['make','-C',str(tree),'O='+str(build),'-j1',
                   'M='+str(destination),'modules']
        with (destination / 'build.log').open('wb') as log:
            subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=240)
        modules = []
        for module in sorted(destination.glob('*.ko')):
            subprocess.run([args.cross_compile+'strip','--strip-debug',str(module)],
                           check=True, timeout=30)
            h = digest(module)
            modules.append({'module':module.name,'sha256':h,
                            'matches_deployed_bytes':h==wanted[module.name]
                            if module.name in wanted else None})
        if not modules:
            raise RuntimeError('Kbuild produced no modules: '+component)
        results.append({'component':component,'compiled':True,'modules':modules})
    report = {'compiled':True,'architecture':args.arch,'components':results,
              'kernel_config_sha256':digest(build / '.config'),
              'module_install_or_load_attempted':False,'camera_accessed':False,
              'binary_firmware_in_sources':False}
    (output / 'build.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
