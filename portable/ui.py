"""Two-input portable ISO wizard. Own work: djultra64."""
from __future__ import annotations

from pathlib import Path
import threading

from PySide6.QtCore import QCoreApplication, QDir, QEvent, QThread, Qt, Signal, Slot
from PySide6.QtWidgets import (
    QApplication, QComboBox, QDialog, QHBoxLayout, QLabel, QLineEdit,
    QProgressBar, QPushButton, QStackedWidget, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget,
)


class PortablePathDialog(QDialog):
    """Read-only Qt browser with no native dialog or persistent settings.

    QFileDialog always saves history via internal QSettings, including when
    DontUseNativeDialog is set. In Windows that writes to the registry. Building
    the picker from plain widgets avoids that side effect on both platforms.
    QFileSystemModel also initializes Windows shell caches while gathering file
    metadata, even with an icon provider override. Read-only pathlib enumeration
    keeps the browser independent of the shell and its profile writes.
    """
    def __init__(self, parent, *, destination: bool, directory: Path):
        super().__init__(parent)
        self.destination = destination
        self.selected_path = None
        self.directory = directory.resolve()
        if not self.directory.is_dir():
            self.directory = Path.cwd()
        self.setWindowTitle('Elige la carpeta portable' if destination else 'Elige tu ISO original de Xbox')
        self.resize(760, 480)
        layout = QVBoxLayout(self)
        navigation = QHBoxLayout()
        up = QPushButton('Subir')
        up.clicked.connect(lambda: self.navigate(self.directory.parent))
        navigation.addWidget(up)
        self.location_edit = QLineEdit(str(self.directory))
        self.location_edit.setAccessibleName('Ubicación actual')
        self.location_edit.returnPressed.connect(lambda: self.navigate(Path(self.location_edit.text())))
        navigation.addWidget(self.location_edit, 1)
        go = QPushButton('Ir')
        go.clicked.connect(lambda: self.navigate(Path(self.location_edit.text())))
        navigation.addWidget(go)
        layout.addLayout(navigation)
        locations = QComboBox()
        locations.addItem('Ir a una unidad o carpeta…', '')
        locations.addItem('Carpeta personal', str(Path.home()))
        for drive in QDir.drives():
            locations.addItem(drive.absoluteFilePath(), drive.absoluteFilePath())
        locations.activated.connect(lambda index: self.navigate(Path(locations.itemData(index)))
                                    if locations.itemData(index) else None)
        layout.addWidget(locations)
        self.tree = QTreeWidget()
        self.tree.setHeaderLabels(['Nombre', 'Tipo', 'Tamaño'])
        self.tree.setRootIsDecorated(False)
        self.tree.setColumnWidth(0, 390)
        self.tree.setSortingEnabled(True)
        self.tree.sortItems(0, Qt.SortOrder.AscendingOrder)
        self.tree.itemClicked.connect(self.select_item)
        self.tree.itemDoubleClicked.connect(self.open_item)
        layout.addWidget(self.tree, 1)
        layout.addWidget(QLabel('Carpeta portable (nueva o vacía):' if destination else 'ISO de Xbox:'))
        self.selection_edit = QLineEdit('MaxSpin' if destination else '')
        self.selection_edit.returnPressed.connect(self.accept)
        layout.addWidget(self.selection_edit)
        self.error_label = QLabel('')
        self.error_label.setWordWrap(True)
        layout.addWidget(self.error_label)
        buttons = QHBoxLayout()
        buttons.addStretch(1)
        cancel = QPushButton('Cancelar')
        cancel.clicked.connect(self.reject)
        choose = QPushButton('Elegir carpeta' if destination else 'Elegir ISO')
        choose.clicked.connect(self.accept)
        buttons.addWidget(cancel)
        buttons.addWidget(choose)
        layout.addLayout(buttons)
        self.list_directory()

    def list_directory(self):
        self.tree.clear()
        try:
            paths = sorted(self.directory.iterdir(), key=lambda path: path.name.casefold())
            for path in paths:
                is_directory = path.is_dir()
                if not is_directory and (self.destination or path.suffix.lower() != '.iso'):
                    continue
                try:
                    size = '' if is_directory else f'{path.stat().st_size:,} bytes'
                except OSError:
                    size = ''
                item = QTreeWidgetItem([path.name, 'Carpeta' if is_directory else 'Imagen Xbox', size])
                item.setData(0, Qt.ItemDataRole.UserRole, str(path))
                self.tree.addTopLevelItem(item)
        except OSError:
            self.error_label.setText('No se puede leer esa carpeta. Comprueba sus permisos o elige otra.')

    def navigate(self, path):
        path = path.resolve()
        if not path.is_dir():
            self.error_label.setText('No se puede abrir esa carpeta. Comprueba la ruta y los permisos.')
            return
        self.directory = path
        self.location_edit.setText(str(path))
        self.error_label.clear()
        self.list_directory()
        if not self.destination:
            self.selection_edit.clear()

    def select_item(self, item, column):
        path = Path(item.data(0, Qt.ItemDataRole.UserRole))
        if self.destination or path.is_file():
            self.selection_edit.setText(path.name)

    def open_item(self, item, column):
        path = Path(item.data(0, Qt.ItemDataRole.UserRole))
        if path.is_dir():
            self.navigate(path)
            if self.destination:
                self.selection_edit.setText('MaxSpin')
        elif not self.destination:
            self.selection_edit.setText(path.name)
            self.accept()

    def accept(self):
        name = self.selection_edit.text().strip()
        if not name:
            self.error_label.setText('Escribe el nombre de la carpeta.' if self.destination else 'Elige la ISO original de Xbox.')
            return
        path = (self.directory / name).absolute()
        if not self.destination and (not path.is_file() or path.suffix.lower() != '.iso'):
            self.error_label.setText('Selecciona un archivo ISO que exista y se pueda leer.')
            return
        if self.destination:
            try:
                if path.exists() and (not path.is_dir() or any(path.iterdir())):
                    self.error_label.setText('Esa carpeta contiene archivos. Elige una carpeta nueva o vacía.')
                    return
            except OSError:
                self.error_label.setText('No se puede leer esa carpeta. Comprueba sus permisos o elige otra.')
                return
        self.selected_path = path
        super().accept()


