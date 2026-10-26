#!/usr/bin/env python3
"""Prepare private Xbox character candidates from preserved research. djultra64.

Output is outside the active characters folder by default. This tool never
launches the title or changes original resources, packages, saves or fixtures.
"""
import argparse
import importlib.util
import io
import json
from pathlib import Path
import re
import shutil
import struct
import tempfile

from character_formats import (Pml, convert_model, namespace_textures, pack_zlb,
    parse_figure, payloads, rebuild_pam, sha, unpack_zlb, validate_models,
    validate_native_package)
from character_profile import profile_catalog, validate_profile
from character_uv import apply_uv_alignment

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RESEARCH = ROOT.parent / 'character-research/legacy'
CHARACTERS = {
    'federer': {'slot': 9, 'id': 15, 'target': 'TukurXX', 'name': 'Roger Federer',
                'display': 'R. Federer', 'short': 'Fed', 'surname': 'Federer', 'right': '1'},
    'nadal': {'slot': 10, 'id': 16, 'target': 'Houston', 'name': 'Rafael Nadal',
              'display': 'R. Nadal', 'short': 'Nad', 'surname': 'Nadal', 'right': '0'},
}


def character_ini(original, name, short, long_name):
    changes = {'Name': name, 'ShortName': short, 'LongName': long_name}
    result = original
    for key, value in changes.items():
        pattern = rb'(?m)^(' + key.encode('ascii') + rb'[ \t]*=[ \t]*)[^\r\n]*'
        result, count = re.subn(pattern, lambda m: m[1] + value.encode('cp1252'), result)
        if count != 1:
            raise ValueError('Expected one original Xbox INI name field: ' + key)
    # Baked native models already contain their own hair. Disable the legacy
    # original Manka/Dread attachment rather than drawing a second hairstyle.
    result, count = re.subn(rb'(?m)(^\[Accessories\][\s\S]*?^Hair[ \t]*=)[^\r\n]*', lambda m: m[1], result)
    if count != 1:
        raise ValueError('Expected one original Xbox Accessories/Hair field')
    # No PC creator flags, ADN references, new attachment names or paths enter.
    return result


def _properties(tree, node):
    return {tree.names.get(k, '%08x' % k): v for k, v in node.properties}


def read_preserved(research, relative, inventory):
    """Read only resources recorded in the verified migration inventory."""
    research = Path(research).resolve()
    if Path(relative).is_absolute() or '..' in Path(relative).parts:
        raise ValueError('Preserved source path escapes research')
    path = research / relative
    if not path.resolve().is_relative_to(research):
        raise ValueError('Preserved source link escapes research')
    record = inventory.get('legacy/' + relative)
    if not isinstance(record, dict) or not isinstance(record.get('bytes'), int) or not 0 <= record['bytes'] <= 128 * 1024 * 1024:
        raise ValueError('Preserved source is absent from the verified inventory: ' + relative)
    data = path.read_bytes()
    if len(data) != record['bytes'] or sha(data) != record.get('sha256'):
        raise ValueError('Preserved source differs from the migration inventory: ' + relative)
    return data


