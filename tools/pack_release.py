#!/usr/bin/env python3
"""Package Max Spin native builds and portable runtime relinking materials.

Own work: djultra64. Inputs are explicit local artifacts; no network access or
machine control. Raw build metadata and original game files are never copied.
"""
import argparse
from collections import deque
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

IMAGES = ('boot', 'menu', 'game', 'career', 'training', 'playercreator', 'shop', 'online', 'stop')
SYSTEM_DLLS = frozenset('''kernel32.dll kernelbase.dll ntdll.dll user32.dll gdi32.dll
 gdi32full.dll advapi32.dll shell32.dll shlwapi.dll ole32.dll oleaut32.dll uuid.dll
 comdlg32.dll comctl32.dll ws2_32.dll winmm.dll imm32.dll version.dll setupapi.dll
 hid.dll cfgmgr32.dll bcrypt.dll crypt32.dll secur32.dll iphlpapi.dll dnsapi.dll
 dwmapi.dll dinput8.dll dxgi.dll d3d11.dll d3d12.dll dxguid.dll dwrite.dll
 d2d1.dll uxtheme.dll powrprof.dll msimg32.dll opengl32.dll glu32.dll avrt.dll
 propsys.dll wintrust.dll normaliz.dll rpcrt4.dll winhttp.dll wininet.dll
 urlmon.dll psapi.dll dbghelp.dll msvcrt.dll ucrtbase.dll winspool.drv
 shcore.dll wtsapi32.dll dxva2.dll mf.dll mfplat.dll mfreadwrite.dll mfuuid.dll
 mmdevapi.dll ksuser.dll msacm32.dll oleacc.dll vfw32.dll userenv.dll
 combase.dll dhcpcsvc.dll netapi32.dll wldap32.dll ncrypt.dll usp10.dll
 authz.dll win32u.dll cabinet.dll xinput1_4.dll xinput9_1_0.dll xaudio2_8.dll xaudio2_9.dll'''.split())
SOURCE_EXCLUDE = frozenset(('.git', '.venv', '__pycache__', 'assets', 'build', 'bin', 'out',
                            'reports', 'analysis', 'cache', 'logs', 'saves', 'manual-builds',
                            'research', 'deps', 'sdk', 'xdk', 'proposed', 'save-backups'))
PRIVATE_DIRECTORY_NAMES = frozenset(('userdata', 'udata', 'titledata', 'tdata', 'cmakefiles'))
BUILD_METADATA_NAMES = frozenset(('build.ninja', 'rules.ninja', 'compile_commands.json', 'cmakecache.txt'))
FORBIDDEN_SUFFIXES = frozenset(('.xbe', '.iso', '.xpr', '.abc', '.rui', '.ruirr', '.pml',
                                '.log', '.pyc', '.exe', '.dll', '.o', '.obj', '.a', '.lib', '.rsp', '.pdb'))
ARTIFACT_SUFFIXES = ('.o', '.obj', '.a', '.lib', '.def', '.res')


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def normalized(value):
    return str(value).replace('\\', '/').rstrip('/')


def basename(value):
    return normalized(value).split('/')[-1]


def absolute(value):
    return str(value).startswith('/') or bool(re.match(r'^[A-Za-z]:[/\\]', str(value)))


