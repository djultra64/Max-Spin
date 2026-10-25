#!/usr/bin/env python3
"""Read-only first-start readiness checks; no game or build work. djultra64."""
import argparse,json,os
from pathlib import Path

OUTPUTS={
 'menu-pc-v4':('newmain.scene','newmain.sceneData','main_optionrules.sceneData','manifest.json'),
 'sprite-ui-v1-menu':('pc_cross.xpr','readystart.xpr','shop_slider.xpr','manifest.json'),
 'controller-prompts-v4-extended9-hud-v1':tuple(name+'/manifest.txt'for name in ('menu','game','career','training','playercreator','shop','online'))+('game/HudEng_Attitude.xpr','game/HudFre_Attitude.xpr','game/HudGer_Attitude.xpr')
}

def check(root,build):
 root,build=Path(root),Path(build);native=root/'native-linux'
 try:
  images=json.loads((native/'config/images.json').read_text())
  if not isinstance(images,dict)or len(images)!=9 or any(not isinstance(v,str)or Path(v).name!=v for v in images.values()):raise ValueError('image list')
 except (OSError,ValueError,TypeError):
  return ['Esta copia de Max Spin está incompleta. Vuelve a extraer el paquete completo en una carpeta nueva.']
 issues=[]
 assets=root/'assets'
 missing=[v for v in images.values()if not (assets/v).is_file()]
 if missing or not (assets/'Media').is_dir():
  issues.append('Faltan los archivos originales de Top Spin para Xbox. Si tienes una ISO, extrae su contenido completo en la carpeta assets, incluidos Media y los archivos .xbe. Esta versión no abre la ISO directamente.')
 executables=[build/('topspin_'+image)for image in images]+[build/'topspin_window_host']
 if any(not p.is_file()or not os.access(p,os.X_OK)for p in executables):
  issues.append('Esta copia todavía no está compilada o está incompleta. Descarga el paquete Linux precompilado completo, o sigue docs/build.md si quieres compilar desde el código fuente.')
 else:
  manifest=build/'manifest.json'
  ready=False
  try:
   if manifest.stat().st_size<=2*1024*1024:
    data=json.loads(manifest.read_text())
    ready=isinstance(data,dict)and type(data.get('ui_cache_version'))is int and data['ui_cache_version']==4 and type(data.get('controller_cache_version'))is int and data['controller_cache_version']==4 and all((native/'cache'/name/file).is_file()for name,files in OUTPUTS.items()for file in files)
    if ready and 'ui_layout_cache_version'in data:
     layout=native/'cache/ui-layout-v1'
     ready=type(data['ui_layout_cache_version'])is int and data['ui_layout_cache_version']==1 and (layout/'manifest.json').is_file()and (layout/'view').is_dir()
  except (OSError,ValueError):pass
  if not ready:
   issues.append('Falta preparar los recursos para el primer inicio. Después de añadir tus archivos originales, sigue el paso de preparación de docs/install.md. No necesitas recompilar el juego si descargaste el paquete precompilado.')
 return issues

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--root',type=Path,required=True);parser.add_argument('--build-dir',type=Path,required=True)
 args=parser.parse_args();issues=check(args.root,args.build_dir)
 if issues:
  print('Max Spin necesita completar estos pasos antes de iniciar:\n\n'+'\n\n'.join(issues)+'\n\nGuía: '+str(args.root/'docs/install.md'))
  return 2
 return 0
if __name__=='__main__':raise SystemExit(main())
