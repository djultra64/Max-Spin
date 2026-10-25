#!/usr/bin/env python3
"""Actual SDK epoch lookup and allocation with counted getenv calls. djultra64."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
sdk = root / 'vendor/xboxrecomp'
with tempfile.TemporaryDirectory(prefix='contiguous-startup-') as temporary:
    executable = Path(temporary) / 'cache'
    subprocess.run([
        'cc', '-D_GNU_SOURCE', '-std=c11', '-O1', '-ffunction-sections', '-fdata-sections',
        '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
        '-I' + str(sdk / 'src'), '-I' + str(sdk / 'src/platform'), '-I' + str(sdk / 'src/kernel'), '-I' + str(sdk / 'src/d3d'),
        str(root / 'tests/contiguous_startup_cache_cpu.c'),
        str(sdk / 'src/platform/win32_compat.c'), '-Wl,--gc-sections',
        '-pthread', '-lm', '-ldl', '-o', str(executable),
    ], check=True)
    for mode in range(4):
        subprocess.run([str(executable), str(mode)], check=True, timeout=15)
