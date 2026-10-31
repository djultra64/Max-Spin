"""CPU-only portable wizard and frozen helper contract. Own work: djultra64."""
import importlib
import os
from pathlib import Path
import subprocess
import sys
import time

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')


def test_tool_mode_never_loads_qt_and_accepts_only_bundled_helpers(tmp_path):
    app = importlib.import_module('portable.app')
    tools = tmp_path / 'native-linux' / 'tools'
    tools.mkdir(parents=True)
    script = tools / 'character_session.py'
    script.write_text('import sys\nprint("helper:", sys.argv[1])\n')
    result = subprocess.run([sys.executable, str(ROOT / 'portable/app.py'), '--tool', str(script), 'argument'],
                            capture_output=True, text=True, env=os.environ | {'MAXSPIN_TEST_UNUSED': '1'})
    # A similarly named helper outside the actual distribution is forbidden.
    assert result.returncode != 0
    assert 'no permitido' in result.stderr
    assert app.run_tool(tmp_path, str(script), ['argument']) == 0
    with pytest.raises(ValueError, match='no permitido'):
        app.run_tool(tmp_path, '../escape.py', [])


def test_self_check_runs_without_qt_or_writes(tmp_path):
    guard = tmp_path / 'sitecustomize.py'
    guard.write_text('import sys\nclass Guard:\n def find_spec(self, name, *args):\n  if name.startswith("PySide6"): raise RuntimeError("Qt loaded")\nsys.meta_path.insert(0, Guard())\n')
    result = subprocess.run([sys.executable, '-B', str(ROOT / 'portable/app.py'), '--self-check'],
                            capture_output=True, text=True,
                            env=os.environ | {'PYTHONPATH': str(tmp_path), 'HOME': str(tmp_path), 'PYTHONDONTWRITEBYTECODE': '1'})
    assert result.returncode == 0, result.stderr
    assert 'Max Spin' in result.stdout
    assert sorted(p.name for p in tmp_path.iterdir()) == ['sitecustomize.py']


def test_helper_main_bypasses_qt_and_preserves_pipe_output(tmp_path):
    tools = tmp_path / 'native-linux/tools'
    tools.mkdir(parents=True)
    script = tools / 'character_session.py'
    script.write_text('import sys\nprint("manifest-ready:" + sys.argv[1])\n')
    program = '''
import sys
from pathlib import Path
class Guard:
 def find_spec(self, name, *args):
  if name.startswith('PySide6'): raise RuntimeError('Qt imported in helper mode')
sys.meta_path.insert(0, Guard())
from portable import app
app.distribution_root = lambda: Path(sys.argv[1])
raise SystemExit(app.main(['--tool', sys.argv[2], 'portable']))
'''
    result = subprocess.run([sys.executable, '-B', '-c', program, str(tmp_path), str(script)],
                            capture_output=True, text=True, cwd=ROOT,
                            env=os.environ | {'PYTHONPATH': str(ROOT)})
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == 'manifest-ready:portable'
    assert not list(tmp_path.rglob('__pycache__'))


def test_frozen_root_follows_relocated_executable(monkeypatch, tmp_path):
    app = importlib.import_module('portable.app')
    exe = tmp_path / 'relocated' / 'portable-runtime' / 'MaxSpin-Setup.exe'
    monkeypatch.setattr(sys, 'frozen', True, raising=False)
    monkeypatch.setattr(sys, 'executable', str(exe))
    assert app.distribution_root() == exe.resolve().parents[1]


def test_setup_handoff_forces_wizard_even_with_completed_marker(monkeypatch, tmp_path):
    from portable import app, ui
    (tmp_path/'portable-install.json').write_text('{"schema":1,"platform":"windows"}')
    monkeypatch.setattr(app,'distribution_root',lambda:tmp_path)
    launches=[];windows=[]
    monkeypatch.setattr(app,'launch',lambda root:launches.append(root))
    monkeypatch.setattr(ui,'run',lambda *args,**kwargs:windows.append((args,kwargs)) or 0)
    assert app.main(['--setup'])==0
    assert not launches
    assert len(windows)==1
    assert not windows[0][1].get('initial_error'), 'setup handoff is a supported mode, not an argument error'


def test_frozen_linux_helper_restores_native_library_search_path(monkeypatch,tmp_path,capsys):
    from portable import app
    native=tmp_path/'native-linux';native.mkdir()
    script=native/'launch.py'
    script.write_text('import os\nprint(os.environ["LD_LIBRARY_PATH"])\n')
    (tmp_path/'portable-install.json').write_text('{"schema":1,"platform":"linux"}')
    inherited=str(tmp_path/'portable-runtime/_internal')+':'+str(tmp_path/'bin')
    monkeypatch.setenv('LD_LIBRARY_PATH',inherited)
    assert app.run_tool(tmp_path,str(script),[])==0
    assert capsys.readouterr().out.strip()==str(tmp_path/'bin')
    assert os.environ['LD_LIBRARY_PATH']==inherited


