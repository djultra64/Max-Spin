"""Validated private character asset sessions. Own work: djultra64.

Original resources are never opened for writing. Package payloads are read once
before launch; only owned read-only copies are placed in the temporary view.
"""
from contextlib import contextmanager
import configparser
from pathlib import Path, PurePosixPath
import hashlib
import json
import math
import os
import re
import stat
import sys
import tempfile

SCHEMA = 'maxspin-character-v1'
# Verified original male selector order: top row left-to-right, then bottom.
# Local title evidence: reports/character-native-candidates-20261025/default-034000ms.png.
SLOTS = (
    ('Sampras', 7), ('HewittX', 8), ('Gambill', 9), ('GrosJea', 10),
    ('Kuerten', 11), ('ChangXX', 12), ('BlakeXX', 13), ('Robredo', 14),
    ('TukurXX', 15), ('Houston', 16), ('WangXXX', 17), ('KoenigX', 18),
    ('GideXXX', 19), ('Riosxxx', 20), ('Emoryxx', 21), ('NashXXX', 22),
    # Female allocation follows native IDs, pending a female selector capture.
    # This is deliberately not described as verified visible selector order.
    ('HingisX', 516), ('AnnaXXX', 517), ('Barbara', 518), ('Daniela', 519),
    ('Dementi', 520), ('Coetzer', 521), ('Harkler', 522), ('Shaughn', 523),
    ('Yamanix', 524), ('Baretto', 525), ('Tellawi', 526), ('Abercro', 527),
    ('Auyeung', 528), ('Hammond', 529), ('sergeye', 530), ('Hillxxx', 531),
)
MAX_MANIFEST = 128 * 1024
MAX_FILE = 128 * 1024 * 1024
MAX_PACKAGE = 256 * 1024 * 1024
_SHA256 = re.compile(r'[0-9a-f]{64}\Z')


def slot_info(slot):
    """Return the native identity and the evidence limits of a package folder."""
    if type(slot) is not int or not 1 <= slot <= len(SLOTS):
        raise ValueError('slot must be an integer from 1 through 32')
    target, identifier = SLOTS[slot - 1]
    return {'slot': slot, 'target': target, 'database_id': identifier,
            'gender': 'male' if slot <= 16 else 'female',
            'order_status': 'visible-verified' if slot <= 16 else 'provisional-native-id-order'}


def _relative(value):
    if not isinstance(value, str) or not value or '\\' in value or '\x00' in value:
        raise ValueError('invalid relative resource path')
    parts = value.split('/')
    if any(part in ('', '.', '..') for part in parts) or PurePosixPath(value).is_absolute():
        raise ValueError('resource path escapes package')
    return value


def _read_package(root, relative, limit):
    """Read through directory descriptors, rejecting every symlink component."""
    parts = _relative(relative).split('/')
    descriptor = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        for part in parts[:-1]:
            next_descriptor = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=descriptor)
            os.close(descriptor)
            descriptor = next_descriptor
        file_descriptor = os.open(parts[-1], os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=descriptor)
        with os.fdopen(file_descriptor, 'rb') as source:
            info = os.fstat(source.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size > limit:
                raise ValueError('package file is not regular or exceeds size limit')
            data = source.read(limit + 1)
            if len(data) > limit:
                raise ValueError('package file exceeds size limit')
            return data
    finally:
        os.close(descriptor)


def _pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'duplicate JSON key: {key}')
        result[key] = value
    return result


def _hash(value):
    if not isinstance(value, str) or not _SHA256.fullmatch(value):
        raise ValueError('invalid SHA256 digest')
    return value


def _native_validate(manifest, base, payloads):
    from character_formats import validate_native_package
    return validate_native_package(manifest, base, payloads)


