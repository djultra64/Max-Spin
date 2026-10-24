/* Development input integration. Own work: djultra64.
 * SDL maps physical positions through input_host; title-specific action
 * remapping and matching Xbox/PlayStation/Switch prompts remain release work.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdatomic.h>
#include "input/input_host.h"
#include "usb/ohci.h"
#include "controls.h"
static TopSpinActionBindings actions;
static TopSpinControlContext context=TOPSPIN_CONTEXT_MENU;
static InputHostConfig current_config;
static int controls_started;
_Static_assert(INPUT_SRC_COUNT<=64,"prompt source packing requires six bits");
#include "prompt_sources9.h"
static TopspinSources9 applied_prompt_sources;
static void publish_prompt_sources(const InputHostConfig*cfg){(void)topspin_sources9_publish(&applied_prompt_sources,&cfg->padmap);}
int topspin_controls_guest_sources9(uint8_t out[9]){return topspin_sources9_read(&applied_prompt_sources,out);}
int topspin_controls_guest_sources(uint8_t out[6]){uint8_t all[9];if(!out||!topspin_controls_guest_sources9(all))return 0;memcpy(out,all,6);return 1;}

void topspin_controls_start(void) {
    InputHostConfig cfg;
    xbox_HostInputDefaults(&cfg);
    /* Local modern bumper convention, shared with original Black/White
     * prompt slots: White=left bumper, Black=right bumper. */
    cfg.padmap.source[INPUT_WHITE]=INPUT_SRC_LEFT_SHOULDER;
    cfg.padmap.source[INPUT_BLACK]=INPUT_SRC_RIGHT_SHOULDER;
    cfg.use_sdl=1; cfg.players=1;
    /* Diagnostic isolation only: use the SDK's SDL virtual pad while leaving
     * every physical controller untouched. Both explicit gates are required. */
    const char*virtual_pad=getenv("TOPSPIN_DIAGNOSTIC_VIRTUAL_PAD");
    const char*diagnostics=getenv("TOPSPIN_MANUAL_DIAGNOSTICS");
    if(virtual_pad&&!strcmp(virtual_pad,"1")&&diagnostics&&!strcmp(diagnostics,"1")) {
        cfg.no_pads=1;cfg.virtual_pads=1;
        /* Only this isolated diagnostic pad may keep receiving title-local
         * scripted input while screenshots are inspected in another window. */
        cfg.ignore_focus=1;
        fprintf(stderr,"[INPUT] Diagnostic virtual pad enabled; physical pads excluded\n");
    }
    const char*script_keys=getenv("RECOMP_PAD_SCRIPT_KEYS");
    cfg.keyboard=cfg.virtual_pads?(script_keys&&!strcmp(script_keys,"1")?INPUT_HOST_KEYBOARD_SLOT1:INPUT_HOST_KEYBOARD_OFF):INPUT_HOST_KEYBOARD_MERGE; cfg.rumble_on_connect=0;
    /* A title zero, including vibration OFF, must stop an active pulse.
     * The SDK's default minimum200ms would extend it after that zero. */
    cfg.rumble_min_ms=0;
    input_bindings_parse(&cfg.keys,INPUT_START,"Return");
    input_bindings_parse(&cfg.keys,INPUT_A,"Space");
    input_bindings_parse(&cfg.keys,INPUT_B,"Escape");
    input_bindings_parse(&cfg.keys,INPUT_X,"U");
    input_bindings_parse(&cfg.keys,INPUT_Y,"I");
    input_bindings_parse(&cfg.keys,INPUT_BLACK,"O");
    input_bindings_parse(&cfg.keys,INPUT_WHITE,"P");
    input_bindings_parse(&cfg.keys,INPUT_BACK,"Tab");
    input_bindings_parse(&cfg.keys,INPUT_LEFT_TRIGGER,"Q");
    input_bindings_parse(&cfg.keys,INPUT_RIGHT_TRIGGER,"E");
    input_bindings_parse(&cfg.keys,INPUT_LEFT_THUMB,"LShift");
    input_bindings_parse(&cfg.keys,INPUT_RIGHT_THUMB,"RShift");
    input_bindings_parse(&cfg.keys,INPUT_LSTICK_UP,"W");
    input_bindings_parse(&cfg.keys,INPUT_LSTICK_DOWN,"S");
    input_bindings_parse(&cfg.keys,INPUT_LSTICK_LEFT,"A");
    input_bindings_parse(&cfg.keys,INPUT_LSTICK_RIGHT,"D");
    input_bindings_parse(&cfg.keys,INPUT_RSTICK_UP,"T");
    input_bindings_parse(&cfg.keys,INPUT_RSTICK_DOWN,"G");
    input_bindings_parse(&cfg.keys,INPUT_RSTICK_LEFT,"F");
    input_bindings_parse(&cfg.keys,INPUT_RSTICK_RIGHT,"H");
    input_bindings_parse(&cfg.keys,INPUT_DPAD_UP,"Up");
    input_bindings_parse(&cfg.keys,INPUT_DPAD_DOWN,"Down");
    input_bindings_parse(&cfg.keys,INPUT_DPAD_LEFT,"Left");
    input_bindings_parse(&cfg.keys,INPUT_DPAD_RIGHT,"Right");
    topspin_actions_defaults(&actions);
    /* Explicit image context is only a starting context. A verified pause/UI
     * transition must call topspin_controls_context before custom match/UI
     * bindings are advertised as complete in all scenes. */
