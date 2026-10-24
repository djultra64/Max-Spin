/* Host input lifecycle for window close and image exit. Own work: djultra64. */
#ifndef TOPSPIN_HOST_SHUTDOWN_H
#define TOPSPIN_HOST_SHUTDOWN_H
#ifdef __cplusplus
extern "C" {
#endif
/* Call outside input/UI locks, never from the input reader. Concurrent exit
 * requests wait for the first stop to finish; repeated calls are harmless. */
void topspin_host_input_shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
