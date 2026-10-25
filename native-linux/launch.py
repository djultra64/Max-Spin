#!/usr/bin/env python3
"""Run whitelisted native images and preserve original Xbox launch pages.
Own work: djultra64. No guest assets are modified.
"""
import argparse
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
from window_owner import PersistentWindow

HERE = Path(__file__).resolve().parent
IMAGES = json.loads((HERE / 'config/images.json').read_text())


def select_prompt_cache(build_dir, environment):
    """Select new resources only for an explicitly capable frozen build."""
    path = build_dir / 'manifest.json'
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
    except FileNotFoundError:
        return
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_size > 2 * 1024 * 1024:
            raise ValueError('Invalid prompt capability manifest')
        raw = os.read(fd, 2 * 1024 * 1024 + 1)
        def unique(pairs):
            result = {}
            for key, value in pairs:
                if key in result:
                    raise ValueError('Duplicate prompt capability key')
                result[key] = value
            return result
        manifest = json.loads(raw, object_pairs_hook=unique)
        if not isinstance(manifest, dict):
            raise ValueError('Invalid prompt capability manifest')
        if 'career_save_version' in manifest:
            career = manifest['career_save_version']
            sdk = manifest.get('sdk_patches_through')
            if type(career) is not int or career != 1 or type(sdk) is not int or sdk < 136:
                raise ValueError('Invalid Career save capability/runtime')
            environment['TOPSPIN_CAREER_SAVE_VERSION'] = '1'
            environment['TOPSPIN_CREATOR_MATERIAL_COHERENCE'] = '1'
            environment['RECOMP_SYNTHETIC_PHYSICAL_NAMESPACE'] = '1'
        version = manifest.get('controller_cache_version')
        slots = manifest.get('controller_glyph_slots')
        hud = manifest.get('controller_hud_cache_version')
        if 'controller_hud_cache_version' in manifest:
            if type(hud) is not int or hud != 1 or type(version) is not int or version != 4 or type(slots) is not int or slots != 9:
                raise ValueError('Invalid native HUD controller cache capability')
        sprite_version = manifest.get('sprite_ui_cache_version')
        ui_version = manifest.get('ui_cache_version')
        if type(ui_version) is int and ui_version == 3 and manifest.get('native_graphics_screen') is True:
            environment['TOPSPIN_MENU_PC_DIR'] = str(HERE / 'cache/menu-pc-v3')
        if type(ui_version) is int and ui_version == 4 and manifest.get('native_graphics_screen') is True and manifest.get('window_mode_options') is True:
            environment['TOPSPIN_MENU_PC_DIR'] = str(HERE / 'cache/menu-pc-v4')
        if type(sprite_version) is int and sprite_version == 1:
            environment['TOPSPIN_SPRITE_UI_CACHE'] = str(HERE / 'cache/sprite-ui-v1-menu')
        if type(version) is int and version == 4 and type(slots) is int and slots == 9:
            environment['TOPSPIN_PROMPTS_ROOT'] = str(HERE / ('cache/controller-prompts-v4-extended9-hud-v1' if hud == 1 else 'cache/controller-prompts-v4-extended9'))
    finally:
        os.close(fd)


def canonical_controller_family(value):
    if value == 'switch':
        return 'nintendo'
    if value not in ('auto', 'xbox', 'playstation', 'nintendo'):
        raise ValueError('invalid TOPSPIN_CONTROLLER_FAMILY')
    return value


def read_controller_family(path):
    # The session belongs to the supervisor, rather than a caller-supplied
    # preferences path. Reject nonregular/symlink state without blocking on a
    # pipe. Read a bounded canonical token; never inspect guest launch data.
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
    try:
        info = os.fstat(fd)
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid()
                or stat.S_IMODE(info.st_mode) != 0o600 or info.st_size > 16):
            raise ValueError('invalid controller family session file')
        raw = os.read(fd, 17)
    finally:
        os.close(fd)
    if raw not in (b'auto\n', b'xbox\n', b'playstation\n', b'nintendo\n'):
        raise ValueError('invalid controller family session state')
    return raw[:-1].decode('ascii')