def _load(package, assets):
    try:
        manifest = json.loads(_read_package(package, 'manifest.json', MAX_MANIFEST),
                              object_pairs_hook=_pairs,
                              parse_constant=lambda value: (_ for _ in ()).throw(ValueError(f'nonfinite JSON value: {value}')))
    except RecursionError as error:
        raise ValueError('character manifest exceeds JSON nesting limit') from error
    if not isinstance(manifest, dict) or manifest.get('schema') != SCHEMA:
        raise ValueError('unsupported character manifest schema')
    slot = manifest.get('slot')
    if type(slot) is not int or not 1 <= slot <= len(SLOTS) or str(slot) != package.name:
        raise ValueError('slot does not match numbered folder')
    gender = slot_info(slot)['gender']
    if manifest.get('gender', gender) != gender:
        raise ValueError('gender does not match native slot')
    target = manifest.get('target')
    if not isinstance(target, str) or target.casefold() != SLOTS[slot - 1][0].casefold():
        raise ValueError('target does not match visible selector slot')
    for field in ('name', 'author'):
        value = manifest.get(field)
        if not isinstance(value, str) or not value.strip() or len(value) > 128 or any(ord(c) < 32 for c in value):
            raise ValueError(f'invalid character {field}')
    compatibility = manifest.get('compatibility', {})
    if not isinstance(compatibility, dict) or compatibility.get('platform') != 'xbox' or compatibility.get('validated') is not True:
        raise ValueError('native Xbox compatibility has not been validated')
    if not isinstance(compatibility.get('evidence'), str) or not compatibility['evidence'].strip():
        raise ValueError('native compatibility evidence is missing')
    files = manifest.get('files')
    expected = {f'Media/Wads/Players/{target}{outfit:02d}.wad'.casefold() for outfit in (0, 1)}
    if not isinstance(files, dict) or len(files) != 2 or {_relative(key).casefold() for key in files} != expected:
        raise ValueError('package must contain both native outfit WADs')
    pml_updates = manifest.get('pml_updates', [])
    if not isinstance(pml_updates, list) or len(pml_updates) > 512 or any(not isinstance(row, dict) for row in pml_updates):
        raise ValueError('invalid or excessive typed updates')
    member_updates = manifest.get('member_updates', [])
    if not isinstance(member_updates, list) or len(member_updates) > 32 or any(not isinstance(row, dict) for row in member_updates):
        raise ValueError('invalid or excessive shared member updates')
    touched = set(files)
    for row in pml_updates:
        if row.get('archive') != 'Media/Wads/Db.wad':
            raise ValueError('typed updates only support Db.wad')
        touched.add(row['archive'])
    for row in member_updates:
        if row.get('archive') != 'Media/Wads/GameIni.wad':
            raise ValueError('member updates only support GameIni.wad')
        touched.add(row['archive'])
    bases = manifest.get('base_files')
    cloth = 'Male' if gender == 'male' else 'Female'
    source_only = {'Media/Wads/GamePart.wad', f'Media/Wads/PlayerCreator{cloth}Cloth.wad'}
    if not isinstance(bases, dict) or not touched <= set(bases) or set(bases) - touched - source_only:
        raise ValueError('source bindings do not cover exact native resources')
    payloads, originals, replacements = {}, {}, {}
    total = 0
    for archive, digest in bases.items():
        _relative(archive)
        with (assets / archive).open('rb') as source:
            data = source.read(MAX_FILE + 1)
        if len(data) > MAX_FILE or hashlib.sha256(data).hexdigest() != _hash(digest):
            raise ValueError(f'original source hash mismatch: {archive}')
        total += len(data)
        if total > MAX_PACKAGE:
            raise ValueError('source resources exceed package budget')
        originals[archive] = data
    for archive, row in list(files.items()) + [(None, row) for row in member_updates]:
        if not isinstance(row, dict):
            raise ValueError('invalid payload descriptor')
        relative = _relative(row.get('path'))
        if relative in payloads:
            raise ValueError('duplicate package payload')
        data = _read_package(package, relative, MAX_FILE)
        total += len(data)
        if total > MAX_PACKAGE or hashlib.sha256(data).hexdigest() != _hash(row.get('sha256')):
            raise ValueError('package payload hash mismatch or package too large')
        payloads[relative] = data
        if archive is not None:
            replacements[archive] = data
    report = _native_validate(manifest, originals, payloads) or {}
    return manifest, originals, replacements, payloads, report


