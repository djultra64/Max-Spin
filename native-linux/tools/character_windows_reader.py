"""Bounded Win32 package reads with pinned, reparse-free ancestry.

Own work: djultra64. Every handle forbids write/delete sharing, so checked path
components cannot be renamed or converted to reparse points during a read.
No elevation or link support needed.
"""
import os
from pathlib import Path

if os.name == 'nt':
    import ctypes
    from ctypes import wintypes

    class _Information(ctypes.Structure):
        _fields_ = [('attributes', wintypes.DWORD),
                    ('created', wintypes.FILETIME), ('accessed', wintypes.FILETIME),
                    ('written', wintypes.FILETIME), ('volume', wintypes.DWORD),
                    ('size_high', wintypes.DWORD), ('size_low', wintypes.DWORD),
                    ('links', wintypes.DWORD), ('index_high', wintypes.DWORD),
                    ('index_low', wintypes.DWORD)]

    _kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    _kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                   wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD,
                                   wintypes.HANDLE]
    _kernel.CreateFileW.restype = wintypes.HANDLE
    _kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    _kernel.CloseHandle.restype = wintypes.BOOL
    _kernel.GetFileInformationByHandle.argtypes = [wintypes.HANDLE, ctypes.POINTER(_Information)]
    _kernel.GetFileInformationByHandle.restype = wintypes.BOOL
    _kernel.GetFileType.argtypes = [wintypes.HANDLE]
    _kernel.GetFileType.restype = wintypes.DWORD
    _kernel.ReadFile.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD,
                                ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
    _kernel.ReadFile.restype = wintypes.BOOL
    _INVALID_HANDLE = ctypes.c_void_p(-1).value


def _information(handle):
    info = _Information()
    if not _kernel.GetFileInformationByHandle(handle, ctypes.byref(info)):
        raise ctypes.WinError(ctypes.get_last_error())
    if info.attributes & 0x400:
        raise ValueError('package resource contains a reparse point')
    if _kernel.GetFileType(handle) != 1:
        raise ValueError('package resource is not a regular disk resource')
    return info


def _open(path, directory):
    # FILE_READ_ATTRIBUTES for ancestors; GENERIC_READ for the final payload.
    # FILE_SHARE_WRITE/DELETE are absent: pin names and prevent reparse mutation.
    handle = _kernel.CreateFileW(str(path), 0x80 if directory else 0x80000000,
                                1, None, 3,
                                0x00200000 | 0x02000000, None)
    if handle == _INVALID_HANDLE:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        info = _information(handle)
        if bool(info.attributes & 0x10) != directory:
            raise ValueError('package resource has the wrong file type')
    except BaseException:
        _kernel.CloseHandle(handle)
        raise
    return handle


def _read_handle(handle, limit):
    info = _information(handle)
    if ((info.size_high << 32) | info.size_low) > limit:
        raise ValueError('package file exceeds size limit')
    chunks, size = [], 0
    while True:
        count = min(1024 * 1024, limit + 1 - size)
        buffer = ctypes.create_string_buffer(count)
        received = wintypes.DWORD()
        if not _kernel.ReadFile(handle, buffer, count, ctypes.byref(received), None):
            raise ctypes.WinError(ctypes.get_last_error())
        if not received.value:
            return b''.join(chunks)
        size += received.value
        if size > limit:
            raise ValueError('package file exceeds size limit')
        chunks.append(buffer.raw[:received.value])


def read_bounded(root, relative, limit):
    """Return immutable bytes and actual relative case; hold all ancestry.

    Root paths may include spaces/Unicode. Package-relative paths use the shared
    portable validator and unambiguous case-insensitive component resolution.
    """
    if os.name != 'nt':
        raise OSError('native Windows package reader requires Win32')
    from character_packages import _relative
    parts = _relative(relative).split('/')
    if type(limit) is not int or limit < 0:
        raise ValueError('invalid package read bound')
    root = Path(os.path.abspath(root))
    if str(root).startswith(('\\\\?\\', '\\\\.\\')):
        raise ValueError('Windows device namespace roots are unsupported')
    handles, selected = [], []
    try:
        current = Path(root.anchor)
        handles.append(_open(current, True))
        for component in root.parts[1:]:
            current /= component
            handles.append(_open(current, True))
        for index, requested in enumerate(parts):
            matches = [name for name in os.listdir(current) if name.casefold() == requested.casefold()]
            if len(matches) != 1:
                raise ValueError('missing or case-colliding package resource')
            component = _relative(matches[0])
            selected.append(component)
            current /= component
            handle = _open(current, index < len(parts) - 1)
            handles.append(handle)
        return _read_handle(handle, limit), '/'.join(selected)
    finally:
        for handle in reversed(handles):
            _kernel.CloseHandle(handle)
