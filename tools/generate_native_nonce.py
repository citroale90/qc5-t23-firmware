"""Generate a fresh private producer IPC nonce, never a key or Wi-Fi profile."""
# SPDX-License-Identifier: GPL-3.0-only
import argparse
import os
from pathlib import Path
import secrets


def generate(output):
    nonce = secrets.token_bytes(16)
    content = ('/* Generated build-local IPC nonce; do not publish this file. */\n'
               'static const unsigned char qc5_live_nonce[16]={' +
               ','.join(str(byte) for byte in nonce) + '};\n').encode()
    # Refuse overwriting another build's nonce or following an existing symlink.
    fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as stream:
        stream.write(content)
        stream.flush()
        os.fsync(stream.fileno())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    generate(args.output)
    print('Private producer nonce generated (mode 0600).')


if __name__ == '__main__':
    main()
