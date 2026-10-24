/* NV2A flip register synchronization. Own integration: djultra64. */
#pragma once
#include <stdint.h>
void nv2a_flip_enable(int enabled);
int nv2a_flip_enabled(void);
uint32_t nv2a_flip_surface(void);
void nv2a_flip_surface_write(uint32_t value);
void nv2a_flip_set_read(uint32_t value);
void nv2a_flip_set_write(uint32_t value);
void nv2a_flip_set_modulo(uint32_t value);
void nv2a_flip_increment_read(void);
void nv2a_flip_increment_write(void);
int nv2a_flip_stalled(void);
void nv2a_flip_wait(void);