def _pml_property(database, row, manifest):
    member, node, prop, value = (row.get(key) for key in ('member', 'node', 'property', 'value'))
    target = manifest['target']
    info = slot_info(manifest['slot'])
    identifier = info['database_id']
    gender = 'm' if info['gender'] == 'male' else 'w'
    if not all(isinstance(item, str) for item in (member, node, prop, value)):
        raise ValueError('typed property updates require string selectors and values')
    if len(value) > 128 or any(ord(c) < 32 for c in value):
        raise ValueError('invalid metadata string')
    value.encode('cp1252')
    identity = {'Name', 'Firstname', 'Lastname', 'Country'}
    bounds = {'ZoneID': (0, 8), 'BirthplaceID': (0, 1000), 'Age': (15, 65),
              'Taille': (120, 230), 'Poids': (30, 180), 'LeftHanded': (0, 1),
              'ServeStyle': (0, 3), 'NaturalTalent': (0, 2)}
    if member == 'media/db/playerlist.pml':
        if node not in (f'TPLayerInfo/DirName={target}00', f'TPLayerInfo/DirName={target}01'):
            raise ValueError('player-list update addresses a foreign row')
        if database.get(node, 'DirName').casefold() not in (target.casefold() + '00', target.casefold() + '01'):
            raise ValueError('player-list target identity mismatch')
        if database.get(node, 'IsWoman') != ('0' if gender == 'm' else '1'):
            raise ValueError('player-list gender does not match native slot')
        identity = {'Name', 'ShortName'}
        bounds = {'Available': (0, 1), 'FromPlayerCreator': (0, 1), 'RightHanded': (0, 1)}
    elif member in (f'media/db/players({gender}).pml', f'media/db/profils({gender}).pml'):
        parts = node.split('/')
        tables = ('TRPlayer', 'TPlayers') if member.endswith(f'profils({gender}).pml') else ('TRPlayer',)
        if len(parts) not in (2, 3) or parts[0] not in tables or parts[1] != f'Id={identifier}':
            raise ValueError('database update addresses a foreign player row')
        base_node = '/'.join(parts[:2])
        if database.get(base_node, 'CodeName').casefold() != target.casefold():
            raise ValueError('database target identity mismatch')
        if parts[0] == 'TPlayers':
            identity = set()
        if len(parts) == 3:
            if parts[0] != 'TRPlayer' or not parts[2].startswith('Name=') or parts[2] == 'Name=Endurance' or prop != 'Value':
                raise ValueError('unsupported skill update')
            identity, bounds = set(), {'Value': (0, 10)}
    else:
        raise ValueError('unsupported database member')
    if prop in bounds:
        try:
            numeric = float(value)
        except ValueError as error:
            raise ValueError('invalid numeric character attribute') from error
        lower, upper = bounds[prop]
        if not math.isfinite(numeric) or numeric != int(numeric) or not lower <= numeric <= upper:
            raise ValueError('nonfinite or out-of-range character attribute')
    elif prop not in identity or not value.strip():
        raise ValueError('unsupported character metadata property')
    database.set(node, prop, value)


def _member_payload(member, original, replacement, target):
    from character_formats import unpack_zlb
    prefixes = [f'media/gamepart/players/{target.lower()}{outfit:02d}/' for outfit in (0, 1)]
    prefix = next((prefix for prefix in prefixes if member.startswith(prefix)), None)
    if prefix is None:
        raise ValueError('shared member addresses a foreign athlete')
    leaf = member[len(prefix):]
    before, after = unpack_zlb(original), unpack_zlb(replacement)
    if leaf == 'player.ini':
        def parse(data):
            parser = configparser.ConfigParser(interpolation=None)
            parser.optionxform = str
            parser.read_string(data.decode('cp1252'))
            return {section:dict(parser[section]) for section in parser.sections()}
        source, candidate = parse(before), parse(after)
        if source.keys() != candidate.keys():
            raise ValueError('native INI sections must remain unchanged')
        for section, values in source.items():
            if values.keys() != candidate[section].keys():
                raise ValueError('native INI fields must remain unchanged')
            for name, value in values.items():
                revised = candidate[section][name]
                if revised == value:
                    continue
                # These custom heads already contain their own baked hair.
                # Only disabling the original accessory is supported here.
                if section == 'Accessories' and name == 'Hair' and revised == '':
                    continue
                if section != 'General' or name not in ('Name', 'ShortName', 'LongName') or not revised.strip() or len(revised) > 128 or any(ord(c) < 32 for c in revised):
                    raise ValueError('native INI dependency or unsupported value changed')
    elif leaf in ('vignette1.dds', 'vignettevisage.dds', 'scorename.dds'):
        if len(before) < 128 or len(after) != len(before) or before[:4] != b'DDS ' or after[:128] != before[:128]:
            raise ValueError('native portrait DDS layout must match original')
    else:
        raise ValueError('unsupported shared character member')
    return replacement