#ifdef TOPSPIN_XBE_NAME
    if(strstr(TOPSPIN_XBE_NAME,"(Game)")||strstr(TOPSPIN_XBE_NAME,"(Training)"))context=TOPSPIN_CONTEXT_MATCH;
#endif
    for(unsigned a=0;a<TOPSPIN_ACTION_COUNT;a++) {
        char env[96];const char*name=topspin_action_name((TopSpinAction)a);
        snprintf(env,sizeof(env),"TOPSPIN_BIND_%s",name);
        for(char*p=env;*p;p++)*p=(char)toupper((unsigned char)*p);
        const char*value=getenv(env);
        if(value&&!topspin_action_bind(&actions,(TopSpinAction)a,value))fprintf(stderr,"[INPUT] Invalid action source for %s\n",name);
        snprintf(env,sizeof(env),"TOPSPIN_KEY_%s",name);
        for(char*p=env;*p;p++)*p=(char)toupper((unsigned char)*p);
        value=getenv(env);
        if(value&&!topspin_action_bind_key(&actions,(TopSpinAction)a,value))fprintf(stderr,"[INPUT] Invalid action key for %s\n",name);
    }
    if(!topspin_actions_project(&actions,context,&cfg.padmap,&cfg.keys)) {
        fprintf(stderr,"[INPUT] Conflicting action bindings; using defaults\n");
        topspin_actions_defaults(&actions);
        topspin_actions_project(&actions,context,&cfg.padmap,&cfg.keys);
    }
    current_config=cfg;
    xbox_HostInputOnPadSlot(xbox_UsbSetPadCount);
    int found=xbox_HostInputStart(&cfg);
    publish_prompt_sources(&cfg);
    controls_started=1;
    const char*family=getenv("TOPSPIN_CONTROLLER_FAMILY");
    if(family){int f=topspin_family_parse(family);if(f==-2||!xbox_HostInputSetPromptFamily(f))fprintf(stderr,"[INPUT] Invalid controller family override\n");}
    xbox_UsbSetPadCount(xbox_HostInputPlayerCount());
    fprintf(stderr,"[INPUT] Native probe: %d physical gamepad(s); keyboard navigation enabled\n",found);
}

void topspin_controls_key(unsigned key,int down) {
    int vk=0;
    switch(key) {
    case SDLK_TAB: vk=0x09; break;
    case SDLK_LSHIFT: vk=0xa0; break;
    case SDLK_RSHIFT: vk=0xa1; break;
    case SDLK_RETURN: vk=0x0d; break;
    case SDLK_SPACE: vk=0x20; break;
    case SDLK_ESCAPE: vk=0x1b; break;
    case SDLK_I: vk=0x49; break;
    case SDLK_UP: vk=0x26; break;
    case SDLK_DOWN: vk=0x28; break;
    case SDLK_LEFT: vk=0x25; break;
    case SDLK_RIGHT: vk=0x27; break;
    default:
        if(key>=SDLK_A && key<=SDLK_Z) vk=(int)(key-SDLK_A)+'A';
        else if(key>=SDLK_0 && key<=SDLK_9) vk=(int)(key-SDLK_0)+'0';
        else if(key>=SDLK_F1 && key<=SDLK_F12) vk=(int)(key-SDLK_F1)+0x70;
        break;
    }
    if(vk) xbox_HostInputKey(vk,down);
}

void topspin_controls_context(TopSpinControlContext next) {
    if(next!=TOPSPIN_CONTEXT_MENU&&next!=TOPSPIN_CONTEXT_MATCH)return;
    if(!controls_started){context=next;return;}
    InputHostConfig config=current_config;
    if(topspin_actions_project(&actions,next,&config.padmap,&config.keys)) {
        current_config=config;context=next;xbox_HostInputConfigure(&current_config);
        publish_prompt_sources(&current_config);
    }
}
int topspin_controls_prompt(TopSpinAction action,TopSpinActionPrompt*prompt) {
    if(!controls_started||!topspin_action_in_context(action,context))return 0;
    InputDeviceIdentity identity;
    xbox_HostInputLastDevice(&identity);
    return topspin_action_prompt(&actions,action,&identity,prompt);
}
