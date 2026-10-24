#!/usr/bin/env python3
"""Create a new local sprite cache from user-supplied game files. djultra64."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import zipfile

SPECS = (
    ('Base.ruirr', 'pc_cross', 'f6f4546c6279547e78e6acfdc4db700ca495b8c3c8c2e78ae1ef0664f9ad4bec'),
    ('MainMenu.ruirr', 'readystart', 'c9ead8ebee0a06da395fdfcc0353a9b94a2827753d0c7ef27397d3ea752d417d'),
    ('Base.ruirr', 'shop_slider', '4d41ea0dd70b2b885346b46d145e517aebe4c07b43ce10765a6b7b279bf0d07f'),
)


def prepare(assets, destination):
    """Validate sources before creating an exclusive, immutable destination."""
    destination = Path(destination).absolute()
    if destination.exists() or destination.is_symlink():
        raise ValueError('sprite destination must be new')
    payloads, records = [], []
    for package, name, expected in SPECS:
        with zipfile.ZipFile(Path(assets) / 'Media/MenuPart/Rui' / package) as archive:
            data = archive.read('Textures/' + name + '.xpr')
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError('unsupported sprite source: ' + name)
        payloads.append((name + '.xpr', data))
        records.append(dict(name=name + '.xpr', sha256=expected, bytes=len(data)))
    metadata = dict(version=1, algorithm=1, author='djultra64',
                    scope='Menu whole independent UI sources only', resources=records)
    payloads.append(('manifest.json', (json.dumps(metadata, indent=2) + '\n').encode()))
    destination.mkdir(mode=0o700)
    try:
        for name, data in payloads:
            with (destination / name).open('xb') as stream:
                stream.write(data)
            (destination / name).chmod(0o444)
        destination.chmod(0o555)
    except BaseException:
        destination.chmod(0o700)
        shutil.rmtree(destination)
        raise
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    prepare(args.assets, args.output)
    print('Prepared local sprite resources; original files unchanged.')


if __name__ == '__main__':
    main()
