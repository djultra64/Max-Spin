"""One owned native window across sequential image processes. djultra64."""
import json
import os
import selectors
import subprocess
import time
import resource
import stat
import tempfile
from pathlib import Path

QUERY_LIMIT = 4096
VIDEO_HEIGHTS = (720, 900, 1080, 1440, 1800, 2160)


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate video query JSON key')
        result[key] = value
    return result


def video_query_capable(build_dir, environment):
    """Never discover support by executing an unknown flag on a legacy title."""
    path = build_dir / 'manifest.json'
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
    except FileNotFoundError:
        return environment.get('TOPSPIN_VIDEO_CONFIG_QUERY') == '1'
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_size > 2 * 1024 * 1024:
            raise ValueError('Invalid video query capability manifest')
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            data = stream.read(2 * 1024 * 1024 + 1)
        if len(data) > 2 * 1024 * 1024:
            raise ValueError('Video query capability manifest exceeds limit')
        manifest = json.loads(data, object_pairs_hook=_unique_object)
        if not isinstance(manifest, dict):
            raise ValueError('Invalid video query capability manifest')
        # A manifest without this exact capability is legacy, regardless of an
        # inherited development opt-in. No SDK-version inference or flag probe.
        return manifest.get('video_config_query') is True
    finally:
        os.close(fd)


def _query_file_limit():
    # subprocess.run's capture_output buffers without a size limit. Private
    # regular output files plus a child-only kernel limit bound both streams,
    # including before timeout, without a pipe/communicate memory blow-up.
    _, hard = resource.getrlimit(resource.RLIMIT_FSIZE)
    maximum = QUERY_LIMIT + 1 if hard == resource.RLIM_INFINITY else min(hard, QUERY_LIMIT + 1)
    resource.setrlimit(resource.RLIMIT_FSIZE, (maximum, maximum))


