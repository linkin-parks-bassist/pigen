#ifndef PIGEN_FSM_H
#define PIGEN_FSM_H

#include <stddef.h>

/* Replaces Pigen FSM blocks with ordinary SystemVerilog controllers. */
char *pigen_lower_fsms(const char *source, size_t length, size_t *lowered_length);

#endif
