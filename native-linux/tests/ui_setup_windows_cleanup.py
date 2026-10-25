"""Read-only private files must not break Windows setup finalization. djultra64."""
from pathlib import Path
import sys,tempfile,stat
N=Path(__file__).resolve().parents[1];sys.path.insert(0,str(N/'tools'))
from setup_game import clean_tree
from ui_layout import discard_tree
with tempfile.TemporaryDirectory()as folder:
 root=Path(folder);original=root/'original';original.mkdir();keep=original/'keep';keep.write_bytes(b'original bytes')
 before=stat.S_IMODE(keep.stat().st_mode)
 for cleanup in (clean_tree,discard_tree):
  private=root/'private';(private/'patches').mkdir(parents=True)
  (private/'patches/derived.rui').write_bytes(b'private bytes');(private/'patches/derived.rui').chmod(0o444)
  (private/'manifest.json').write_text('{}');(private/'manifest.json').chmod(0o444)
  (private/'patches').chmod(0o555);private.chmod(0o555)
  cleanup(private)
  assert not private.exists()and keep.read_bytes()==b'original bytes'
  assert stat.S_IMODE(keep.stat().st_mode)==before
print('PASS private read-only file cleanup for setup rollback/finalization and layout failure; original bytes/permissions preserved')
