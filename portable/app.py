"""Portable Max Spin entry point, shared by Linux and Windows. djultra64."""
from __future__ import annotations

import os
from pathlib import Path
import runpy
import subprocess
import sys

# Helper imports must never create bytecode beside original or extracted files.
sys.dont_write_bytecode = True

TOOLS = frozenset({
    'native-linux/tools/setup_game.py',
    'native-linux/tools/character_session.py',
    'native-linux/launch.py',
})


def distribution_root() -> Path:
    """Resolve against the executable, so an installed folder can be moved."""
    if getattr(sys, 'frozen', False):
        return Path(sys.executable).resolve().parents[1]
    return Path(__file__).resolve().parents[1]


def restore_standard_streams() -> None:
    """Windowed PyInstaller omits streams even when a parent supplies pipes."""
    for name, descriptor, handle_id in (('stdout', 1, -11), ('stderr', 2, -12)):
        if getattr(sys, name) is not None:
            continue
        if os.name == 'nt':
            # A windowed bootloader has no CRT descriptors. _dup(1) can invoke
            # the Windows invalid-parameter dialog instead of raising OSError.
            # Duplicate the parent's Win32 pipe directly, without probing CRT.
            import ctypes
            from ctypes import wintypes
            import msvcrt
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.GetStdHandle.argtypes = [wintypes.DWORD]
            kernel.GetStdHandle.restype = wintypes.HANDLE
            kernel.GetCurrentProcess.restype = wintypes.HANDLE
            kernel.DuplicateHandle.argtypes = [wintypes.HANDLE,wintypes.HANDLE,wintypes.HANDLE,
                                               ctypes.POINTER(wintypes.HANDLE),wintypes.DWORD,wintypes.BOOL,wintypes.DWORD]
            handle = kernel.GetStdHandle(handle_id & 0xffffffff)
            if handle in (None, 0, ctypes.c_void_p(-1).value):
                continue
            process = kernel.GetCurrentProcess()
            copied = wintypes.HANDLE()
            if not kernel.DuplicateHandle(process,handle,process,ctypes.byref(copied),0,False,2):
                continue
            try:
                duplicate = msvcrt.open_osfhandle(copied.value, os.O_WRONLY | os.O_BINARY)
            except OSError:
                kernel.CloseHandle(copied)
                continue
        else:
            try: duplicate = os.dup(descriptor)
            except OSError: continue
        setattr(sys, name, os.fdopen(duplicate, 'w', encoding='utf-8', errors='replace', buffering=1))


def run_tool(root: Path, script: str, arguments: list[str]) -> int:
    """Run only the shipped preparation/launch scripts, without loading Qt."""
    root = root.resolve()
    path = Path(script)
    path = (root / path).resolve() if not path.is_absolute() else path.resolve()
    try:
        relative = path.relative_to(root).as_posix()
    except ValueError:
        relative = ''
    if relative not in TOOLS or not path.is_file():
        raise ValueError('Programa auxiliar no permitido: ' + script)
    original_arguments, original_path = sys.argv[:], sys.path[:]
    native_libraries = relative == 'native-linux/launch.py' and (root/'portable-install.json').is_file()
    previous_libraries = os.environ.get('LD_LIBRARY_PATH')
    try:
        if native_libraries:
            # The onedir bootloader prepends its Qt/Python directory again
            # when this executable is entered as --tool. Native workers need
            # the installation's game libraries rather than Qt's copies.
            os.environ['LD_LIBRARY_PATH'] = str(root/'bin')
        sys.argv = [str(path), *arguments]
        sys.path[:0] = [str(path.parent), str(root), str(root / 'native-linux/tools')]
        try:
            runpy.run_path(str(path), run_name='__main__')
        except SystemExit as result:
            return result.code if isinstance(result.code, int) else (0 if result.code is None else 1)
    finally:
        if native_libraries:
            if previous_libraries is None: os.environ.pop('LD_LIBRARY_PATH', None)
            else: os.environ['LD_LIBRARY_PATH'] = previous_libraries
        sys.argv = original_arguments
        sys.path[:] = original_path
    return 0


def launch(root: Path) -> None:
    from portable.install import launch_command
    command, environment, working_directory = launch_command(root)
    environment = dict(environment)
    # The installation engine has already selected portable native libraries.
    # Windows also inherits PyInstaller's process-level DLL search directory.
    if os.name == 'nt' and getattr(sys, 'frozen', False):
        import ctypes
        ctypes.windll.kernel32.SetDllDirectoryW(None)
    subprocess.Popen(command, env=environment, cwd=working_directory)


def main(arguments: list[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if arguments is None else arguments)
    root = distribution_root()
    if str(root) not in sys.path:
        sys.path.insert(0, str(root))
    # Internal acceptance paths exercise the shipped executable; end users use
    # the wizard. Neither path starts a game session.
    if arguments == ['--self-check-gui']:
        from PySide6.QtCore import QTimer
        from PySide6.QtWidgets import QApplication
        from portable.ui import InstallWindow
        application = QApplication.instance() or QApplication(['Max Spin'])
        window = InstallWindow(root, launcher=launch)
        window.show()
        QTimer.singleShot(100, window.close)
        QTimer.singleShot(150, application.quit)
        return application.exec()
    if len(arguments) == 3 and arguments[0] == '--install':
        restore_standard_streams()
        from portable.install import install
        try:
            install(Path(arguments[1]), Path(arguments[2]), root)
            return 0
        except (OSError, ValueError, InterruptedError) as error:
            if sys.stderr is not None: print(str(error), file=sys.stderr)
            return 1
    if arguments and arguments[0] in ('--tool', '--self-check'):
        restore_standard_streams()
        try:
            if arguments == ['--self-check']:
                if sys.stdout is not None:
                    print('Max Spin portable runtime OK')
                return 0
            if arguments[0] == '--tool' and len(arguments) >= 2:
                return run_tool(root, arguments[1], arguments[2:])
            raise ValueError('Argumentos de programa auxiliar incorrectos.')
        except (OSError, ValueError) as error:
            if sys.stderr is not None:
                print(str(error), file=sys.stderr)
            return 1
    from portable.ui import run
    if arguments == ['--setup']:
        return run(root, launch)
    if arguments and arguments != ['--play']:
        return run(root, launch, initial_error='Argumentos incorrectos. Abre Max Spin sin opciones.')
    if arguments == ['--play'] or (root / 'portable-install.json').is_file():
        try:
            launch(root)
            return 0
        except (OSError, ValueError) as error:
            return run(root, launch, initial_error='No se pudo iniciar Max Spin: ' + str(error))
    return run(root, launch)


if __name__ == '__main__':
    raise SystemExit(main())
