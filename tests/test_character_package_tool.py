"""Private character authoring CLI contracts. Own work: djultra64."""
import contextlib
import hashlib
import importlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'native-linux/tools'))


class AuthoringToolTests(unittest.TestCase):
    def tool(self):
        try:
            return importlib.import_module('character_package_tool')
        except ImportError:
            self.fail('character package authoring tool is missing')

    def test_template_seeds_two_private_outfits_without_activating_or_overwriting(self):
        tool = self.tool()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            assets = root / 'assets'
            originals = assets / 'Media/Wads/Players'
            originals.mkdir(parents=True)
            for outfit in (0, 1):
                (originals / f'HingisX{outfit:02d}.wad').write_bytes(f'original outfit {outfit}'.encode())
            package = tool.create_template(assets, root / 'drafts', 17, 'Future woman')
            self.assertEqual(package, root / 'drafts/17')
            self.assertFalse((package / 'manifest.json').exists())
            manifest = json.loads((package / 'manifest.template.json').read_text())
            self.assertEqual(manifest['target'], 'HingisX')
            self.assertEqual(manifest['gender'], 'female')
            self.assertFalse(manifest['compatibility']['validated'])
            self.assertEqual(manifest['author'], 'djultra64')
            self.assertEqual((package / 'outfit0.wad').read_bytes(), b'original outfit 0')
            self.assertEqual(manifest['files']['Media/Wads/Players/HingisX01.wad']['sha256'], hashlib.sha256(b'original outfit 1').hexdigest())
            with self.assertRaises(FileExistsError):
                tool.create_template(assets, root / 'drafts', 17, 'Overwrite')
            self.assertEqual((originals / 'HingisX00.wad').read_bytes(), b'original outfit 0')

    def test_template_uses_the_native_case_sensitive_rios_archive_paths(self):
        tool = self.tool()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            originals = root / 'assets/Media/Wads/Players'
            originals.mkdir(parents=True)
            for outfit in (0, 1):
                (originals / f'Riosxxx{outfit:02d}.wad').write_bytes(b'original')
            package = tool.create_template(root / 'assets', root / 'drafts', 14, 'New athlete')
            manifest = json.loads((package / 'manifest.template.json').read_text())
            self.assertIn('Media/Wads/Players/Riosxxx00.wad', manifest['files'])

    def test_refresh_binds_typed_shared_resources_and_hashes_without_self_certifying(self):
        tool = self.tool()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            originals = root / 'assets/Media/Wads/Players'
            originals.mkdir(parents=True)
            for outfit in (0, 1):
                (originals / f'TukurXX{outfit:02d}.wad').write_bytes(b'original')
            package = tool.create_template(root / 'assets', root / 'drafts', 9, 'New athlete')
            path = package / 'manifest.template.json'
            manifest = json.loads(path.read_text())
            manifest['pml_updates'] = [{'archive':'Media/Wads/Db.wad', 'member':'media/db/players(m).pml',
                'node':'TRPlayer/Id=15', 'property':'Name', 'value':'New athlete'}]
            (root / 'assets/Media/Wads/Db.wad').write_bytes(b'original database')
            path.write_text(json.dumps(manifest))
            (package / 'outfit0.wad').write_bytes(b'edited native outfit')
            tool.refresh_hashes(package, root / 'assets')
            revised = json.loads(path.read_text())
            self.assertEqual(revised['base_files']['Media/Wads/Db.wad'], hashlib.sha256(b'original database').hexdigest())
            self.assertEqual(revised['files']['Media/Wads/Players/TukurXX00.wad']['sha256'], hashlib.sha256(b'edited native outfit').hexdigest())
            self.assertFalse(revised['compatibility']['validated'])
            self.assertFalse((package / 'manifest.json').exists())
            revised['files']['Media/Wads/Db.wad'] = {'path':'outfit0.wad', 'sha256':'0'*64}
            path.write_text(json.dumps(revised))
            with self.assertRaises(ValueError):
                tool.refresh_hashes(package, root / 'assets')

    def test_validation_cli_reports_invalid_slot_and_returns_failure_without_launching(self):
        self.tool()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'assets').mkdir()
            (root / 'characters/17').mkdir(parents=True)
            (root / 'characters/17/manifest.json').write_text('{invalid')
            result = subprocess.run([sys.executable, str(ROOT/'native-linux/tools/character_package_tool.py'),
                                     'validate', '--assets', str(root/'assets'), '--packages', str(root/'characters')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            report = json.loads(result.stdout)
            self.assertFalse(report['valid'])
            self.assertEqual(report['rejected'][0]['slot'], '17')


if __name__ == '__main__':
    unittest.main()
