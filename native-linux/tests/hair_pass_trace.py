#!/usr/bin/env python3
"""Production read-only wrappers and actual linker seams; no GPU/game. djultra64."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'native-linux/src'
with tempfile.TemporaryDirectory(prefix='hair-pass-cpu-') as temporary:
    folder = Path(temporary)
    # Separate object files reproduce actual cross-TU references. The material
    # original is renamed explicitly because same-TU --wrap is insufficient.
    symbols = [('sub_00079AE0', 'manager'), ('sub_00085450', 'model'),
               ('sub_0006BCA0', 'rigid'), ('sub_00068AE0_hair_trace_original', 'material'),
               ('sub_00086810', 'selector')]
    originals = []
    for symbol, role in symbols:
        path = folder / f'{role}.c'
        path.write_text(f'extern void fixture_original_{role}(void);\nvoid {symbol}(void){{fixture_original_{role}();}}\n')
        originals.append(str(path))
    binary = folder / 'fixture'
    command = [os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror',
               '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
               '-DTOPSPIN_HAIR_PASS_MANAGER_LIMIT=6', '-DTOPSPIN_HAIR_PASS_SNAPSHOT_LIMIT=6',
               '-I'+str(SRC), '-I'+str(SRC/'recomp/game'),
               str(ROOT/'native-linux/tests/hair_pass_trace.c'), str(SRC/'hair_pass_trace.c'),
               str(SRC/'actor_binding_cpu_guard.c'), *originals,
               '-Wl,--wrap=sub_00079AE0', '-Wl,--wrap=sub_00085450', '-Wl,--wrap=sub_0006BCA0', '-Wl,--wrap=sub_00086810',
               '-lm', '-o', str(binary)]
    subprocess.run(command, check=True)
    clean = {key:value for key,value in os.environ.items() if not key.startswith('TOPSPIN_HAIR_PASS_TRACE')}
    def run(mode, enabled=None, minimum=None):
        environment = dict(clean)
        if enabled is not None:
            environment['TOPSPIN_HAIR_PASS_TRACE'] = enabled
        if minimum is not None:
            environment['TOPSPIN_HAIR_PASS_TRACE_MIN_COUNTER'] = minimum
        result = subprocess.run([str(binary), mode], text=True, capture_output=True, env=environment)
        if result.returncode:
            print(result.stdout);print(result.stderr);result.check_returncode()
        assert result.stdout.startswith('PASS ')
        return result
    for value in (None, '', '0', 'true', '1garbage'):
        assert not run('invalid', value).stderr  # inaccessible guest memory is untouched while off
    assert not run('invalid', '1').stderr  # checked pointer rejection also works when on
    for mode in ('good', 'bad', 'nested', 'skip', 'dedup', 'budget', 'duplicate', 'gateprogram', 'gatecontext'):
        off, on = run(mode), run(mode, '1')
        assert off.stdout == on.stdout, (mode, off.stdout, on.stdout)  # exact original memory/CPU effects
        assert not off.stderr
        summaries = [line for line in on.stderr.splitlines() if '[HAIR-PASS] summary=' in line]
        details = [line for line in on.stderr.splitlines() if '[HAIR-PASS] detail=' in line]
        assert 1 <= len(summaries) <= 6 and 1 <= len(details) <= 6
        assert all('author=djultra64' in line for line in summaries+details)
        assert all('selected_first=2 selected_last=2' in line for line in summaries)
        if mode == 'duplicate':
            assert all('unique_lod4294967295/tier_mask06' in line for line in summaries)
        else:
            assert all('unique_lod2/tier_mask04' in line for line in summaries)
        if mode == 'good':
            assert 'rigid=1 material=1' in summaries[0]
            assert 'model_pre_boundsEC_118=' in details[0] and 'argument_matrix=40400000,' in details[0]
            assert 'rigid_post_frustum30C550=3F800000,' in details[0]
            assert 'material_this=00030004,00000000' in details[0]
            assert 'material_enabled7D_leaf81=00000001,00000000' in details[0]
            assert 'material_argument_context=00050000,00000000' in details[0]
            assert 'material_context6C=00000002,00000000' in details[0]
        if mode == 'gateprogram':
            assert 'material_enabled7D_leaf81=00000000,00000000' in details[0]
        if mode == 'gatecontext':
            assert 'material_context6C=00000003,00000000' in details[0]
        if mode == 'bad':
            assert 'rigid=1 material=0' in summaries[0] and 'hidden0>1' in summaries[0]
        if mode == 'nested':
            assert len(summaries) == 1 and 'model_calls=1 heads=1 rigid=1 material=1' in summaries[0]
        if mode == 'skip':
            assert 'rigid=0 material=0' in summaries[0]
        if mode == 'dedup':
            assert len(summaries) == 6 and len(details) == 4  # two per actual model-pass signature
        if mode == 'budget':
            assert len(summaries) == 6 and len(details) == 6
            assert 'calls=30/30/30/15' in on.stdout
    assert not run('good', '1', '124').stderr
    assert run('good', '1', '123').stderr
    print('PASS production wrappers: exact opt-in, checked reads, all represented CPU/fenv preserved; originals exactly once, cross-TU linker interception, nested scope isolation, actual selector return with duplicate-tier ambiguity, read-only memory equality, dedup and hard budgets; no GPU/game')
