"""Bounded, read-only original-Xbox XDVDFS reader. Own work: djultra64.

Format reference (not implementation code): XboxDev/extract-xiso,
https://github.com/XboxDev/extract-xiso/blob/master/extract-xiso.c
Supports stripped XISO and original Xbox XGD1 dumps; not Xbox 360 images.
Names use a deterministic Latin-1 byte mapping and the same conservative
Windows-compatible checks on both hosts. Extraction requires a new directory.
The caller owns cleanup of a partially extracted directory after an exception.
"""
from __future__ import annotations

from dataclasses import dataclass, field
import os
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import unicodedata

SECTOR_SIZE = 2048
MAGIC = b'MICROSOFT*XBOX*MEDIA'
PARTITION_OFFSETS = (0, 0x18300000)
MAX_TABLE_BYTES = 16 * 1024 * 1024
MAX_TOTAL_TABLE_BYTES = 64 * 1024 * 1024
MAX_ENTRIES = 100_000
MAX_DEPTH = 64
COPY_CHUNK = 1024 * 1024
_RESERVED = {'CON', 'PRN', 'AUX', 'NUL', 'CLOCK$', 'CONIN$', 'CONOUT$'}
_RESERVED.update(f'{prefix}{number}' for prefix in ('COM', 'LPT') for number in '123456789¹²³')


class InvalidXboxISO(ValueError):
    """The ISO is unsupported, corrupt, ambiguous, or unsafe to extract."""


@dataclass(frozen=True)
class Entry:
    path: PurePosixPath
    size: int
    is_dir: bool
    _offset: int = field(repr=False)
    _owner: object = field(repr=False, compare=False)


def _safe_name(raw: bytes) -> str:
    name = raw.decode('latin-1')
    if (not name or name in ('.', '..') or name[-1] in ' .'
            or any(ord(c) < 32 or ord(c) == 127 or c in '<>:"/\\|?*' for c in name)
            or name.split('.')[0].rstrip(' ').upper() in _RESERVED
            or re.fullmatch(r'[^.]{1,6}~[0-9]+(?:\.[^.]*)?', name)):
        raise InvalidXboxISO('Unsafe or ambiguous filename in Xbox ISO')
    return name


def _check_directory(path: Path) -> None:
    info = path.lstat()
    # Junctions and other Windows reparse points must never be followed.
    if (not stat.S_ISDIR(info.st_mode) or stat.S_ISLNK(info.st_mode)
            or getattr(info, 'st_file_attributes', 0) & 0x400):
        raise InvalidXboxISO('Extraction path contains a link or non-directory')