def pe_imports(path):
    """Read normal and delay imports from a PE32/PE32+ file without executing it."""
    data = path.read_bytes()
    def unpack(fmt, at):
        size = struct.calcsize(fmt)
        if at < 0 or at + size > len(data):
            raise ValueError('truncated PE: ' + path.name)
        return struct.unpack_from(fmt, data, at)
    if data[:2] != b'MZ':
        raise ValueError('not a PE file: ' + path.name)
    pe, = unpack('<I', 0x3c)
    if data[pe:pe+4] != b'PE\0\0':
        raise ValueError('missing PE signature: ' + path.name)
    sections, = unpack('<H', pe + 6)
    optional_size, = unpack('<H', pe + 20)
    optional = pe + 24
    magic, = unpack('<H', optional)
    if magic not in (0x10b, 0x20b):
        raise ValueError('unsupported PE optional header')
    dd = optional + (96 if magic == 0x10b else 112)
    directory_count, = unpack('<I', dd - 4)
    image_base, = unpack('<I' if magic == 0x10b else '<Q', optional + (28 if magic == 0x10b else 24))
    if sections > 96 or optional_size < dd - optional:
        raise ValueError('invalid PE section/header bounds')
    section_start = optional + optional_size
    table = []
    for index in range(sections):
        start = section_start + 40 * index
        virtual_size, rva, raw_size, raw = unpack('<4I', start + 8)
        table.append((rva, max(virtual_size, raw_size), raw, raw_size))
    def offset(rva):
        for start, size, raw, raw_size in table:
            if start <= rva < start + size:
                delta = rva - start
                if delta >= raw_size or raw + delta >= len(data):
                    break
                return raw + delta
        if 0 <= rva < section_start:
            return rva
        raise ValueError('unmapped PE RVA in ' + path.name)
    def dll_name(rva):
        start = offset(rva)
        end = data.find(b'\0', start, min(len(data), start + 260))
        if end < 0:
            raise ValueError('unterminated PE import name')
        name = data[start:end].decode('ascii')
        if not re.fullmatch(r'[A-Za-z0-9_.+-]+\.(?:dll|drv)', name, re.I):
            raise ValueError('invalid PE dependency name: ' + name)
        return name
    result = set()
    for index, width in ((1, 20), (13, 32)):
        if directory_count <= index or dd + 8 * (index + 1) > optional + optional_size:
            continue
        rva, size = unpack('<2I', dd + 8 * index)
        if not rva:
            continue
        if size > 1024 * 1024:
            raise ValueError('unbounded PE import directory')
        start = offset(rva)
        for count in range(min(4096, max(1, size // width + 1))):
            fields = unpack('<' + 'I' * (width // 4), start + count * width)
            if not any(fields):
                break
            name_rva = fields[3] if index == 1 else fields[1]
            if index == 13 and not fields[0] & 1:
                name_rva -= image_base
            result.add(dll_name(name_rva))
        else:
            raise ValueError('unterminated PE import directory')
    return sorted(result, key=str.casefold)


class Paths:
    def __init__(self, maps):
        self.maps = sorted([(normalized(a), Path(b).resolve()) for a, b in maps],
                           key=lambda pair: len(pair[0]), reverse=True)

    def resolve(self, value, directory):
        clean = normalized(value)
        for prefix, local in self.maps:
            if clean.casefold() == prefix.casefold():
                return local
            if clean.casefold().startswith(prefix.casefold() + '/'):
                return local / clean[len(prefix)+1:]
        if re.match(r'^[A-Za-z]:/', clean):
            raise ValueError('unmapped Windows input path: ' + clean)
        path = Path(clean)
        if path.is_absolute():
            return path
        return self.resolve(directory, '.') / path if absolute(directory) else Path(directory).resolve() / path


def split_response(text):
    # Clang/GNU response syntax: backslashes before normal path characters are
    # literal; only quote/backslash escaping inside a quoted token is consumed.
    tokens, current, quote, i = [], [], None, 0
    while i < len(text):
        char = text[i]
        if char in ('"', "'"):
            if quote == char:
                quote = None
            elif quote is None:
                quote = char
            else:
                current.append(char)
        elif char == '\\' and i + 1 < len(text) and text[i+1] in ('"', '\\'):
            i += 1
            current.append(text[i])
        elif char.isspace() and quote is None:
            if current:
                tokens.append(''.join(current)); current = []
        else:
            current.append(char)
        i += 1
    if quote:
        raise ValueError('unterminated response-file quote')
    if current:
        tokens.append(''.join(current))
    return tokens


def expand_responses(arguments, directory, paths, depth=0):
    if depth > 4:
        raise ValueError('recursive response files')
    result = []
    for argument in arguments:
        if argument.startswith('@'):
            path = paths.resolve(argument[1:], directory)
            if path.stat().st_size > 16 * 1024 * 1024:
                raise ValueError('oversized response file')
            result.extend(expand_responses(split_response(path.read_text()), directory, paths, depth+1))
        else:
            result.append(argument)
    return result


def copy_source(source, destination):
    """Copy only already-curated public source, never follow symlinks."""
    for path in sorted(source.rglob('*')):
        relative = path.relative_to(source)
        # Licensed controller artwork is the only assets subtree admitted.
        licensed_art = relative.parts[:3] == ('native-linux', 'assets', 'controller-prompts-xelu')
        excluded = (relative.parts[:3] == ('native-linux', 'src', 'recomp') or
                    any(part in SOURCE_EXCLUDE or part.casefold() in PRIVATE_DIRECTORY_NAMES
                        or part.startswith('build-') for part in relative.parts))
        if excluded and not licensed_art:
            continue
        if path.is_symlink():
            raise ValueError('symlink in source allowlist: ' + str(relative))
        if (not path.is_file() or path.suffix.lower() in FORBIDDEN_SUFFIXES
                or path.name.casefold() in BUILD_METADATA_NAMES):
            continue
        with path.open('rb') as stream:
            magic = stream.read(4)
        if magic == b'\x7fELF' or magic[:2] == b'MZ':
            raise ValueError('unexpected binary in corresponding source: ' + str(relative))
        output = destination / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, output)


RELINK_RUNNER = '''#!/usr/bin/env python3
"""Relink the selected Max Spin module with optional rebuilt runtime libraries."""
import argparse,json,subprocess
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('target')
parser.add_argument('--compiler',help='matching native C compiler; default recorded compiler basename')
parser.add_argument('--runtime-build',type=Path,help='CMake build containing replacement runtime archives')
args=parser.parse_args()
root=Path(__file__).resolve().parent
manifest=json.loads((root/'targets.json').read_text())
record=manifest['targets'].get(args.target)
if record is None:parser.error('unknown target; choices: '+', '.join(manifest['targets']))
command=[args.compiler or record['compiler']]+record['arguments']
if args.runtime_build:
 for index,value in enumerate(command):
  artifact=manifest['artifacts'].get(value)
  if artifact and artifact['runtime_archive']:
   matches=[p for p in args.runtime_build.rglob(artifact['name'])if p.is_file()]
   if len(matches)!=1:parser.error('expected exactly one rebuilt '+artifact['name'])
   command[index]=str(matches[0].resolve())
(root/record['output']).parent.mkdir(parents=True,exist_ok=True)
subprocess.run(command,cwd=root,check=True)
'''


class Relink:
    def __init__(self, root, paths, prefixes, strip_tool=None):
        self.root, self.paths, self.prefixes, self.strip_tool = root, paths, prefixes, strip_tool
        self.artifacts = {}

    def artifact(self, value, directory):
        path = self.paths.resolve(value, directory)
        if not path.is_file():
            raise ValueError('missing relink input: ' + str(path))
        name = path.name
        if not name.lower().endswith(ARTIFACT_SUFFIXES):
            raise ValueError('unsupported relink input: ' + name)
        identity = sha(path)
        relative = 'artifacts/' + identity[:20] + '/' + name
        if relative not in self.artifacts:
            output = self.root / relative
            output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, output)
            if self.strip_tool and name.lower().endswith(('.o', '.obj', '.a', '.lib')):
                subprocess.run([self.strip_tool, '--strip-debug', str(output)], check=True)
            ensure_no_private_paths(output, self.prefixes)
            self.artifacts[relative] = dict(name=name, sha256=sha(output),
                runtime_archive=bool(re.fullmatch(r'(?:lib)?(?:xbox_[A-Za-z0-9_]+|platform)\.(?:a|lib)', name)),
                debug_stripped=bool(self.strip_tool))
        return relative

    def command(self, record, platform):
        directory = record.get('directory', '.')
        original = record['arguments']
        if not original or not isinstance(original, list) or any(not isinstance(a, str) for a in original):
            raise ValueError('link commands require argument arrays')
        arguments = expand_responses(original, directory, self.paths)
        compiler = basename(arguments.pop(0))
        if not re.fullmatch(r'[A-Za-z0-9_.+-]+', compiler):
            raise ValueError('invalid compiler name')
        output, result, index = None, [], 0
        while index < len(arguments):
            value = arguments[index]
            if value in ('-o', '/OUT:'):
                index += 1
                if index >= len(arguments):
                    raise ValueError('missing linker output')
                output = basename(arguments[index])
                result += ['-o', '../' + ('' if platform == 'windows' else 'bin/') + output]
            elif value.startswith('/OUT:'):
                output = basename(value[5:]); result.append('/OUT:../' + output)
            elif value in ('-L', '-F'):
                index += 1  # Installed toolchain must supply external search paths.
                if index >= len(arguments):
                    raise ValueError('missing library search path')
            elif value.startswith(('-L', '-F')):
                pass
            elif value.startswith(('-Wl,--dependency-file=', '-Wl,--dependency-file,')):
                pass  # CMake link dependency output is build metadata, not a relink input.
            elif value.startswith(('-ffile-prefix-map=', '-fdebug-prefix-map=')):
                pass  # Compile-time flags add no relinking behavior.
            elif value.startswith('-Wl,--out-implib,'):
                result.append('-Wl,--out-implib,outputs/' + basename(value.split(',', 2)[2]))
            elif value.startswith('-Wl,-rpath,') or value.startswith('-Wl,-rpath='):
                result.append('-Wl,-rpath,$ORIGIN')
            elif value.lower().endswith(ARTIFACT_SUFFIXES) and not value.startswith('-'):
                result.append(self.artifact(value, directory))
            elif re.search(r'\.so(?:\.[0-9]+)*$', value) and not value.startswith('-'):
                result.append('-l:' + basename(value))
            elif absolute(value):
                raise ValueError('unhandled absolute linker argument: ' + value)
            else:
                result.append(value)
            index += 1
        if output is None:
            raise ValueError('link command has no output')
        for value in result:
            ensure_public_text(value, self.prefixes)
        return output, dict(compiler=compiler, arguments=result,
                           output='../' + ('' if platform == 'windows' else 'bin/') + output)


def ensure_public_text(text, prefixes):
    clean = normalized(text).casefold()
    for prefix in prefixes:
        if prefix and normalized(prefix).casefold() in clean:
            raise ValueError('private build/source prefix remains in packaged material')
    if re.search(r'(?:/home/|/var/home/|[A-Za-z]:[/\\]Users[/\\])', text, re.I):
        raise ValueError('personal absolute path remains in packaged material')


def ensure_no_private_paths(path, prefixes):
    data = path.read_bytes()
    for prefix in prefixes:
        for text in {str(prefix), normalized(prefix), str(prefix).replace('/', '\\')}:
            if text and (text.encode() in data or text.encode('utf-16le') in data):
                raise ValueError('private path embedded in ' + path.name + '; rebuild with file/debug prefix maps or use --strip-tool for debug records')


def collect_dlls(executables, search, destination, additional_system=()):
    index = {}
    for directory in search:
        for path in directory.rglob('*'):
            if path.is_file() and path.suffix.lower() == '.dll':
                index.setdefault(path.name.casefold(), []).append(path)
    system = SYSTEM_DLLS | frozenset(name.casefold() for name in additional_system)
    queue, seen, copied = deque(executables), set(), {}
    while queue:
        path = queue.popleft()
        for name in pe_imports(path):
            key = name.casefold()
            if key in system or key.startswith(('api-ms-win-', 'ext-ms-win-')) or key in seen:
                continue
            seen.add(key)
            matches = index.get(key, [])
            if not matches:
                raise ValueError('missing non-system DLL: ' + name)
            if len({sha(item) for item in matches}) != 1:
                raise ValueError('ambiguous DLL versions: ' + name)
            source = matches[0]
            target = destination / source.name
            shutil.copy2(source, target)
            copied[source.name] = dict(sha256=sha(target), imports=pe_imports(target))
            queue.append(target)
    return copied


def load_feature_parity(source):
    """Refuse releases whose declared user features differ by platform."""
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('duplicate feature declaration')
            result[key] = value
        return result
    try:
        with (Path(source) / 'FEATURES.json').open('rb') as stream:
            data = stream.read(65537)
        if len(data) > 65536:
            raise ValueError('oversized declaration')
        declaration = json.loads(data, object_pairs_hook=unique)
        if (not isinstance(declaration, dict)
                or set(declaration) != {'schema', 'platforms'}
                or declaration['schema'] != 'maxspin-feature-parity-v1'):
            raise ValueError('unsupported declaration')
        platforms = declaration['platforms']
        if not isinstance(platforms, dict) or set(platforms) != {'linux', 'windows'}:
            raise ValueError('both supported platforms required')
        for features in platforms.values():
            if not isinstance(features, dict) or not features or len(features) > 128:
                raise ValueError('nonempty feature set required')
            for name, revision in features.items():
                if (not re.fullmatch(r'[a-z][a-z0-9-]{0,63}', name)
                        or type(revision) is not int or not 1 <= revision <= 2147483647):
                    raise ValueError('invalid feature or revision')
        if platforms['linux'] != platforms['windows']:
            raise ValueError('Linux and Windows features/revisions must match')
        return dict(platforms['linux'])
    except (OSError, ValueError, RecursionError) as error:
        raise ValueError('feature parity: ' + str(error)) from error


def package(args):
    source, build = args.source_root.resolve(), args.build_root.resolve()
    output = args.output.absolute()
    if output.exists() or output.is_symlink():
        raise ValueError('package output must be new')
    if output == source or source in output.parents:
        raise ValueError('package output must be outside the source tree')
    features = load_feature_parity(source)
    specification = json.loads(args.link_commands.read_text())
    commands = specification.get('commands') if isinstance(specification, dict) else specification
    if not isinstance(commands, list):
        raise ValueError('expected a commands array')
    paths = Paths(args.path_map)
    prefixes = [str(source), str(build), *(prefix for prefix, _ in args.path_map), *args.private_prefix]
    binaries = []
    extension = '.exe' if args.platform == 'windows' else ''
    required = ['topspin_' + name + extension for name in IMAGES]
    required.append('MaxSpin.exe' if args.platform == 'windows' else 'topspin_window_host')
    for name in required:
        path = build / name
        if not path.is_file() or path.is_symlink():
            raise ValueError('missing native executable: ' + name)
        magic = path.read_bytes()[:4]
        if (args.platform == 'windows' and magic[:2] != b'MZ') or (args.platform == 'linux' and magic != b'\x7fELF'):
            raise ValueError('native executable format mismatch: ' + name)
        binaries.append(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix='.max-spin-package-', dir=output.parent))
    try:
        copy_source(source, temporary)
        for required_file in ('LICENSE', 'THIRD_PARTY.md', 'AI_DISCLOSURE.md',
                              'vendor/xboxrecomp/NOTICE', 'vendor/xboxrecomp/LICENSE',
                              'vendor/xboxrecomp/LICENSES/LGPL-2.1.txt'):
            if not (temporary / required_file).is_file():
                raise ValueError('missing source/license file: ' + required_file)
        bin_dir = temporary if args.platform == 'windows' else temporary / 'bin'
        bin_dir.mkdir(parents=True, exist_ok=True)
        for path in binaries:
            target = bin_dir / path.name
            shutil.copy2(path, target)
            if args.strip_tool:
                subprocess.run([args.strip_tool, '--strip-debug', str(target)], check=True)
            ensure_no_private_paths(target, prefixes)
        dlls = collect_dlls(binaries, [build, *args.dll_dir], bin_dir, args.system_dll) if args.platform == 'windows' else {}
        for name in dlls:
            ensure_no_private_paths(bin_dir / name, prefixes)
        relink = temporary / 'relink'
        relink.mkdir()
        (relink / 'outputs').mkdir()
        collector = Relink(relink, paths, prefixes, args.strip_tool)
        targets = {}
        for record in commands:
            name, prepared = collector.command(record, args.platform)
            if name in required:
                if name in targets:
                    raise ValueError('duplicate relink command for ' + name)
                targets[name] = prepared
        if set(targets) != set(required):
            raise ValueError('missing relink commands: ' + ', '.join(sorted(set(required) - set(targets))))
        toolchain = specification.get('toolchain', {}) if isinstance(specification, dict) else {}
        # Clang --version reports an installation directory that is not part
        # of the toolchain identity or portable reproduction instructions.
        if isinstance(toolchain, dict):
            toolchain = {key: re.sub(r'(?m)^InstalledDir:.*(?:\n|$)', '', value).strip()
                         if isinstance(value, str) else value for key, value in toolchain.items()}
        ensure_public_text(json.dumps(toolchain), prefixes)
        (relink / 'targets.json').write_text(json.dumps(dict(version=1, platform=args.platform,
            toolchain=toolchain, targets=targets, artifacts=collector.artifacts), indent=2) + '\n')
        (relink / 'relink.py').write_text(RELINK_RUNNER)
        licenses = temporary / 'licenses/runtime-dependencies'
        for path in args.extra_license:
            if not path.is_file() or path.is_symlink():
                raise ValueError('extra license must be a plain file')
            licenses.mkdir(parents=True, exist_ok=True)
            target = licenses / sha(path)[:20] / path.name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
        for bundle in args.dependency_bundle:
            if not bundle.is_dir() or bundle.is_symlink():
                raise ValueError('dependency bundle must be a plain directory')
            destination = temporary / 'third-party-sources' / bundle.name
            if destination.exists():
                raise ValueError('duplicate dependency bundle name')
            destination.mkdir(parents=True)
            for path in sorted(bundle.rglob('*')):
                relative = path.relative_to(bundle)
                if path.is_symlink() or '.git' in relative.parts:
                    raise ValueError('symlink or history in dependency bundle')
                if not path.is_file():
                    continue
                if path.suffix.lower() in ('.xbe', '.iso', '.xpr', '.abc', '.rui', '.ruirr', '.pml', '.exe', '.dll', '.rsp', '.pdb'):
                    raise ValueError('unexpected payload in dependency source bundle')
                with path.open('rb') as stream:
                    magic = stream.read(4)
                if magic == b'\x7fELF' or magic[:2] == b'MZ':
                    raise ValueError('native binary in dependency source bundle')
                ensure_no_private_paths(path, prefixes)
                target = destination / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
        (temporary / 'RELINKING.md').write_text(relink_instructions(args.platform))
        files = {str(p.relative_to(temporary)): sha(p) for p in sorted(temporary.rglob('*')) if p.is_file()}
        (temporary / 'RELEASE_MANIFEST.json').write_text(json.dumps(dict(
            version=1, project='Max Spin', author='djultra64', platform=args.platform,
            files=files, runtime_dlls=dlls, source_included=True, features=features,
            limits=['Requires original game files supplied by the user.',
                    'Native game workers contain locally generated translations; project MIT does not license original game rights.',
                    'Packaging is not gameplay or platform validation.']), indent=2) + '\n')
        # Reserve output exclusively; a concurrent existing directory is never replaced.
        output.mkdir()
        try:
            shutil.copytree(temporary, output, dirs_exist_ok=True)
        except BaseException:
            shutil.rmtree(output)
            raise
    finally:
        shutil.rmtree(temporary)
    return len(files)


def relink_instructions(platform):
    configure = ('cmake -S windows/native -B rebuilt-runtime -G Ninja -DCMAKE_C_COMPILER=clang '
                 '-DCMAKE_CXX_COMPILER=clang++ -DTOPSPIN_RUNTIME_ONLY=ON' if platform == 'windows' else
                 'cmake -S native-linux -B rebuilt-runtime -DTOPSPIN_RUNTIME_ONLY=ON')
    example = 'topspin_game.exe' if platform == 'windows' else 'topspin_game'
    return '''# Rebuilding and relinking the runtime

This package includes the corresponding project/runtime source, full retained
license notices, native module objects, runtime archives, and portable linker
arguments. The original game and its raw files are not included. The Max Spin
MIT license does not grant rights to the underlying game code in native workers.

Use the matching native compiler, linker, architecture, and dependency versions
recorded in `relink/targets.json`. Install the required SDL3, Vulkan, shaderc,
and compiler development packages. All system search paths come from your local
toolchain; no private build paths or raw build response files are included.

From the package root, edit the LGPL/runtime sources under `vendor/xboxrecomp`
then rebuild their archives:

```text
''' + configure + '''
cmake --build rebuilt-runtime --parallel
python relink/relink.py ''' + example + ''' --runtime-build rebuilt-runtime
```

Repeat the relink command for each target you use. The helper replaces every
recorded runtime archive with its matching rebuilt basename, while retaining the
module objects and original link ordering. `--compiler` selects an equivalent
native compiler when its executable name differs. Run the command without
`--runtime-build` to reproduce the included archive link. Outputs replace the
package's native executable for that target; back up your package first.

The retained objects allow runtime relinking without distributing generated game
C. To regenerate or modify the game translation, supply your original files and
follow `docs/build.md` or `docs/windows.md`. Preserve the complete save folder
when testing runtime changes. Packaging and relinking materials do not establish
gameplay, save, controller, graphics, or timing compatibility.
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True, help='curated public source only')
    parser.add_argument('--build-root', type=Path, required=True, help='folder with all ten native executables')
    parser.add_argument('--platform', choices=('linux', 'windows'), required=True)
    parser.add_argument('--link-commands', type=Path, required=True, help='private JSON commands with directory and argument arrays')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--path-map', action='append', default=[], type=lambda value: value.split('=', 1), metavar='ORIGINAL=LOCAL')
    parser.add_argument('--dll-dir', action='append', default=[], type=Path)
    parser.add_argument('--system-dll', action='append', default=[], help='explicit extra OS-provided DLL name')
    parser.add_argument('--extra-license', action='append', default=[], type=Path)
    parser.add_argument('--dependency-bundle', action='append', default=[], type=Path,
                        help='reviewed corresponding-source/recipe directory, included with file hashes')
    parser.add_argument('--private-prefix', action='append', default=[])
    parser.add_argument('--strip-tool', help='optional native llvm-strip/strip for copied executable/object debug records')
    args = parser.parse_args()
    if any(len(pair) != 2 or not pair[0] or not pair[1] for pair in args.path_map):
        parser.error('--path-map requires ORIGINAL=LOCAL')
    try:
        count = package(args)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(2, 'Max Spin packaging failed: ' + str(error) + '\n')
    print('Prepared Max Spin package with', count, 'hashed files. Upload and platform acceptance are separate steps.')


if __name__ == '__main__':
    main()
