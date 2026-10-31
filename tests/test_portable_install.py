"""Installation confinement and launch contracts. Own work: djultra64."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import hashlib
import types

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))


def portable_fixture(root, *, runtime=b'fixture'):
    """Small real distribution and XDVDFS image for transaction regressions."""
    from test_xdvdfs import image
    payload = root/'download'
    payload.mkdir()
    setup = b'def prepare(assets,build,cache,art,**kwargs):\n (cache/"ready").write_text("yes")\n'
    content = {
        'portable-runtime/MaxSpin-Setup': runtime,
        'native-linux/tools/setup_game.py': setup,
        'native-linux/config/original-images.json': json.dumps({
            'default.xbe': hashlib.sha256(b'hello').hexdigest()}).encode(),
    }
    for relative, data in content.items():
        path = payload/relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    (payload/'RELEASE_MANIFEST.json').write_text(json.dumps({
        'platform': 'linux',
        'files': {name: hashlib.sha256(data).hexdigest() for name, data in content.items()},
    }))
    return payload, image(root/'game.iso')


class PortableInstall(unittest.TestCase):
    def test_destination_changed_after_validation_preserves_unrelated_files(self):
        from portable.install import install
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload, iso = portable_fixture(root)
            destination = root/'selected'
            destination.mkdir()
            foreign = destination/'user-document.txt'
            def concurrent_change():
                # The cancellation hook runs just after destination validation.
                # Represent a user's file arriving during payload validation.
                foreign.write_text('keep my document')
                return False
            with self.assertRaises(ValueError):
                install(iso, destination, payload, cancelled=concurrent_change)
            self.assertTrue(foreign.exists(), 'rollback deleted a file the installer did not create')
            self.assertEqual(foreign.read_text(), 'keep my document')
            self.assertEqual({p.name for p in destination.iterdir()}, {'user-document.txt'})
            self.assertFalse((destination/'portable-install.json').exists())

    def test_cancel_during_payload_hashing_never_starts_copying(self):
        from portable.install import install
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload, iso = portable_fixture(root, runtime=b'x'*(3*1024*1024))
            checks, stages = [], []
            def cancelled():
                checks.append(True)
                return len(checks) >= 4
            destination = root/'cancelled'
            with self.assertRaises(InterruptedError):
                install(iso, destination, payload, cancelled=cancelled,
                        progress=lambda stage, *args: stages.append(stage))
            self.assertNotIn('copy', stages, 'cancellation waited until after payload validation')
            self.assertFalse(destination.exists())
            self.assertFalse((destination/'portable-install.json').exists())

    def test_changed_payload_is_rejected_before_running_copied_helpers(self):
        from portable.install import install
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload, iso = portable_fixture(root)
            changed = []
            def mutate_source(stage, *args):
                if stage == 'copy' and not changed:
                    changed.append(True)
                    (payload/'native-linux/tools/setup_game.py').write_text(
                        'raise AssertionError("executed changed preparation code")\n')
            destination = root/'installed'
            with self.assertRaisesRegex(ValueError, 'dañado|cambió'):
                install(iso, destination, payload, progress=mutate_source)
            self.assertFalse((destination/'portable-install.json').exists())
            self.assertFalse(destination.exists())

    def test_already_copied_helper_changed_later_is_not_executed(self):
        from portable.install import install
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload, iso = portable_fixture(root)
            destination = root/'installed'
            changed = []
            def mutate_copy(stage, *args):
                if (stage == 'copy' and not changed
                        and (destination/'native-linux/config/original-images.json').exists()):
                    changed.append(True)
                    (destination/'native-linux/tools/setup_game.py').write_text(
                        'raise AssertionError("executed corrupted installed helper")\n')
            with self.assertRaisesRegex(ValueError, 'dañado|cambió'):
                install(iso, destination, payload, progress=mutate_copy)
            self.assertTrue(changed)
            self.assertFalse((destination/'portable-install.json').exists())
            self.assertFalse(destination.exists())

    def test_package_corrupted_during_preparation_cannot_publish_completion_marker(self):
        from portable.install import install
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload, iso = portable_fixture(root)
            setup = payload/'native-linux/tools/setup_game.py'
            setup.write_text('def prepare(assets,build,cache,art,**kwargs):\n'
                             ' (assets.parent/"portable-runtime/MaxSpin-Setup").write_bytes(b"corrupted")\n')
            manifest_path = payload/'RELEASE_MANIFEST.json'
            manifest = json.loads(manifest_path.read_text())
            manifest['files']['native-linux/tools/setup_game.py'] = hashlib.sha256(setup.read_bytes()).hexdigest()
            manifest_path.write_text(json.dumps(manifest))
            destination = root/'installed'
            with self.assertRaisesRegex(ValueError, 'dañado|cambió'):
                install(iso, destination, payload)
            self.assertFalse((destination/'portable-install.json').exists())
            self.assertFalse(destination.exists())

    def test_retry_preparation_reloads_installed_modules_and_restores_prior_imports(self):
        from portable.install import prepare_resources
        helper_name = 'portable_fixture_helper'
        original_helper = sys.modules.get(helper_name)
        prior = types.ModuleType(helper_name)
        prior.read_proof = lambda: 'wrong module from the original environment'
        sys.modules[helper_name] = prior
        original_path = sys.path[:]
        original_bytecode = sys.dont_write_bytecode
        try:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                roots = [root/'failed', root/'retried']
                for index, installed in enumerate(roots):
                    tools = installed/'native-linux/tools'
                    tools.mkdir(parents=True)
                    cache = installed/'native-linux/cache'
                    cache.mkdir()
                    (installed/'native-linux/proof.txt').write_text(str(index))
                    (tools/(helper_name+'.py')).write_text(
                        'from pathlib import Path\nNATIVE = Path(__file__).resolve().parents[1]\n'
                        'def read_proof(): return (NATIVE/"proof.txt").read_text()\n')
                    (tools/'setup_game.py').write_text(
                        'from portable_fixture_helper import read_proof\n'
                        'def prepare(assets, build, cache, art, **kwargs):\n'
                        ' value = read_proof()\n'
                        ' if value == "0": raise ValueError("first attempt failed")\n'
                        ' (cache/"proof").write_text(value)\n')
                with self.assertRaisesRegex(ValueError, 'first attempt failed'):
                    prepare_resources(roots[0], 'linux')
                self.assertIs(sys.modules[helper_name], prior)
                shutil.rmtree(roots[0])
                prepare_resources(roots[1], 'linux')
                self.assertEqual((roots[1]/'native-linux/cache/proof').read_text(), '1')
                self.assertIs(sys.modules[helper_name], prior)
                self.assertEqual(sys.path, original_path)
                self.assertEqual(sys.dont_write_bytecode, original_bytecode)
                self.assertFalse(list(root.rglob('__pycache__')))
        finally:
            if original_helper is None:
                sys.modules.pop(helper_name, None)
            else:
                sys.modules[helper_name] = original_helper

    def test_relocatable_layout_rejects_foreign_link_without_changing_its_target(self):
        from portable.install import _relocatable_layout
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            installed = root/'installed'
            cache = installed/'native-linux/cache'
            cache.mkdir(parents=True)
            foreign = root/'user-data'
            foreign.mkdir()
            manifest = foreign/'manifest.json'
            manifest.write_text('{"source":"keep original"}')
            before = foreign.stat().st_mode
            try:
                (cache/'ui-layout-v1').symlink_to(foreign, target_is_directory=True)
            except OSError:
                self.skipTest('symlinks unavailable for current account')
            with self.assertRaises(ValueError):
                _relocatable_layout(installed)
            self.assertEqual(manifest.read_text(), '{"source":"keep original"}')
            self.assertEqual(foreign.stat().st_mode, before)

    def test_launch_uses_only_install_local_state_and_bundled_tools(self):
        from portable.install import launch_command
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'Movido con espacios'
            root.mkdir()
            (root/'portable-install.json').write_text('{"schema":1,"platform":"linux"}')
            command, env, cwd = launch_command(root, platform='linux')
            self.assertEqual(command[:3], [str(root/'portable-runtime/MaxSpin-Setup'), '--tool', str(root/'native-linux/launch.py')])
            self.assertNotIn('python', command[0].lower())
            self.assertEqual(cwd, root)
            self.assertEqual(env['TOPSPIN_GRAPHICS_INI'], str(root/'Settings/graphics.ini'))
            self.assertEqual(env['MAX_SPIN_PORTABLE_ROOT'], str(root))
            for key in ('TMPDIR','TEMP','TMP','XDG_CACHE_HOME','XDG_CONFIG_HOME','XDG_DATA_HOME'):
                self.assertTrue(Path(env[key]).is_relative_to(root), key)
            self.assertEqual(command[command.index('--saves')+1], str(root/'Saves'))
            self.assertEqual(env['RECOMP_SHADER_CACHE'], str(root/'Cache/Shaders'))

    def test_windows_launch_defaults_are_also_portable_after_relocation(self):
        from portable.install import launch_command
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)/'Max Spin';root.mkdir()
            (root/'portable-install.json').write_text('{"schema":1,"platform":"windows"}')
            command,env,cwd=launch_command(root, platform='windows')
            self.assertEqual(command[0],str(root/'MaxSpin.exe'))
            self.assertEqual(command[command.index('--resources')+1],str(root/'native-linux/cache'))
            self.assertEqual(command[command.index('--saves')+1],str(root/'Saves'))
            self.assertEqual(env['MAX_SPIN_PYTHON'],str(root/'portable-runtime/MaxSpin-Setup.exe'))
            self.assertEqual(env['LOCALAPPDATA'],str(root/'Cache/LocalAppData'))

    def test_destination_inside_payload_and_existing_data_are_refused(self):
        from portable.install import validate_destination
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);payload=root/'payload';payload.mkdir()
            with self.assertRaises(ValueError): validate_destination(payload/'nested',payload)
            existing=root/'old';existing.mkdir();(existing/'keep').write_text('keep')
            with self.assertRaises(ValueError): validate_destination(existing,payload)
            self.assertEqual((existing/'keep').read_text(),'keep')
            empty=root/'empty';empty.mkdir()
            self.assertEqual(validate_destination(empty,payload),empty)

    def test_preparation_uses_installed_helpers_not_external_interpreter(self):
        from portable.install import prepare_resources
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);tools=root/'native-linux/tools';tools.mkdir(parents=True)
            (tools/'setup_game.py').write_text('from pathlib import Path\ndef prepare(assets,build,cache,art,**kwargs):\n (cache/"proof").write_text(str(assets))\n')
            (root/'native-linux/cache').mkdir()
            prepare_resources(root,platform='linux')
            self.assertEqual((root/'native-linux/cache/proof').read_text(),str(root/'assets'))

    def test_installs_from_iso_and_rolls_back_failed_preparation(self):
        from portable.install import install
        from test_xdvdfs import image
        import hashlib
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); payload=root/'download';payload.mkdir()
            setup='from pathlib import Path\ndef prepare(assets,build,cache,art,**kwargs):\n (cache/"ready").write_text("yes")\n'
            content={'portable-runtime/MaxSpin-Setup':b'fixture',
                     'native-linux/tools/setup_game.py':setup.encode(),
                     'native-linux/config/original-images.json':json.dumps({'default.xbe':hashlib.sha256(b'hello').hexdigest()}).encode()}
            for relative,data in content.items():
                path=payload/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
            (payload/'RELEASE_MANIFEST.json').write_text(json.dumps({'platform':'linux','files':{k:hashlib.sha256(v).hexdigest() for k,v in content.items()}}))
            iso=image(root/'game.iso')
            result=install(iso,root/'installed',payload)
            self.assertEqual((result/'assets/default.xbe').read_bytes(),b'hello')
            self.assertEqual((result/'native-linux/cache/ready').read_text(),'yes')
            self.assertEqual(json.loads((result/'portable-install.json').read_text())['platform'],'linux')
            self.assertEqual({p.name for p in root.iterdir()},{'download','game.iso','installed'})
            cancelled=lambda: True
            with self.assertRaises(InterruptedError): install(iso,root/'cancelled',payload,cancelled=cancelled)
            self.assertFalse((root/'cancelled').exists())
            bad=b'def prepare(*a,**k):\n raise ValueError("unsupported game")\n'
            (payload/'native-linux/tools/setup_game.py').write_bytes(bad)
            manifest=json.loads((payload/'RELEASE_MANIFEST.json').read_text());manifest['files']['native-linux/tools/setup_game.py']=hashlib.sha256(bad).hexdigest()
            (payload/'RELEASE_MANIFEST.json').write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError,'unsupported game'): install(iso,root/'failed',payload)
            self.assertFalse((root/'failed').exists())

    def test_linux_ui_cache_remains_valid_after_moving_installation(self):
        # This catches absolute source identity and symlinks retained by the
        # existing resource preparer when the installation is moved.
        import importlib.util
        tools=ROOT/'native-linux/tools';sys.path.insert(0,str(tools))
        import ui_layout
        from portable.install import _relocatable_layout
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)/'first';root.mkdir();assets=root/'assets';assets.mkdir()
            (assets/'default.xbe').write_bytes(b'original')
            cache=root/'native-linux/cache/ui-layout-v1';(cache/'view').mkdir(parents=True)
            (cache/'view/default.xbe').symlink_to(assets/'default.xbe')
            (cache/'manifest.json').write_text(json.dumps({'source':str(assets)}))
            _relocatable_layout(root)
            moved=root.with_name('moved');root.rename(moved)
            self.assertEqual((moved/'native-linux/cache/ui-layout-v1/view/default.xbe').read_bytes(),b'original')
            report=json.loads((moved/'native-linux/cache/ui-layout-v1/manifest.json').read_text())
            self.assertEqual(ui_layout.source_directory(moved/'native-linux/cache/ui-layout-v1',report),moved/'assets')


if __name__ == '__main__': unittest.main()
