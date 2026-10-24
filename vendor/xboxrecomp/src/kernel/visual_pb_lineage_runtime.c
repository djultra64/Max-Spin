#include "visual_pb_lineage_runtime.h"
#include "resource_epoch_api.h"
#include <string.h>
#include <limits.h>
extern ptrdiff_t xbox_GetMemoryOffset(void);
static int authorized(VplRuntime*r){return r&&r->owner&&r->owner(r->user);}
static int allocation(void*user,uint32_t va,size_t n,uint32_t*canonical,uint32_t*base,uint32_t*size,uint64_t*epoch){
 VplRuntime*r=user;if(!authorized(r)||!n||n>UINT32_MAX)return 0;
 /* These are the actual scanner physical-offset and contiguous aliases only.
  * Lookup validates allocation lifetime AFTER canonicalization. */
 uint32_t at=va;if(va<0x4000000u)at=va|0x80000000u;
 else if(va<0x80000000u||va>=0x84000000u)return 0;
 if((uint64_t)at+n>0x84000000u||!xbox_ContiguousEpoch(at,(uint32_t)n,base,size,epoch))return 0;
 *canonical=at;return 1;
}
static int copy(void*user,uint32_t at,void*out,size_t n){
 uint32_t a,b,s,a2,b2,s2;uint64_t e,e2;
 if(!out||!allocation(user,at,n,&a,&b,&s,&e))return 0;
 memcpy(out,(const void*)((uintptr_t)xbox_GetMemoryOffset()+a),n);
 return allocation(user,at,n,&a2,&b2,&s2,&e2)&&a==a2&&b==b2&&s==s2&&e==e2;
}
VplReader vpl_runtime_reader(VplRuntime*r,int enabled){VplReader out={r,enabled,allocation,copy};return out;}