def query_video_size(build_dir, executable, environment):
    executable = Path(executable)
    images = json.loads((Path(__file__).parent / 'config/images.json').read_text())
    if executable.parent != build_dir or executable.name not in {'topspin_' + key for key in images}:
        raise ValueError('Video query executable is outside the image whitelist')
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise ValueError(f'Video query image is not built: {executable}')
    # This helper only prints dimensions. Overlay constructors can append to
    # their own logs before main and hit its deliberately small RLIMIT_FSIZE.
    # Keep injection on the actual window/game; exclude it only from this probe.
    query_environment = dict(environment)
    query_environment.pop('LD_PRELOAD', None)
    with tempfile.TemporaryFile() as output, tempfile.TemporaryFile() as errors:
        try:
            result = subprocess.run([str(executable), '--query-video-size'],
                                    stdin=subprocess.DEVNULL, stdout=output,
                                    stderr=errors, env=query_environment, timeout=5,
                                    preexec_fn=_query_file_limit)
        except subprocess.TimeoutExpired as error:
            raise ValueError('Video size query timed out before window creation') from error
        if max(os.fstat(output.fileno()).st_size, os.fstat(errors.fileno()).st_size) > QUERY_LIMIT:
            raise ValueError('Video size query output exceeds limit')
        output.seek(0)
        errors.seek(0)
        data, diagnostic = output.read(QUERY_LIMIT + 1), errors.read(QUERY_LIMIT + 1)
    if result.returncode:
        detail = diagnostic[:1024].decode('utf-8', errors='replace').strip()
        raise ValueError(f'Video size query failed (exit {result.returncode}): {detail}')
    try:
        value = json.loads(data, object_pairs_hook=_unique_object)
    except (ValueError, UnicodeError) as error:
        raise ValueError('Video size query returned invalid JSON') from error
    if (not isinstance(value, dict) or set(value) != {'width', 'height'}
            or type(value['width']) is not int or type(value['height']) is not int
            or value['height'] not in VIDEO_HEIGHTS
            or value['width'] != value['height'] // 9 * 16):
        raise ValueError('Video size query returned unsupported dimensions')
    return value['width'], value['height']


class PersistentWindow:
    def __init__(self, build_dir, environment, video_executable=None):
        self.environment = environment.copy()
        self.environment.pop('TOPSPIN_PARENT_X11_WINDOW', None)
        # A newly launched supervisor must resolve newly saved preferences,
        # never reuse another image/session's effective render configuration.
        self.environment.pop('TOPSPIN_SESSION_INTERNAL_HEIGHT', None)
        self.environment.pop('TOPSPIN_WINDOW_WIDTH', None)
        self.environment.pop('TOPSPIN_WINDOW_HEIGHT', None)
        self.build_dir = build_dir
        self.video_executable = video_executable
        self.session_height = None
        self.enabled = self.environment.get('TOPSPIN_PERSISTENT_WINDOW') == '1'
        self.executable = build_dir / 'topspin_window_host'
        self.owner = None
        self.messages = bytearray()
        self.quit_requested = False

    def __enter__(self):
        if not self.enabled:
            return self
        if not self.executable.is_file() or not os.access(self.executable, os.X_OK):
            raise ValueError(f'Persistent window host is not built: {self.executable}')
        width, height = 1280, 720
        if self.video_executable is not None and video_query_capable(self.build_dir, self.environment):
            width, height = query_video_size(self.build_dir, self.video_executable, self.environment)
            self.session_height = height
            print(f'[WINDOW-OWNER] startup output={width}x{height}; fixed until restart', flush=True)
        self.environment['TOPSPIN_WINDOW_WIDTH'] = str(width)
        self.environment['TOPSPIN_WINDOW_HEIGHT'] = str(height)
        self.environment['SDL_VIDEO_DRIVER'] = 'x11'
        self.environment['SDL_VIDEODRIVER'] = 'x11'
        try:
            self.owner = subprocess.Popen([str(self.executable)], stdin=subprocess.PIPE,
                                          stdout=subprocess.PIPE, env=self.environment)
            data = bytearray()
            deadline = time.monotonic() + 15
            with selectors.DefaultSelector() as selector:
                selector.register(self.owner.stdout, selectors.EVENT_READ)
                while b'\n' not in data:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0 or not selector.select(remaining):
                        raise ValueError('Persistent window host did not become ready')
                    chunk = os.read(self.owner.stdout.fileno(), 4096 - len(data))
                    if not chunk or len(data) + len(chunk) >= 4096:
                        raise ValueError('Persistent window host returned no valid ready message')
                    data.extend(chunk)
            record = json.loads(data.split(b'\n', 1)[0])
            self.messages.extend(data.split(b'\n', 1)[1])
            if (not isinstance(record, dict) or record.get('protocol') != 1
                    or record.get('driver') != 'x11'
                    or type(record.get('window')) is not int
                    or not 0 < record['window'] <= 0xffffffff
                    or any(type(record.get(k)) is not int or not 0 < record[k] <= 16384
                           for k in ('width', 'height'))):
                raise ValueError('Persistent window host returned an invalid X11 ready message')
            self.environment['TOPSPIN_PARENT_X11_WINDOW'] = str(record['window'])
            print(f"[WINDOW-OWNER] window={record['window']} driver=x11; retained across image transitions", flush=True)
            return self
        except BaseException:
            self.close()
            raise

    @staticmethod
    def stop_owned_child(child):
        if child.poll() is not None:
            return
        child.terminate()
        try:
            child.wait(timeout=7)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()

    def run(self, command, environment):
        if not self.enabled:
            return subprocess.run(command, env=environment).returncode
        # This close comes from the user's owner window, not an image handoff.
        closing = self.close_status()
        if closing is not None:
            return closing
        child = subprocess.Popen(command, env=environment)
        try:
            while True:
                try:
                    status = child.wait(timeout=.1)
                    closing = self.close_status()
                    return closing if closing is not None else status
                except subprocess.TimeoutExpired:
                    closing = self.close_status()
                    if closing is not None:
                        self.stop_owned_child(child)
                        return closing
        except BaseException:
            self.stop_owned_child(child)
            raise

    def close_status(self):
        if not self.enabled:
            return None
        with selectors.DefaultSelector() as selector:
            selector.register(self.owner.stdout, selectors.EVENT_READ)
            while selector.select(0):
                chunk = os.read(self.owner.stdout.fileno(), 4096)
                if not chunk:
                    break
                self.messages.extend(chunk)
                if len(self.messages) > 4096:
                    raise ValueError('Persistent window event exceeds protocol limit')
        while b'\n' in self.messages:
            line, _, rest = self.messages.partition(b'\n')
            self.messages = bytearray(rest)
            if json.loads(line) != {'event': 'close'}:
                raise ValueError('Persistent window returned an invalid event')
            self.quit_requested = True
        if self.quit_requested:
            return 0
        status = self.owner.poll()
        return (0 if status == 0 else 2) if status is not None else None

    def close(self):
        if self.owner is None:
            return
        if self.owner.stdin:
            self.owner.stdin.close()
        try:
            self.owner.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.stop_owned_child(self.owner)
        if self.owner.stdout:
            self.owner.stdout.close()
        self.owner = None

    def __exit__(self, *_):
        self.close()
