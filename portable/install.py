"""Portable installation and launch, with no machine-wide changes. djultra64."""
import hashlib
import importlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import sys

MARKER = 'portable-install.json'


def _plain_parents(path):
    for part in (path, *path.parents):
        if part.is_symlink() or (hasattr(part, 'is_junction') and part.is_junction()):
            raise ValueError('Elige una carpeta local sin enlaces ni redirecciones.')


def validate_destination(destination, payload):
    destination, payload = Path(destination).absolute(), Path(payload).resolve()
    _plain_parents(destination)
    destination = destination.resolve()
    if destination == payload or destination.is_relative_to(payload) or payload.is_relative_to(destination):
        raise ValueError('Elige otra carpeta para instalar el juego, fuera del paquete descargado.')
    if destination.exists() and (not destination.is_dir() or any(destination.iterdir())):
        raise ValueError('La carpeta elegida contiene archivos. Elige una carpeta vacía.')
    if not destination.parent.is_dir():
        raise ValueError('La carpeta que contiene el destino no existe.')
    return destination


def _hash(path, cancelled=None):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while True:
            _cancel(cancelled)
            block = stream.read(1024*1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def _payload_files(payload, cancelled=None, progress=None):
    encoded = (payload/'RELEASE_MANIFEST.json').read_bytes()
    manifest = json.loads(encoded)
    if manifest.get('platform') not in ('windows', 'linux') or not isinstance(manifest.get('files'), dict):
        raise ValueError('El paquete está incompleto. Descarga el paquete completo de Max Spin.')
    files = []
    for index, (relative, digest) in enumerate(manifest['files'].items()):
        _cancel(cancelled)
        if progress:
            progress('validate', index, len(manifest['files']), relative)
        item = Path(relative)
        if item.is_absolute() or '..' in item.parts or '\\' in relative or ':' in relative:
            raise ValueError('El paquete contiene una ruta inválida.')
        source = payload/item
        _plain_parents(source)
        if not source.is_file() or _hash(source, cancelled) != digest:
            raise ValueError('El paquete está incompleto o dañado: '+relative)
        files.append((source, item, digest))
    suffix = '.exe' if manifest['platform'] == 'windows' else ''
    required = {'portable-runtime/MaxSpin-Setup'+suffix, 'native-linux/tools/setup_game.py',
                'native-linux/config/original-images.json'}
    if not required.issubset(manifest['files']):
        raise ValueError('Este paquete no incluye el instalador portátil completo.')
    # Preserve the exact validated manifest; do not reread a mutable download
    # after its members have been checked and copied.
    return manifest['platform'], files, encoded


def _cancel(cancelled):
    if cancelled and cancelled():
        raise InterruptedError('Instalación cancelada.')


def _verify_copied(root, files, cancelled):
    """Recheck installed bytes before executing helpers and publishing success."""
    for _, relative, checksum in files:
        path = root/relative
        _plain_parents(path)
        if not path.is_file() or _hash(path, cancelled) != checksum:
            raise ValueError('El paquete cambió o está dañado: '+str(relative))


def prepare_resources(root, platform=None, progress=None, cancelled=None):
    root = Path(root)
    platform = platform or ('windows' if os.name == 'nt' else 'linux')
    tools = root/'native-linux/tools'
    spec = importlib.util.spec_from_file_location('_maxspin_prepare', tools/'setup_game.py')
    module = importlib.util.module_from_spec(spec)
    # A retry may select another destination after the previous one was removed.
    # Helpers such as ui_layout cache their installed root in module globals.
    # Evict every local helper name, then restore the caller's imports on both
    # success and failure so no import retains a stale installation directory.
    helper_names = {path.stem for path in tools.glob('*.py') if path.stem.isidentifier()}
    def is_helper(name):
        return name.partition('.')[0] in helper_names
    previous_modules = {name: value for name, value in tuple(sys.modules.items()) if is_helper(name)}
    for name in previous_modules:
        del sys.modules[name]
    previous_path = sys.path[:]
    sys.path.insert(0, str(tools))
    importlib.invalidate_caches()
    previous = sys.dont_write_bytecode
    sys.dont_write_bytecode = True
    try:
        spec.loader.exec_module(module)
        module.prepare(root/'assets', root if platform == 'windows' else root/'bin',
                       root/'native-linux/cache', root/'native-linux/assets/controller-prompts-xelu',
                       progress=progress, cancelled=cancelled)
    finally:
        sys.dont_write_bytecode = previous
        sys.path[:] = previous_path
        for name in tuple(sys.modules):
            if is_helper(name):
                del sys.modules[name]
        sys.modules.update(previous_modules)


def _relocatable_layout(root):
    cache = root/'native-linux/cache/ui-layout-v1'
    _plain_parents(cache)
    if not cache.is_dir():
        return
    # The preparer seals its generated trees. Change only our freshly prepared
    # tree; never chmod a symlink target in the extracted originals.
    for current, directories, files in os.walk(cache):
        _plain_parents(Path(current))
        Path(current).chmod(0o700)
        for name in directories + files:
            link = Path(current)/name
            if hasattr(link, 'is_junction') and link.is_junction():
                raise ValueError('Un recurso preparado contiene una redirección de carpetas.')
            if link.is_symlink():
                target = link.resolve()
                if not target.is_relative_to(root):
                    raise ValueError('Un recurso preparado apunta fuera de la instalación.')
                link.unlink()
                link.symlink_to(os.path.relpath(target, link.parent))
    manifest = cache/'manifest.json'
    _plain_parents(manifest)
    report = json.loads(manifest.read_text())
    report['source'] = os.path.relpath(root/'assets', cache)
    manifest.chmod(0o600)
    manifest.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')


def _remove_owned(root):
    # Never chmod through a replaced root, symlink, or Windows junction.
    _plain_parents(root)
    for current, directories, files in os.walk(root):
        directories[:] = [name for name in directories
                          if not (Path(current)/name).is_symlink()
                          and not (hasattr(Path(current)/name, 'is_junction')
                                   and (Path(current)/name).is_junction())]
        Path(current).chmod(0o700)
        for name in files:
            file = Path(current)/name
            info = file.lstat()
            if stat.S_ISREG(info.st_mode) and info.st_nlink == 1:
                file.chmod(0o600)
    shutil.rmtree(root)


def install(iso, destination, payload, progress=None, cancelled=None):
    from portable.xdvdfs import XboxISO
    payload = Path(payload).resolve()
    destination = validate_destination(destination, payload)
    report = progress or (lambda *args: None)
    _cancel(cancelled)
    report('validate', 0, 0, 'Comprobando la ISO y el paquete…')
    platform, files, manifest_bytes = _payload_files(payload, cancelled, report)
    with XboxISO(iso) as disc:
        expected = json.loads((payload/'native-linux/config/original-images.json').read_text())
        entries = {str(entry.path).casefold(): entry for entry in disc.entries if not entry.is_dir}
        for name, checksum in expected.items():
            _cancel(cancelled)
            entry = entries.get(name.casefold())
            if not entry or entry.size > 64*1024*1024 or hashlib.sha256(disc.read(entry)).hexdigest() != checksum:
                raise ValueError('La ISO no corresponde a la edición de Top Spin para Xbox compatible con este paquete, o está dañada.')
        required_bytes = disc.total_bytes + sum(src.stat().st_size for src, _, _ in files)
        # Resource preparation needs working space alongside its final caches.
        if shutil.disk_usage(destination.parent).free < required_bytes + 1024*1024*1024:
            raise ValueError('No hay espacio suficiente en el destino. Se necesita espacio para el juego y sus recursos.')
        existed = destination.exists()
        if not existed: destination.mkdir()
        # Reserve an existing empty destination, then recheck it before writing.
        reservation = destination/'.maxspin-installing'
        try:
            with reservation.open('x') as stream: stream.write('Max Spin — djultra64\n')
        except BaseException:
            if not existed and destination.exists() and not any(destination.iterdir()): destination.rmdir()
            raise
        # This is still an unowned folder until the reservation check succeeds.
        # Another process may have placed documents here after validation; only
        # our reservation can be removed when that happens.
        try:
            if set(destination.iterdir()) != {reservation}:
                raise ValueError('La carpeta de destino cambió. Elige una carpeta vacía.')
        except BaseException:
            reservation.unlink(missing_ok=True)
            if not existed and destination.exists() and not any(destination.iterdir()):
                destination.rmdir()
            raise
        try:
            total = sum(src.stat().st_size for src, _, _ in files)
            done = 0
            for source, relative, checksum in files:
                _cancel(cancelled)
                target = destination/relative
                target.parent.mkdir(parents=True, exist_ok=True)
                copied_hash = hashlib.sha256()
                with source.open('rb') as incoming, target.open('xb') as outgoing:
                    for block in iter(lambda: incoming.read(1024*1024), b''):
                        _cancel(cancelled)
                        outgoing.write(block); done += len(block)
                        copied_hash.update(block)
                        report('copy', done, total, 'Copiando Max Spin…')
                if copied_hash.hexdigest() != checksum:
                    raise ValueError('El paquete cambió o está dañado: '+str(relative))
                shutil.copymode(source, target)
            with (destination/'RELEASE_MANIFEST.json').open('xb') as stream:
                stream.write(manifest_bytes)
            disc.extract(destination/'assets',
                         progress=lambda done, total, path: report('extract', done, total, str(path)),
                         cancelled=cancelled)
            _cancel(cancelled)
            _state_directories(destination)
            report('prepare', 0, 0, 'Preparando los recursos del juego…')
            _verify_copied(destination, files, cancelled)
            prepare_resources(destination, platform, progress=report, cancelled=cancelled)
            _relocatable_layout(destination)
            _verify_copied(destination, files, cancelled)
            _cancel(cancelled)
            with (destination/MARKER).open('x', encoding='utf-8') as stream:
                stream.write(json.dumps({'schema':1, 'platform':platform, 'author':'djultra64'})+'\n')
            reservation.unlink()
            report('complete', 1, 1, 'Max Spin está listo para jugar.')
            return destination
        except BaseException:
            _remove_owned(destination)
            if existed: destination.mkdir()
            raise


def _state_directories(root):
    for relative in ('Saves', 'Settings', 'Logs', 'Cache/Temp', 'Cache/Sessions', 'Cache/Shaders',
                     'Cache/Config', 'Cache/Data', 'Cache/LocalAppData', 'native-linux/cache'):
        path = root/relative
        _plain_parents(path)
        path.mkdir(parents=True, exist_ok=True)


def launch_command(root, platform=None):
    root = Path(root).resolve()
    manifest = json.loads((root/MARKER).read_text(encoding='utf-8'))
    if manifest.get('schema') != 1: raise ValueError('La instalación está incompleta.')
    platform = platform or manifest['platform']
    _state_directories(root)
    env = os.environ.copy()
    env.update({'MAX_SPIN_PORTABLE_ROOT':str(root), 'PYTHONDONTWRITEBYTECODE':'1',
                'TOPSPIN_GRAPHICS_INI':str(root/'Settings/graphics.ini'),
                'TOPSPIN_PERSISTENT_WINDOW':'1', 'RECOMP_SHADER_CACHE':str(root/'Cache/Shaders'),
                'TMPDIR':str(root/'Cache/Temp'), 'TEMP':str(root/'Cache/Temp'), 'TMP':str(root/'Cache/Temp'),
                'XDG_CACHE_HOME':str(root/'Cache'), 'XDG_CONFIG_HOME':str(root/'Cache/Config'),
                'XDG_DATA_HOME':str(root/'Cache/Data'), 'LOCALAPPDATA':str(root/'Cache/LocalAppData'),
                'APPDATA':str(root/'Cache/Config'), 'MESA_SHADER_CACHE_DIR':str(root/'Cache/Shaders'),
                '__GL_SHADER_DISK_CACHE_PATH':str(root/'Cache/Shaders'), 'RECOMP_RUNTIME_QUIET':'1',
                'RECOMP_PAD_HOST':'1', 'RECOMP_PB_EXEC':'1', 'RECOMP_PB_D3D11':'1', 'RECOMP_VBLANK':'1',
                'RECOMP_USB':'1', 'RECOMP_TITLE_KEVENTS':'1', 'RECOMP_CONTIG_RECLAIM':'1',
                'RECOMP_GUEST_TARGETS':'1', 'RECOMP_NO_FENCE_CATCHUP':'1', 'RECOMP_NO_FENCE_FOLLOW':'1',
                'RECOMP_WATCHDOG_SECS':'0'})
    for key in tuple(env):
        if key.startswith('TOPSPIN_VISUAL_') or key in ('RECOMP_HEADLESS','RECOMP_PAD_SCRIPT','RECOMP_PAD_PRESS','RECOMP_INPUT_VIRTUAL_PADS'):
            env.pop(key)
    if platform == 'windows':
        env['MAX_SPIN_PYTHON'] = str(root/'portable-runtime/MaxSpin-Setup.exe')
        command = [str(root/'MaxSpin.exe'), '--assets', str(root/'assets'), '--resources', str(root/'native-linux/cache'),
                   '--saves', str(root/'Saves'), '--graphics', str(root/'Settings/graphics.ini'), '--characters', str(root/'characters')]
    elif platform == 'linux':
        command = [str(root/'portable-runtime/MaxSpin-Setup'), '--tool', str(root/'native-linux/launch.py'),
                   'menu', '--build-dir', str(root/'bin'), '--assets', str(root/'assets'), '--saves', str(root/'Saves'),
                   '--characters', str(root/'characters')]
        env['LD_LIBRARY_PATH'] = str(root/'bin')
    else: raise ValueError('Plataforma no compatible.')
    return command, env, root