def test_play_preserves_portable_engine_library_path(monkeypatch, tmp_path):
    from portable import app, install
    observed = []
    monkeypatch.setattr(sys, 'frozen', True, raising=False)
    environment = {'LD_LIBRARY_PATH': str(tmp_path / 'bin'), 'LD_LIBRARY_PATH_ORIG': '/machine/path'}
    monkeypatch.setattr(install, 'launch_command', lambda root: (['never-run-game'], environment, root))
    monkeypatch.setattr(app.subprocess, 'Popen', lambda command, **kwargs: observed.append(kwargs))
    app.launch(tmp_path)
    assert observed[0]['env']['LD_LIBRARY_PATH'] == str(tmp_path / 'bin')
    assert observed[0]['cwd'] == tmp_path


@pytest.fixture
def qt():
    from PySide6.QtWidgets import QApplication
    from PySide6.QtCore import QCoreApplication, QEvent
    app = QApplication.instance() or QApplication([])
    yield app
    app.processEvents()
    for widget in app.topLevelWidgets():
        widget.close()
        worker = getattr(widget, 'worker', None)
        if worker is not None and worker.isRunning():
            worker.stop_requested.set()
            assert worker.wait(5000), 'installation worker did not stop during cleanup'
        widget.deleteLater()
    QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)


def wait_for(qt, condition):
    deadline = time.monotonic() + 5
    while not condition() and time.monotonic() < deadline:
        qt.processEvents()
        time.sleep(0.005)
    assert condition()


def test_wizard_has_only_iso_then_destination_and_installs_off_ui_thread(qt, tmp_path):
    from portable.ui import InstallWindow
    import threading
    calls = []
    destination = tmp_path / 'portable'
    def installer(iso, target, payload, progress, cancelled):
        calls.append((iso, target, payload, threading.current_thread()))
        progress('extract', 1, 2, 'data.wad')
        return target
    window = InstallWindow(tmp_path, installer=installer, launcher=lambda root: calls.append(root))
    assert window.pages.currentIndex() == 0
    window.iso_edit.setText(str(tmp_path / 'original.iso'))
    window.next_button.click()
    assert window.pages.currentIndex() == 1
    window.destination_edit.setText(str(destination))
    window.install_button.click()
    wait_for(qt, lambda: window.play_button.isEnabled())
    assert calls[0][:3] == (tmp_path / 'original.iso', destination, tmp_path)
    assert calls[0][3] is not threading.current_thread()
    window.play_button.click()
    assert calls[1] == destination
    window.close()


@pytest.mark.parametrize('failure', [ValueError('ISO incompatible'), OSError('Sin espacio')])
def test_error_shows_actionable_retry_without_reentering_paths(qt, tmp_path, failure):
    from portable.ui import InstallWindow
    def installer(*args, **kwargs):
        raise failure
    window = InstallWindow(tmp_path, installer=installer)
    window.iso_edit.setText(str(tmp_path / 'original.iso'))
    window.next_button.click()
    window.destination_edit.setText(str(tmp_path / 'portable'))
    window.install_button.click()
    wait_for(qt, lambda: window.retry_button.isEnabled())
    assert str(failure) in window.status_label.text()
    assert 'ISO' in window.status_label.text() and 'carpeta' in window.status_label.text()
    window.retry_button.click()
    assert window.pages.currentIndex() == 0
    assert window.iso_edit.text().endswith('original.iso')
    assert window.destination_edit.text().endswith('portable')
    window.close()


def test_cancel_requests_cooperative_stop_and_allows_retry(qt, tmp_path):
    from portable.ui import InstallWindow
    def installer(*args, cancelled, **kwargs):
        deadline = time.monotonic() + 3
        while not cancelled() and time.monotonic() < deadline:
            time.sleep(0.005)
        if cancelled():
            raise InterruptedError('cancelled')
        raise AssertionError('cancellation was not delivered')
    window = InstallWindow(tmp_path, installer=installer)
    window.iso_edit.setText(str(tmp_path / 'original.iso'))
    window.next_button.click()
    window.destination_edit.setText(str(tmp_path / 'portable'))
    window.install_button.click()
    window.cancel_button.click()
    wait_for(qt, lambda: window.retry_button.isEnabled())
    assert 'cancelada' in window.status_label.text().lower()
    assert not window.play_button.isEnabled()
    window.close()


def test_progress_supports_full_xbox_disc_byte_counts(qt, tmp_path):
    from portable.ui import InstallWindow
    observed = []
    def installer(iso, target, payload, progress, cancelled):
        progress('extract', 4_000_000_000, 8_000_000_000, 'Disco Xbox')
        return target
    window = InstallWindow(tmp_path, installer=installer)
    window.iso_edit.setText(str(tmp_path / 'original.iso'))
    window.show_destination()
    window.destination_edit.setText(str(tmp_path / 'portable'))
    original = window.update_progress
    def capture(*args):
        original(*args)
        observed.append(window.progress_bar.value())
    window.update_progress = capture
    window.begin_install()
    wait_for(qt, lambda: window.play_button.isEnabled())
    assert observed == [500]
    window.close()