def make_file_dialog(parent, *, destination: bool, directory: Path) -> PortablePathDialog:
    return PortablePathDialog(parent, destination=destination, directory=directory)


class InstallThread(QThread):
    # ISO progress counts can exceed Qt's signed 32-bit "int" signal type.
    progress_changed = Signal(str, object, object, str)
    completed = Signal(object)
    failed = Signal(str)
    cancelled = Signal()

    def __init__(self, installer, iso, destination, payload, parent=None):
        super().__init__(parent)
        self.installer = installer
        self.iso, self.destination, self.payload = iso, destination, payload
        self.stop_requested = threading.Event()

    def run(self):
        try:
            root = self.installer(self.iso, self.destination, self.payload,
                                  progress=self.progress_changed.emit,
                                  cancelled=self.stop_requested.is_set)
        except InterruptedError:
            self.cancelled.emit()
        except Exception as error:
            self.failed.emit(str(error))
        else:
            self.completed.emit(root)


class InstallWindow(QWidget):
    def __init__(self, payload: Path, installer=None, launcher=None, initial_error=None):
        super().__init__()
        if installer is None:
            from portable.install import install
            installer = install
        if launcher is None:
            from portable.app import launch
            launcher = launch
        self.payload = Path(payload).resolve()
        self.installer, self.launcher = installer, launcher
        self.worker = None
        self.installed_root = None
        self.close_when_stopped = False
        self.setWindowTitle('Max Spin · Instalación portable')
        self.resize(650, 330)
        outer = QVBoxLayout(self)
        outer.setContentsMargins(28, 24, 28, 24)
        title = QLabel('Max Spin')
        title.setStyleSheet('font-size: 28px; font-weight: bold;')
        outer.addWidget(title)
        outer.addWidget(QLabel('Tu juego, tus archivos y tus partidas en una sola carpeta.'))
        self.pages = QStackedWidget()
        outer.addWidget(self.pages, 1)
        iso_page, iso_layout = self._page('1 · Elige tu ISO original de Xbox')
        iso_layout.addWidget(self._wrapped('Selecciona la imagen ISO de tu disco de Top Spin para Xbox.'))
        self.iso_edit = QLineEdit()
        self.iso_edit.setPlaceholderText('Ruta de la imagen .iso')
        self.iso_edit.setAccessibleName('ISO original de Xbox')
        self._path_row(iso_layout, self.iso_edit, self.choose_iso)
        self.next_button = QPushButton('Continuar')
        self.next_button.clicked.connect(self.show_destination)
        iso_layout.addWidget(self.next_button)
        destination_page, destination_layout = self._page('2 · Elige la carpeta portable')
        destination_layout.addWidget(self._wrapped(
            'Elige una carpeta nueva. Max Spin guardará aquí el juego, las partidas y la configuración.'))
        self.destination_edit = QLineEdit()
        self.destination_edit.setPlaceholderText('Ruta de la nueva carpeta MaxSpin')
        self.destination_edit.setAccessibleName('Carpeta portable')
        self._path_row(destination_layout, self.destination_edit, self.choose_destination)
        controls = QHBoxLayout()
        self.back_button = QPushButton('Atrás')
        self.back_button.clicked.connect(lambda: self.pages.setCurrentIndex(0))
        self.install_button = QPushButton('Preparar Max Spin')
        self.install_button.clicked.connect(self.begin_install)
        controls.addWidget(self.back_button)
        controls.addWidget(self.install_button, 1)
        destination_layout.addLayout(controls)
        progress_page, progress_layout = self._page('Preparando Max Spin')
        self.status_label = self._wrapped('Preparando la carpeta portable…')
        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 0)
        self.detail_label = self._wrapped('')
        progress_layout.addWidget(self.status_label)
        progress_layout.addWidget(self.progress_bar)
        progress_layout.addWidget(self.detail_label)
        actions = QHBoxLayout()
        self.cancel_button = QPushButton('Cancelar')
        self.cancel_button.clicked.connect(self.cancel_install)
        self.retry_button = QPushButton('Revisar y reintentar')
        self.retry_button.setEnabled(False)
        self.retry_button.clicked.connect(self.retry)
        self.play_button = QPushButton('Jugar')
        self.play_button.setEnabled(False)
        self.play_button.clicked.connect(self.play)
        for button in (self.cancel_button, self.retry_button, self.play_button):
            actions.addWidget(button)
        progress_layout.addLayout(actions)
        self.notice = self._wrapped(initial_error or '')
        outer.addWidget(self.notice)

    def _page(self, heading):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 18, 0, 0)
        label = QLabel(heading)
        label.setStyleSheet('font-size: 17px; font-weight: bold;')
        layout.addWidget(label)
        self.pages.addWidget(page)
        return page, layout

    @staticmethod
    def _wrapped(text):
        label = QLabel(text)
        label.setWordWrap(True)
        return label

    @staticmethod
    def _path_row(layout, edit, callback):
        row = QHBoxLayout()
        row.addWidget(edit, 1)
        browse = QPushButton('Examinar…')
        browse.clicked.connect(callback)
        row.addWidget(browse)
        layout.addLayout(row)

    def _choose(self, destination):
        edit = self.destination_edit if destination else self.iso_edit
        current = Path(edit.text()) if edit.text().strip() else self.payload
        directory = current.parent if edit.text().strip() else current
        dialog = make_file_dialog(self, destination=destination, directory=directory)
        if dialog.exec() == QDialog.DialogCode.Accepted and dialog.selected_path is not None:
            edit.setText(str(dialog.selected_path))
        dialog.deleteLater()

    def choose_iso(self):
        self._choose(False)

    def choose_destination(self):
        self._choose(True)

    def show_destination(self):
        if not self.iso_edit.text().strip():
            self.notice.setText('Elige la ISO original de Xbox para continuar.')
            return
        self.notice.clear()
        self.pages.setCurrentIndex(1)

    def begin_install(self):
        if self.worker is not None and self.worker.isRunning():
            return
        if not self.destination_edit.text().strip():
            self.notice.setText('Elige una carpeta nueva para guardar Max Spin.')
            return
        self.notice.clear()
        self.pages.setCurrentIndex(2)
        self.status_label.setText('Preparando la carpeta portable…')
        self.detail_label.clear()
        self.progress_bar.setRange(0, 0)
        self.cancel_button.setEnabled(True)
        self.retry_button.setEnabled(False)
        self.play_button.setEnabled(False)
        self.worker = InstallThread(self.installer, Path(self.iso_edit.text()),
                                    Path(self.destination_edit.text()), self.payload, self)
        self.worker.progress_changed.connect(self.update_progress)
        self.worker.completed.connect(self.install_completed)
        self.worker.failed.connect(self.install_failed)
        self.worker.cancelled.connect(self.install_cancelled)
        self.worker.finished.connect(self.worker_finished)
        self.worker.start()

    @Slot(str, object, object, str)
    def update_progress(self, stage, done, total, detail):
        stages = {'extract': 'Extrayendo tu ISO…', 'extraction': 'Extrayendo tu ISO…',
                  'copy': 'Copiando Max Spin…', 'payload': 'Copiando Max Spin…',
                  'prepare': 'Preparando los recursos del juego…',
                  'preparation': 'Preparando los recursos del juego…',
                  'validate': 'Comprobando tu juego…', 'complete': 'Max Spin está listo.'}
        self.status_label.setText(stages.get(stage, 'Preparando Max Spin…'))
        self.detail_label.setText(detail)
        if total > 0:
            self.progress_bar.setRange(0, 1000)
            self.progress_bar.setValue(max(0, min(1000, int(done * 1000 / total))))
        else:
            self.progress_bar.setRange(0, 0)

    @Slot(object)
    def install_completed(self, root):
        self.installed_root = Path(root)
        self.status_label.setText('Max Spin está listo. Puedes mover esta carpeta cuando quieras.')
        self.detail_label.setText(str(self.installed_root))
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(100)
        self.cancel_button.setEnabled(False)
        self.play_button.setEnabled(True)

    @Slot(str)
    def install_failed(self, error):
        self.status_label.setText('No se pudo preparar Max Spin. ' + error +
                                 '\nRevisa la ISO, el espacio disponible y los permisos de la carpeta; vuelve a intentarlo.')
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        self.cancel_button.setEnabled(False)
        self.retry_button.setEnabled(True)

    @Slot()
    def install_cancelled(self):
        self.status_label.setText('Instalación cancelada. Puedes revisar la ISO y la carpeta para volver a intentarlo.')
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        self.cancel_button.setEnabled(False)
        self.retry_button.setEnabled(True)

    def cancel_install(self):
        if self.worker is not None and self.worker.isRunning():
            self.worker.stop_requested.set()
            self.cancel_button.setEnabled(False)
            self.status_label.setText('Cancelando y limpiando la instalación incompleta…')

    def worker_finished(self):
        if self.close_when_stopped:
            self.close()

    def retry(self):
        if self.worker is not None and self.worker.isRunning():
            return
        self.notice.clear()
        self.pages.setCurrentIndex(0)

    def play(self):
        if self.installed_root is None:
            return
        try:
            self.launcher(self.installed_root)
        except (ValueError, OSError) as error:
            self.status_label.setText('No se pudo iniciar Max Spin: ' + str(error) +
                                     '\nComprueba que la carpeta portable conserva todos sus archivos y vuelve a pulsar Jugar.')

    def closeEvent(self, event):
        if self.worker is not None and self.worker.isRunning():
            self.close_when_stopped = True
            self.cancel_install()
            event.ignore()
        else:
            event.accept()


def run(payload: Path, launcher, initial_error=None) -> int:
    application = QApplication.instance() or QApplication(['Max Spin'])
    application.setApplicationName('Max Spin')
    window = InstallWindow(payload, launcher=launcher, initial_error=initial_error)
    window.show()
    try:
        return application.exec()
    finally:
        # Destroy widgets and their completed QThreads while QApplication is
        # alive. Deferring this to Python shutdown crashes Windows after cancel.
        window.deleteLater()
        QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
