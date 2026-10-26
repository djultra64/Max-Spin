#!/usr/bin/env python3
"""Prepare and validate private native athlete packages without launching. djultra64."""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import sys

from character_packages import (SCHEMA, SLOTS, MAX_FILE, MAX_MANIFEST, _compose,
                                _load, _pairs, _read_package, slot_info)


def _digest(data):
    return hashlib.sha256(data).hexdigest()


def _write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def create_template(assets, output_parent, slot, name):
    """Seed a disabled private draft from the user's own two native outfits."""
    info = slot_info(slot)
    if not isinstance(name, str) or not name.strip() or len(name) > 128 or any(ord(c) < 32 for c in name):
        raise ValueError('athlete name must be a nonempty plain string of at most 128 characters')
    files, bases, copies = {}, {}, {}
    for outfit in (0, 1):
        archive = f'Media/Wads/Players/{info["target"]}{outfit:02d}.wad'
        data = _read_package(Path(assets), archive, MAX_FILE)
        relative = f'outfit{outfit}.wad'
        files[archive] = {'path': relative, 'sha256': _digest(data)}
        bases[archive] = _digest(data)
        copies[relative] = data
    manifest = {'schema': SCHEMA, 'slot': slot, 'target': info['target'],
                'gender': info['gender'], 'name': name, 'author': 'djultra64',
                'compatibility': {'platform': 'xbox', 'validated': False, 'evidence': ''},
                'base_files': bases, 'files': files, 'pml_updates': [], 'member_updates': []}
    parent = Path(output_parent)
    parent.mkdir(parents=True, exist_ok=True)
    package = parent / str(slot)
    package.mkdir(mode=0o700)  # Never overwrite an existing athlete or draft.
    try:
        for relative, data in copies.items():
            (package / relative).write_bytes(data)
        _write_json(package / 'manifest.template.json', manifest)
    except BaseException:
        # Only remove files created here; a preexisting folder never reaches here.
        for relative in (*copies, 'manifest.template.json'):
            (package / relative).unlink(missing_ok=True)
        package.rmdir()
        raise
    return package


def refresh_hashes(package, assets):
    """Refresh declared hashes; preserve disabled status and compatibility evidence."""
    package, assets = Path(package), Path(assets)
    candidates = [name for name in ('manifest.json', 'manifest.template.json')
                  if (package / name).exists() or (package / name).is_symlink()]
    if len(candidates) != 1:
        raise ValueError('keep exactly one manifest.json or manifest.template.json in the package')
    filename = candidates[0]
    manifest = json.loads(_read_package(package, filename, MAX_MANIFEST), object_pairs_hook=_pairs)
    if not isinstance(manifest, dict) or manifest.get('schema') != SCHEMA:
        raise ValueError('unsupported character manifest schema')
    info = slot_info(manifest.get('slot'))
    if package.name != str(info['slot']) or manifest.get('target') != info['target'] or manifest.get('gender', info['gender']) != info['gender']:
        raise ValueError('manifest identity must match the native numbered slot')
    files = manifest.get('files')
    expected = {f'Media/Wads/Players/{info["target"]}{outfit:02d}.wad' for outfit in (0, 1)}
    if not isinstance(files, dict) or files.keys() != expected:
        raise ValueError('files must contain exactly the two target outfits; shared WAD replacement is forbidden')
    touched = set(expected)
    descriptors = list(files.values())
    for key, archive in (('pml_updates', 'Media/Wads/Db.wad'), ('member_updates', 'Media/Wads/GameIni.wad')):
        rows = manifest.get(key, [])
        if not isinstance(rows, list) or any(not isinstance(row, dict) or row.get('archive') != archive for row in rows):
            raise ValueError('unsupported typed shared update')
        if rows:
            touched.add(archive)
        if key == 'member_updates':
            descriptors.extend(rows)
    cloth = 'Male' if info['gender'] == 'male' else 'Female'
    source_only = {'Media/Wads/GamePart.wad', f'Media/Wads/PlayerCreator{cloth}Cloth.wad'}
    bases = manifest.get('base_files', {})
    if not isinstance(bases, dict) or set(bases) - touched - source_only:
        raise ValueError('unsupported or foreign-gender source binding')
    # Read everything before rewriting the manifest. Safe readers reject links,
    # traversal, special files and oversized files exactly as the launcher does.
    revised_bases = {archive: _digest(_read_package(assets, archive, MAX_FILE))
                     for archive in sorted(touched | set(bases))}
    seen = set()
    for descriptor in descriptors:
        if not isinstance(descriptor, dict) or descriptor.get('path') in seen:
            raise ValueError('invalid or duplicate package payload')
        relative = descriptor.get('path')
        data = _read_package(package, relative, MAX_FILE)
        descriptor['sha256'] = _digest(data)
        seen.add(relative)
    manifest['base_files'] = revised_bases
    _write_json(package / filename, manifest)
    return manifest


def validate_packages(assets, packages):
    """Validate whole packages together with the launcher's exact conflict rules."""
    assets, packages = Path(assets), Path(packages)
    if not assets.is_dir() or not packages.is_dir():
        raise ValueError('assets and packages must be existing directories')
    current, claims, accepted, rejected, disabled = {}, set(), [], [], []
    for package in sorted((p for p in packages.iterdir() if p.name.isdecimal()), key=lambda p: int(p.name)):
        if not (package / 'manifest.json').exists() and not (package / 'manifest.json').is_symlink():
            disabled.append(package.name)
            continue
        try:
            loaded = _load(package, assets)
            current, claims = _compose(loaded, current, claims)
            manifest, _, _, _, native_report = loaded
            accepted.append({**slot_info(manifest['slot']), 'name': manifest['name'],
                             'figure_verified': native_report.get('figure_verified', False)})
        except (ValueError, OSError, KeyError, TypeError, StopIteration, UnicodeError, ImportError, configparser.Error) as error:
            rejected.append({'slot': package.name, 'error': str(error)})
    return {'valid': not rejected, 'accepted': accepted, 'rejected': rejected,
            'disabled': disabled, 'replacement_archives': sorted(current)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('catalog', help='print all native slot identities and order evidence limits')
    template = commands.add_parser('template', help='copy two private originals into a disabled draft')
    template.add_argument('--assets', type=Path, required=True)
    template.add_argument('--output-parent', type=Path, required=True)
    template.add_argument('--slot', type=int, required=True)
    template.add_argument('--name', required=True)
    refresh = commands.add_parser('refresh', help='refresh hashes without approving or activating a package')
    refresh.add_argument('--assets', type=Path, required=True)
    refresh.add_argument('--package', type=Path, required=True)
    validate = commands.add_parser('validate', help='validate active packages together, without launching')
    validate.add_argument('--assets', type=Path, required=True)
    validate.add_argument('--packages', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == 'catalog':
            result = {'slots': [slot_info(slot) for slot in range(1, len(SLOTS) + 1)]}
        elif args.command == 'template':
            result = {'package': str(create_template(args.assets, args.output_parent, args.slot, args.name)), 'active': False}
        elif args.command == 'refresh':
            manifest = refresh_hashes(args.package, args.assets)
            result = {'package': str(args.package), 'compatibility': manifest['compatibility'], 'hashes_refreshed': True}
        else:
            result = validate_packages(args.assets, args.packages)
        print(json.dumps(result, indent=2, ensure_ascii=False))
        return 1 if result.get('valid') is False else 0
    except (ValueError, OSError, KeyError, TypeError, UnicodeError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