class XboxISO:
    """Parse and validate an ISO before creating any extraction output.

    ``progress(done_bytes, total_bytes, current_path)`` runs after each copied
    chunk and empty file. ``cancelled()`` is checked before creating output and
    between chunks; cancellation raises InterruptedError. Paths are PurePosixPath.
    Keep the input ISO unchanged until close; this object is not thread-safe.
    """

    def __init__(self, path):
        self.path = Path(path)
        # Avoid retaining stale file bytes if the user truncates/replaces input
        # while an installation is running; reads must observe the open handle.
        self._source = self.path.open('rb', buffering=0)
        self._owner = object()
        self.entries = []
        self.total_bytes = 0
        try:
            self._size = os.fstat(self._source.fileno()).st_size
            if not stat.S_ISREG(os.fstat(self._source.fileno()).st_mode):
                raise InvalidXboxISO('Xbox ISO must be a regular file')
            self._parse()
        except BaseException:
            self.close()
            raise

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def close(self):
        self._source.close()

    def _read_at(self, offset, size):
        if offset < 0 or size < 0 or offset > self._size - size:
            raise InvalidXboxISO('Truncated Xbox ISO extent')
        self._source.seek(offset)
        data = self._source.read(size)
        if len(data) != size:
            raise InvalidXboxISO('Xbox ISO changed or was truncated while reading')
        return data

    def _extent(self, sector, size):
        offset = self.partition_offset + sector * SECTOR_SIZE
        if offset > self._size - size:
            raise InvalidXboxISO('Xbox ISO extent lies outside the image')
        return offset

    def _parse(self):
        for base in PARTITION_OFFSETS:
            at = base + 0x10000
            if at + SECTOR_SIZE > self._size:
                continue
            header = self._read_at(at, SECTOR_SIZE)
            if header[:20] == MAGIC:
                if header[-20:] != MAGIC:
                    raise InvalidXboxISO('Invalid Xbox volume descriptor footer')
                self.partition_offset = base
                root_sector, root_size = struct.unpack_from('<II', header, 20)
                break
        else:
            raise InvalidXboxISO('Not a supported original Xbox ISO (XISO or XGD1)')

        if bool(root_sector) != bool(root_size):
            raise InvalidXboxISO('Invalid Xbox root directory extent')
        pending = [(PurePosixPath(), root_sector, root_size, 0)]
        directory_extents = set()
        extents = [(self.partition_offset + 0x10000, self.partition_offset + 0x10800)]
        table_bytes = 0
        while pending:
            parent, sector, size, depth = pending.pop()
            if not size:
                continue
            if depth > MAX_DEPTH or size > MAX_TABLE_BYTES:
                raise InvalidXboxISO('Xbox directory exceeds safe parsing limits')
            offset = self._extent(sector, size)
            if offset in directory_extents:
                raise InvalidXboxISO('Cyclic or aliased Xbox directory table')
            directory_extents.add(offset)
            extents.append((offset, offset + size))
            table_bytes += size
            if table_bytes > MAX_TOTAL_TABLE_BYTES:
                raise InvalidXboxISO('Xbox directory tables exceed safe parsing limits')
            table = self._read_at(offset, size)
            nodes = [0]
            visited = set()
            spans = []
            aliases = set()
            children = []
            while nodes:
                at = nodes.pop()
                if at in visited:
                    raise InvalidXboxISO('Cyclic or shared Xbox directory tree pointer')
                visited.add(at)
                if at + 2 > size:
                    raise InvalidXboxISO('Xbox directory pointer is outside its table')
                if table[at:at + 2] == b'\xff\xff':
                    if at == 0 and table == b'\xff' * size:
                        continue
                    # Some writers point at sector padding preceding the node.
                    after = ((at // SECTOR_SIZE) + 1) * SECTOR_SIZE
                    if after >= size or table[at:after] != b'\xff' * (after - at):
                        raise InvalidXboxISO('Invalid Xbox directory padding pointer')
                    nodes.append(after)
                    continue
                if at + 14 > size:
                    raise InvalidXboxISO('Truncated Xbox directory record')
                left, right, start, length, attributes, name_length = struct.unpack_from('<HHIIBB', table, at)
                end = at + 14 + name_length
                if end > size or at // SECTOR_SIZE != (end - 1) // SECTOR_SIZE:
                    raise InvalidXboxISO('Truncated or sector-crossing Xbox directory filename')
                spans.append((at, end))
                name = _safe_name(table[at + 14:end])
                alias = unicodedata.normalize('NFC', name).casefold()
                if alias in aliases:
                    raise InvalidXboxISO('Duplicate or case-aliased Xbox filename')
                aliases.add(alias)
                path = parent / name
                is_dir = bool(attributes & 0x10)
                entry_offset = self._extent(start, length)
                if len(self.entries) >= MAX_ENTRIES or len(path.parts) > MAX_DEPTH:
                    raise InvalidXboxISO('Xbox image exceeds safe entry or path-depth limits')
                entry = Entry(path, length, is_dir, entry_offset, self._owner)
                self.entries.append(entry)
                if is_dir:
                    children.append((path, start, length, depth + 1))
                else:
                    self.total_bytes += length
                    if length:
                        extents.append((entry_offset, entry_offset + length))
                for pointer in (right, left):
                    if pointer:
                        nodes.append(pointer * 4)
            spans.sort()
            if any(first[1] > second[0] for first, second in zip(spans, spans[1:])):
                raise InvalidXboxISO('Overlapping Xbox directory records')
            pending.extend(reversed(children))
        extents.sort()
        if any(first[1] > second[0] for first, second in zip(extents, extents[1:])):
            raise InvalidXboxISO('Overlapping Xbox file or directory extents')

    def read(self, entry, limit=None):
        """Read a file, optionally limited to a prefix (for executable validation)."""
        if not isinstance(entry, Entry) or entry._owner is not self._owner or entry.is_dir:
            raise ValueError('Entry is not a file belonging to this Xbox ISO')
        if limit is not None and (not isinstance(limit, int) or limit < 0):
            raise ValueError('Read limit must be a nonnegative integer')
        return self._read_at(entry._offset, entry.size if limit is None else min(entry.size, limit))

    @staticmethod
    def _cancel(cancelled):
        if cancelled is not None and cancelled():
            raise InterruptedError('Xbox ISO extraction cancelled')

    def extract(self, destination, progress=None, cancelled=None):
        """Extract into a new destination; never merge into or overwrite a tree."""
        self._cancel(cancelled)
        # abspath preserves symlinks for explicit checks; resolve would hide them.
        destination = Path(os.path.abspath(os.fspath(destination)))
        for ancestor in reversed(destination.parents):
            _check_directory(ancestor)
        if os.path.lexists(destination):
            raise FileExistsError(f'Extraction destination already exists: {destination}')
        if os.name == 'posix' and os.open in os.supports_dir_fd:
            self._extract_posix(destination, progress, cancelled)
        else:
            self._extract_paths(destination, progress, cancelled)

    def _copy(self, output, entry, done, progress, cancelled):
        remaining = entry.size
        self._source.seek(entry._offset)
        while remaining:
            self._cancel(cancelled)
            data = self._source.read(min(COPY_CHUNK, remaining))
            if not data:
                raise InvalidXboxISO('Xbox ISO was truncated during extraction')
            output.write(data)
            remaining -= len(data)
            done += len(data)
            if progress is not None:
                progress(done, self.total_bytes, entry.path)
        if entry.size == 0 and progress is not None:
            progress(done, self.total_bytes, entry.path)
        return done

    def _extract_posix(self, destination, progress, cancelled):
        flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
        parent_fd = os.open(destination.anchor, flags)
        root_fd = None
        try:
            for part in destination.parent.parts[1:]:
                next_fd = os.open(part, flags, dir_fd=parent_fd)
                os.close(parent_fd)
                parent_fd = next_fd
            os.mkdir(destination.name, 0o700, dir_fd=parent_fd)
            root_fd = os.open(destination.name, flags, dir_fd=parent_fd)
            done = 0
            for entry in self.entries:
                self._cancel(cancelled)
                directory_fd = os.dup(root_fd)
                try:
                    for part in entry.path.parts[:-1]:
                        next_fd = os.open(part, flags, dir_fd=directory_fd)
                        os.close(directory_fd)
                        directory_fd = next_fd
                    name = entry.path.name
                    if entry.is_dir:
                        os.mkdir(name, 0o700, dir_fd=directory_fd)
                    else:
                        output_fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                                            0o600, dir_fd=directory_fd)
                        with os.fdopen(output_fd, 'wb') as output:
                            done = self._copy(output, entry, done, progress, cancelled)
                finally:
                    os.close(directory_fd)
        finally:
            if root_fd is not None:
                os.close(root_fd)
            os.close(parent_fd)

    def _extract_paths(self, destination, progress, cancelled):
        destination.mkdir(mode=0o700)
        done = 0
        for entry in self.entries:
            self._cancel(cancelled)
            target = destination.joinpath(*entry.path.parts)
            for ancestor in reversed(target.parents):
                _check_directory(ancestor)
            if entry.is_dir:
                target.mkdir(mode=0o700)
            else:
                with target.open('xb') as output:
                    done = self._copy(output, entry, done, progress, cancelled)
