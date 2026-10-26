#!/usr/bin/env python3
"""Prepare approved private Xbox athlete packages. djultra64.

Never activates packages or edits original assets, research or Federer slot 9.
"""
import argparse
import json
from pathlib import Path
import shutil
import tempfile

from approved_roster_identity import ROSTER, character_ini, country_status, database_updates
from character_formats import pack_zlb, payloads, rebuild_pam, sha, unpack_zlb, validate_native_package
from prepare_characters import portrait_dds, read_portrait, score_name_dds

ROOT = Path(__file__).resolve().parents[2]
CHARACTERS = ('nadal', 'djokovic', 'agassi')
REMAINING_CHARACTERS = ('becker', 'mcenroe', 'pioline', 'haas')
PRESERVED_PORTRAIT_MODE = 'original native portraits preserved; provisional pending user portraits'
PORTRAITS = ROOT / 'reports/four-character-integration-20261025/portraits'
APPROVED_PORTRAIT_HASHES = {
    'nadal': '66d9eae7bce6855138617217dc636cf7e21842d488bdde9edb0d8525d258fa6f',
    'djokovic': '114a321be2f6089e8f588098a0e8a03029575910a7e3db07be76badc48160b5f',
    'agassi': 'e76cdcf259a5e830c9ab961028265de67d367b81f71cbf6df627cb58d3f26eeb',
}
PORTRAIT_SELECTION = ROOT / 'reports/four-character-integration-20261025/portraits-corrected/selection.json'


def _portrait_inputs(portraits, portrait_hashes, selection, legacy):
    """Resolve distinct screenshot roles against caller-selected SHA256 bindings."""
    metadata = {}
    if legacy:
        if portrait_hashes:
            raise ValueError('Legacy bust mode cannot use independent portrait bindings')
        portraits = dict(portraits or {})
        if not set(portraits) <= set(CHARACTERS):
            raise ValueError('Unknown approved portrait athlete')
        paths = {character: {role: portraits.get(character, PORTRAITS / (character+'-bust-portrait.png'))
                            for role in ('card', 'face')} for character in CHARACTERS}
        hashes = {character: dict.fromkeys(('card', 'face'), APPROVED_PORTRAIT_HASHES[character]) for character in CHARACTERS}
        metadata['mode'] = 'legacy bust; interim development and capture only'
    elif portraits is None:
        selection = Path(selection or PORTRAIT_SELECTION)
        raw = selection.read_bytes()
        chosen = json.loads(raw)
        if chosen.get('status') != 'approved':
            raise ValueError('Native screenshot portrait selection is not approved')
        rows = chosen['portraits']
        def selected_path(value):
            path = Path(value)
            return path if path.is_absolute() else selection.parent / path
        paths = {character: {role: selected_path(rows[character][role]['path']) for role in ('card', 'face')} for character in CHARACTERS}
        hashes = {character: {role: rows[character][role]['sha256'] for role in ('card', 'face')} for character in CHARACTERS}
        metadata.update(mode='independent native screenshot card and face', selection=str(selection.resolve()),
                        selection_sha256=sha(raw), selected_roles={character: rows[character] for character in CHARACTERS})
    else:
        paths, hashes = portraits, portrait_hashes
        metadata['mode'] = 'independent native screenshot card and face'
    if not isinstance(paths, dict) or set(paths) != set(CHARACTERS) or not isinstance(hashes, dict) or set(hashes) != set(CHARACTERS):
        raise ValueError('Trusted portrait roles are required for every approved athlete')
    images, proofs = {}, {}
    for character in CHARACTERS:
        if not isinstance(paths[character], dict) or set(paths[character]) != {'card', 'face'} or not isinstance(hashes[character], dict) or set(hashes[character]) != {'card', 'face'}:
            raise ValueError('Each approved portrait requires distinct card and face roles')
        images[character], proofs[character] = {}, {'selection': metadata}
        for role in ('card', 'face'):
            path = Path(paths[character][role])
            expected = hashes[character][role]
            if not isinstance(expected, str):
                raise ValueError('Image differs from approved portrait role: '+character+'/'+role)
            try:
                image, proof = read_portrait(path)
            except (OSError, ValueError) as error:
                raise ValueError('Invalid approved portrait role: '+character+'/'+role) from error
            if proof['sha256'] != expected:
                raise ValueError('Image differs from approved portrait role: '+character+'/'+role)
            if not legacy and image.size != (512, 512):
                raise ValueError('Independent native portrait roles require 512 pixel square PNGs')
            images[character][role] = image
            proofs[character][role] = dict(proof, role=role,
                native_member='vignette1.dds' if role == 'card' else 'vignettevisage.dds')
        if not legacy and (hashes[character]['card'] == hashes[character]['face'] or
                           images[character]['card'].tobytes() == images[character]['face'].tobytes()):
            raise ValueError('Card and face portraits must be independent crops')
    return images, proofs