def _database_updates(original, source, info, profile=None, catalog=None):
    old, new = payloads(original), payloads(source)
    profile = validate_profile(profile or {}, catalog or profile_catalog(original_db=original, source_db=source))
    country = None
    if 'countryId' in profile:
        country = next(c for c in (catalog or profile_catalog(original_db=original, source_db=source))['countries'] if c['id'] == profile['countryId'])
    updates = []
    members = ('media/db/playerlist.pml', 'media/db/players(m).pml', 'media/db/profils(m).pml')
    for member in members:
        xbox, pc = Pml(unpack_zlb(old[member])), Pml(unpack_zlb(new[member]))
        target = info['target']
        if member.endswith('playerlist.pml'):
            for outfit in range(2):
                node = 'TPLayerInfo/DirName=%s%02d' % (target, outfit)
                xbox.node(node)
                wanted = {'Name': profile.get('displayName', info['display']), 'ShortName': profile.get('shortName', info['short']),
                          'Available': '1', 'RightHanded': str(int(not profile['leftHanded'])) if 'leftHanded' in profile else info['right']}
                # Keep FromPlayerCreator=0: Xbox loads verified baked models.
                for prop, value in wanted.items():
                    if xbox.get(node, prop) != value:
                        updates.append({'archive': 'Media/Wads/Db.wad', 'member': member,
                                        'node': node, 'property': prop, 'value': value})
            continue
        tables = ('TRPlayer', 'TPlayers') if member.endswith('profils(m).pml') else ('TRPlayer',)
        for table in tables:
            node = '%s/Id=%d' % (table, info['id'])
            if xbox.get(node, 'CodeName') != target or pc.get(node, 'CodeName') != target:
                raise ValueError('Source and Xbox roster identities differ')
            old_props = _properties(xbox, xbox.node(node))
            source_props = _properties(pc, pc.node(node))
            for field, prop in [('age', 'Age'), ('heightCm', 'Taille'), ('weightKg', 'Poids')]:
                if field in profile:
                    source_props[prop] = str(profile[field])
            if 'leftHanded' in profile:
                source_props['LeftHanded'] = str(int(profile['leftHanded']))
            if country:
                source_props.update(BirthplaceID=str(country['id']), Country=country['countryToken'], ZoneID=str(country['zoneId']))
            if table == 'TRPlayer' and ('firstName' in profile or 'lastName' in profile):
                source_props['Firstname'] = profile.get('firstName', source_props['Firstname'])
                source_props['Lastname'] = profile.get('lastName', source_props['Lastname'])
                source_props['Name'] = source_props['Firstname'] + ' ' + source_props['Lastname']
            allowed = ('BirthplaceID', 'Age', 'Taille', 'Poids', 'LeftHanded', 'ServeStyle', 'NaturalTalent')
            if table == 'TRPlayer':
                allowed += ('Name', 'Firstname', 'Lastname', 'Country', 'ZoneID')
            for prop in allowed:
                if prop in old_props and prop in source_props and old_props[prop] != source_props[prop]:
                    updates.append({'archive': 'Media/Wads/Db.wad', 'member': member,
                                    'node': node, 'property': prop, 'value': source_props[prop]})
            if table == 'TRPlayer':
                for child in pc.node(node).children:
                    skills = _properties(pc, child)
                    name, value = skills.get('Name'), skills.get('Value')
                    skill_overrides = dict(profile.get('skills', {}))
                    if 'backhandStyle' in profile:
                        skill_overrides['Style'] = profile['backhandStyle']
                    if 'playStyle' in profile:
                        skill_overrides['PlayStyle'] = profile['playStyle']
                    if name in ('Endurance', 'PlayStyleDouble') or name == 'AccelSpeed' and name not in skill_overrides or name is None or value is None:
                        continue
                    value = str(skill_overrides.get(name, value))
                    skill_node = node + '/Name=' + name
                    if xbox.get(skill_node, 'Value') != value:
                        updates.append({'archive': 'Media/Wads/Db.wad', 'member': member,
                                        'node': skill_node, 'property': 'Value', 'value': value})
    return updates


