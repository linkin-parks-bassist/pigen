#ifndef PIGEN_SYNTAX_COMMON_H
#define PIGEN_SYNTAX_COMMON_H

#include "pigen/preprocess.h"

typedef struct {
	pigen_token_id first;
	pigen_token_id after;
} pigen_token_extent;

typedef struct {
	pigen_token_extent extent;
	pigen_origin_id origin;
	/* Invalid when the expanded extent has no single original-source range. */
	pigen_source_span source_span;
} pigen_syntax_location;

typedef struct {
	pigen_origin_id origin;
	pigen_source_span span;
	const char *message;
} pigen_syntax_error;

typedef enum {
	PIGEN_SYNTAX_SIGN_IMPLICIT,
	PIGEN_SYNTAX_SIGN_UNSIGNED,
	PIGEN_SYNTAX_SIGN_SIGNED
} pigen_syntax_signedness;

#endif