def resolve_destination(path):
    """Accept disc-root XAPI destinations, never arbitrary host/guest paths."""
    if not isinstance(path, str) or not path or any(ord(c) < 32 for c in path):
        raise ValueError('empty or invalid launch destination')
    name = path
    if path.casefold().startswith('\\device\\cdrom0;'):
        name = path[len('\\Device\\CdRom0;'):]
    elif path.casefold().startswith('\\device\\cdrom0\\'):
        name = path[len('\\Device\\CdRom0\\'):]
    elif path[:3].casefold() == 'd:\\':
        name = path[3:]
    if any(c in name for c in '\\/:;') or name in ('.', '..'):
        raise ValueError(f'launch path is outside the allowed disc root: {path!r}')
    for key, filename in IMAGES.items():
        if name.casefold() == filename.casefold():
            return key
    raise ValueError(f'launch image is not whitelisted: {path!r}')


def page_destination(page):
    if len(page) != 4096:
        raise ValueError(f'launch page must contain exactly 4096 bytes (got {len(page)})')
    field = page[8:528]
    end = field.find(b'\0')
    if end < 0:
        raise ValueError('launch destination has no terminator in its 520-byte field')
    try:
        return resolve_destination(field[:end].decode('ascii'))
    except UnicodeDecodeError as error:
        raise ValueError('launch destination is not ASCII') from error


def image_command(image, build_dir, assets, saves):
    executable = build_dir / ('topspin_' + image)
    original = assets / IMAGES[image]
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise ValueError(f'Image is not built: {image}; expected {executable}. Run pipeline.py {image} and build TOPSPIN_IMAGE={image}.')
    if not original.is_file():
        raise ValueError(f'Missing original image: {original}')
    return [str(executable), str(assets), str(original), str(saves)]



def freeze_render_preferences(environment, session):
    """Freeze bounded raw INI once, before owner query; C remains its parser."""
    environment.pop('TOPSPIN_SESSION_RENDER_INI', None)
    raw = b''
    source = environment.get('TOPSPIN_GRAPHICS_INI')
    if source:
        try:
            fd = os.open(source, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
        except OSError:
            fd = None  # Same unavailable-INI fallback as the C consumer.
        if fd is not None:
            try:
                before = os.fstat(fd)
                if stat.S_ISREG(before.st_mode) and before.st_size <= 4096:
                    candidate = os.read(fd, 4097)
                    after = os.fstat(fd)
                    if (len(candidate) <= 4096 and len(candidate) == before.st_size
                            and (before.st_size, before.st_mtime_ns, before.st_ctime_ns)
                            == (after.st_size, after.st_mtime_ns, after.st_ctime_ns)):
                        raw = candidate
            finally:
                os.close(fd)
    path = session / 'render-startup.ini'
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o400)
    os.fchmod(fd, 0o400)
    with os.fdopen(fd, 'wb') as stream:
        stream.write(raw)
    environment['TOPSPIN_SESSION_RENDER_INI'] = str(path)


def supervise(image, build_dir, assets, saves):
    # Never import caller-supplied IN/OUT, including snapshots from a past run.
    child_env = os.environ.copy()
    select_prompt_cache(build_dir, child_env)
    child_env.pop('TOPSPIN_LAUNCH_PAGE_IN', None)
    child_env.pop('TOPSPIN_LAUNCH_PAGE_OUT', None)
    child_env.pop('TOPSPIN_CONTROLLER_SESSION_FILE', None)
    child_env.pop('TOPSPIN_SESSION_RENDER_INI', None)
    initial_family = canonical_controller_family(child_env.get('TOPSPIN_CONTROLLER_FAMILY', 'auto'))
    csv_prefix = child_env.pop('TOPSPIN_FRAME_CSV', None)
    initial_command = image_command(image, build_dir, assets, saves)
    with tempfile.TemporaryDirectory(prefix='topspin-launch-') as directory:
        freeze_render_preferences(child_env, Path(directory))
        return _supervise_session(image, build_dir, assets, saves, child_env, initial_command, directory, initial_family, csv_prefix)

