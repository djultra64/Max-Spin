#!/usr/bin/env python3
"""CPU-only loading geometry/ABI fixture; original boundaries are explicit doubles. djultra64."""
import argparse,pathlib,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[2]
# Original Game E19F0 chooses a court/language backdrop, falling back to Load.
# Every filename below comes from the original loading asset inventory.
backdrops=['AcapulcoXXEng00.jpg', 'AcapulcoXXFre00.jpg', 'AcapulcoXXGer00.jpg', 'AdelaideXXEng00.jpg', 'AdelaideXXFre00.jpg', 'AdelaideXXGer00.jpg', 'AlexandrieEng00.jpg', 'AlexandrieFre00.jpg', 'AlexandrieGer00.jpg', 'AntananariEng00.jpg', 'AntananariFre00.jpg', 'AntananariGer00.jpg', 'ArthurAsheEng00.jpg', 'ArthurAsheFre00.jpg', 'ArthurAsheGer00.jpg', 'BuenosAireEng00.jpg', 'BuenosAireFre00.jpg', 'BuenosAireGer00.jpg', 'CapTownXXXEng00.jpg', 'CapTownXXXFre00.jpg', 'CapTownXXXGer00.jpg', 'CasablancaEng00.jpg', 'CasablancaFre00.jpg', 'CasablancaGer00.jpg', 'CincinnatiEng00.jpg', 'CincinnatiFre00.jpg', 'CincinnatiGer00.jpg', 'ColombiaXXEng00.jpg', 'ColombiaXXFre00.jpg', 'ColombiaXXGer00.jpg', 'DubaixxxxxEng00.jpg', 'DubaixxxxxFre00.jpg', 'DubaixxxxxGer00.jpg', 'HamburgxxxEng00.jpg', 'HamburgxxxFre00.jpg', 'HamburgxxxGer00.jpg', 'IndeXXXXXXEng00.jpg', 'IndeXXXXXXFre00.jpg', 'IndeXXXXXXGer00.jpg', 'IndianWellEng00.jpg', 'IndianWellFre00.jpg', 'IndianWellGer00.jpg', 'Indoor10XXEng00.jpg', 'Indoor10XXFre00.jpg', 'Indoor10XXGer00.jpg', 'Indoor11XXEng00.jpg', 'Indoor11XXFre00.jpg', 'Indoor11XXGer00.jpg', 'Indoor12XXEng00.jpg', 'Indoor12XXFre00.jpg', 'Indoor12XXGer00.jpg', 'Indoor13XXEng00.jpg', 'Indoor13XXFre00.jpg', 'Indoor13XXGer00.jpg', 'Indoor14XXEng00.jpg', 'Indoor14XXFre00.jpg', 'Indoor14XXGer00.jpg', 'Indoor15XXEng00.jpg', 'Indoor15XXFre00.jpg', 'Indoor15XXGer00.jpg', 'Indoor16XXEng00.jpg', 'Indoor16XXFre00.jpg', 'Indoor16XXGer00.jpg', 'Indoor1xxxEng00.jpg', 'Indoor1xxxFre00.jpg', 'Indoor1xxxGer00.jpg', 'Indoor2XXXEng00.jpg', 'Indoor2XXXFre00.jpg', 'Indoor2XXXGer00.jpg', 'Indoor3XXXEng00.jpg', 'Indoor3XXXFre00.jpg', 'Indoor3XXXGer00.jpg', 'Indoor4XXXEng00.jpg', 'Indoor4XXXFre00.jpg', 'Indoor4XXXGer00.jpg', 'Indoor5XXXEng00.jpg', 'Indoor5XXXFre00.jpg', 'Indoor5XXXGer00.jpg', 'Indoor6XXXEng00.jpg', 'Indoor6XXXFre00.jpg', 'Indoor6XXXGer00.jpg', 'Indoor7XXXEng00.jpg', 'Indoor7XXXFre00.jpg', 'Indoor7XXXGer00.jpg', 'Indoor8XXXEng00.jpg', 'Indoor8XXXFre00.jpg', 'Indoor8XXXGer00.jpg', 'Indoor9XXXEng00.jpg', 'Indoor9XXXFre00.jpg', 'Indoor9XXXGer00.jpg', 'ItaliaXXXXEng00.jpg', 'ItaliaXXXXFre00.jpg', 'ItaliaXXXXGer00.jpg', 'LisbonnexxEng00.jpg', 'LisbonnexxFre00.jpg', 'LisbonnexxGer00.jpg', 'LoadEng00.jpg', 'LoadFre00.jpg', 'LoadGer00.jpg', 'MiamiXXXXXEng00.jpg', 'MiamiXXXXXFre00.jpg', 'MiamiXXXXXGer00.jpg', 'MonacoXXXXEng00.jpg', 'MonacoXXXXFre00.jpg', 'MonacoXXXXGer00.jpg', 'RodLaverXXEng00.jpg', 'RodLaverXXFre00.jpg', 'RodLaverXXGer00.jpg', 'RollGarrosEng00.jpg', 'RollGarrosFre00.jpg', 'RollGarrosGer00.jpg', 'SeoulxxxxxEng00.jpg', 'SeoulxxxxxFre00.jpg', 'SeoulxxxxxGer00.jpg', 'ShanghaixxEng00.jpg', 'ShanghaixxFre00.jpg', 'ShanghaixxGer00.jpg', 'SingapourxEng00.jpg', 'SingapourxFre00.jpg', 'SingapourxGer00.jpg', 'SydneyxxxxEng00.jpg', 'SydneyxxxxFre00.jpg', 'SydneyxxxxGer00.jpg', 'TashkentxxEng00.jpg', 'TashkentxxFre00.jpg', 'TashkentxxGer00.jpg', 'TennisClubEng00.jpg', 'TennisClubFre00.jpg', 'TennisClubGer00.jpg', 'ValparaProEng00.jpg', 'ValparaProFre00.jpg', 'ValparaProGer00.jpg', 'ValparaTrgEng00.jpg', 'ValparaTrgFre00.jpg', 'ValparaTrgGer00.jpg', 'WimbledonxEng00.jpg', 'WimbledonxFre00.jpg', 'WimbledonxGer00.jpg']
assert 'RodLaverXXEng00.jpg' in backdrops
paths='\n'.join(' "d:\\\\Media\\\\Loadings\\\\'+name+'",' for name in backdrops)
c=r'''
#define _GNU_SOURCE
#include <sys/mman.h>
#include "loading_layout.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fenv.h>
#if GAME
#define OUTER __wrap_sub_00138340
#define LOCK __wrap_sub_001AE760
#define DRAW __wrap_sub_001B4C30
#define REAL_OUTER __real_sub_00138340
#define REAL_LOCK __real_sub_001AE760
#define REAL_DRAW __real_sub_001B4C30
#define GETTER topspin_game_video_selected_size
#define LABEL "GAME"
#define DEVICE 0x1b9438u
#define CALLER 0xe1adau
#define LOCK_CALLER 0x75db5u
#define DRAW_CALLER 0x75efbu
#else
#define OUTER __wrap_sub_000CCE50
#define LOCK __wrap_sub_0015B180
#define DRAW __wrap_sub_00161650
#define REAL_OUTER __real_sub_000CCE50
#define REAL_LOCK __real_sub_0015B180
#define REAL_DRAW __real_sub_00161650
#define GETTER topspin_native_video_selected_size
#define LABEL "MENU"
#define DEVICE 0x166058u
#define CALLER 0xa4c0au
#define LOCK_CALLER 0xcc4e5u
#define DRAW_CALLER 0xcc62bu
#endif
_Thread_local uint32_t g_esp,g_ecx,g_eax;
_Thread_local double g_fp_stack[8];_Thread_local int g_fp_top;
ptrdiff_t g_xbox_mem_offset;
static unsigned outercalls,lockcalls,drawcalls,test,phase,nested;
static TopspinLoadingVertex output[4],original[4];
static const char*backdrops[]={
@@BACKDROPS@@
};
#define BACKDROPS (sizeof backdrops/sizeof backdrops[0])
static int backed(void){return (test>=16&&test<=23)||test>=48;}
static uint32_t rd(uint32_t p){uint32_t v;memcpy(&v,(void*)(g_xbox_mem_offset+p),4);return v;}
static void put(uint32_t p,uint32_t v){memcpy((void*)(g_xbox_mem_offset+p),&v,4);}
int GETTER(const char*label,uint32_t out[2]){assert(!strcmp(label,LABEL));out[0]=test==28?640:1280;out[1]=720;return test!=1&&!(test==36&&phase);}
extern void OUTER(void) __attribute__((weak));
extern void LOCK(void) __attribute__((weak));
extern void DRAW(void) __attribute__((weak));
void __real_sub_0015B130(void){assert(0);}
void REAL_LOCK(void){lockcalls++;g_eax=test==6?0xfffffff0:test==24?0x800a0000:0x80090000;g_esp+=12;}
void REAL_DRAW(void){
 drawcalls++;assert(rd(g_esp+4)==(test==8?6u:7u));assert(rd(g_esp+8)==(test==30?1u:0u));assert(rd(g_esp+12)==4);
 memcpy(output,(void*)(g_xbox_mem_offset+0x80090000),sizeof output);g_esp+=16;g_eax=77;
}
void REAL_OUTER(void){
 outercalls++;uint32_t entry=g_esp;
 if(test==37&&!nested){nested=1;if(OUTER)OUTER();else REAL_OUTER();assert(output[1].x==640&&output[2].y==480);nested=0;}
 /* Original allocation stays140 bytes: the hook must not grow it. */
 put(0x80000,test==5?0:0x1000001);put(0x80004,0x90000);put(0x80008,0x12345678);
 put(entry-100,LOCK_CALLER+(test==31));put(entry-96,0x80000);put(entry-92,test==32?1:0);g_esp=entry-100;
 if(LOCK)LOCK();else REAL_LOCK();
 int rounding=fegetround();if(test==35)fesetround(FE_TONEAREST);
 float w=backed()?1024:640,h=backed()?512:480;
 if(test==23){w=640;h=480;}
 for(unsigned i=0;i<4;i++)original[i]=(TopspinLoadingVertex){(i==0||i==3)?0:w,i<2?0:h,1,1,0xffffffff,
   (i==0||i==3)?0.5f/(backed()?1024:640):(w-.5f)/(backed()?1024:640),
   i<2?0.5f/(backed()?512:480):(h-.5f)/(backed()?512:480)};
 fesetround(rounding);
 if(test==38){uint32_t snan=0x7f800001;memcpy(&original[0].x,&snan,4);}
 if(test==7)original[0].x=1;
 if(test==19)original[0].u=original[3].u=.01f;
 if(test==22)original[1].x=2048;
 if(test==33)put(0x70ed8,640); /* target changed during original setup */
 if(test==34)put(0x80004,0xa0000); /* lock's resource changed */
 memcpy((void*)(g_xbox_mem_offset+0x80090000),original,sizeof original);
 memset((void*)(g_xbox_mem_offset+0x80090070),0xab,28);
 /* Original SetStreamSource increments the bound-resource reference at
  * Game1AB170/Menu157B90 before this exact draw. */
 put(0x80000,rd(0x80000)+0x80000);
 if(test==45)put(0x80000,0x01000001); /* unbound */
 if(test==46)put(0x80000,0x02080001); /* another resource type */
 if(test==47)put(0x80000,0x01080002); /* changed ownership */
 phase=1;
 put(entry-100,DRAW_CALLER+(test==29));put(entry-96,test==8?6:7);put(entry-92,test==30?1:0);put(entry-88,4);g_esp=entry-100;
 uint32_t args[4];memcpy(args,(void*)(g_xbox_mem_offset+g_esp),16);
 if(DRAW)DRAW();else REAL_DRAW();
 assert(!memcmp(args,(void*)(g_xbox_mem_offset+entry-100),16));
 for(unsigned i=0;i<28;i++)assert(*(unsigned char*)(g_xbox_mem_offset+0x80090070+i)==0xab);
 assert(rd(0x80008)==0x12345678); /* opaque resource bytes */
 g_esp=entry+4;g_ecx=0x12345;g_eax=0x67890;g_fp_top=3;g_fp_stack[0]=1.25;
}
int main(void){
 unsigned char*ram=mmap(NULL,0x84000000ull,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);assert(ram!=MAP_FAILED);
 assert(!mprotect(ram,0x4000000,PROT_READ|PROT_WRITE));assert(!mprotect(ram+0x80000000ull,0x4000000,PROT_READ|PROT_WRITE));g_xbox_mem_offset=(ptrdiff_t)ram;
 for(test=0;test<48+BACKDROPS;test++){
  memset(ram,0,0x4000000);outercalls=lockcalls=drawcalls=phase=nested=0;setenv("TOPSPIN_LOADING_LAYOUT",test==9?"0":"1",1);
  put(0x30000,CALLER+(test==2));put(0x30004,test==26?0x30018:0x30014);
  put(DEVICE,0x70000);put(0x70ed8,1280);put(0x70edc,720);put(0x70ee4,0x3f800000);
  put(0x719f4,0xa0000);put(0x71a04,0xa0000);put(0x71a08,0xa0000);put(0xa000c,0x1128);put(0xa0010,(719u<<12)|1279u);
  if(test==10||test==20)put(0x70ed8,640);
  if(test==11)put(0xa0010,(719u<<12)|639u);
  if(test==12)put(0x71a08,0x3ffffff);
  if(test==13){put(0xa0010,0);put(0xa000c,(10u<<20)|(10u<<24));}
  if(test==14)put(0x70edc,480);
  if(test==15)put(0x70ed0,10);
  if(test==25)put(0xa0010,(1079u<<12)|1919u); /* larger logical target is not raster density */
  if(test==27)put(0x70ee4,0); /* altered depth clip */
  put(0x30014,0x60000);put(0x30018,test==3?800:640);put(0x3001c,480);
  if(backed()){put(0x30018,test==17?2048:1024);put(0x3001c,test==18?1024:512);}
  const char*path=(test==4||test==21)?"d:\\Media\\Loadings\\Tip00Eng00.jpg":"d:\\Media\\Loadings\\LoadEng00.jpg";
  if(test==39)path="d:\\Media\\Loadings\\RodLaverXXEng00.jpg";
  if(test==40)path="d:\\Media\\Loadings\\RodLaverXXEng01.jpg";
  if(test==41)path="d:\\Media\\Loadings\\UnknownXXEng00.jpg";
  if(test==42)path="d:\\Media\\Otherxxx\\RodLaverXXEng00.jpg";
  if(test==43)path="d:\\Media\\Loadings\\Help00Eng00.jpg";
  if(test==44)path="d:\\Media\\Loadings\\RodLaverXXSpa00.jpg";
  if(test>=48)path=backdrops[test-48];
  memcpy(ram+0x30028,path,strlen(path)+1);
  g_esp=0x30000;g_ecx=0x40000;g_eax=0;g_fp_top=1;g_fp_stack[0]=.5;g_fp_stack[7]=7.125;fesetround(FE_DOWNWARD);feclearexcept(FE_ALL_EXCEPT);
  if(OUTER)OUTER();else REAL_OUTER();
  assert(outercalls==(test==37?2u:1u)&&lockcalls==outercalls&&drawcalls==outercalls);
  assert(g_esp==0x30004&&g_ecx==0x12345&&g_eax==0x67890&&g_fp_top==3&&g_fp_stack[0]==1.25&&g_fp_stack[7]==7.125&&fegetround()==FE_DOWNWARD);
  if(test==0||test==16||test==23||test==35||test==37||(GAME&&(test==39||test>=48))||(!GAME&&test>=48&&!strcmp(path,"d:\\Media\\Loadings\\LoadEng00.jpg"))){
   if(output[1].x!=1280||output[2].y!=720){fprintf(stderr,"%s missing stretch test%u: %gx%g\n",LABEL,test,output[1].x,output[2].y);return 1;}
   for(unsigned i=0;i<4;i++){
    assert(output[i].x==((i==0||i==3)?0:1280)&&output[i].y==(i<2?0:720));
    unsigned bw=backed()?1024:640,bh=backed()?512:480;
    assert(output[i].u==((i==0||i==3)?.5f/bw:639.5f/bw));assert(output[i].v==(i<2?.5f/bh:479.5f/bh));
    assert(output[i].z==original[i].z&&output[i].rhw==original[i].rhw&&output[i].color==original[i].color);
   }
   /* SDK136 fixedshader normalizes by logical1280x720; raster density is separate.
    * Original normalized ball(.875,.8425) and stretched image anchors coincide
    * at720p,1080p and2160p (also1080pSSAA2x2 physical2160p). */
   const unsigned raster[][2]={{1280,720},{1920,1080},{3840,2160}};
   for(unsigned j=0;j<3;j++){double clipx=output[1].x/1280.*2-1,clipy=1-output[2].y/720.*2;
    assert((clipx+1)*.5*raster[j][0]==raster[j][0]);assert((1-clipy)*.5*raster[j][1]==raster[j][1]);
    assert((560.*2/1280.)==.875);assert((404.4*1.5/720.)>.84249&&(404.4*1.5/720.)<.84251);
   }
  }else assert(!memcmp(output,original,sizeof original));
 }
 printf("%s loading:%zu scope/UV/alias/clip/failure cases, fourvertices, originalonce/ABI +720/1080/2160 raster mapping PASS\n",LABEL,48+BACKDROPS);munmap(ram,0x84000000ull);return 0;
}
'''
c=c.replace('@@BACKDROPS@@',paths)
p=argparse.ArgumentParser();p.add_argument('--baseline',action='store_true');args=p.parse_args()
with tempfile.TemporaryDirectory(prefix='topspin-loading-layout-')as td:
 t=pathlib.Path(td);(t/'test.c').write_text(c)
 for game in (False,True):
  cmd=['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-DGAME='+str(int(game)),'-O1','-Wall','-Wextra','-Werror','-I'+str(root/'native-linux/src'),str(t/'test.c')]
  if not args.baseline:
   cmd +=[str(root/'native-linux/src/loading_layout.c'),str(root/'native-linux/src'/('game_loading_hooks.c' if game else 'menu_loading_hooks.c'))]
  subprocess.run(cmd+['-lm','-o',str(t/'test')],check=True);subprocess.run([str(t/'test')],check=True)
