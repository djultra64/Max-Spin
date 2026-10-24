/* Included in exact Menu resource/focus adapter; guest token required.
 * Own work: djultra64. No SDL/window/overlay/input interception. */
#include "controller_family_session.h"
#include "sprite_ui_graphics.h"
#include "input/input_host.h"
extern void sub_00027EF0(void);
static struct {
 TopspinNativeGraphics model;
 uint32_t control[262];
 int editing,family;unsigned pending_restore;
} graphics;
static const struct{unsigned id;const char*name;} graphics_controls[]={
 {256,"PcGraphicsWindowMode"},{259,"PcGraphicsWindowModeValue"},{260,"PcGraphicsWindowModeMinus"},{261,"PcGraphicsWindowModePlus"},
 {217,"PcGraphicsResolution"},{220,"PcGraphicsAntialiasing"},{223,"PcGraphicsSpriteFilter"},{226,"PcGraphicsFrameLimit"},
 {229,"PcGraphicsSave"},{232,"PcGraphicsBack"},{250,"PcGraphicsControllerPrompts"},
 {235,"PcGraphicsResolutionMinus"},{236,"PcGraphicsResolutionPlus"},{237,"PcGraphicsAntialiasingMinus"},{238,"PcGraphicsAntialiasingPlus"},
 {239,"PcGraphicsSpriteFilterMinus"},{240,"PcGraphicsSpriteFilterPlus"},{241,"PcGraphicsFrameLimitMinus"},{242,"PcGraphicsFrameLimitPlus"},
 {243,"PcGraphicsSaveRequest"},{244,"PcGraphicsBackRequest"},{254,"PcGraphicsControllerPromptsMinus"},{255,"PcGraphicsControllerPromptsPlus"},
 {245,"PcGraphicsResolutionValue"},{246,"PcGraphicsAntialiasingValue"},{247,"PcGraphicsSpriteFilterValue"},{248,"PcGraphicsFrameLimitValue"},
 {249,"PcGraphicsStatus"},{253,"PcGraphicsControllerPromptsValue"}
};
static const char*graphics_family_label(void){
 switch(graphics.family){case INPUT_FAMILY_AUTO:return "AUTO";case INPUT_FAMILY_XBOX:return "XBOX";case INPUT_FAMILY_PLAYSTATION:return "PLAYSTATION";case INPUT_FAMILY_NINTENDO:return "NINTENDO SWITCH";default:return "AUTO";}
}
int topspin_menu_graphics_configure(const char*path,TopspinNativeGraphicsReady ready,void*user,unsigned height,TopspinGraphicsAA aa,TopspinGraphicsFilter filter){
 return topspin_controller_family_load(&graphics.family,NULL,0)&&topspin_native_graphics_configure(&graphics.model,path,ready,user,height,aa,filter);
}
static void graphics_scene_reset(void){memset(graphics.control,0,sizeof graphics.control);graphics.editing=0;graphics.pending_restore=0;}
static void graphics_register_control(uint32_t scene,const uint32_t args[3]){
 uint32_t control=word(args[1]);if(!control_valid(control,scene))return;
 for(unsigned i=0;i<sizeof graphics_controls/sizeof*graphics_controls;i++)if(str_is(args[2],graphics_controls[i].name)){graphics.control[graphics_controls[i].id]=control;return;}
}
static const char*graphics_label(uint32_t object,char value[40]){
 static const struct{const char*id,*label;} labels[]={
  {"pc_graphics_options","GRAPHICS OPTIONS"},{"pc_quit_game","QUIT GAME"},{"pc_graphics_title","GRAPHICS OPTIONS"},
  {"pc_graphics_window_mode","WINDOW MODE"},{"pc_graphics_resolution","RESOLUTION"},{"pc_graphics_antialiasing","ANTIALIASING"},{"pc_graphics_sprite_filter","SPRITE FILTER"},
  {"pc_graphics_frame_limit","FRAME LIMIT"},{"pc_graphics_controller_prompts","CONTROLLER PROMPTS"},{"pc_graphics_save","SAVE"},{"pc_graphics_back","BACK"}
 };
 for(unsigned i=0;i<sizeof labels/sizeof*labels;i++)if(str_is(object,labels[i].id))return labels[i].label;
 static const char*ids[]={"pc_graphics_value_resolution","pc_graphics_value_antialiasing","pc_graphics_value_filter","pc_graphics_value_cap"};
 for(unsigned i=0;i<4;i++)if(str_is(object,ids[i]))return topspin_native_graphics_value(&graphics.model,i,value,40)?value:"UNAVAILABLE";
 if(str_is(object,"pc_graphics_value_window_mode"))return topspin_native_graphics_value(&graphics.model,4,value,40)?value:"UNAVAILABLE";
 if(str_is(object,"pc_graphics_value_family"))return graphics_family_label();
 if(str_is(object,"pc_graphics_status"))return graphics.model.status[0]?graphics.model.status:"SAVE AND RESTART";
 return NULL;
}
static int graphics_set_focus(unsigned id){
 const char*name=NULL;
 for(unsigned i=0;i<sizeof graphics_controls/sizeof*graphics_controls;i++)if(graphics_controls[i].id==id)name=graphics_controls[i].name;
 if(!name||!control_valid(graphics.control[id],pc.scene))return 0;
 Cpu cpu;uint32_t base;unsigned char saved[256];if(!scratch(&cpu,&base,saved))return 0;
 memcpy(map(base+160,strlen(name)+1),name,strlen(name)+1);
 call0(sub_00022C30,base+96,base+144,base+160,0);
 internal++;call0(sub_00023780_gen,base+96,pc.scene,base+144,0);internal--;
 call0(sub_00020B80,base+96,base+144,0,0);
 int success=current_focus(pc.scene)==graphics.control[id];unscratch(&cpu,base,saved);return success;
}
/* Original34E60 binds control+2C to the named graph node. RGuiTextNode destructor
 * 26070 proves1E5F38; original27EF0 re-resolves TextID and calls original
 * 27BC0 to update its rendered text. Do not write widget/string internals. */
