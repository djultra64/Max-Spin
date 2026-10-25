"""CPU contract for the actual Windows RUI overlay path selector. djultra64."""
from pathlib import Path
import argparse,subprocess,tempfile
R=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--baseline',action='store_true');args=p.parse_args()
source=r'''
#include <stdio.h>
#include <string.h>
#include "ui_layout_resources.h"
extern int topspin_ui_layout_disc_index(const char*) __attribute__((weak));
static int select(const char *path){return topspin_ui_layout_disc_index?topspin_ui_layout_disc_index(path):-1;}
int main(void){
 const char *good[]={"d:\\Media\\Rui\\MatchIntro.rui","D:\\MEDIA/RUI/matchintro.RUI","\\Device\\CdRom0\\Media\\Rui\\MatchIntro.rui","\\??\\D:\\Media\\Rui\\MatchIntro.rui"};
 for(unsigned i=0;i<sizeof good/sizeof good[0];i++){int k=select(good[i]);if(k<0||strcmp(topspin_ui_layout_resources[k].path,"Media/Rui/MatchIntro.rui")){fprintf(stderr,"Missing exact private RUI route: %s\n",good[i]);return 1;}}
 const char *bad[]={NULL,"","d:","d:\\","d:\\Media\\Rui\\MatchIntro.rui.extra","d:\\Media\\Rui\\..\\Rui\\MatchIntro.rui","u:\\Media\\Rui\\MatchIntro.rui","\\Device\\Harddisk0\\Partition1\\Media\\Rui\\MatchIntro.rui","d:\\Media\\Rui\\TrainingIntro.rui","d:\\TopSpin(Game).xbe","d:\\Media\\Loadings\\LoadEng00.jpg"};
 for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++)if(select(bad[i])!=-1){fputs("unrelated game/save path redirected\n",stderr);return 1;}
 puts("PASS exact original disc RUI selector: drive/device aliases, case/separators, unchanged resources, executables, saves and traversal");return 0;
}
'''
with tempfile.TemporaryDirectory()as folder:
 t=Path(folder);(t/'test.c').write_text(source)
 cmd=['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(R/'native-linux/src'),str(t/'test.c')]
 if not args.baseline:cmd.append(str(R/'windows/native/src/ui_layout_overlay.c'))
 subprocess.run(cmd+['-o',str(t/'test')],check=True);subprocess.run([str(t/'test')],check=True)
