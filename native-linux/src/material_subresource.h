/* The frontend and backend use the same original-XDK mip contract.
 * SDK100 is required; no independent alias/layout heuristic. djultra64. */
#ifndef TOPSPIN_MATERIAL_SUBRESOURCE_H
#define TOPSPIN_MATERIAL_SUBRESOURCE_H
#include "resource_mip_layout.h"
typedef RjpMip McSubresource;
static inline int mc_subresource_at(const uint32_t parent[5],uint32_t level,McSubresource*out){return rjp_mip_at(parent,level,out);}
static inline int mc_subresource_surface(uint32_t parent_resource,const uint32_t parent[5],const uint32_t surface[6],McSubresource*out){return rjp_mip_surface(parent_resource,parent,surface,out);}
#endif
