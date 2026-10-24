#ifndef XBOX_RESOURCE_EPOCH_API_H
#define XBOX_RESOURCE_EPOCH_API_H
#include <stdint.h>
typedef struct {uint32_t get,put;} XboxNv2aDrainState;
int xbox_ContiguousEpoch(uint32_t,uint32_t,uint32_t*,uint32_t*,uint64_t*);
int xbox_Nv2aDrainSnapshot(XboxNv2aDrainState*);
#endif
