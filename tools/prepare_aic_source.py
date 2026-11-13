"""Reconstruct the R43 Linux radio driver source offline; no firmware blobs."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/aic-source')
    args = parser.parse_args()
    manifest = json.loads((ROOT / 'native/radio/SOURCE-MANIFEST.json').read_text())
    patch = ROOT / 'native/radio/qc5-r43-aic8800.patch'
    sha = lambda b: hashlib.sha256(b).hexdigest()
    if not args.archive.is_file() or args.archive.is_symlink():
        parser.error('Use the original regular upstream source archive')
    if sha(args.archive.read_bytes()) != manifest['archive_sha256']:
        parser.error('Upstream archive checksum mismatch')
    if sha(patch.read_bytes()) != manifest['patch_sha256']:
        parser.error('QC5 patch checksum mismatch')
    if args.output.is_symlink() or args.output.exists():
        parser.error('Use a fresh output directory; existing data is preserved')
    output = args.output.resolve()
    output.mkdir(parents=True)
    with tarfile.open(args.archive, 'r:gz') as archive:
        entries = archive.getmembers()
        if len({m.name for m in entries}) != len(entries):
            raise ValueError('Duplicate archive entries')
        members = {m.name:m for m in entries}
        for row in manifest['files']:
            name = PurePosixPath(row['path'])
            if name.is_absolute() or '..' in name.parts or '\\' in row['path']:
                raise ValueError('Invalid source manifest path')
            if name.suffix not in ('.c','.h') and name.name not in ('Makefile','Kconfig'):
                raise ValueError('Unexpected source input type')
            if row['upstream_sha256'] is None:
                continue  # A new QC5 source file is created by the patch.
            member = members.get(manifest['archive_driver_prefix'] + str(name))
            if member is None or not member.isfile():
                raise ValueError('Source entry is not a regular file')
            data = archive.extractfile(member).read()
            if sha(data) != row['upstream_sha256']:
                raise ValueError('Unexpected upstream source bytes')
            destination = output / str(name)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
    subprocess.run(['patch','-p1','-t','-F0','-i',str(patch)],
                   cwd=output, check=True, capture_output=True, timeout=60)
    for row in manifest['files']:
        path = output / row['path']
        if path.is_symlink() or sha(path.read_bytes()) != row['sha256']:
            raise ValueError('Source reconstruction failed: '+row['path'])
    print('Verified '+str(len(manifest['files']))+' R43 driver source files; '
          'no radio firmware, flash write, camera access or module load.')


if __name__ == '__main__':
    main()
