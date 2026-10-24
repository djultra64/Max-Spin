/* Complete Save operation wiring proposal. Own work: djultra64. */
#ifndef TOPSPIN_CREATOR_SAVE_OPERATION_HOOKS_H
#define TOPSPIN_CREATOR_SAVE_OPERATION_HOOKS_H
#include <stdint.h>
int topspin_creator_save_checkpoint(void);
int topspin_creator_save_continue(void);
void sub_000626A0(void);
void sub_000FD550(void);
/* REQUIRED strong root implementation. Runs after original cleanup, only after
 * transaction failure, with no open operation. Must authenticate scene/context
 * before original UI calls. Creator and Shop owners are different classes.
 * This callback may change UI memory; caller restores guest CPU afterwards. */
void topspin_creator_save_failure_recover(unsigned kind,uint32_t owner);
#endif
