#ifndef PIGEN_TRANSFER_H
#define PIGEN_TRANSFER_H

#include <stddef.h>

/* Lower token-delimited `transfer begin ... end` blocks to one concatenated
 * assignment before procedural guard and signal analysis. */
char *pigen_lower_transfer_blocks(const char *source, size_t length,
	size_t *output_length);

#endif
