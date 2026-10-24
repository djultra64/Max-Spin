"""Local Xbox-to-C pipeline for the native Linux probe. Own work: djultra64."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import re

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SDK = Path(os.environ.get('TOPSPIN_SDK_ROOT', ROOT / 'vendor/xboxrecomp')).resolve()
# One shared whitelist feeds the pipeline, CMake and desktop launcher.
IMAGES = json.loads((HERE/'config/images.json').read_text())
if not IMAGES or any(not re.fullmatch(r'[a-z][a-z0-9_]*', key)
                     or not isinstance(name,str)
                     or not re.fullmatch(r'(?:default|TopSpin\([A-Za-z]+\))\.xbe', name)
                     for key,name in IMAGES.items()):
    raise ValueError('Invalid title image whitelist')


def sdk_revision():
    revision = SDK / 'SOURCE_REVISION'
    if revision.is_file():
        value = revision.read_text().strip()
        if not re.fullmatch(r'[0-9a-f]{40}', value):
            raise ValueError('Invalid SDK source revision')
        return value
    return subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=SDK, text=True).strip()


def manual_wrapped_addresses(hook_text):
    # The upstream scanner recognizes one canonical extern per line. A legal
    # combined C declaration otherwise excludes the manual bodies without
    # emitting their originals, leaving _gen calls unresolved at link time.
    references = set(re.findall(r'\bsub_([0-9A-Fa-f]{8})_gen\b', hook_text))
    declared = set(re.findall(r'^extern void sub_([0-9A-Fa-f]{8})_gen\(void\);',
                              hook_text, re.MULTILINE))
    missing = references - declared
    if missing:
        raise ValueError('Each wrapped original requires its own extern void '
                         'sub_XXXXXXXX_gen(void); line: ' + ', '.join(sorted(missing)))
    return sorted(declared)


def route_observational_wrappers(data, source_name, wrapped):
    # Keep the shared header identical to generation without manual hooks.
    # Originals renamed _gen need an explicit body section because their
    # declarations no longer inherit guest_code from recomp_funcs.h.
    for address in wrapped:
        original = f'sub_{address}'.encode()
        generated = original + b'_gen'
        signature = re.compile(rb'^(?:__attribute__.* )?void ' + generated
                               + rb'\(void\)([^\r\n]*)$')
        prototype = re.compile(rb'^(?:__attribute__.* )?void ' + original
                               + rb'\(void\)\s*;\s*$')
        output = []
        header_declaration_seen = False
        for line in data.splitlines(keepends=True):
            match = signature.match(line.rstrip(b'\r\n'))
            body = (match and not match.group(1).lstrip().startswith(b';')
                    and source_name != 'recomp_funcs.h')
            if body:
                if b'section("guest_code")' not in line:
                    line = b'__attribute__((section("guest_code"))) ' + line
            else:
                line = line.replace(generated, original)
            # Also normalize headers produced by the previous routing scheme,
            # which declared both the renamed original and the wrapper.
            if source_name == 'recomp_funcs.h' and prototype.match(line):
                if header_declaration_seen:
                    continue
                header_declaration_seen = True
            output.append(line)
        data = b''.join(output)
    return data



ORIGINAL_HASHES={
 '000626A0':'f2cdd5165977e5593ae09bc62d17502c8065ffb6ce0739abfef0855d2a9b603d',
 '000FD550':'93f02c1aeb2213b19ff8b32f90cda2908f5da5bc3ef50a293fff96b93f519e36',
}
SEAMS={'000626A0':('000628C9','00062920'),'000FD550':('000FD889','000FD997')}
EARLY_SEAMS={'000626A0':('0006288E','00062920')}
ORIGINAL_XBE={
 '000626A0':(0x62991,'a14ffef3943b7864d0c41c1c55822fed242a532b50420a7cfbfbbbb3aa5147b8'),
 '000FD550':(0xfd9ce,'fb6c568bb2537c2b8bf00db0db6aad1bd834957cdfac92735dfcfcb5ef49a9de'),
}

def route_creator_save_operation(data):
    changed=False
    for address,expected in ORIGINAL_HASHES.items():
        match=re.search(rb'void sub_'+address.encode()+rb'(?:_gen)?\(void\)\s*\{',data)
        if not match:continue
        end=data.index(b'\n}\n',match.start())+3
        body=data[match.start():end]
        canonical=re.sub(rb'\b(sub_[0-9A-Fa-f]{8})_gen\b',rb'\1',body)
        if hashlib.sha256(canonical).hexdigest()!=expected:
            raise ValueError('Unreviewed complete original Save body '+address)
        label,cleanup=SEAMS[address];anchor=('loc_'+label+': ;\n').encode()
        if body.count(anchor)!=1 or body.count(('loc_'+cleanup+': ;').encode())!=1:
            raise ValueError('Save handoff/cleanup seam drift '+address)
        gate=('    if (!topspin_creator_save_checkpoint()) goto loc_'+cleanup+';\n').encode()
        new=canonical.replace(anchor,anchor+gate).replace(('sub_'+address+'(void)').encode(),('sub_'+address+'_gen(void)').encode(),1)
        if address in EARLY_SEAMS:
            early,early_cleanup=EARLY_SEAMS[address]
            early_anchor=('loc_'+early+': ;\n').encode()
            if new.count(early_anchor)!=1:
                raise ValueError('Save phase continuation seam drift '+address)
            early_gate=('    if (!topspin_creator_save_continue()) goto loc_'+early_cleanup+';\n').encode()
            new=new.replace(early_anchor,early_anchor+early_gate)
        data=data[:match.start()]+new+data[end:];changed=True
    if changed:data=b'#include "playercreator_save_operation_hooks.h"\n'+data
    return data


FENCE_ORIGINAL_HASHES={1745552: '7743fff785fb205b69b7061e7cad04269284251e8fde481bf5a9c74949e6db32', 1745728: 'b958743e439c50dcbd05240e7af6fdead071cff2003e1c6895eb75c54f1bdc63'}
def route_fence_writes(text,image_slug):
 if image_slug!='game':return text
 changed=False
 for va in (0x1aa290,0x1aa340):
  m=re.search(r'void sub_%08X\(void\)'%va,text)
  if not m:continue
  end=text.index('\n}\n',m.start())+3;body=text[m.start():end]
  got=hashlib.sha256(body.encode()).hexdigest()
  if got!=FENCE_ORIGINAL_HASHES[va]:raise RuntimeError('original fence body drift %08X %s'%(va,got))
  if va==0x1aa290:
   old='MEM32(eax + 0x1C) = 0;'
   assert body.count(old)==1
   new=body.replace(old,old+'\n    topspin_pb_lineage_fence_birth(eax,edi);')
  else:
   new=body
   pairs=[('MEM32(edi + 0x10) = 0x40110;\n    MEM32(edi + 0x14) = eax;','topspin_pb_lineage_fence_patch(1u,edi,MEM32(ebp));'),('MEM32(edi + 0x18) = 0x40100;\n    MEM32(edi + 0x1C) = 5;','topspin_pb_lineage_fence_patch(2u,edi,MEM32(ebp));'),('MEM32(edi + 0x10) = eax;\n    MEM32(edi + 0x14) = ecx;\n    MEM32(edi + 0x18) = eax;\n    MEM32(edi + 0x1C) = ecx;','topspin_pb_lineage_fence_patch(3u,edi,MEM32(ebp));')]
   for old,replacement in pairs:
    if new.count(old)!=1:raise RuntimeError('fence store seam drift %08X'%va)
    new=new.replace(old,replacement)
  text=text[:m.start()]+new+text[end:];changed=True
 if changed:
  text='#include <stdint.h>\nextern void topspin_pb_lineage_fence_birth(uint32_t,uint32_t);\nextern void topspin_pb_lineage_fence_patch(uint32_t,uint32_t,uint32_t);\n'+text
 return text


def route_pb_publication_observers(data):
    # Observe the real hardware/dummy store and jump reset exactly once.
    # A changed original body must be reviewed before this hook can apply.
    hashes = {
        '001AA170': '691a1240ca4ff33a77ce96699e2488aeb7d3fc1e842835e8fd8802263cfd4fd1',
        '001AA4B0': 'e886b32bcb7efd13759a2b96ee4690a974cb7e51dea7b02e8877933e289bbc8d',
    }
    changed = False
    for address, expected in hashes.items():
        match = re.search(rb'void sub_' + address.encode() + rb'\(void\)\s*\{', data)
        if not match:
            continue
        end = data.index(b'\n}\n', match.start()) + 3
        body = data[match.start():end]
        if hashlib.sha256(body).hexdigest() != expected:
            raise ValueError('Unreviewed PB publication original ' + address)
        if address == '001AA170':
            new = body
            for reg in [b'edx', b'eax']:
                old = b'MEM32(ecx + 0x40) = ' + reg + b';'
                assert new.count(old) == 1
                new = new.replace(old, b'topspin_pb_lineage_put_store(ecx + 0x40,' + reg + b');')
        else:
            old = b'MEM32(esi) = edi;'
            assert body.count(old) == 1
            new = body.replace(old, b'topspin_pb_lineage_wrap(MEM32(esi),MEM32(MEM32(esi)),edi);\n    ' + old)
        data = data[:match.start()] + new + data[end:]
        changed = True
    if changed:
        data = b'#include "visual_pb_hooks.h"\n' + data
    return data


def route_creator_material_reservation_observer(data):
    # Actual5558 reservation JUMP/reset, not inferred cursor ordering.
    match = re.search(rb'void sub_00159CF0\(void\)\s*\{', data)
    if not match:
        return data
    end = data.index(b'\n}\n', match.start()) + 3
    body = data[match.start():end]
    if hashlib.sha256(body).hexdigest() != '372153b6670b665611d43f31ad74487fbc5d04900220fc6a8a219b49abb88c09':
        raise ValueError('Unreviewed original Creator159CF0 reservation')
    jump = b'MEM32(edx) = ecx;'
    reset = b'MEM32(esi) = edi;'
    assert body.count(jump) == 1 and body.count(reset) == 1
    new = body.replace(b'{', b'{\n    uint32_t _mc_wrap_at = 0, _mc_wrap_word = 0;', 1)
    new = new.replace(jump, jump + b'\n    _mc_wrap_at = edx; _mc_wrap_word = ecx;')
    new = new.replace(reset, b'topspin_creator_material_wrap_observe(esi,_mc_wrap_at,_mc_wrap_word,edi);\n    ' + reset)
    return b'#include "playercreator_material_tracking_hooks.h"\n' + data[:match.start()] + new + data[end:]


def route_actor_binding_wrappers(data):
    # Frozen original bodies; host wrappers call each renamed original once.
    # Explicit section retains the original scheduler's guest-code boundary.
    hashes = {'00074B90': '01635855d6f188320ee1600ae9d2e50d72c2c1800a3c58ffecfc0989659d22a7', '00074520': '0ef0d67e99acae9a6c0937e4c0acc80bf32d781e13ebdc460fd3840db127fd6c', '000862B0': 'bcb0e0fe722c0c6f62f88ef98dadec69c53233ac27601c08a03f84ec7e545245', '00086430': '2d8509c1bdf4d16599f23a85bd89829392b34d4d49fe45abe0f0b665e0db281d', '000857C0': '5f01cda67af0621d61ea008a737b0138dc54bb68c95fa193e3cde79ed7a65ab1', '00086A00': 'b2980a55f9f6acad4f411116d858b43a343c674f46260c2e7c1f02ca8bdf6e17', '000F1A30': 'c11ff87f7093688c1d6ef4f4aefb4e96f920dd73da55389b7a1818741b77c56a', '00146320': '3e76e1161f95dd15d28b2ab5da8a51cc01590ad1ca4a651e6540228285bc8111'}
    for address, expected in hashes.items():
        name = address.encode()
        if re.search(rb'void sub_' + name + rb'_vadb_original\(void\)\s*\{', data):
            raise ValueError('Actor original already routed ' + address)
        match = re.search(rb'void sub_' + name + rb'\(void\)\s*\{', data)
        if not match:
            continue
        end = data.index(b'\n}\n', match.start()) + 3
        body = data[match.start():end]
        if hashlib.sha256(body).hexdigest() != expected:
            raise ValueError('Unreviewed actor original ' + address)
        original = b'void sub_' + name + b'(void)'
        replacement = b'__attribute__((section("guest_code"))) void sub_' + name + b'_vadb_original(void)'
        data = data[:match.start()] + body.replace(original, replacement, 1) + data[end:]
    return data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('image', nargs='?', choices=IMAGES)
    parser.add_argument('--list-images', action='store_true', help='List supported image keys and original filenames without generating')
    parser.add_argument('--resolve-image', choices=IMAGES, help='Resolve one whitelisted filename without generating')
    parser.add_argument('--recomp-only', action='store_true', help='Reuse the existing parsed image and disassembly')
    args = parser.parse_args()
    if args.list_images:
        for key,name in IMAGES.items():print(f'{key}\t{name}')
        return
    if args.resolve_image:
        print(IMAGES[args.resolve_image]);return
    if not args.image:parser.error('an image is required unless listing/resolving')
    xbe = ROOT / 'assets' / IMAGES[args.image]
    output = HERE / 'analysis' / args.image
    output.mkdir(parents=True, exist_ok=True)
    gen = HERE / 'src/recomp' / args.image
    gen.mkdir(parents=True, exist_ok=True)

    def run(stage, options):
        log = ROOT / 'reports' / f'native-{args.image}-{stage}.log'
        log.parent.mkdir(parents=True, exist_ok=True)
        command = [sys.executable, '-m', f'tools.{stage}', str(xbe), *map(str, options)]
        print('Running', stage, '→', log, flush=True)
        with log.open('w') as stream:
            subprocess.run(command, cwd=SDK, stdout=stream, stderr=subprocess.STDOUT, check=True)

    if not args.recomp_only:
        run('xbe_parser', ['--json', output/'xbe.json'])
    disasm_options = ['--analysis-json', output/'xbe.json', '-o', output/'disasm']
    seeds = HERE/'config'/f'{args.image}-seeds.json'
    if seeds.is_file():
        disasm_options += ['--seed-functions', seeds, '--force']
    if not args.recomp_only:
        run('disasm', disasm_options)
        run('func_id', ['--functions',output/'disasm/functions.json',
                    '--strings',output/'disasm/strings.json',
                    '--xrefs',output/'disasm/xrefs.json','-o',output/'func_id'])
        run('abi_analysis', ['--functions',output/'disasm/functions.json',
                         '--func-id-dir',output/'func_id','--output-dir',output/'abi'])
    with tempfile.TemporaryDirectory(prefix='topspin-gen-') as staged:
        staged = Path(staged)
        hook_sources = {
            'game': [HERE/'src/timing_hooks.c', HERE/'src/video_hooks.c', HERE/'src/replay_sample_hooks.c', HERE/'src/game_canvas_hooks.c', HERE/'src/game_rumble_hooks.c', HERE/'src/game_scene_lifecycle_hooks.c', HERE/'src/prompt_hooks_game.c'],
            'menu': [HERE/'src/menu_canvas_hooks.c', HERE/'src/prompt_hooks_menu.c', HERE/'src/menu_pc_entries.c', HERE/'src/menu_career_save_hooks.c', HERE/'src/menu_loading_ball_hooks.c'],
            'career': [HERE/'src/career_canvas_hooks.c', HERE/'src/prompt_hooks_career.c'],
            'training': [HERE/'src/training_canvas_hooks.c', HERE/'src/training_rumble_hooks.c', HERE/'src/prompt_hooks_training.c'],
            'playercreator': [HERE/'src/playercreator_canvas_hooks.c', HERE/'src/playercreator_career_save_hooks.c', HERE/'src/creator_appearance_import.c', HERE/'src/creator_input_observer.c', HERE/'src/playercreator_preview_hooks.c', HERE/'src/playercreator_material_read_trace.c', HERE/'src/playercreator_material_tracking_hooks.c', HERE/'src/playercreator_material_export_hooks.c', HERE/'src/playercreator_save_operation_hooks.c', HERE/'src/prompt_hooks_playercreator.c'],
            'shop': [HERE/'src/shop_canvas_hooks.c', HERE/'src/shop_preview_hooks.c', HERE/'src/prompt_hooks_shop.c'],
            'online': [HERE/'src/online_canvas_hooks.c', HERE/'src/prompt_hooks_online.c'],
        }.get(args.image, [])
        hook_text = '\n'.join(p.read_text() for p in hook_sources)
        wrapped = manual_wrapped_addresses(hook_text)
        manual_options = []
        if hook_sources:
            # The SDK scans one file; this is scan input only, never compiled.
            scan_file = output/'recomp/topspin-manual-hooks.c'
            scan_file.parent.mkdir(parents=True, exist_ok=True)
            scan_file.write_text(hook_text)
            manual_options = ['--exclude-manual', scan_file]
        run('recomp', ['--all','--game-name','Max Spin - djultra64',
                   '--disasm-dir',output/'disasm','--func-id-dir',output/'func_id',
                   '--abi-dir',output/'abi','-o',output/'recomp',
                   '--split','250','--gen-dir',staged, *manual_options])
        produced = {p.name for p in staged.iterdir() if p.is_file()}
        for source in staged.iterdir():
            if source.is_file():
                target = gen/source.name
                data = source.read_bytes()
                if source.name.endswith('.c'):
                    data = route_fence_writes(data.decode('utf-8'), args.image).encode('utf-8')
                if args.image == 'playercreator' and source.name.endswith('.c'):
                    data = route_creator_save_operation(data)
                data = route_observational_wrappers(data, source.name, wrapped)
                if args.image == 'game':
                    data = route_pb_publication_observers(data)
                    data = route_actor_binding_wrappers(data)
                if args.image == 'playercreator':
                    data = route_creator_material_reservation_observer(data)
                if source.name == 'recomp_funcs.h':
                    # The POSIX scheduler may preempt generated guest code,
                    # but must defer while inside allocator/graphics/MMIO code.
                    data = re.sub(rb'^(void sub_[0-9A-Fa-f]+(?:_gen)?\(void\);)$',
                                  rb'__attribute__((section("guest_code"))) \1',
                                  data, flags=re.MULTILINE)
                if source.name == 'recomp_stubs_unresolved.c':
                    # A missing function must stop this compatibility probe.
                    # Returning guessed values can corrupt later game state.
                    data = re.sub(rb'void (sub_([0-9A-Fa-f]{8}))\(void\) \{[^\n}]*\}',
                                  rb'void \1(void) { recomp_icall_fail_log(0x\2u); }',
                                  data)
                if not target.exists() or target.read_bytes() != data:
                    target.write_bytes(data)
        for obsolete in gen.iterdir():
            if obsolete.is_file() and obsolete.name not in produced:
                obsolete.unlink()
    if not list(gen.glob('*.c')) or not (gen/'recomp_types.h').is_file():
        raise RuntimeError('The recompiler did not produce buildable translation units')
    manifest = {'image': args.image,'xbe':xbe.name,'sha256':hashlib.sha256(xbe.read_bytes()).hexdigest(),
                'patches':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((HERE/'patches').glob('*.patch'))},
                'runtime':'vyanhursky/xboxrecomp','commit':sdk_revision()}
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('Pipeline complete; this does not prove runtime compatibility.', flush=True)


if __name__ == '__main__':
    main()
