#ifndef PIGEN_SEMANTIC_ERROR_H
#define PIGEN_SEMANTIC_ERROR_H

#include "pigen/source.h"

typedef struct {
	pigen_origin_id origin;
	pigen_source_span span;
	const char *message;
} pigen_semantic_error;

#endif