def _matching_dds(original, candidate):
    a, b = unpack_zlb(original), unpack_zlb(candidate)
    if len(a) < 128 or a[:4] != b'DDS ' or b[:4] != b'DDS ' or len(a) != len(b) or a[12:20] != b[12:20] or a[28:32] != b[28:32] or a[84:88] not in (b'DXT3', b'DXT5') or b[84:88] not in (b'DXT3', b'DXT5'):
        raise ValueError('Source portrait/scoreboard DDS differs from native Xbox storage layout')
    height, width = struct.unpack_from('<II', a, 12)
    mips = max(1, struct.unpack_from('<I', a, 28)[0])
    expected = sum(max(1, (max(1, width >> l) + 3) // 4) * max(1, (max(1, height >> l) + 3) // 4) * 16 for l in range(mips))
    if not 1 <= width <= 4096 or not 1 <= height <= 4096 or mips > 13 or len(a) != 128 + expected:
        raise ValueError('Invalid original Xbox portrait DDS dimensions')
    body = bytearray(b[128:])
    if a[84:88] != b[84:88]:
        for at in range(0, len(body), 16):
            alpha = bytes(body[at:at + 8])
            if b[84:88] == b'DXT5':
                x, y = alpha[:2]
                palette = [x, y]
                if x > y:
                    palette += [((7 - i) * x + i * y) // 7 for i in range(1, 7)]
                else:
                    palette += [((5 - i) * x + i * y) // 5 for i in range(1, 5)] + [0, 255]
                bits = int.from_bytes(alpha[2:], 'little')
                values = [palette[(bits >> (i * 3)) & 7] for i in range(16)]
                encoded = sum(min(15, (v + 8) // 17) << (i * 4) for i, v in enumerate(values)).to_bytes(8, 'little')
            else:
                bits = int.from_bytes(alpha, 'little')
                values = [((bits >> (i * 4)) & 15) * 17 for i in range(16)]
                x, y = max(values), min(values)
                palette = [x, y]
                if x > y:
                    palette += [((7 - i) * x + i * y) // 7 for i in range(1, 7)]
                else:
                    palette += [x] * 4 + [0, 255]
                encoded = bytes((x, y)) + sum(min(range(8), key=lambda p: abs(palette[p] - v)) << (i * 3) for i, v in enumerate(values)).to_bytes(6, 'little')
            body[at:at + 8] = encoded
    # Xbox header, mip layout and RGB blocks remain exact; alpha is converted
    # between native BC2/BC3 encodings only when their original formats differ.
    return pack_zlb(a[:128] + bytes(body), original)


def read_portrait(path):
    """Bounded user-selected PNG; no painting or image generation occurs."""
    from PIL import Image
    with Path(path).open('rb') as source:
        raw = source.read(8 * 1024 * 1024 + 1)
    if len(raw) > 8 * 1024 * 1024:
        raise ValueError('Portrait PNG exceeds 8 MiB')
    with Image.open(io.BytesIO(raw)) as source:
        if source.format != 'PNG' or source.width != source.height or not 64 <= source.width <= 2048:
            raise ValueError('Portrait must be a square PNG from 64 to 2048 pixels')
        source.load()
        image = source.convert('RGBA')
    if image.getchannel('A').getbbox() is None:
        raise ValueError('Portrait PNG contains no visible pixels')
    return image, {'source': str(Path(path).resolve()), 'sha256': sha(raw),
                   'dimensions': list(image.size), 'kind': 'user-selected PNG; no generated or repainted pixels'}


def portrait_dds(original, image):
    from PIL import Image
    template = unpack_zlb(original)
    if len(template) < 128 or template[:4] != b'DDS ':
        raise ValueError('Invalid original native DDS template')
    height, width = struct.unpack_from('<II', template, 12)
    mips = max(1, struct.unpack_from('<I', template, 28)[0])
    if (width, height) not in ((256, 256), (64, 64), (128, 16)) or mips > 9:
        raise ValueError('Unsupported native portrait or nameplate dimensions')
    body = bytearray()
    for level in range(mips):
        frame = image.convert('RGBA').resize((max(1, width >> level), max(1, height >> level)), Image.Resampling.LANCZOS)
        output = io.BytesIO()
        frame.save(output, format='DDS', pixel_format='DXT5')
        encoded = output.getvalue()
        if encoded[84:88] != b'DXT5':
            raise ValueError('Unsupported portrait DDS encoder')
        body.extend(encoded[128:])
    header = bytearray(template[:128])
    header[84:88] = b'DXT5'
    return _matching_dds(original, bytes(header) + bytes(body))


def score_name_dds(original, name, assets):
    from scoreboard_font import render_score_name
    return portrait_dds(original, render_score_name(assets, name))


def apply_character_wardrobe(models, texture_pack, assets, selection):
    from character_wardrobe import apply_wardrobe
    return apply_wardrobe(models, texture_pack, assets, selection)


def prepare(assets, research, output, characters=('federer', 'nadal'), recipes=None, portraits=None):
    assets, research, output = Path(assets).resolve(), Path(research).resolve(), Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise ValueError('Candidate output already exists; preserve it and select a new directory')
    output = output.resolve(strict=False)
    if output == assets or output.is_relative_to(assets) or output == research or output.is_relative_to(research):
        raise ValueError('Candidate output must be outside original assets and preserved research')
    recipes = recipes or {}
    portraits = portraits or {}
    if not isinstance(recipes, dict) or not set(recipes) <= set(CHARACTERS) or not set(recipes) <= set(characters):
        raise ValueError('Recipe character must be one of the selected packages')
    if not isinstance(portraits, dict) or not set(portraits) <= set(characters):
        raise ValueError('Portrait character must be one of the selected packages')
    compiler = None
    if recipes:
        spec = importlib.util.spec_from_file_location('maxspin_face_recipe_compiler', ROOT / 'face-builder/compile.py')
        compiler = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(compiler)
    migration = json.loads((research.parent / 'preservation-manifest.json').read_text())
    inventory = {record['path']: record for record in migration['files']}
    source_prefix = 'build/Federer-Creator-Test/Media/Wads/'
    originals = {name: (assets / name).read_bytes() for name in (
        'Media/Wads/Db.wad', 'Media/Wads/GameIni.wad', 'Media/Wads/GamePart.wad')}
    source_db = read_preserved(research, source_prefix + 'Db.wad', inventory)
    catalog = profile_catalog(original_db=originals['Media/Wads/Db.wad'], source_db=source_db)
    xbox_ini = payloads(originals['Media/Wads/GameIni.wad'])
    source_ini = payloads(read_preserved(research, source_prefix + 'GameIni.wad', inventory))
    figure = unpack_zlb(payloads(originals['Media/Wads/GamePart.wad'])['media/gamepart/default.fig'])
    figure_names = parse_figure(figure)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix='.character-prepare-', dir=output.parent))
    summary = {'author': 'djultra64', 'private_only': True, 'schema': 'maxspin-character-preparation-v1',
               'assets': str(assets), 'research': str(research), 'packages': [],
               'title_validation': 'pending actual native Xbox title render and animation check',
               'default_activation': False, 'figure_sha256': sha(figure), 'figure_names': len(figure_names)}
    try:
        for character in characters:
            info = dict(CHARACTERS[character])
            recipe = recipes.get(character, {})
            profile = validate_profile(recipe.get('profile', {}), catalog)
            if 'displayName' in profile:
                info['display'] = profile['displayName']
            if 'shortName' in profile:
                info['short'] = profile['shortName']
            if 'lastName' in profile:
                info['surname'] = profile['lastName']
            if 'firstName' in profile or 'lastName' in profile:
                defaults = catalog['defaults'][character]
                info['name'] = profile.get('firstName', defaults['firstName']) + ' ' + profile.get('lastName', defaults['lastName'])
            portrait, portrait_proof = read_portrait(portraits[character]) if character in portraits else (None, None)
            package = temporary / str(info['slot'])
            package.mkdir()
            package_files, payload_bytes, base = {}, {}, dict(originals)
            member_updates, proofs = [], []
            def write(relative, data):
                destination = package / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)
                payload_bytes[relative] = data
                return {'path': relative, 'sha256': sha(data)}
            for outfit in range(2):
                target = info['target']
                relative = 'Media/Wads/Players/%s%02d.wad' % (target, outfit)
                base[relative] = (assets / relative).read_bytes()
                source_raw = read_preserved(research, source_prefix + 'Players/%s%02d.wad' % (target, outfit), inventory)
                original, source = payloads(base[relative]), payloads(source_raw)
                prefix = 'media/gamepart/players/%s%02d/' % (target.lower(), outfit)
                models, conversions, recipe_proofs, uv_proofs = {}, {}, {}, {}
                for lod in range(2):
                    name = 'playerlod%02d.ooo' % lod
                    model = unpack_zlb(source[prefix + name])
                    if character in recipes:
                        model, recipe_proofs[name] = compiler.compile_model(character, model, outfit, lod, recipes[character])
                    if 'uvAlignment' in recipe:
                        model, uv_proofs[name] = apply_uv_alignment(character, model, outfit, lod, recipe['uvAlignment'])
                    hair_consistency = None
                    if character == 'federer':
                        from federer_hair_consistency import correct_model
                        model, hair_consistency = correct_model(model, lod)
                    models[name], conversions[name] = convert_model(model)
                    if hair_consistency is not None:
                        conversions[name]['hair_consistency'] = hair_consistency
                texture_pack = unpack_zlb(source[prefix + 'textures/tex.pak'])
                wardrobe_proof = None
                if 'wardrobe' in recipe:
                    if outfit == 0:
                        models, texture_pack, wardrobe_proof = apply_character_wardrobe(models, texture_pack, assets, recipe['wardrobe'])
                        wardrobe_proof['scope'] = 'primary_outfit_only'
                    else:
                        wardrobe_proof = {'schema': 'maxspin-wardrobe-proof-v1', 'author': 'djultra64',
                            'scope': 'second_outfit_preserved', 'applied': False, 'changes': [],
                            'reason': 'Wardrobe choices target the primary outfit; the second outfit retains its existing clothes.',
                            'geometry_uv_skinning_unchanged': True, 'face_textures_unchanged': True,
                            'texture_source_sha256': sha(texture_pack), 'texture_result_sha256': sha(texture_pack)}
                    for source_name, digest in wardrobe_proof.get('source_files', {}).items():
                        data = base.get(source_name)
                        if data is None:
                            data = (assets / source_name).read_bytes()
                        if sha(data) != digest:
                            raise ValueError('Wardrobe source changed during preparation')
                        base[source_name] = data
                models, texture_pack, mapping = namespace_textures(models, texture_pack, character)
                from hair_mip_coverage import correct_texture_pack
                texture_pack, hair_mip_proof = correct_texture_pack(models, texture_pack)
                validation = validate_models(models, texture_pack, figure=figure)
                replacements = {prefix + name: pack_zlb(data, original[prefix + name]) for name, data in models.items()}
                replacements[prefix + 'textures/tex.pak'] = pack_zlb(texture_pack, original[prefix + 'textures/tex.pak'])
                ini_name = prefix + 'player.ini'
                replacements[ini_name] = pack_zlb(character_ini(unpack_zlb(original[ini_name]),
                    info['surname'], info['short'], info['display']), original[ini_name])
                replacements[prefix + 'scorename.dds'] = score_name_dds(original[prefix + 'scorename.dds'], info['display'], assets)
                candidate = rebuild_pam(base[relative], replacements)
                package_files[relative] = write('native/%s%02d.wad' % (target, outfit), candidate)
                member_updates.append({'archive': 'Media/Wads/GameIni.wad', 'member': ini_name,
                    **write('shared/%s%02d-player.ini' % (target, outfit), pack_zlb(character_ini(unpack_zlb(xbox_ini[ini_name]),
                        info['surname'], info['short'], info['display']), xbox_ini[ini_name]))})
                for leaf in ('vignette1.dds', 'vignettevisage.dds'):
                    key = prefix + leaf
                    if key in xbox_ini and key in source_ini:
                        member_updates.append({'archive': 'Media/Wads/GameIni.wad', 'member': key,
                            **write('shared/%s%02d-%s' % (target, outfit, leaf), portrait_dds(xbox_ini[key], portrait) if portrait is not None else _matching_dds(xbox_ini[key], source_ini[key]))})
                proofs.append({'outfit': outfit, 'source_archive_sha256': sha(source_raw),
                    'original_xbox_archive_sha256': sha(base[relative]), 'candidate_archive_sha256': sha(candidate),
                    'conversion': conversions, 'recipe_compilation': recipe_proofs, 'texture_namespace': mapping,
                    'uv_alignment': uv_proofs,
                    'wardrobe': wardrobe_proof,
                    'hair_mip_coverage': hair_mip_proof,
                    'models': {name: {'sha256': r['sha256'], 'objects': r['objects'], 'vertices': r['vertices'],
                                      'bone_ids': r['bone_ids']} for name, r in validation['models'].items()},
                    'all_source_pixel_payloads_preserved': not hair_mip_proof['changed_texture_ids'] and (not wardrobe_proof or not wardrobe_proof.get('changes')),
                    'all_other_xbox_archive_payloads_preserved': True})
            manifest = {'schema': 'maxspin-character-v1', 'slot': info['slot'], 'target': info['target'],
                'name': info['name'], 'author': 'djultra64',
                'compatibility': {'platform': 'xbox', 'validated': True,
                    'evidence': 'compatibility-report.json; complete typed Xbox reader grammar, FIG bone names and texture dependency verification',
                    'scope': 'structural native resources; title rendering and athlete likeness pending'},
                'base_files': {name: sha(data) for name, data in base.items()}, 'files': package_files,
                'member_updates': member_updates,
                'pml_updates': _database_updates(originals['Media/Wads/Db.wad'], source_db, info, profile=profile, catalog=catalog)}
            validation = validate_native_package(manifest, base, payload_bytes)
            proof = {'author': 'djultra64', 'private_only': True, 'character': character,
                'source_database_sha256': sha(source_db), 'figure_sha256': sha(figure),
                'recipe': recipes.get(character),
                'profile': profile, 'portrait': portrait_proof,
                'figure_bones_verified': validation['figure_verified'], 'outfits': proofs,
                'texture_identities': validation['texture_identities'],
                'title_validation': 'pending actual native title preview',
                'portrait_limit': 'User-selected renderer PNG encoded to native sizes.' if portrait is not None else 'Retained source-derived portraits; custom Federer geometry and Nadal likeness need review.',
                'uniform_policy': 'Explicit original Xbox wardrobe diffuse choices affect the primary outfit only; second outfit clothes and both model geometries are preserved.' if recipe.get('wardrobe') else 'Both preserved creator outfit models retained; selected custom hair lower-mip alpha receives explicit coverage preservation.',
                'source_route': 'Xbox original archive templates; retained baked geometry; no PC ADN regeneration.'}
            from scoreboard_font import font_provenance
            proof['scoreboard_font'] = font_provenance(assets)
            (package / 'compatibility-report.json').write_text(json.dumps(proof, indent=2) + '\n')
            (package / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
            summary['packages'].append({'character': character, 'slot': info['slot'], 'target': info['target'],
                                        'manifest_sha256': sha((package / 'manifest.json').read_bytes())})
        (temporary / 'preparation-report.json').write_text(json.dumps(summary, indent=2) + '\n')
        temporary.rename(output)
        return summary
    except BaseException:
        shutil.rmtree(temporary)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, default=ROOT / 'assets')
    parser.add_argument('--research', type=Path, default=DEFAULT_RESEARCH)
    parser.add_argument('--output', type=Path, default=ROOT / 'reports/character-candidates')
    parser.add_argument('--character', choices=tuple(CHARACTERS), action='append')
    parser.add_argument('--federer-recipe', type=Path, help='source-bound JSON exported by the local Federer Face Builder')
    parser.add_argument('--nadal-recipe', type=Path, help='source-bound JSON exported by the local Nadal Face Builder')
    parser.add_argument('--federer-portrait', type=Path, help='transparent PNG exported by the local Federer Face Builder')
    parser.add_argument('--nadal-portrait', type=Path, help='transparent PNG exported by the local Nadal Face Builder')
    args = parser.parse_args()
    try:
        recipes = {}
        portraits = {}
        for character in CHARACTERS:
            path = getattr(args, character + '_recipe')
            if path is not None:
                recipes[character] = json.loads(path.read_text())
            portrait = getattr(args, character + '_portrait')
            if portrait is not None:
                portraits[character] = portrait
        report = prepare(args.assets, args.research, args.output, args.character or tuple(CHARACTERS), recipes=recipes, portraits=portraits)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, 'Character preparation failed: %s\n' % error)
    print('Prepared %d private native candidates at %s; actual title validation remains pending.' % (len(report['packages']), args.output))


if __name__ == '__main__':
    main()
