"""Render localized assets and build unarmed QC5 packages, entirely offline."""

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

SOURCE = Path(__file__).resolve().parent
sys.path.insert(0, str(SOURCE / 'tools'))
RELEASE = '0.6.7-r4g5b-i18n1'
LOCALE_MARKER = '<!-- QC5_LOCALE_BOOTSTRAP -->'


def render_web(output):
    catalogs = {}
    for path in sorted((SOURCE / 'locales').glob('*.json')):
        code = path.stem
        if not re.fullmatch('[a-z]{2}', code) or code == 'it':
            raise ValueError(f'Invalid catalog language: {code}')
        catalogs[code] = json.loads(path.read_text())
    if 'en' not in catalogs:
        raise ValueError('The English fallback catalog is required')
    serialized = json.dumps(catalogs, ensure_ascii=False, separators=(',', ':'))
    serialized = serialized.replace('<', '\\u003c').replace('\u2028', '\\u2028').replace('\u2029', '\\u2029')
    runtime = (SOURCE / 'locale.js').read_text()
    bootstrap = '<script>\n' + runtime.replace('/* QC5_LOCALE_CATALOGS */', serialized) + '</script>'
    output.mkdir(parents=True, exist_ok=True)
    assets = {}
    for source in sorted((SOURCE / 'web').iterdir()):
        content = source.read_bytes()
        if source.suffix == '.html':
            html = content.decode()
            if html.count(LOCALE_MARKER) != 1:
                raise ValueError(f'Expected exactly one locale bootstrap in {source.name}')
            content = html.replace(LOCALE_MARKER, bootstrap).encode()
        (output / source.name).write_bytes(content)
        assets[source.name] = content
    return assets


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build_firmware(assets, output, base_path, stock_path, r4e2_path, tool):
    from dissect.squashfs import SquashFS
    from squashfs_tree import collect, summary, archive_shadow
    import firmware_app_pair as fw
    from prepare_app_install import prepare

    def images(path, fingerprints):
        if not path.is_file() or path.stat().st_size != 0x800000:
            raise ValueError('Use a regular 8 MiB flash dump')
        flash = path.read_bytes()
        fw.verify_immutable(flash)
        system = flash[fw.SYSTEM_OFFSET:fw.WWW_OFFSET]
        www = flash[fw.WWW_OFFSET:fw.WWW_OFFSET + fw.WWW_SIZE]
        if (sha(system), sha(www)) != fingerprints:
            raise ValueError('Application partitions do not match the selected input version')
        return flash, (system, www)

    baseline, (base_system, base_www) = images(base_path, (
        '84aa56693a7f24f10d0d1ba4df4ef9d2ba83a3c39702321dfdbd8c6743486d32',
        '1ebc3700883f3ae9f60293273690074ac32935ee1361e6f918383c7d809a6fd2',
    ))
    original, (stock_system, stock_www) = images(stock_path, (fw.PARTITIONS[3][3], fw.PARTITIONS[4][3]))
    r4e2 = images(r4e2_path, (
        '1c5866a5c4c3c6dce3ace38f510fe8107b848731c983699c4b969eb0dc4553f7',
        '1ebc3700883f3ae9f60293273690074ac32935ee1361e6f918383c7d809a6fd2',
    )) if r4e2_path else None

    def unpack(image):
        return collect(SquashFS(io.BytesIO(image)))

    system, www = unpack(base_system), unpack(base_www)
    original_system_listing, original_www_listing = summary(system), summary(www)
    previous_assets = {name.removeprefix('qc5/www/'): item for name, item in system.items()
                       if name.startswith('qc5/www/') and item['type'] == 'file'}
    if previous_assets.keys() != assets.keys():
        raise ValueError('Unexpected WebUI asset inventory')
    if 'qc5-web' in www:
        raise ValueError('Existing WWW resource directory')
    www['qc5-web'] = system['qc5/www'].copy()
    for name, content in assets.items():
        www['qc5-web/' + name] = dict(previous_assets[name], data=content)
        del system['qc5/www/' + name]
    system['qc5/www'] = dict(system['qc5/www'], type='symlink', mode=0o777,
                             target='/system/www/qc5-web')
    assert {k: v for k, v in summary(system).items() if not k.startswith('qc5/www')} == {
        k: v for k, v in original_system_listing.items() if not k.startswith('qc5/www')}
    assert {k: summary(www)[k] for k in original_www_listing} == original_www_listing

    output.mkdir(mode=0o700, parents=True, exist_ok=False)

    def squash(label, entries, limit):
        root = entries['']
        with tempfile.TemporaryDirectory(dir=output) as tmp:
            image = Path(tmp) / 'image.sqfs'
            cmd = [str(tool), '-', str(image), '-tar', '-numeric-owner', '-noappend',
                   '-nopad', '-no-xattrs', '-comp', 'xz', '-b', '65536', '-Xdict-size',
                   '65536', '-no-tailends', '-exports', '-processors', '1', '-no-progress',
                   '-mkfs-time', str(root['mtime']), '-root-uid', str(root['uid']),
                   '-root-gid', str(root['gid']), '-root-mode', format(root['mode'], '04o'),
                   '-root-time', str(root['mtime'])]
            result = subprocess.run(cmd, input=archive_shadow(entries), capture_output=True, timeout=90)
            (output / (label + '.build.log')).write_bytes(result.stdout + result.stderr)
            result.check_returncode()
            data = image.read_bytes()
        if len(data) > limit:
            raise ValueError(f'{label} exceeds partition capacity by {len(data) - limit} bytes')
        assert summary(unpack(data)) == summary(entries)
        return data.ljust(limit, b'\xff'), limit - len(data)

    system_image, system_free = squash('SYSTEM', system, fw.SYSTEM_SIZE)
    www_image, www_free = squash('WWW', www, fw.WWW_SIZE)
    forwards = {
        'prima-installazione': fw.create(system_image, www_image, RELEASE),
        'aggiornamento-da-r4g5b': fw.create(system_image, www_image, RELEASE, 'modified', base_system, base_www),
    }
    if r4e2:
        forwards['aggiornamento-da-r4e2'] = fw.create(system_image, www_image, RELEASE, 'modified', *r4e2[1])
    stock_return = fw.create(stock_system, stock_www, 'stock-from-r4g5b-i18n1', 'stock', system_image, www_image)
    updated = fw.simulate(original, forwards['prima-installazione'])
    for name, source in [('aggiornamento-da-r4g5b', baseline)] + (
            [('aggiornamento-da-r4e2', r4e2[0])] if r4e2 else []):
        expected = source[:fw.SYSTEM_OFFSET] + updated[fw.SYSTEM_OFFSET:fw.WWW_OFFSET + fw.WWW_SIZE] + source[fw.WWW_OFFSET + fw.WWW_SIZE:]
        assert fw.simulate(source, forwards[name]) == expected
    assert fw.simulate(updated, stock_return) == original
    broken = bytearray(updated)
    broken[fw.SYSTEM_OFFSET:fw.WWW_OFFSET + fw.WWW_SIZE] = bytes(fw.PAYLOAD_SIZE)
    assert fw.simulate(bytes(broken), stock_return, recovery=True) == original

    verifier = www['qc5-verify']['data']
    assert sha(verifier) == '135739d2cad7fb8f222ac62c79e4af7dab0acf4158f550691cf216e919f59487'
    configuration = (SOURCE / 'sdcard/config.txt.example').read_bytes()
    helper = (SOURCE / 'tools/prepare_sd_standalone.py').read_text()
    for variant, forward in forwards.items():
        destination = output / variant
        destination.mkdir()
        capsule = destination / 'qc5-install'
        prepare(forward, stock_return, verifier, capsule, configuration)
        program = helper
        for key, data in [('MANIFEST_SHA', (capsule / 'manifest.json').read_bytes()),
                          ('FORWARD_SHA', forward), ('RETURN_SHA', stock_return)]:
            placeholder = '@' + key + '@'
            assert program.count(placeholder) == 1
            program = program.replace(placeholder, sha(data))
        compile(program, 'prepara_sd.py', 'exec')
        (destination / 'prepara_sd.py').write_text(program)
    result = {'release': RELEASE, 'system_free_bytes': system_free, 'www_free_bytes': www_free,
              'languages': ['it'] + [p.stem for p in sorted((SOURCE / 'locales').glob('*.json'))],
              'web_assets_only': True, 'all_forward_paths_and_stock_restore_simulated': True,
              'armed': False, 'hardware_tested': False,
              'packages': {name: fw.check(data) for name, data in forwards.items()}}
    (output / 'build.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'packages'}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--web-output', type=Path, default=SOURCE / 'build/web')
    parser.add_argument('--base-flash', type=Path, help='Private 8 MiB dump from installed R4g5b')
    parser.add_argument('--factory-flash', type=Path, help='Private 8 MiB dump of supported factory firmware')
    parser.add_argument('--r4e2-flash', type=Path, help='Optional private 8 MiB dump from installed R4e2 with NTP')
    parser.add_argument('--mksquashfs', default='mksquashfs')
    parser.add_argument('--output', type=Path, default=SOURCE / 'build/firmware')
    args = parser.parse_args()
    if bool(args.base_flash) != bool(args.factory_flash) or (args.r4e2_flash and not args.base_flash):
        parser.error('Firmware assembly requires both --base-flash and --factory-flash')
    os.umask(0o077)
    rendered = render_web(args.web_output)
    if args.base_flash:
        build_firmware(rendered, args.output, args.base_flash, args.factory_flash,
                       args.r4e2_flash, args.mksquashfs)