def _supervise_session(image, build_dir, assets, saves, child_env, initial_command, directory, initial_family, csv_prefix):
    imported = None
    with PersistentWindow(build_dir, child_env, video_executable=Path(initial_command[0])) as window:
        child_env = window.environment
        if window.session_height is not None:
            # Saving a new INI affects the next supervisor session, not the
            # next image in this same persistent window. The query itself and
            # the parent SDL owner never receive this child-only pin.
            child_env['TOPSPIN_SESSION_INTERNAL_HEIGHT'] = str(window.session_height)
        session = Path(directory)
        family_path = session / 'controller-family'
        fd = os.open(family_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o600)
        with os.fdopen(fd, 'wb') as stream:
            stream.write((initial_family + '\n').encode('ascii'))
        child_env['TOPSPIN_CONTROLLER_SESSION_FILE'] = str(family_path)
        generation = 0
        while True:
            closing = window.close_status()
            if closing is not None:
                return closing
            filename = IMAGES[image]
            command = image_command(image, build_dir, assets, saves)
            output = session / f'{generation:06d}-{image}.page'
            env = child_env.copy()
            # A selected AUTO must replace an initial environment override.
            # Existing image input initialization consumes this canonical env.
            env['TOPSPIN_CONTROLLER_FAMILY'] = read_controller_family(family_path)
            env['TOPSPIN_LAUNCH_PAGE_OUT'] = str(output)
            if imported is not None:
                env['TOPSPIN_LAUNCH_PAGE_IN'] = str(imported)
            if csv_prefix:
                env['TOPSPIN_FRAME_CSV'] = f'{csv_prefix}.{generation:06d}-{image}.csv'
            if generation:
                if env.get('TOPSPIN_TEST_SCRIPT_INITIAL_ONLY') == '1':
                    env.pop('RECOMP_PAD_SCRIPT', None)
                    env.pop('RECOMP_PAD_SCRIPT_FORMAT', None)
                # Preserve the preceding image's diagnostic evidence. Input
                # commands from its menu must not replay in the next image.
                for variable in ('TOPSPIN_CAPTURE_PREFIX', 'TOPSPIN_BLACK_WATCH_PREFIX',
                                 'RECOMP_POINT_CAPTURE_PREFIX', 'TOPSPIN_REPLAY_SAMPLE_PREFIX',
                                 'TOPSPIN_PHYSICS_TRACE', 'TOPSPIN_RENDER_TRACE',
                                 'TOPSPIN_INTERVAL_EVENTS', 'TOPSPIN_TIMING_TRACE',
                                 'TOPSPIN_CPU_PROFILE', 'TOPSPIN_MENU_INPUT_TRACE',
                                 'TOPSPIN_CAREER_TRACE', 'TOPSPIN_SCHEDULER_TRACE'):
                    if env.get(variable):
                        env[variable] = f'{env[variable]}.{generation:06d}-{image}'
                if env.get('RECOMP_PAD_LIVE'):
                    live = session / f'{generation:06d}-{image}.input'
                    live.write_text('')
                    env['RECOMP_PAD_LIVE'] = str(live)
                    print(f'[LAUNCH] fresh live input: {live}', flush=True)
            print(f'[LAUNCH] {generation}: {image} ({filename})', flush=True)
            # No shell, pipes, backgrounding, display/focus or console overrides.
            status = window.run(command, env)
            if status != 0:
                return status if status > 0 else 128 - status
            if not output.exists():
                return 0
            page = output.read_bytes()
            destination = page_destination(page)
            # Keep the exact bytes needed to diagnose preferences/port routing
            # after the temporary supervisor session has gone away. One small,
            # private record per valid handoff alongside the existing log CSV;
            # no guest-memory edits, extra title runs or input injection.
            if csv_prefix:
                archive = Path(f'{csv_prefix}.{generation:06d}-{image}.launch-page')
                try:
                    fd = os.open(archive, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
                    with os.fdopen(fd, 'wb') as stream:
                        stream.write(page)
                except OSError as error:
                    print(f'[LAUNCH] cannot archive launch evidence: {error}', file=sys.stderr, flush=True)
            image = destination
            imported = output
            generation += 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', choices=IMAGES, nargs='?', default='game')
    parser.add_argument('--build-dir', type=Path, default=HERE / 'build')
    parser.add_argument('--assets', type=Path, default=HERE.parent / 'assets')
    parser.add_argument('--saves', type=Path, default=HERE / 'saves')
    args = parser.parse_args(argv)
    try:
        build_dir = args.build_dir.resolve()
        print(f'[BUILD] {build_dir.name!r}; fixed for this session', flush=True)
        assets = args.assets.resolve()
        layout = HERE / 'cache/ui-layout-v1'
        if (layout.exists() or layout.is_symlink()) and assets == (HERE.parent / 'assets').resolve():
            sys.path.insert(0, str(HERE / 'tools'))
            from ui_layout import select_assets
            assets = select_assets(assets, layout)
        return supervise(args.image, build_dir, assets, args.saves.resolve())
    except (ValueError, OSError) as error:
        print(f'[LAUNCH] {error}', file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 130


if __name__ == '__main__':
    sys.exit(main())
