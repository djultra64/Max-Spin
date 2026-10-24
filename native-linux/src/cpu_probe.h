/* Top Spin XDK 5558 D3DX CPU probe compatibility. djultra64. */
#include <stdint.h>
int topspin_cpu_probe_validate(uint8_t *memory);
int topspin_cpu_probe_validate_at(uint8_t *memory, uint32_t address);
int topspin_cpu_probe_instruction(const char *text, uint32_t va, uint8_t *memory);
