#ifndef PIGEN_SPECIALIZE_H
#define PIGEN_SPECIALIZE_H

#include <stddef.h>

/* Rewrite parameterized instances of Pigen modules into fixed, specialized
 * module copies. Returns a malloc-owned replacement source, or NULL when the
 * source needs no specialization. */
char *pigen_specialize_parameterized_instances(const char *source,
	size_t length, const char **error, size_t *error_offset);

#endif
