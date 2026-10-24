/* Actual backend/journal proof -> source-lock bridge. djultra64. */
#include "material_coherence_bridge.h"
#include "resource_coherence_backend.h"
#include "nv2a_executor_job.h"
#include "material_subresource.h"
#include <string.h>
/* Called only as McProducerCapture inside the one93 owner job. */
int topspin_creator_material_producer_capture(const McLock*l,McSnapshot*s,unsigned char*rgba,size_t capacity){
 if(!nv2a_executor_is_owner()||!l||!s||!rgba||(l->format!=6&&l->format!=18)||l->face||l->level>9||(l->format==18&&(l->level||l->pitch<l->width*4u||l->pitch>2048u))||(!l->header[5]&&l->level)||!l->memory_epoch||!l->width||l->width>512||!l->height||l->height>512){d3d8_vk_coherence_diagnostic_reject(MCD_PRODUCER_ARGS,NULL);return 0;}
 size_t bytes=(size_t)(l->format==18?l->pitch:l->width*4u)*l->height;if(capacity<bytes){d3d8_vk_coherence_diagnostic_reject(MCD_PRODUCER_CAPACITY,NULL);return 0;}
 RjpQuery q={0};q.resource=l->resource;q.parent=l->header[5];memcpy(q.header,l->header,sizeof q.header);memcpy(q.parent_header,l->parent_header,sizeof q.parent_header);
 q.resolved_data=l->bits;q.width=l->width;q.height=l->height;q.pitch=l->pitch;q.format=l->format;q.guest_epoch=l->memory_epoch;
 if(l->header[5]){
  McSubresource mip;
  if(l->face||!mc_subresource_surface(l->header[5],l->parent_header,l->header,&mip)||mip.level!=l->level||
     mip.bits!=l->bits||mip.data!=l->data||mip.width!=l->width||mip.height!=l->height||mip.pitch!=l->pitch){d3d8_vk_coherence_diagnostic_reject(MCD_PRODUCER_MIP,&q);return 0;}
#ifdef RJP_QUERY_MIP_LEVEL
  /* The actual typed original Lock above still belongs to this Surface. GPU
   * source authority belongs to its live parent, which survives temporary
   * GetSurfaceLevel allocation/release/reuse. Preserve the real parent header;
   * SDK verifies its explicit mip view without inventing Data or a producer. */
  q.resource=l->header[5];q.parent=0;memset(q.header,0,sizeof q.header);
  memcpy(q.header,l->parent_header,sizeof l->parent_header);memset(q.parent_header,0,sizeof q.parent_header);q.level=l->level;
#else
  if(l->level){d3d8_vk_coherence_diagnostic_reject(MCD_PRODUCER_MIP,&q);return 0;} /* old SDK cannot attest a parent mip view */
#endif
 }
 D3D8VkCoherenceProof p={0};int result=d3d8_vk_coherence_source(&q,rgba,(uint32_t)bytes,&p);
 d3d8_vk_coherence_diagnostic_result(result);
 if(result<0){d3d8_vk_coherence_diagnostic_reject(MCD_PRODUCER_BACKEND,&q);return 0;}
 s->lock=*l;
 if(result==0)return 2; /* explicit NEVER_GPU, not a lookup miss */
 if(result!=1||!p.source.guest_id||p.source.guest_epoch!=l->memory_epoch||!p.source.allocation||!p.source.contents||!p.source.producer_serial||p.get!=p.put||p.width!=l->width||p.height!=l->height||p.pitch!=l->pitch){d3d8_vk_coherence_diagnostic_reject(MCD_PRODUCER_PROOF,&q);return 0;}
 s->allocation=s->observed_allocation=p.source.allocation;s->contents=s->observed_contents=p.source.contents;
 s->guest_generation=s->observed_guest_generation=p.source.guest_id;
 /* The backend publishes only after its actual readback fence and a second
  * identity/version/epoch/drain validation; this is not a guessed GPU clock. */
 s->producer_serial=s->completed_serial=p.source.producer_serial;
 s->flags=MC_OWNER|MC_SOURCE_BOUND|MC_PRODUCER_DRAINED|MC_GUEST_GENERATION;s->layout=MC_RGBA8_LINEAR;s->bytes=bytes;s->rgba=rgba;return 1;
}