static void graphics_refresh(void){
 static const unsigned values[]={245,246,247,248,249,253,259};
 for(unsigned i=0;i<sizeof values/sizeof*values;i++){
  uint32_t control=graphics.control[values[i]];if(!control_valid(control,pc.scene))continue;
  uint32_t node=word(control+0x2c);if(!map(node,0x54)||word(node)!=0x1e5f38u)continue;
  Cpu cpu;uint32_t base;unsigned char saved[256];if(!scratch(&cpu,&base,saved))return;
  call0(sub_00027EF0,base+96,node,0,0);unscratch(&cpu,base,saved);
 }
}
static void graphics_family_adjust(int direction){
 static const int families[]={INPUT_FAMILY_AUTO,INPUT_FAMILY_XBOX,INPUT_FAMILY_PLAYSTATION,INPUT_FAMILY_NINTENDO};
 unsigned current=0;while(current<4&&families[current]!=graphics.family)current++;
 if(current==4)return;
 int next=families[(current+4+direction)%4];
 if(topspin_controller_family_change(graphics.family,next,xbox_HostInputSetPromptFamily,NULL,0)){graphics.family=next;strcpy(graphics.model.status,"PROMPTS UPDATED");}
 else strcpy(graphics.model.status,"PROMPT CHANGE FAILED");
}
static void graphics_guest_poll(void){
 if(!graphics.model.configured||!pc.configured||!newmain(pc.scene))return;
 uint32_t manager=word(0x2b969c);if(!map(manager,0x1248)||word(manager+0x1244)!=pc.scene){graphics.editing=0;graphics.pending_restore=0;return;}
 uint32_t focus=current_focus(pc.scene);if(!control_valid(focus,pc.scene)){graphics.editing=0;graphics.pending_restore=0;return;}
 unsigned id=0;for(unsigned i=0;i<sizeof graphics_controls/sizeof*graphics_controls;i++)if(focus==graphics.control[graphics_controls[i].id]){id=graphics_controls[i].id;break;}
 if(!id){graphics.editing=0;graphics.pending_restore=0;return;}
 if(!graphics.editing){graphics.model.running.filter2d=topspin_sprite_ui_filter_current();topspin_native_graphics_begin(&graphics.model);graphics.editing=1;graphics_refresh();}
 if(graphics.pending_restore){if(graphics_set_focus(graphics.pending_restore)){graphics.pending_restore=0;graphics_refresh();}return;}
 if(id>=235&&id<=242){unsigned row=(id-235)/2;topspin_native_graphics_adjust(&graphics.model,row,(id&1)?-1:1);graphics.pending_restore=217+3*row;}
 else if(id==260||id==261){topspin_native_graphics_adjust(&graphics.model,4,id==260?-1:1);graphics.pending_restore=256;}
 else if(id==243){topspin_native_graphics_save(&graphics.model);graphics.pending_restore=229;}
 else if(id==254||id==255){graphics_family_adjust(id==254?-1:1);graphics.pending_restore=250;}
 if(graphics.pending_restore&&graphics_set_focus(graphics.pending_restore)){graphics.pending_restore=0;graphics_refresh();}
}
