"""Export frozen character replacements for the native Windows launcher.

Own work: djultra64. Uses the same validator/composer as the Linux launcher.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import struct
import sys

from character_packages import MAX_FILE, collect_replacements, resource_path


def export_session(assets, characters, output):
    """Create a new owned directory; remove partial output on every failure."""
    output = Path(output)
    output.mkdir(mode=0o700)
    try:
        replacements, originals = collect_replacements(Path(assets), Path(characters))
        if len(replacements) > 96:
            raise ValueError('too many character session resources')
        manifest = bytearray(b'MSCHAR1\0' + struct.pack('<I', len(replacements)))
        files = output / 'files'
        files.mkdir(mode=0o700)
        seen = set()
        for index, (path, data) in enumerate(sorted(replacements.items())):
            resource_path(path)
            encoded = path.encode('ascii')
            if path.casefold() in seen or len(data) > MAX_FILE or len(originals[path]) != 32:
                raise ValueError('invalid character session resource')
            seen.add(path.casefold())
            manifest.extend(struct.pack('<H', len(encoded)) + encoded)
            manifest.extend(struct.pack('<Q', len(data)))
            manifest.extend(originals[path] + hashlib.sha256(data).digest())
            (files / f'{index:04d}.wad').write_bytes(data)
        (output / 'manifest.bin').write_bytes(manifest)
    except BaseException:
        shutil.rmtree(output)
        raise
    return output


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--characters', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        export_session(args.assets, args.characters, args.output)
    except (ValueError, OSError) as error:
        print(f'Character session preparation failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
