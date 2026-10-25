"""Windows private layout preparation never needs links or copies the disc. djultra64."""
from pathlib import Path
import sys,tempfile,json
N=Path(__file__).resolve().parents[1];sys.path.insert(0,str(N/'tools'))
import ui_layout
# A links-only implementation would fail even without Windows developer mode.
with tempfile.TemporaryDirectory()as folder:
 out=Path(folder)/'private'
 report=ui_layout.prepare(N.parent/'assets',out,asset_view=False)
 assert report['footer_nodes']==32 and report['intro_fixed']
 assert not (out/'view').exists(),'Windows cache must contain only modified archives'
 assert not any(p.is_symlink()for p in out.rglob('*'))
 assert {p.relative_to(out/'patches').as_posix()for p in (out/'patches').rglob('*.rui')}==set(report['archives'])
 assert set(p.name for p in out.iterdir())=={'manifest.json','patches'}
 assert ui_layout.resource_header(report)==(N/'src/ui_layout_resources.h').read_bytes(),'compiled Windows identities must match authenticated private output'
 ui_layout.discard_tree(out)
print('PASS Windows overlay: 32 footer placements, exact intro correction, patched archives only, no links/admin/original copies, compiled identities match')
