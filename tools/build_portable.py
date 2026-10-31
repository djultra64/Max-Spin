#!/usr/bin/env python3
"""Build the self-contained desktop installer on its target OS. djultra64.

Build-time Python/PySide6/PyInstaller only; end users install nothing.
"""
import argparse
import ast
import json
import importlib.metadata
import os
from pathlib import Path
import subprocess
import sys
import sysconfig
import shutil


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--console',action='store_true',help='developer diagnostic build')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    output,work=args.output.absolute(),args.work.absolute()
    if output.exists(): parser.error('output must be a new directory')
    work.mkdir(parents=True,exist_ok=True)
    # Helpers stay as installed source files, so __file__ and data paths resolve
    # to the portable destination. Freeze their standard-library dependencies,
    # rather than freezing helper modules with incorrect _internal paths.
    modules={'ctypes','ctypes.util','zipfile','xml.etree.ElementTree','encodings.cp1252',
             'encodings.utf_16_le','hashlib','zlib','_ctypes','struct','configparser'}
    for directory in (root/'native-linux',root/'portable'):
        for source in directory.rglob('*.py'):
            if any(part in ('cache','build','manual-builds','proposed','__pycache__') for part in source.relative_to(root).parts): continue
            try: tree=ast.parse(source.read_text(encoding='utf-8'))
            except (SyntaxError,UnicodeError): continue
            for node in ast.walk(tree):
                if isinstance(node,ast.Import): candidates=[alias.name for alias in node.names]
                elif isinstance(node,ast.ImportFrom): candidates=[node.module] if node.module else []
                else: continue
                for candidate in candidates:
                    if candidate.split('.')[0] in sys.stdlib_module_names: modules.add(candidate)
    command=[sys.executable,'-m','PyInstaller','--noconfirm','--clean','--onedir','--console' if args.console else '--windowed',
             '--name','MaxSpin-Setup','--distpath',str(work/'dist'),'--workpath',str(work/'build'),
             '--specpath',str(work),'--paths',str(root)]
    for module in sorted(modules): command+=['--hidden-import',module]
    command+=[str(root/'portable/app.py')]
    subprocess.run(command,cwd=root,check=True)
    bundle=work/'dist/MaxSpin-Setup'
    notices=bundle/'licenses'
    for name in ('PySide6','PySide6-Essentials','shiboken6','pyinstaller','altgraph','packaging','pefile','pywin32-ctypes'):
        try: distribution=importlib.metadata.distribution(name)
        except importlib.metadata.PackageNotFoundError: continue
        source=Path(distribution._path)
        candidates=[source/'licenses',source/'LICENSE',source/'LICENSE.txt',
                    Path('/usr/share/licenses')/('python3-'+name.lower()),
                    Path('/usr/share/licenses/qt6-qtbase') if name=='PySide6' else source/'not-present']
        for index,candidate in enumerate(candidates):
            if not candidate.exists(): continue
            target=notices/name/str(index)
            target.parent.mkdir(parents=True,exist_ok=True)
            if candidate.is_dir(): shutil.copytree(candidate,target)
            else: shutil.copy2(candidate,target)
    python_license=Path(sysconfig.get_path('stdlib'))/'LICENSE.txt'
    if not python_license.is_file(): python_license=Path(sys.base_prefix)/'LICENSE.txt'
    if python_license.is_file():
        notices.mkdir(exist_ok=True);shutil.copy2(python_license,notices/'PYTHON-LICENSE.txt')
    binary=bundle/('MaxSpin-Setup.exe' if os.name=='nt' else 'MaxSpin-Setup')
    subprocess.run([str(binary),'--self-check'],cwd=bundle,check=True,timeout=30)
    (bundle/'PORTABLE_RUNTIME.json').write_text(json.dumps({'schema':1,'platform':'windows' if os.name=='nt' else 'linux',
        'format':'onedir','self_check':True})+'\n')
    bundle.rename(output)
    print(output)


if __name__=='__main__':main()