def test_closing_during_install_cancels_and_waits_for_cleanup(qt, tmp_path):
    from portable.ui import InstallWindow
    cleaned = []
    def installer(*args, cancelled, **kwargs):
        while not cancelled():
            time.sleep(0.005)
        cleaned.append(True)
        raise InterruptedError()
    window = InstallWindow(tmp_path, installer=installer)
    window.iso_edit.setText(str(tmp_path / 'original.iso'))
    window.show_destination()
    window.destination_edit.setText(str(tmp_path / 'portable'))
    window.show()
    window.begin_install()
    window.close()
    assert window.isVisible()
    wait_for(qt, lambda: not window.isVisible())
    assert cleaned == [True]


def test_file_picker_selects_iso_without_persisting_history(qt, tmp_path):
    from portable.ui import make_file_dialog
    from PySide6.QtCore import QCoreApplication, QEvent
    from PySide6.QtWidgets import QDialog
    iso = tmp_path / 'private.iso'
    iso.touch()
    before = set(tmp_path.rglob('*'))
    dialog = make_file_dialog(None, destination=False, directory=tmp_path)
    assert isinstance(dialog, QDialog)
    dialog.selection_edit.setText(str(iso))
    dialog.accept()
    assert dialog.selected_path == iso
    dialog.deleteLater()
    QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
    qt.processEvents()
    assert set(tmp_path.rglob('*')) == before


def test_folder_picker_selects_new_destination_without_creating_it(qt, tmp_path):
    from portable.ui import make_file_dialog
    dialog = make_file_dialog(None, destination=True, directory=tmp_path)
    dialog.selection_edit.setText('Mi Max Spin')
    dialog.accept()
    assert dialog.selected_path == tmp_path / 'Mi Max Spin'
    assert not dialog.selected_path.exists()
    dialog.close()


def test_folder_picker_accepts_empty_existing_folder_and_rejects_occupied(qt, tmp_path):
    from portable.ui import make_file_dialog
    empty = tmp_path / 'empty'
    empty.mkdir()
    dialog = make_file_dialog(None, destination=True, directory=tmp_path)
    dialog.selection_edit.setText('empty')
    dialog.accept()
    assert dialog.selected_path == empty
    dialog.close()
    (empty / 'keep.txt').write_text('original')
    other = make_file_dialog(None, destination=True, directory=tmp_path)
    other.selection_edit.setText('empty')
    other.accept()
    assert other.selected_path is None
    assert other.error_label.text()
    assert (empty / 'keep.txt').read_text() == 'original'
    other.close()


def test_dialog_destruction_does_not_write_isolated_home(tmp_path):
    program = '''
from pathlib import Path
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QCoreApplication, QEvent
from portable.ui import make_file_dialog
app = QApplication([])
dialog = make_file_dialog(None, destination=False, directory=Path.cwd())
dialog.selection_edit.setText('/private/disc.iso')
dialog.deleteLater()
QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
app.processEvents()
'''
    environment = os.environ | {'HOME': str(tmp_path), 'XDG_CONFIG_HOME': str(tmp_path / 'config'),
                                'XDG_CACHE_HOME': str(tmp_path / 'cache'), 'PYTHONDONTWRITEBYTECODE': '1',
                                'USERPROFILE': str(tmp_path), 'APPDATA': str(tmp_path / 'appdata'),
                                'LOCALAPPDATA': str(tmp_path / 'localappdata'),
                                'QT_QPA_PLATFORM': 'offscreen', 'PYTHONPATH': str(ROOT)}
    result = subprocess.run([sys.executable, '-B', '-c', program], env=environment,
                            capture_output=True, text=True, cwd=ROOT)
    assert result.returncode == 0, result.stderr
    assert not list(tmp_path.rglob('*'))


def test_cancelled_wizard_exits_native_process_cleanly(tmp_path):
    program = '''
import sys, time
from pathlib import Path
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QTimer
from portable import ui
application = QApplication(['Max Spin'])
original = ui.InstallWindow
def install(*args, cancelled, **kwargs):
 while not cancelled(): time.sleep(0.005)
 raise InterruptedError('cancelled')
class CancelWindow(original):
 def __init__(self, payload, launcher, initial_error=None):
  super().__init__(payload, installer=install, launcher=launcher)
  self.iso_edit.setText(str(payload/'original.iso'))
  self.destination_edit.setText(str(payload/'portable'))
  QTimer.singleShot(0, self.begin_install)
  QTimer.singleShot(20, self.cancel_install)
  self.close_timer = QTimer(self)
  self.close_timer.timeout.connect(self.close_when_cancelled)
  self.close_timer.start(10)
 def close_when_cancelled(self):
  if self.retry_button.isEnabled(): self.close()
ui.InstallWindow = CancelWindow
raise SystemExit(ui.run(Path(sys.argv[1]), lambda root: None))
'''
    result = subprocess.run([sys.executable, '-B', '-c', program, str(tmp_path)],
                            capture_output=True, text=True, cwd=ROOT, timeout=10,
                            env=os.environ | {'QT_QPA_PLATFORM': 'offscreen', 'PYTHONDONTWRITEBYTECODE': '1'})
    assert result.returncode == 0, result.stderr