def prepare(assets, research, output, *, portraits=None, portrait_hashes=None,
            portrait_selection=None, legacy_portraits=False, assembler=None,
            characters=CHARACTERS, preserve_native_portraits=False):
    """Atomically create complete, source-bound private candidate packages.

    portraits and portrait_hashes map athlete -> {card: value, face: value}.
    Without explicit paths, use the approved screenshot selection.json. Legacy
    busts require legacy_portraits=True and are labelled for interim capture.

    The default three athletes retain strict approved screenshot requirements.
    Explicit remaining-athlete selections require preserve_native_portraits=True;
    their original selector portrait members remain untouched and provisional.
    This review mode rejects all portrait replacement inputs.

    The optional assembler isolates geometry assembly for tests. It has the same
    contract as approved_character_models.assemble_models; package validation
    and source/payload hash checks always run on its actual output.
    """
    if not isinstance(characters, (tuple, list)) or not characters or any(
            not isinstance(character, str) for character in characters):
        raise ValueError('A nonempty explicit character selection is required')
    characters = tuple(characters)
    if len(set(characters)) != len(characters) or not set(characters) <= set(CHARACTERS+REMAINING_CHARACTERS):
        raise ValueError('Unknown or duplicate character selection')
    if preserve_native_portraits:
        if not set(characters) <= set(REMAINING_CHARACTERS):
            raise ValueError('Preserved portrait review mode is limited to remaining characters')
        if legacy_portraits or any(value is not None for value in (portraits, portrait_hashes, portrait_selection)):
            raise ValueError('Preserved native portraits cannot be combined with portrait replacement inputs')
    elif characters != CHARACTERS:
        raise ValueError('Remaining character review requires preserve_native_portraits=True')
    assets, research = Path(assets).resolve(), Path(research).resolve()
    output = Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise ValueError('Candidate output already exists; select a new directory')
    output = output.resolve(strict=False)
    for protected in (assets, research, ROOT / 'characters'):
        protected = protected.resolve()
        if output == protected or output.is_relative_to(protected):
            raise ValueError('Candidate output must remain outside originals, research and active characters')
    if preserve_native_portraits:
        images = {}
        portrait_proofs = {character: {'mode': PRESERVED_PORTRAIT_MODE,
            'original_native_portraits_preserved': True, 'preserved_members': {}}
            for character in characters}
    else:
        images, portrait_proofs = _portrait_inputs(portraits, portrait_hashes, portrait_selection, legacy_portraits)
    if assembler is None:
        from approved_character_models import assemble_models
        assembler = assemble_models
    originals = {name: (assets / name).read_bytes() for name in (
        'Media/Wads/Db.wad', 'Media/Wads/GameIni.wad', 'Media/Wads/GamePart.wad')}
    xbox_ini = payloads(originals['Media/Wads/GameIni.wad'])
    source_cache = dict(originals)
    source_digests = {}
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix='.approved-roster-', dir=output.parent))
    summary = {'schema': 'maxspin-approved-roster-preparation-v1', 'author': 'djultra64',
               'private_only': True, 'default_activation': False,
               'assets': str(assets), 'research': str(research), 'packages': [],
               'portrait_mode': PRESERVED_PORTRAIT_MODE if preserve_native_portraits else (
                   'legacy bust; interim development and capture only' if legacy_portraits else 'independent native screenshot card and face'),
               'federer_slot_9': 'existing final package preserved; not generated or modified',
               'title_validation': 'pending actual native title render and animation check'}
    try:
        for character in characters:
            info = ROSTER[character]
            package = temporary / str(info['slot'])
            package.mkdir()
            models, packs, model_proof = assembler(character, assets, research, ROOT)
            if set(models) != {(outfit, lod) for outfit in (0, 1) for lod in (0, 1)} or set(packs) != {0, 1}:
                raise ValueError('Approved assembly requires both outfits and both LODs')
            base, files, blobs, member_updates, outfits = dict(originals), {}, {}, [], []
            # Bind both outfit wardrobe selections to the same immutable original
            # source bytes; a repeated source is read/hash-checked only once here.
            for wardrobe in model_proof.get('wardrobe', []):
                for source, digest in wardrobe.get('source_files', {}).items():
                    if source not in ('Media/Wads/Db.wad', 'Media/Wads/PlayerCreatorMaleCloth.wad'):
                        raise ValueError('Unsupported native wardrobe source binding')
                    if source not in source_cache:
                        source_cache[source] = (assets / source).read_bytes()
                    if source not in source_digests:
                        source_digests[source] = sha(source_cache[source])
                    if source_digests[source] != digest:
                        raise ValueError('Native wardrobe source hash mismatch: '+source)
                    base[source] = source_cache[source]

            def write(relative, data):
                destination = package / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)
                blobs[relative] = data
                return {'path': relative, 'sha256': sha(data)}

            for outfit in (0, 1):
                from hair_mip_coverage import correct_texture_pack
                packs[outfit], hair_mip_proof = correct_texture_pack(
                    {lod:models[outfit, lod] for lod in (0, 1)}, packs[outfit])
                target = info['target']
                archive = f'Media/Wads/Players/{target}{outfit:02d}.wad'
                base[archive] = (assets / archive).read_bytes()
                original = payloads(base[archive])
                prefix = f'media/gamepart/players/{target.lower()}{outfit:02d}/'
                changes = {prefix+f'playerlod{lod:02d}.ooo': pack_zlb(models[outfit, lod], original[prefix+f'playerlod{lod:02d}.ooo']) for lod in (0, 1)}
                key = prefix+'textures/tex.pak'
                changes[key] = pack_zlb(packs[outfit], original[key])
                key = prefix+'player.ini'
                changes[key] = pack_zlb(character_ini(unpack_zlb(original[key]), character), original[key])
                key = prefix+'scorename.dds'
                changes[key] = score_name_dds(original[key], info['display'], assets)
                candidate = rebuild_pam(base[archive], changes)
                files[archive] = write(f'native/{target}{outfit:02d}.wad', candidate)
                key = prefix+'player.ini'
                member_updates.append({'archive': 'Media/Wads/GameIni.wad', 'member': key,
                    **write(f'shared/{target}{outfit:02d}-player.ini',
                            pack_zlb(character_ini(unpack_zlb(xbox_ini[key]), character), xbox_ini[key]))})
                for leaf in ('vignette1.dds', 'vignettevisage.dds'):
                    key = prefix+leaf
                    if key in xbox_ini and preserve_native_portraits:
                        portrait_proofs[character]['preserved_members'][key] = sha(xbox_ini[key])
                    elif key in xbox_ini:
                        member_updates.append({'archive': 'Media/Wads/GameIni.wad', 'member': key,
                            **write(f'shared/{target}{outfit:02d}-{leaf}', portrait_dds(xbox_ini[key], images[character]['card' if leaf == 'vignette1.dds' else 'face']))})
                if any(value != payloads(candidate)[key] for key, value in original.items() if key not in changes):
                    raise ValueError('Unrelated original Xbox member changed')
                outfits.append({'outfit': outfit, 'original_sha256': sha(base[archive]),
                                'candidate_sha256': sha(candidate), 'original_audio_preserved': True,
                                'hair_mip_coverage': hair_mip_proof,
                                'all_source_pixel_payloads_preserved': not hair_mip_proof['changed_texture_ids'],
                                'unrelated_original_members_preserved': True})
            manifest = {'schema': 'maxspin-character-v1', 'slot': info['slot'], 'target': info['target'],
                        'name': info['first']+' '+info['last'], 'author': 'djultra64',
                        'compatibility': {'platform': 'xbox', 'validated': True,
                            'evidence': 'compatibility-report.json; complete native reader, FIG bones and texture dependencies validated',
                            'scope': 'structural native resources; actual title render remains pending'},
                        'base_files': {name: sha(raw) for name, raw in base.items()},
                        'files': files, 'member_updates': member_updates,
                        'pml_updates': database_updates(originals['Media/Wads/Db.wad'], character)}
            validation = validate_native_package(manifest, base, blobs)
            proof = {'author': 'djultra64', 'private_only': True, 'character': character,
                     'portrait': portrait_proofs[character], 'country': country_status(originals['Media/Wads/Db.wad'], character),
                     'models': model_proof, 'outfits': outfits,
                     'figure_bones_verified': validation['figure_verified'],
                     'texture_identities': validation['texture_identities'],
                     'original_gameplay_stats_preserved': True, 'original_audio_preserved': True,
                     'selector_portraits': summary['portrait_mode']+'; existing native DDS members only',
                     'title_validation': summary['title_validation']}
            from scoreboard_font import font_provenance
            proof['scoreboard_font'] = font_provenance(assets)
            (package / 'compatibility-report.json').write_text(json.dumps(proof, indent=2)+'\n')
            (package / 'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
            # Exercise the exact launcher admission path before exposing output.
            from character_packages import _load
            _load(package, assets)
            summary['packages'].append({'character': character, 'slot': info['slot'], 'target': info['target'],
                'country': proof['country'], 'files': {str(path.relative_to(package)): sha(path.read_bytes())
                    for path in sorted(package.rglob('*')) if path.is_file()}})
        (temporary / 'preparation-report.json').write_text(json.dumps(summary, indent=2)+'\n')
        temporary.rename(output)
        return summary
    except BaseException:
        shutil.rmtree(temporary)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, default=ROOT / 'assets')
    parser.add_argument('--research', type=Path, default=ROOT.parent / 'character-research/legacy')
    parser.add_argument('--output', type=Path, default=ROOT / 'reports/four-character-integration-20261025/candidates')
    parser.add_argument('--portrait-selection', type=Path, help='Approved independent screenshot selection; defaults to the existing three-athlete selection')
    parser.add_argument('--remaining-four', action='store_true', help='Explicitly prepare only Becker, McEnroe, Pioline and Haas review candidates')
    parser.add_argument('--preserve-native-portraits', action='store_true', help='Keep original selector portraits provisionally; requires --remaining-four')
    parser.add_argument('--legacy-bust-portraits', action='store_true', help='Explicit interim development/capture package using historical bust images')
    args = parser.parse_args()
    try:
        report = prepare(args.assets, args.research, args.output,
                         portrait_selection=args.portrait_selection, legacy_portraits=args.legacy_bust_portraits,
                         characters=REMAINING_CHARACTERS if args.remaining_four else CHARACTERS,
                         preserve_native_portraits=args.preserve_native_portraits)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, 'Approved private package preparation failed: %s\n' % error)
    print('Prepared %d private packages at %s; actual title validation remains pending.' % (len(report['packages']), args.output))


if __name__ == '__main__':
    main()
