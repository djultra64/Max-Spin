/* Bounded, read-only Pioline pass decision observations. Own work: djultra64. */
#ifndef TOPSPIN_HAIR_PASS_TRACE_H
#define TOPSPIN_HAIR_PASS_TRACE_H
#ifndef TOPSPIN_HAIR_PASS_MANAGER_LIMIT
#define TOPSPIN_HAIR_PASS_MANAGER_LIMIT 12000u
#endif
#ifndef TOPSPIN_HAIR_PASS_SNAPSHOT_LIMIT
#define TOPSPIN_HAIR_PASS_SNAPSHOT_LIMIT 256u
#endif
/* No initialization or polling required. Exact TOPSPIN_HAIR_PASS_TRACE=1 opts
 * in; TOPSPIN_HAIR_PASS_TRACE_MIN_COUNTER optionally filters original guest
 * MEM32(0x30DBC8), never an inferred GPU flip. stderr is the only output.
 * Link --wrap for 79AE0,85450,6BCA0,86810. Rename only the 68AE0 body definition to
 * sub_00068AE0_hair_trace_original through the existing generation pipeline:
 * the normal symbol below intercepts its same-TU callers as well.
 * Originals execute once with their own post-call CPU/memory effects intact. */
/* Material records are entries to 68AE0, not proof of an eventual PB draw.
 * First two include this=rigid_leaf+4, byte[this+7D] (alias leaf+81), and
 * the actual first argument's context+6C rejection gate. */
void __wrap_sub_00079AE0(void);
void __wrap_sub_00085450(void);
void __wrap_sub_0006BCA0(void);
void __wrap_sub_00086810(void);
void sub_00068AE0(void);
#endif
