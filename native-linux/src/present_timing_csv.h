/* Optional appended driver timing columns; original CSV otherwise. djultra64. */
#ifndef TOPSPIN_PRESENT_TIMING_CSV_H
#define TOPSPIN_PRESENT_TIMING_CSV_H
#include <stdio.h>
#include <stdint.h>
int topspin_present_csv_init(int explicit_extended);
void topspin_present_csv_header(FILE*);
void topspin_present_csv_row(FILE*,uint64_t frame,uint64_t now,uint64_t interval,uint32_t swap,uint32_t guest_interval);
#endif
