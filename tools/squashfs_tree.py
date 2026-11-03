"""Read and rebuild a SquashFS file tree without extracting private flash data."""

import hashlib
import io
import stat
import tarfile
from pathlib import PurePosixPath


def safe_path(name):
    path = PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or '\\' in name:
        raise ValueError('Invalid path in firmware tree')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def collect(fs):
    entries = {}

    def walk(node, path):
        item = dict(mode=stat.S_IMODE(node.mode), uid=node.uid, gid=node.gid,
                    mtime=int(node.header.mtime))
        if node.is_dir():
            item['type'] = 'directory'
        elif node.is_file():
            item['type'] = 'file'
            with node.open() as f:
                item['data'] = f.read()
        elif node.is_symlink():
            item.update(type='symlink', target=node.link)
        else:
            raise ValueError('Unexpected special node: ' + path)
        entries[path] = item
        if node.is_dir():
            for child in node.iterdir():
                if child.name in ('.', '..') or '/' in child.name:
                    raise ValueError('Invalid image pathname')
                walk(child, path + '/' + child.name if path else child.name)

    walk(fs.root, '')
    return entries


def summary(entries):
    result = {}
    for path, item in entries.items():
        copy = {k: v for k, v in item.items() if k != 'data'}
        if item['type'] == 'file':
            copy.update(bytes=len(item['data']), sha256=sha(item['data']))
        result[path] = copy
    return result


def archive_shadow(entries):
    buf=io.BytesIO()
    # Symlinks last: extraction never writes through one of our flash aliases.
    order=sorted(entries,key=lambda p:({'directory':0,'file':1,'symlink':2}[entries[p]['type']],p))
    with tarfile.open(fileobj=buf,mode='w',format=tarfile.USTAR_FORMAT) as tar:
        for path in order:
            safe_path(path);item=entries[path];info=tarfile.TarInfo(path if path else '.')
            for field in ('uid','gid','mode','mtime'):setattr(info,field,item[field])
            if item['type']=='directory':info.type=tarfile.DIRTYPE
            elif item['type']=='symlink':info.type=tarfile.SYMTYPE;info.linkname=item['target']
            else:info.type=tarfile.REGTYPE;info.size=len(item['data'])
            tar.addfile(info,io.BytesIO(item['data']) if item['type']=='file' else None)
    return buf.getvalue()
