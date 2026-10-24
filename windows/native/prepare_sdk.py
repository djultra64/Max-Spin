#!/usr/bin/env python3
"""Prepare an isolated native Windows SDK without modifying the Linux SDK.
Own work: djultra64. Applies exact context only; no downloads or original assets.
"""
import argparse
import hashlib
import json
import re
import shutil
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCHES = [HERE / 'vulkan-backend/windows-vulkan.patch',
           HERE / 'runtime/windows-runtime.patch',
           HERE / 'runtime/windows-save-runtime.patch',
           HERE / 'native-host.patch']


def apply_patch(root, patch):
    lines = patch.read_text().splitlines(keepends=True)
    index = 0
    while index < len(lines):
        if not lines[index].startswith('--- a/'):
            index += 1
            continue
        name = lines[index][6:].strip()
        index += 1
        if index >= len(lines) or lines[index] != '+++ b/' + name + '\n':
            raise ValueError(f'Unsupported patch path in {patch.name}')
        path = root / name
        if Path(name).is_absolute() or '..' in Path(name).parts:
            raise ValueError('Unsafe patch path')
        original = path.read_text().splitlines(keepends=True)
        output, cursor = [], 0
        index += 1
        while index < len(lines) and not lines[index].startswith('--- a/'):
            if not lines[index].startswith('@@ '):
                index += 1
                continue
            match = re.match(r'@@ -(\d+)(?:,\d+)? \+\d+(?:,\d+)? @@', lines[index])
            if not match:
                raise ValueError('Invalid hunk')
            begin = int(match[1]) - 1
            if begin < cursor:
                raise ValueError('Overlapping hunks')
            output.extend(original[cursor:begin])
            cursor = begin
            index += 1
            while index < len(lines) and lines[index][:1] in (' ', '+', '-') and not lines[index].startswith('--- a/'):
                kind, text = lines[index][0], lines[index][1:]
                if kind in (' ', '-'):
                    if cursor >= len(original) or original[cursor] != text:
                        raise ValueError(f'SDK differs at {name}:{cursor + 1}')
                    cursor += 1
                if kind in (' ', '+'):
                    output.append(text)
                index += 1
        output.extend(original[cursor:])
        path.write_text(''.join(output), newline='\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    source, destination = args.source.resolve(), args.destination.resolve()
    if destination == source or source in destination.parents:
        parser.error('Destination must be outside the original SDK')
    files = sorted(p for p in source.rglob('*') if p.is_file())
    digest = hashlib.sha256()
    for path in files:
        digest.update(path.relative_to(source).as_posix().encode())
        digest.update(path.read_bytes())
    for patch in PATCHES:
        digest.update(patch.read_bytes())
    identity = digest.hexdigest()
    stamp = destination / '.max-spin-native-sdk.json'
    if stamp.is_file() and json.loads(stamp.read_text()).get('source_and_patches_sha256') == identity:
        print('Native SDK already prepared')
        return
    if destination.exists():
        parser.error('Destination exists with a different SDK; choose a fresh build directory')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.max-spin-sdk-', dir=destination.parent) as temporary:
        stage = Path(temporary) / 'sdk'
        shutil.copytree(source, stage)
        for patch in PATCHES:
            apply_patch(stage, patch)
        (stage / '.max-spin-native-sdk.json').write_text(json.dumps(
            {'author': 'djultra64', 'source_and_patches_sha256': identity}, indent=2) + '\n')
        stage.rename(destination)
    print('Native Windows SDK prepared')


if __name__ == '__main__':
    main()
