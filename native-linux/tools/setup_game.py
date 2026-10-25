#!/usr/bin/env python3
"""Prepare Max Spin's local resources after building all modules. djultra64.

Game-derived outputs stay local. Existing resource directories and capability
manifests are refused; this tool never replaces an active installation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import tempfile

from menu_graphics_scene import prepare as prepare_graphics, resource_header
from prepare_extended_prompts import prepare as prepare_prompts, EXTRA
from prepare_hud_prompts import prepare as prepare_hud
from prepare_sprite_cache import prepare as prepare_sprites
from ui_layout import prepare as prepare_ui_layout

NATIVE = Path(__file__).resolve().parents[1]
ROOT = NATIVE.parent
PROMPT_IMAGES = ('menu', 'game', 'career', 'training', 'playercreator', 'shop', 'online')
PROMPT_CACHE = 'controller-prompts-v4-extended9-hud-v1'
OUTPUTS = ('menu-pc-v4', 'sprite-ui-v1-menu', PROMPT_CACHE)
UI_LAYOUT = 'ui-layout-v1'
LOCAL_OUTPUTS = OUTPUTS + ((UI_LAYOUT,) if os.name != 'nt' else ())
BASE_ART = ('XboxSeriesX_A', 'XboxSeriesX_B', 'XboxSeriesX_X', 'XboxSeriesX_Y',
            'PS5_Cross', 'PS5_Circle', 'PS5_Square', 'PS5_Triangle',
            'XboxSeriesX_LB', 'XboxSeriesX_RB', 'PS5_L1', 'PS5_R1',
            'Switch_LB', 'Switch_RB', 'Switch_A', 'Switch_B', 'Switch_X', 'Switch_Y')


def digest(path):
    if not stat.S_ISREG(path.lstat().st_mode):
        raise ValueError('expected a regular non-symlink file: ' + str(path))
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def clean_tree(path):
    """Remove only a tree created by the current invocation, including 0555 dirs."""
    for current, directories, _ in os.walk(path):
        Path(current).chmod(0o700)
        for name in directories:
            child = Path(current) / name
            if not child.is_symlink():
                child.chmod(0o700)
    shutil.rmtree(path)


def verify_art(art):
    provenance = json.loads((art / 'provenance.json').read_text())
    if provenance.get('license') != 'CC0-1.0':
        raise ValueError('expected the documented CC0 controller artwork')
    known = {row['file']: row['sha256'] for row in provenance['files']}
    for name in (*BASE_ART, *EXTRA):
        filename = name + '.png'
        if digest(art / filename) != known.get(filename):
            raise ValueError('controller artwork identity mismatch: ' + filename)


def prepare(assets, build_dir, cache_root, art):
    images = json.loads((NATIVE / 'config/images.json').read_text())
    for filename in images.values():
        if Path(filename).name != filename or not (assets / filename).is_file():
            raise ValueError('missing original executable: ' + filename)
    verify_art(art)
    build_dir = Path(build_dir).absolute()
    if build_dir.is_symlink() or not build_dir.is_dir():
        raise ValueError('build directory must be an existing plain directory')
    manifest_path = build_dir / 'manifest.json'
    if manifest_path.exists() or manifest_path.is_symlink():
        raise ValueError('build manifest already exists; use a new build directory')
    binaries = {}
    for image in (*images, 'window_host'):
        path = build_dir / ('topspin_' + image)
        if not path.exists() and path.with_suffix('.exe').exists():
            path = path.with_suffix('.exe')
        if image == 'window_host' and not path.exists() and (build_dir / 'MaxSpin.exe').exists():
            path = build_dir / 'MaxSpin.exe'
        checksum = digest(path)
        with path.open('rb') as stream:
            magic = stream.read(4)
        if magic != b'\x7fELF' and magic[:2] != b'MZ':
            raise ValueError('not a native executable: ' + path.name)
        binaries[image] = dict(file=path.name, sha256=checksum)
    cache_root = Path(cache_root).absolute()
    if cache_root.is_symlink():
        raise ValueError('cache root cannot be a symlink')
    for name in LOCAL_OUTPUTS:
        path = cache_root / name
        if path.exists() or path.is_symlink():
            raise ValueError('resource output already exists: ' + str(path))
    cache_root.mkdir(mode=0o700, parents=True, exist_ok=True)
    published = []
    temporary = Path(tempfile.mkdtemp(prefix='.max-spin-setup-', dir=cache_root))
    try:
        chunks = prepare_graphics(assets / 'Media/MenuPart/Rui/MainMenu.rui', temporary / OUTPUTS[0])
        expected = (NATIVE / 'src/menu_graphics_resources.h').read_bytes()
        if resource_header(chunks) != expected:
            raise ValueError('generated menu does not match the compiled resource guards')
        prepare_sprites(assets, temporary / OUTPUTS[1])
        base = temporary / 'prompt-base'
        base.mkdir(mode=0o700)
        final = temporary / PROMPT_CACHE
        final.mkdir(mode=0o700)
        for image in PROMPT_IMAGES:
            prepare_prompts(assets, base / image, image,
                            NATIVE / 'tools/prepare_menu_prompts.py', art)
            if image in ('game', 'training'):
                prepare_hud(assets, base / image, final / image)
            else:
                shutil.copytree(base / image, final / image)
        if UI_LAYOUT in LOCAL_OUTPUTS:
            prepare_ui_layout(assets, temporary / UI_LAYOUT)
        manifest = dict(schema='max-spin-local-build-v1', author='djultra64',
                        images=binaries, controller_cache_version=4, controller_glyph_slots=9,
                        controller_hud_cache_version=1, ui_cache_version=4,
                        native_graphics_screen=True, window_mode_options=True,
                        sprite_ui_cache_version=1, career_save_version=1,
                        sdk_patches_through=136)
        resources = {}
        for name in OUTPUTS:
            for path in sorted((temporary / name).rglob('*')):
                if path.is_file():
                    resources[str(path.relative_to(temporary))] = digest(path)
        if UI_LAYOUT in LOCAL_OUTPUTS:
            manifest['ui_layout_cache_version'] = 1
            for path in [temporary / UI_LAYOUT / 'manifest.json', *sorted((temporary / UI_LAYOUT / 'patches').rglob('*.rui'))]:
                resources[str(path.relative_to(temporary))] = digest(path)
        manifest['local_resource_sha256'] = resources
        # Publish only after all inputs, generated guards and modules validate.
        # A failed final manifest commit rolls back only this invocation's trees.
        for name in LOCAL_OUTPUTS:
            source, destination = temporary / name, cache_root / name
            # mkdir reserves the final name exclusively. Never replace a
            # directory another process created during resource generation.
            destination.mkdir(mode=0o700)
            published.append(destination)
            shutil.copytree(source, destination, dirs_exist_ok=True, symlinks=True)
        encoded = (json.dumps(manifest, indent=2) + '\n').encode()
        fd = os.open(manifest_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            with os.fdopen(fd, 'wb') as stream:
                stream.write(encoded)
                stream.flush()
                os.fsync(stream.fileno())
        except BaseException:
            manifest_path.unlink(missing_ok=True)
            raise
    except BaseException:
        for destination in reversed(published):
            clean_tree(destination)
        raise
    finally:
        clean_tree(temporary)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, default=ROOT / 'assets')
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--cache-root', type=Path, default=NATIVE / 'cache',
                        help='advanced preparation/testing destination; launcher uses native-linux/cache')
    parser.add_argument('--art', type=Path, default=NATIVE / 'assets/controller-prompts-xelu')
    args = parser.parse_args()
    try:
        prepare(args.assets.absolute(), args.build_dir, args.cache_root, args.art.absolute())
    except (OSError, ValueError, KeyError) as error:
        parser.exit(2, 'Max Spin preparation failed: ' + str(error) + '\n')
    print('Max Spin local resources and build manifest prepared. This is not a gameplay compatibility test.')


if __name__ == '__main__':
    main()
