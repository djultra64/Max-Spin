/* Actual exporter/93 binding, proposed only. Own work djultra64. */
#ifndef TOPSPIN_PB_VISUAL_PACKET_BACKEND_H
#define TOPSPIN_PB_VISUAL_PACKET_BACKEND_H
#include "pb_visual_packet.h"
/* Call BEFORE Vulkan device creation, with real title scene/view authority.
 * Arm/release go through nv2a_executor_invoke93 using callbacks in core header.
 * This does not arm, capture, draw or mutate any guest state on its own. */
int pb_visual_install_backend(int(*generation)(void*,VfpGeneration*),void*);
int pb_visual_record_backend(uint64_t,const VfpSource*,float,float,int);
#endif
