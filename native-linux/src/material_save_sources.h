/* Bounded original Creator material graph inventory; djultra64. */
#ifndef TOPSPIN_MATERIAL_SAVE_SOURCES_H
#define TOPSPIN_MATERIAL_SAVE_SOURCES_H
#include <stdint.h>
#include <stddef.h>
#define MC_SAVE_NODES 512u
#define MC_SAVE_RESOURCES 128u
typedef int(*McReadWord)(void*,uint32_t,uint32_t*);
typedef struct {uint32_t count,resources[MC_SAVE_RESOURCES];uint64_t fingerprint;} McSaveSources;
/* Conservative union, not an asset name/VA guess: actual CC710 node material (generic vtable+CC, or actual Creator
 * 1EDA78 material phase+C4 preceding its separate C31B0 phase+CC)
 * group58, D2390 array8/C, D0E40 descriptoralias2C/value18, D6E20 value28 with
 * inheritanceC plus D7A30 variants1C/20. Unknown exporter/shape/cycles reject.
 * Caller re-collects and compares after yielding guest turn, before Save.
 * No input values, original callbacks, files or guest memory are modified. */
enum {MCI_READ_RANGE=1,MCI_READ_CALLBACK=2,MCI_RESOURCE_LIMIT=3,MCI_PROPERTY_DEPTH=4,MCI_PROPERTY_CYCLE=5,MCI_PROPERTY_LIMIT=6,MCI_DESCRIPTOR_LIMIT=7,MCI_DESCRIPTOR_CYCLE=8,MCI_GROUP_COUNT=9,MCI_NODE_DEPTH=10,MCI_NODE_CYCLE=11,MCI_NODE_LIMIT=12,MCI_SAVE_CALLBACK=13,MCI_CHILD_COUNT=14,MCI_MODEL_COUNT=15};
typedef struct {uint32_t stage,address,value,node,vtable,save_callback,property,descriptor,count,depth,nodes,properties;} McSaveSourcesDiagnostic;
int mc_save_sources_diagnostic(McReadWord,void*,uint32_t,McSaveSources*,McSaveSourcesDiagnostic*);
int mc_save_sources(McReadWord,void*,uint32_t original_model,McSaveSources*);
#endif
