/* Finalize actor proof from a sealed original frame, no live PB pointers. djultra64. */
#ifndef TOPSPIN_ACTOR_JOURNAL_BINDING_H
#define TOPSPIN_ACTOR_JOURNAL_BINDING_H
#include "actor_draw_binding.h"
#include "visual_scene_journal.h"
int vadb_bind_journal(VadbCollector*,const VsjFrame*,uint32_t command_index,const VclSnapshot* owned_actual_source_writers,VadbBinding*);
#endif
