"""Actual portable desktop entry, without external tools or setup writes. djultra64."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def test_desktop_entry_uses_bundled_application_and_preserves_arguments():
    with tempfile.TemporaryDirectory(prefix='max spin portable ') as directory:
        root=Path(directory);shutil.copy2(ROOT/'MaxSpin.sh',root/'MaxSpin.sh')
        runtime=root/'portable-runtime';runtime.mkdir()
        app=runtime/'MaxSpin-Setup'
        app.write_text('#!/bin/bash\nprintf "%s\\n" "$@"\nexit 7\n');app.chmod(0o700)
        result=subprocess.run(['/bin/bash',str(root/'MaxSpin.sh'),'--play','space argument'],env=os.environ|{'PATH':'/bin'},capture_output=True,text=True)
        assert result.returncode==7
        assert result.stdout=='--play\nspace argument\n'
        assert {p.name for p in root.iterdir()}=={'MaxSpin.sh','portable-runtime'}


def test_incomplete_download_refuses_external_tools_without_creating_files():
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory);shutil.copy2(ROOT/'MaxSpin.sh',root/'MaxSpin.sh')
        result=subprocess.run(['/bin/bash',str(root/'MaxSpin.sh')],capture_output=True,text=True)
        assert result.returncode==2
        assert 'paquete' in result.stderr
        assert {p.name for p in root.iterdir()}=={'MaxSpin.sh'}
