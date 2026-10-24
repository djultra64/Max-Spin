/* Authenticated-old appearance equivalence proposal. Author: djultra64.
 * Pure comparison only: no guest memory, IO, global state or export. */
#include "appearance_equivalence.h"
#include "menu_pc_sha256.h"
#include <stdint.h>
#include <string.h>
static uint32_t read32(const unsigned char*p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int topspin_creator_appearance_equivalent(const void*reference,size_t rn,const void*after,size_t an,const void*second,size_t sn){
 const unsigned char*r=reference,*a=after,*s=second;
 static const uint32_t feet[]={0x4bb33b85u,0x2c02bb6cu,0xd2ba6a3fu,0xb50bead6u};
 if(!r||!a||!s||rn!=1973||an!=2037||sn!=2037)return 0;
 if(!menu_pc_sha_is(r,rn,"5bc0925a6a8754e77b1fc2f42ce82ab48c2536ed217a4dba792a7039b7acbf3a"))return 0;
 if(memcmp(a,s,an)||read32(r+346)!=68||read32(a+346)!=72)return 0;
 if(memcmp(r,a,346)||memcmp(r+1438,a+1502,535))return 0;
 const unsigned char*root_x=NULL;
 for(unsigned i=0;i<68;i++)if(read32(r+350+i*16)==0x0ccd33f7u){if(root_x)return 0;root_x=r+354+i*16;}
 if(!root_x)return 0;
 unsigned kept=0,added=0;
 for(unsigned i=0;i<72;i++){
  const unsigned char*record=a+350+i*16;uint32_t id=read32(record);int foot=-1;
  /* All IDs must be unique, including any duplicate old or added record. */
  for(unsigned j=0;j<i;j++)if(read32(a+350+j*16)==id)return 0;
  for(unsigned j=0;j<4;j++)if(id==feet[j])foot=(int)j;
  if(foot>=0){
   unsigned mask=1u<<(unsigned)foot;if(added&mask)return 0;
   for(unsigned axis=0;axis<3;axis++)if(memcmp(record+4+axis*4,root_x,4))return 0;
   added|=mask;
  }else{
   if(kept>=68||memcmp(record,r+350+kept*16,16))return 0;
   kept++;
  }
 }
 return kept==68&&added==15;
}