def _compose(loaded, current, claims):
    from character_formats import payloads, rebuild_pam, unpack_zlb, pack_zlb, Pml
    manifest, originals, replacements, package_payloads, report = loaded
    result, new_claims = dict(current), set(claims)
    for identity, digest in report.get('texture_identities', {}).items():
        if any(item[0] == 'texture' and item[1] == identity and item[2] != digest for item in new_claims):
            raise ValueError('texture resource ID conflicts with another accepted package')
        new_claims.add(('texture', identity, digest))
    for archive, data in replacements.items():
        key = ('file', archive.casefold())
        if key in new_claims:
            raise ValueError('native resource conflict with another package')
        new_claims.add(key)
        result[archive] = data
    members = {}
    for row in manifest.get('member_updates', []):
        archive, member = row['archive'], _relative(row.get('member'))
        claim = ('member', archive.casefold(), member.casefold())
        if claim in new_claims or any(item[:3] == ('pml', archive.casefold(), member.casefold()) for item in new_claims):
            raise ValueError('duplicate or conflicting archive member')
        new_claims.add(claim)
        if archive not in members:
            members[archive] = payloads(result.get(archive, originals[archive]))
        original = members[archive][member]
        members[archive][member] = _member_payload(member, original, package_payloads[row['path']], manifest['target'])
    trees = {}
    for row in manifest.get('pml_updates', []):
        archive, member = row['archive'], _relative(row.get('member'))
        node, prop = row.get('node'), row.get('property')
        if not isinstance(node, str) or not isinstance(prop, str):
            raise ValueError('invalid typed property selector')
        claim = ('pml', archive.casefold(), member.casefold(), node.casefold(), prop.casefold())
        if claim in new_claims or ('member', archive.casefold(), member.casefold()) in new_claims:
            raise ValueError('duplicate or conflicting typed property')
        new_claims.add(claim)
        if archive not in members:
            members[archive] = payloads(result.get(archive, originals[archive]))
        key = archive, member
        if key not in trees:
            trees[key] = Pml(unpack_zlb(members[archive][member]))
        _pml_property(trees[key], row, manifest)
    for (archive, member), tree in trees.items():
        members[archive][member] = pack_zlb(tree.encode(), template=members[archive][member])
    for archive, updates in members.items():
        result[archive] = rebuild_pam(result.get(archive, originals[archive]), updates)
    return result, new_claims


def _overlay(source, destination, replacements, prefix=''):
    """Own directory structure; link unchanged leaves without making hardlinks."""
    for child in sorted(source.iterdir()):
        relative = f'{prefix}/{child.name}' if prefix else child.name
        output = destination / child.name
        if relative in replacements:
            output.write_bytes(replacements[relative])
            output.chmod(0o400)
        elif child.is_dir():
            output.mkdir(mode=0o700)
            _overlay(child, output, replacements, relative)
        else:
            output.symlink_to(child.resolve())


@contextmanager
def asset_session(assets: Path, packages: Path, *, temporary_root: Path | None = None):
    """Yield a private frozen view, optionally in caller-owned disk storage.

    No directory is created when no package replacements are selected.
    """
    assets, packages = Path(assets), Path(packages)
    replacements, claims = {}, set()
    if packages.is_dir():
        for package in sorted((path for path in packages.iterdir() if path.name.isdecimal()),
                              key=lambda path: int(path.name)):
            manifest = package / 'manifest.json'
            if not manifest.exists() and not manifest.is_symlink():
                continue
            try:
                loaded = _load(package, assets)
                replacements, claims = _compose(loaded, replacements, claims)
            except (ValueError, OSError, KeyError, TypeError, StopIteration, UnicodeError, ImportError, configparser.Error) as error:
                print(f'Character slot {package.name}: {error}; keeping defaults.', file=sys.stderr)
    if not replacements:
        yield assets
        return
    if temporary_root is not None:
        temporary_root = Path(temporary_root)
        temporary_root.mkdir(mode=0o700, parents=True, exist_ok=True)
        temporary_root.chmod(0o700)
    with tempfile.TemporaryDirectory(prefix='maxspin-characters-', dir=temporary_root) as temporary:
        selected = Path(temporary)
        selected.chmod(0o700)
        _overlay(assets.resolve(), selected, replacements)
        yield selected
