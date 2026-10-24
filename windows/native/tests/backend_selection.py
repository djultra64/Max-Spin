#!/usr/bin/env python3
"""CPU/configuration contracts only; never a native Windows compile. djultra64."""
import argparse
import os
from pathlib import Path
import subprocess
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SDK = Path(os.environ.get('TOPSPIN_SDK_ROOT', ROOT / 'vendor/xboxrecomp'))

def run(cmd, **kw):
    return subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kw)

def configure(source, windows, vulkan, available):
    with tempfile.TemporaryDirectory(prefix='topspin-backend-') as t:
        t = Path(t)
        (t/'modules').mkdir()
        (t/'modules/FindVulkan.cmake').write_text(
            'set(Vulkan_FOUND '+('TRUE' if available else 'FALSE')+')\n'
            'if(Vulkan_FOUND)\nadd_library(Vulkan::Vulkan INTERFACE IMPORTED)\nendif()\n')
        (t/'modules/FindPkgConfig.cmake').write_text('set(PKG_CONFIG_FOUND FALSE)\n')
        (t/'shaderc').mkdir()
        (t/'shaderc/shaderc.h').touch()
        (t/'shaderc.lib').touch()
        (t/'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.25)
project(BackendContract C)
set(WIN32 {'TRUE' if windows else 'FALSE'})
set(CMAKE_MODULE_PATH "{t / 'modules'}")
set(XBOXRECOMP_VULKAN {'ON' if vulkan else 'OFF'} CACHE BOOL "")
set(SHADERC_LIBRARY "{t / 'shaderc.lib'}" CACHE FILEPATH "")
set(SHADERC_INCLUDE_DIR "{t}" CACHE PATH "")
add_library(platform INTERFACE)
add_library(xbox_kernel INTERFACE)
# Production requires a real static kernel target for PRIVATE definitions.
set_target_properties(xbox_kernel PROPERTIES IMPORTED_GLOBAL FALSE)
add_subdirectory("{source / 'src/d3d'}" d3d)
get_target_property(s xbox_d3d8 SOURCES)
get_target_property(d xbox_d3d8 INTERFACE_COMPILE_DEFINITIONS)
file(WRITE "${{CMAKE_BINARY_DIR}}/backend.txt" "${{s}}\n${{d}}\n")
'''.replace('add_library(xbox_kernel INTERFACE)', 'file(WRITE "${CMAKE_BINARY_DIR}/kernel.c" "int kernel_contract;\\n")\nadd_library(xbox_kernel STATIC "${CMAKE_BINARY_DIR}/kernel.c")').replace('set_target_properties(xbox_kernel PROPERTIES IMPORTED_GLOBAL FALSE)', ''))
        p = run(['cmake', '-S', str(t), '-B', str(t/'build')])
        result = (t/'build/backend.txt').read_text() if (t/'build/backend.txt').exists() else ''
        return p.returncode, p.stdout, result

def main():
    global SDK
    ap=argparse.ArgumentParser()
    ap.add_argument('--source', type=Path, default=SDK)
    ap.add_argument('--patch', type=Path)
    args=ap.parse_args()
    source=args.source.resolve()
    SDK=source
    candidate=None
    if args.patch:
        candidate=tempfile.TemporaryDirectory(prefix='topspin-sdk-candidate-')
        target=Path(candidate.name)
        shutil.copytree(source/'src',target/'src')
        shutil.copytree(source/'tests',target/'tests')
        p=run(['git','apply','--check',str(args.patch.resolve())],cwd=target)
        assert p.returncode==0,p.stdout
        p=run(['git','apply',str(args.patch.resolve())],cwd=target)
        assert p.returncode==0,p.stdout
        source=target
    count=0
    for windows, vulkan, available, succeeds, renderer in [
        (True, True, True, True, 'd3d8_vk.c'),
        (True, True, False, False, None),
        (True, False, False, True, 'd3d8_device.c'),
        (False, True, True, True, 'd3d8_vk.c'),
    ]:
        rc,out,selected=configure(source, windows, vulkan, available)
        assert (rc==0)==succeeds, (windows,vulkan,available,rc,out)
        if succeeds:
            assert renderer in selected, (windows,vulkan,selected)
            if vulkan:
                assert 'XBOXRECOMP_HAVE_VULKAN=1' in selected
                assert 'd3d8_device.c' not in selected
        else:
            assert 'Vulkan' in out and 'shaderc' in out, out
        count+=1
        print(f'PASS configure windows={windows} vulkan={vulkan} dependencies={available}')
    with tempfile.TemporaryDirectory(prefix='topspin-combiner-') as t:
        t=Path(t)
        # Real production combiner source, OS guards simulated; POSIX NT types
        # preloaded deliberately. This is not a Windows ABI/toolchain test.
        unit=t/'combiner.c'
        unit.write_text(f'''#include "{SDK / 'src/platform/xbox_winnt.h'}"
#define _WIN32 1
#define XBOXRECOMP_HAVE_VULKAN 1
#include "{source / 'src/d3d/d3d8_combiners.c'}"
int main(void) {{ NV2ACombinerState s={{0}}; char text[65536];
return d3d8_combiners_generate_hlsl(&s,text,sizeof text)>0 ? 0:1; }}
''')
        p=run(['cc','-std=c11','-I'+str(SDK/'src'),str(unit),'-o',str(t/'combiner')])
        assert p.returncode==0,p.stdout
        p=run([str(t/'combiner')]); assert p.returncode==0,p.stdout
        count+=1
        print('PASS Windows Vulkan guards compile real combiner generator without D3D11 headers')
        text=(source/'src/nv2a/nv2a_pgraph_d3d11.c').read_text()
        start=text.index('typedef struct {',text.index('#define INLINE_VERT_DWORDS'))
        end=text.index('} OutputVertex;',start)+len('} OutputVertex;')
        for windows,vulkan,size in [(True,True,32),(True,False,28),(False,True,32)]:
            unit.write_text('#include <stdint.h>\n'+('#define _WIN32 1\n' if windows else '')+
                ('#define XBOXRECOMP_HAVE_VULKAN 1\n' if vulkan else '')+text[start:end]+
                f'\n_Static_assert(sizeof(OutputVertex)=={size},"actual backend vertex layout");\n')
            p=run(['cc','-std=c11','-c',str(unit),'-o',str(t/'layout.o')])
            assert p.returncode==0,p.stdout
            count+=1
            print(f'PASS actual PGRAPH vertex layout windows={windows} vulkan={vulkan} bytes={size}')
    print(f'{count} contracts PASS; no native Windows build or GPU execution performed')

if __name__=='__main__':
    main()
