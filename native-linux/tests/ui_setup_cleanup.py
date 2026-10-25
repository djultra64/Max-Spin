"""A failed private UI setup must never chmod linked original directories. djultra64."""
from pathlib import Path
import sys,tempfile,stat
N=Path(__file__).resolve().parents[1];sys.path.insert(0,str(N/'tools'))
from setup_game import clean_tree
with tempfile.TemporaryDirectory()as folder:
 root=Path(folder);original=root/'original';original.mkdir(mode=0o755)
 (original/'keep').write_text('original')
 private=root/'private';private.mkdir();(private/'view').symlink_to(original,target_is_directory=True)
 mode=stat.S_IMODE(original.stat().st_mode)
 clean_tree(private)
 assert not private.exists()and original.is_dir()and (original/'keep').read_text()=='original'
 assert stat.S_IMODE(original.stat().st_mode)==mode,'cleanup changed original directory permissions'
print('PASS private resource cleanup preserves linked originals and permissions')
