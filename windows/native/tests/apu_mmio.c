/* Exact native APU decoder regression plus real VEH read-only TEST.
 * No original assets. Own work: djultra64. */
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "apu.h"
extern MCPXAPUState *g_apu_state;
extern bool apu_hook_handle_mmio(PCONTEXT,uintptr_t,uint32_t,int);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);ExitProcess(1);}}while(0)
static uint64_t next_value;static unsigned reads,writes,width;static uint64_t address;
uint64_t mcpx_apu_mmio_read(MCPXAPUState*s,uint64_t off,unsigned size){CHECK(s==g_apu_state);reads++;width=size;address=off;return next_value;}
void mcpx_apu_mmio_write(MCPXAPUState*s,uint64_t off,uint64_t value,unsigned size){(void)s;(void)off;(void)value;(void)size;writes++;}
static int parity(uint64_t v){unsigned x=(unsigned)v&255;x^=x>>4;x^=x>>2;x^=x>>1;return !(x&1);}
static void test(const unsigned char*code,size_t bytes,unsigned size,uint64_t value,uint64_t mask){CONTEXT c={0};c.Rip=(DWORD64)(uintptr_t)code;c.Rax=0xfedcba9876543210ULL;c.Rsi=0x11223344;c.EFlags=0xAD7;CONTEXT original=c;reads=writes=0;next_value=value;CHECK(apu_hook_handle_mmio(&c,0,0xfe802000u,0));CHECK(c.Rip==(DWORD64)(uintptr_t)code+bytes&&reads==1&&writes==0&&width==size&&address==0x2000);uint64_t result=value&mask;if(size<8)result&=(1ULL<<(size*8))-1;DWORD flags=original.EFlags&~0x8C5u;if(!result)flags|=0x40;if(result&(1ULL<<(size*8-1)))flags|=0x80;if(parity(result))flags|=4;CHECK(c.EFlags==flags);original.Rip=c.Rip;original.EFlags=c.EFlags;CHECK(!memcmp(&c,&original,sizeof c));}
static unsigned char*page;static unsigned faults;
static LONG CALLBACK handler(EXCEPTION_POINTERS*e){if(e->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION||e->ExceptionRecord->NumberParameters<2||(void*)e->ExceptionRecord->ExceptionInformation[1]!=page)return EXCEPTION_CONTINUE_SEARCH;CHECK(e->ExceptionRecord->ExceptionInformation[0]==0);faults++;return apu_hook_handle_mmio(e->ContextRecord,(uintptr_t)page,0xfe802000u,0)?EXCEPTION_CONTINUE_EXECUTION:EXCEPTION_CONTINUE_SEARCH;}
int main(void){g_apu_state=(MCPXAPUState*)(uintptr_t)1;
 const unsigned char observed[]={0xf7,0x84,0x06,0xf0,0x1f,0xfe,0,1,0,0,0};test(observed,sizeof observed,4,0x80000001u,1);test(observed,sizeof observed,4,0x100000000ULL,1);
 const unsigned char byte[]={0xf6,0,3};test(byte,sizeof byte,1,3,3);test(byte,sizeof byte,1,1,3);test(byte,sizeof byte,1,0xffffffffffffff00ULL,3);
 const unsigned char word[]={0x66,0xf7,0,0,0x80};test(word,sizeof word,2,0x8000,0x8000);test(word,sizeof word,2,0xffffffffffff0000ULL,0x8000);
 const unsigned char quad[]={0x48,0xf7,0,0,0,0,0x80};test(quad,sizeof quad,8,0xffffffff00000000ULL,0xffffffff80000000ULL);test(quad,sizeof quad,8,0x8000000000000000ULL,0xffffffff80000000ULL);
 const unsigned char prefixed[]={0x66,0x48,0xf7,0,0,0,0,0x80};test(prefixed,sizeof prefixed,8,0x8000000000000000ULL,0xffffffff80000000ULL);
 const unsigned char parity_high[]={0xf7,0,0xff,0xff,0xff,0xff};test(parity_high,sizeof parity_high,4,0x10000,0xffffffff);
 const unsigned char sib[]={0xf7,4,0x25,0,0x20,0x80,0xfe,0xff,0xff,0xff,0xff};test(sib,sizeof sib,4,0x80000000u,0xffffffffu);
 for(unsigned opcode=0xf6;opcode<=0xf7;opcode++)for(unsigned group=1;group<8;group++){unsigned char rejected[]={(unsigned char)opcode,(unsigned char)(group<<3),0,0,0,0};CONTEXT c={0};c.Rip=(DWORD64)(uintptr_t)rejected;c.Rax=0x12345678;c.EFlags=0xAD7;CONTEXT original=c;reads=writes=0;CHECK(!apu_hook_handle_mmio(&c,0,0xfe802000u,0)&&!memcmp(&c,&original,sizeof c)&&!reads&&!writes);}
 unsigned char reg[]={0xf7,0xc0,1,0,0,0};CONTEXT c={0};c.Rip=(DWORD64)(uintptr_t)reg;c.Rax=0x11223344;c.EFlags=0xAD7;CONTEXT original=c;reads=writes=0;CHECK(!apu_hook_handle_mmio(&c,0,0xfe802000u,0)&&!memcmp(&c,&original,sizeof c)&&!reads&&!writes);
 page=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS);CHECK(page);unsigned char machine[]={0xf7,1,1,0,0,0,0x0f,0x94,0xc0,0x0f,0xb6,0xc0,0xc3};void*fn=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);CHECK(fn);memcpy(fn,machine,sizeof machine);DWORD old;CHECK(VirtualProtect(fn,4096,PAGE_EXECUTE_READ,&old));CHECK(FlushInstructionCache(GetCurrentProcess(),fn,sizeof machine));PVOID veh=AddVectoredExceptionHandler(1,handler);CHECK(veh);next_value=0;CHECK(((int(*)(void*))fn)(page)==1);next_value=1;CHECK(((int(*)(void*))fn)(page)==0);CHECK(faults==2);CHECK(RemoveVectoredExceptionHandler(veh));CHECK(VirtualFree(page,0,MEM_RELEASE)&&VirtualFree(fn,0,MEM_RELEASE));puts("PASS actual Windows APU F6/F7 TEST widths/flags/RIP/no-writes/rejections and real VEH");return 0;}
