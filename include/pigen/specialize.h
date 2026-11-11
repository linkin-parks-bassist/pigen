#ifndef PIGEN_SPECIALIZE_H
#define PIGEN_SPECIALIZE_H

#include <stddef.h>

/* Rewrite parameterized instances of Pigen modules into fixed, specialized
 * module copies. Returns a malloc-owned replacement source, or NULL when the
 * source needs no specialization. */
char *pigen_specialize_parameterized_instances(const char *source,
	size_t length, const char **error, size_t *error_offset);

/* Return an owned fixed point, or NULL with an error. Limits diagnose instead
 * of returning partially rewritten source; zero rounds permits only a no-op. */
char *pigen_specialize_fixed_point(const char *source, size_t length,
	size_t maximum_rounds, size_t maximum_bytes, const char **error,
	size_t *error_offset);

#endif
