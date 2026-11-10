/* Source-spanned syntax nodes for modules and internal signal declarations. */
#include <stdlib.h>
#include <string.h>

#include "pigen/lexer.h"
#include "pigen/syntax.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})
#define INVALID_SYNTAX ((pigen_syntax_id){PIGEN_INVALID_ID})

typedef enum {
	TYPEDEF_NAME_ELIGIBLE,
	TYPEDEF_NAME_OPAQUE_SHADOW
} typedef_name_fact_kind;

typedef struct {
	pigen_token_id name;
	pigen_syntax_id scope;
	typedef_name_fact_kind kind;
} typedef_name_fact;

typedef enum {
	PIPELINE_SCOPE_PIPELINE,
	PIPELINE_SCOPE_STAGE
} pipeline_scope_kind;

typedef struct {
	pigen_token_id name;
	pipeline_scope_kind kind;
	size_t index;
} pipeline_field;

typedef struct {
	pipeline_field *fields;
	size_t count;
	size_t capacity;
} pipeline_scope;

#define MAX_PIPELINE_SCOPES 16

typedef struct {
	pipeline_scope scopes[MAX_PIPELINE_SCOPES];
	size_t depth;
} pipeline_scope_stack;

typedef struct {
	pigen_token_id *names;
	size_t count;
	size_t capacity;
} fsm_state_names;

typedef struct {
	const pigen_expanded_source *expanded;
	pigen_syntax_tree *tree;
	pigen_syntax_error *error;
	typedef_name_fact *typedef_name_facts;
	size_t typedef_name_fact_count;
	size_t typedef_name_fact_capacity;
	pigen_token_id *function_names;
	size_t function_name_count;
	size_t function_name_capacity;
	pipeline_scope_stack pipeline_scopes;
	int pipeline_failed;
	int fsm_body_failed;
	const fsm_state_names *fsm_states;
	size_t source_length;
	size_t opaque_byte_cursor;
} syntax_parser;

static int node_has_function_call(syntax_parser *parser,
	pigen_syntax_id node_id, size_t depth);
static int node_has_structured_construct(syntax_parser *parser,
	pigen_syntax_id node_id, size_t depth);
static int retained_procedural_end(syntax_parser *parser, size_t start,
	size_t limit, size_t *end_out);
static int retained_process_has_structured(syntax_parser *parser,
	size_t start, size_t end);
static void remember_function_name(syntax_parser *parser, size_t start,
	size_t end);

typedef struct {
	const syntax_parser *parser;
	pigen_syntax_id scope;
} structured_type_query;

typedef struct {
	size_t syntax_node_count;
	size_t expression_node_count;
	size_t expression_child_count;
	size_t type_node_count;
	size_t type_argument_count;
	size_t shape_dimension_count;
	pigen_syntax_error error;
} syntax_checkpoint;

typedef size_t syntax_cursor;

static syntax_checkpoint save_checkpoint(const syntax_parser *parser)
{
	syntax_checkpoint checkpoint = {0};
	checkpoint.syntax_node_count = parser->tree->node_count;
	checkpoint.expression_node_count = parser->tree->expressions.node_count;
	checkpoint.expression_child_count = parser->tree->expressions.child_count;
	checkpoint.type_node_count = parser->tree->types.node_count;
	checkpoint.type_argument_count = parser->tree->types.argument_count;
	checkpoint.shape_dimension_count = parser->tree->shape_dimension_count;
	if (parser->error) checkpoint.error = *parser->error;
	return checkpoint;
}

static void restore_checkpoint(syntax_parser *parser,
	syntax_checkpoint checkpoint)
{
	parser->tree->node_count = checkpoint.syntax_node_count;
	parser->tree->expressions.node_count = checkpoint.expression_node_count;
	parser->tree->expressions.child_count = checkpoint.expression_child_count;
	parser->tree->types.node_count = checkpoint.type_node_count;
	parser->tree->types.argument_count = checkpoint.type_argument_count;
	parser->tree->shape_dimension_count = checkpoint.shape_dimension_count;
	if (parser->error) *parser->error = checkpoint.error;
}

static const pigen_expanded_token *token_at(const syntax_parser *parser,
	size_t at)
{
	return pigen_expanded_token_get(parser->expanded,
		(pigen_token_id){(uint32_t)at});
}

static pigen_source_span invalid_source_span(void)
{
	return (pigen_source_span){{PIGEN_INVALID_ID}, 0, 0};
}

static pigen_syntax_location range_location(const syntax_parser *parser,
	size_t first,
	size_t after)
{
	return pigen_syntax_location_from_extent(parser->expanded, first, after);
}

static int token_is(const syntax_parser *parser, size_t at, const char *text)
{
	const pigen_expanded_token *token = token_at(parser, at);
	const char *known;
	size_t length;

	if (!token) return 0;
	known = pigen_expanded_token_text(parser->expanded, token, &length);
	return known && length == strlen(text) && !memcmp(known, text, length);
}

static int tokens_equal(const syntax_parser *parser, pigen_token_id left,
	pigen_token_id right)
{
	const pigen_expanded_token *left_token = token_at(parser, left.index);
	const pigen_expanded_token *right_token = token_at(parser, right.index);
	const char *left_text;
	const char *right_text;
	size_t left_length;
	size_t right_length;

	if (!left_token || !right_token) return 0;
	left_text = pigen_expanded_token_text(parser->expanded, left_token,
		&left_length);
	right_text = pigen_expanded_token_text(parser->expanded, right_token,
		&right_length);
	return left_text && right_text && left_length == right_length &&
		!memcmp(left_text, right_text, left_length);
}

static const typedef_name_fact *typedef_name_fact_in_scope(
	const syntax_parser *parser, pigen_token_id name, pigen_syntax_id scope)
{
	size_t i = parser->typedef_name_fact_count;

	while (i)
	{
		const typedef_name_fact *known = &parser->typedef_name_facts[--i];
		if (known->scope.index == scope.index &&
			tokens_equal(parser, known->name, name))
			return known;
	}
	return NULL;
}

static int structured_type_name_exists(void *context, pigen_token_id name)
{
	const structured_type_query *query = context;
	const typedef_name_fact *known;

	if (!query || !query->parser) return 0;
	known = typedef_name_fact_in_scope(query->parser, name, query->scope);
	if (!known && query->scope.index != 0)
		known = typedef_name_fact_in_scope(query->parser, name,
			(pigen_syntax_id){0});
	if (!known)
	{
		size_t i = query->parser->typedef_name_fact_count;

		while (i)
		{
			const typedef_name_fact *fact =
				&query->parser->typedef_name_facts[--i];
			if (tokens_equal(query->parser, fact->name, name))
			{
				known = fact;
				break;
			}
		}
	}
	return known && known->kind == TYPEDEF_NAME_ELIGIBLE;
}

static void add_typedef_name_fact(syntax_parser *parser,
	pigen_token_id name, pigen_syntax_id scope, typedef_name_fact_kind kind)
{
	if (parser->typedef_name_fact_count ==
		parser->typedef_name_fact_capacity)
	{
		parser->typedef_name_fact_capacity =
			parser->typedef_name_fact_capacity ?
			parser->typedef_name_fact_capacity * 2 : 8;
		parser->typedef_name_facts = pigen_resize(
			parser->typedef_name_facts,
			parser->typedef_name_fact_capacity *
				sizeof(*parser->typedef_name_facts));
	}
	parser->typedef_name_facts[parser->typedef_name_fact_count++] =
		(typedef_name_fact){name, scope, kind};
}

static pigen_syntax_type_ownership declaration_type_ownership(
	const syntax_parser *parser, pigen_syntax_id scope,
	pigen_syntax_type_id type)
{
	structured_type_query query = {parser, scope};

	return pigen_syntax_type_declaration_ownership(parser->expanded,
		&parser->tree->types, type, structured_type_name_exists, &query);
}

static int identifier(const syntax_parser *parser, size_t at)
{
	const pigen_expanded_token *token = token_at(parser, at);
	return token && token->kind == PIGEN_TOKEN_IDENTIFIER;
}

static int fail(syntax_parser *parser, size_t at, const char *message)
{
	if (parser->error)
	{
		const pigen_expanded_token *token = token_at(parser, at);
		parser->error->origin = token ? token->origin :
			INVALID_ID(pigen_origin_id);
		parser->error->span = token ? pigen_origin_expansion_span(
			parser->expanded, token->origin) : invalid_source_span();
		parser->error->message = message;
	}
	return 0;
}

static pigen_syntax_id add_node(syntax_parser *parser, pigen_syntax_node node)
{
	pigen_syntax_id result;
	pigen_syntax_tree *tree = parser->tree;

	if (tree->node_count == PIGEN_INVALID_ID)
		pigen_fail("too many syntax nodes");
	if (tree->node_count == tree->node_capacity)
	{
		tree->node_capacity = tree->node_capacity ? tree->node_capacity * 2 : 32;
		tree->nodes = pigen_resize(tree->nodes,
			tree->node_capacity * sizeof(*tree->nodes));
	}
	result = (pigen_syntax_id){(uint32_t)tree->node_count};
	tree->nodes[tree->node_count++] = node;
	return result;
}

static void add_child(syntax_parser *parser, pigen_syntax_id parent,
	pigen_syntax_id child)
{
	pigen_syntax_node *owner = &parser->tree->nodes[parent.index];
	parser->tree->nodes[child.index].parent = parent;
	if (owner->last_child.index == PIGEN_INVALID_ID)
		owner->first_child = child;
	else
		parser->tree->nodes[owner->last_child.index].next_sibling = child;
	owner->last_child = child;
}

static void add_opaque_bytes(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t end);
/* Emit the bytes between the previous emitted item and the first byte of
 * token `start` as OPAQUE filler. Opaque gaps are byte-exact so the emitter's
 * monotonic-coverage gate sees one contiguous cover of the whole source; the
 * token-extent form left whitespace uncovered and let the module-tail span
 * overlap the last parsed process. The `end` cursor is retained for the
 * historical call shape; coverage is driven by the byte cursor. */
static void add_opaque(syntax_parser *parser, pigen_syntax_id parent,
	syntax_cursor start, syntax_cursor end)
{
	const pigen_expanded_token *token;
	size_t start_byte;

	/* The gap runs to the first byte of the next parsed item (`end`); `start`
	 * records the previous item's end for the historical call shape. */
	(void)start;
	if (end >= parser->expanded->token_count) return;
	token = &parser->expanded->tokens[end];
	if (token->kind == PIGEN_TOKEN_EOF)
		start_byte = parser->source_length;
	else
		start_byte = pigen_origin_expansion_span(parser->expanded,
			token->origin).start;
	add_opaque_bytes(parser, parent, parser->opaque_byte_cursor, start_byte);
}

/* Advance the opaque byte cursor to the end of the last consumed token. */
static void advance_opaque_byte_cursor(syntax_parser *parser, size_t after)
{
	size_t end_byte;

	if (!after || after > parser->expanded->token_count) return;
	end_byte = pigen_origin_expansion_span(parser->expanded,
		parser->expanded->tokens[after - 1].origin).end;
	if (end_byte > parser->opaque_byte_cursor)
		parser->opaque_byte_cursor = end_byte;
}

/* Emit an OPAQUE node with a byte-extent span (start..end are byte offsets
 * in the source). Used to make the OPAQUE spans byte-contiguous across the
 * whole source, covering whitespace and trailing text that token-extent spans
 * miss. The emitter's monotonic-coverage gate requires OPAQUE spans to be
 * contiguous and reach the file length; token-extent spans cannot cover
 * whitespace between tokens or text after the last token. */
static void add_opaque_bytes(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t end)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id id;

	if (start >= end) return;
	node.kind = PIGEN_SYNTAX_OPAQUE;
	node.location.source_span = (pigen_source_span){
		parser->expanded->root_source, start, end};
	node.location.origin = parser->expanded->tokens[0].origin;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	id = add_node(parser, node);
	add_child(parser, parent, id);
}

static int transfer_type_at(const syntax_parser *parser, size_t at,
	pigen_transfer_type *transfer_type)
{
	const pigen_expanded_token *token = token_at(parser, at);
	const pigen_transfer_type_descriptor *descriptor;
	const char *spelling;
	size_t length;

	if (!token || token->kind != PIGEN_TOKEN_IDENTIFIER) return 0;
	spelling = pigen_expanded_token_text(parser->expanded, token, &length);
	if (!pigen_transfer_type_from_spelling(spelling, length, transfer_type))
		return 0;
	descriptor = pigen_transfer_type_descriptor_get(*transfer_type);
	return descriptor && descriptor->is_concrete;
}

static int systemverilog_prefix_transfer_type_at(
	const syntax_parser *parser, size_t at, pigen_transfer_type *transfer_type)
{
	const pigen_transfer_type_descriptor *descriptor;

	if (!transfer_type_at(parser, at, transfer_type)) return 0;
	descriptor = pigen_transfer_type_descriptor_get(*transfer_type);
	return descriptor && descriptor->is_systemverilog_prefix;
}

static size_t matching_bracket(const syntax_parser *parser, size_t open,
	size_t limit)
{
	size_t at;
	size_t depth = 0;
	for (at = open; at < limit; at++)
	{
		if (token_is(parser, at, "[")) depth++;
		else if (token_is(parser, at, "]") && !--depth) return at;
	}
	return limit;
}

static int add_shape_dimension(syntax_parser *parser, size_t open,
	size_t close)
{
	pigen_syntax_tree *tree = parser->tree;
	pigen_syntax_shape_dimension dimension = {0};
	size_t colon = open + 1;
	size_t depth = 0;

	for (; colon < close; colon++)
	{
		if (token_is(parser, colon, "(") || token_is(parser, colon, "[") ||
			token_is(parser, colon, "{")) depth++;
		else if (token_is(parser, colon, ")") ||
			token_is(parser, colon, "]") || token_is(parser, colon, "}")) depth--;
		else if (!depth && token_is(parser, colon, ":")) break;
	}
	if (colon == open + 1 || colon + 1 == close)
		return fail(parser, open, "signal dimension requires an expression");
	dimension.location = range_location(parser, open, close + 1);
	if (colon == close)
	{
		dimension.form = PIGEN_SYNTAX_SHAPE_DIMENSION_COUNT;
		if (!pigen_parse_expression(parser->expanded, open + 1, close,
			&tree->expressions, &tree->types, &dimension.as.count,
			parser->error)) return 0;
	}
	else
	{
		dimension.form = PIGEN_SYNTAX_SHAPE_DIMENSION_RANGE;
		if (!pigen_parse_expression(parser->expanded, open + 1, colon,
			&tree->expressions, &tree->types, &dimension.as.range.left,
			parser->error) ||
			!pigen_parse_expression(parser->expanded, colon + 1, close,
				&tree->expressions, &tree->types, &dimension.as.range.right,
				parser->error))
			return 0;
	}
	if (tree->shape_dimension_count == tree->shape_dimension_capacity)
	{
		tree->shape_dimension_capacity = tree->shape_dimension_capacity ?
			tree->shape_dimension_capacity * 2 : 16;
		tree->shape_dimensions = pigen_resize(tree->shape_dimensions,
			tree->shape_dimension_capacity * sizeof(*tree->shape_dimensions));
	}
	tree->shape_dimensions[tree->shape_dimension_count++] = dimension;
	return 1;
}

static pigen_syntax_id add_signal_declarator(syntax_parser *parser,
	pigen_syntax_id declaration, size_t name, size_t limit, size_t *after)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id id;
	size_t at = name + 1;

	node.kind = PIGEN_SYNTAX_SIGNAL_DECLARATOR;
	node.as.signal_declarator.first_shape_dimension =
		parser->tree->shape_dimension_count;
	while (at < limit && token_is(parser, at, "["))
	{
		size_t close = matching_bracket(parser, at, limit);
		if (close == limit)
		{
			fail(parser, at, "unterminated signal dimension");
			return INVALID_SYNTAX;
		}
		if (!add_shape_dimension(parser, at, close)) return INVALID_SYNTAX;
		node.as.signal_declarator.dimension_count++;
		at = close + 1;
	}
	node.location = range_location(parser, name, at);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.signal_declarator.name = (pigen_token_id){(uint32_t)name};
	id = add_node(parser, node);
	add_child(parser, declaration, id);
	*after = at;
	return id;
}

static int declarator_candidate(const syntax_parser *parser, size_t start,
	size_t after)
{
	size_t at = start;

	if (!identifier(parser, at)) return 0;
	at++;
	while (at < after && token_is(parser, at, "["))
	{
		size_t close = matching_bracket(parser, at, after);
		if (close == after) return 0;
		at = close + 1;
	}
	return at == after;
}

typedef enum {
	DECLARATION_NOT_RECOGNIZED,
	DECLARATION_RECOGNIZED,
	DECLARATION_INVALID
} declaration_parse_result;

static pigen_syntax_direction parse_direction(const syntax_parser *parser,
	size_t *at)
{
	if (token_is(parser, *at, "input"))
	{
		(*at)++;
		return PIGEN_DIRECTION_INPUT;
	}
	if (token_is(parser, *at, "output"))
	{
		(*at)++;
		return PIGEN_DIRECTION_OUTPUT;
	}
	if (token_is(parser, *at, "inout"))
	{
		(*at)++;
		return PIGEN_DIRECTION_INOUT;
	}
	return PIGEN_DIRECTION_INTERNAL;
}

static int leading_transfer_first_declaration(syntax_parser *parser,
	pigen_syntax_id scope, size_t transfer_at, size_t limit)
{
	const pigen_transfer_type_descriptor *descriptor;
	pigen_transfer_type transfer_type;
	pigen_syntax_type_id data_type;
	pigen_syntax_type_ownership ownership;
	syntax_checkpoint checkpoint;
	size_t name;
	int result = 0;

	if (!transfer_type_at(parser, transfer_at, &transfer_type)) return 0;
	descriptor = pigen_transfer_type_descriptor_get(transfer_type);
	if (!descriptor || descriptor->is_static) return 0;
	checkpoint = save_checkpoint(parser);
	if (pigen_parse_type_prefix(parser->expanded, transfer_at + 1, limit,
		&parser->tree->expressions, &parser->tree->types, &data_type, &name,
		parser->error))
	{
		ownership = declaration_type_ownership(parser, scope, data_type);
		result = ownership != PIGEN_SYNTAX_TYPE_UNOWNED &&
			identifier(parser, name);
	}
	restore_checkpoint(parser, checkpoint);
	return result;
}

static int parse_transfer_type_occurrence(syntax_parser *parser, size_t *at,
	size_t limit, pigen_syntax_transfer_type_occurrence *occurrence)
{
	pigen_transfer_type transfer_type;
	const pigen_transfer_type_descriptor *descriptor;
	size_t keyword = *at;

	if (!transfer_type_at(parser, keyword, &transfer_type)) return 0;
	descriptor = pigen_transfer_type_descriptor_get(transfer_type);
	if (!descriptor) return 0;
	occurrence->transfer_type = transfer_type;
	occurrence->location = range_location(parser, keyword, keyword + 1);
	occurrence->argument = INVALID_ID(pigen_syntax_expr_id);
	*at = keyword + 1;
	if (descriptor->parameter == PIGEN_TRANSFER_PARAMETER_NONE)
	{
		if (token_is(parser, *at, "["))
			return fail(parser, *at,
				"transfer type does not accept an argument");
		return 1;
	}
	if (descriptor->parameter == PIGEN_TRANSFER_PARAMETER_DEPTH)
	{
		size_t close;
		if (!token_is(parser, *at, "["))
			return fail(parser, keyword,
				"transfer type requires a depth argument");
		close = matching_bracket(parser, *at, limit);
		if (close == limit)
			return fail(parser, *at, "unterminated transfer argument");
		if (close == *at + 1)
			return fail(parser, *at, "transfer depth requires an expression");
		if (!pigen_parse_expression(parser->expanded, *at + 1, close,
			&parser->tree->expressions, &parser->tree->types,
			&occurrence->argument, parser->error)) return 0;
		*at = close + 1;
		if (token_is(parser, *at, "["))
			return fail(parser, *at,
				"transfer type accepts exactly one argument");
		return 1;
	}
	return fail(parser, keyword, "unsupported transfer parameter form");
}

static int append_declarators(syntax_parser *parser,
	pigen_syntax_id declaration, size_t start, size_t limit)
{
	size_t at = start;

	while (at < limit)
	{
		if (!identifier(parser, at))
			return fail(parser, at, "expected signal name");
		if (add_signal_declarator(parser, declaration, at, limit,
			&at).index == PIGEN_INVALID_ID) return 0;
		if (at == limit) return 1;
		if (!token_is(parser, at, ","))
			return fail(parser, at,
				"expected `,` or declaration terminator after signal name");
		at++;
	}
	return fail(parser, at, "declaration requires a signal name");
}

static pigen_syntax_id add_declaration_node(syntax_parser *parser,
	pigen_syntax_direction direction, pigen_syntax_type_id data_type)
{
	pigen_syntax_node node = {0};

	node.kind = PIGEN_SYNTAX_SIGNAL_DECLARATION;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.signal_declaration.direction = direction;
	node.as.signal_declaration.data_type = data_type;
	node.as.signal_declaration.transfer_type.argument =
		INVALID_ID(pigen_syntax_expr_id);
	return add_node(parser, node);
}

static declaration_parse_result parse_systemverilog_static_declaration(
	syntax_parser *parser, pigen_syntax_id scope, size_t start, size_t limit,
	pigen_syntax_id *declaration)
{
	size_t at = start;
	size_t name;
	size_t parsed_after;
	pigen_syntax_direction direction = parse_direction(parser, &at);
	pigen_syntax_type_id data_type;
	pigen_transfer_type prefix_transfer_type;
	size_t prefix_transfer_at = at;
	int has_prefix_transfer = systemverilog_prefix_transfer_type_at(parser, at,
		&prefix_transfer_type);
	int has_static_prefix = has_prefix_transfer || token_is(parser, at, "logic") ||
		token_is(parser, at, "bit") || token_is(parser, at, "byte") ||
		token_is(parser, at, "integer") ||
		token_is(parser, at, "shortint") ||
		token_is(parser, at, "longint") ||
		token_is(parser, at, "time") || token_is(parser, at, "signed") ||
		token_is(parser, at, "unsigned") || token_is(parser, at, "[") ||
		(direction != PIGEN_DIRECTION_INTERNAL &&
			declarator_candidate(parser, at, limit));
	pigen_syntax_id id;

	if (!has_static_prefix) return DECLARATION_NOT_RECOGNIZED;
	if (has_prefix_transfer)
	{
		at++;
		if (declarator_candidate(parser, at, limit))
		{
			name = at;
			if (!pigen_parse_type_prefix(parser->expanded, at, at,
				&parser->tree->expressions, &parser->tree->types, &data_type,
				&parsed_after, parser->error))
				return DECLARATION_NOT_RECOGNIZED;
		}
		else
		{
			if (!pigen_parse_type_prefix(parser->expanded, at, limit,
				&parser->tree->expressions, &parser->tree->types, &data_type,
				&name, parser->error)) return DECLARATION_NOT_RECOGNIZED;
		}
	}
	else
	{
		if (direction != PIGEN_DIRECTION_INTERNAL &&
			declarator_candidate(parser, at, limit))
		{
			name = at;
			if (!pigen_parse_type_prefix(parser->expanded, at, at,
				&parser->tree->expressions, &parser->tree->types, &data_type,
				&parsed_after, parser->error))
				return DECLARATION_NOT_RECOGNIZED;
		}
		else if (!pigen_parse_type_prefix(parser->expanded, at, limit,
			&parser->tree->expressions, &parser->tree->types, &data_type, &name,
			parser->error)) return DECLARATION_NOT_RECOGNIZED;
		if (transfer_type_at(parser, name, &prefix_transfer_type) &&
			declaration_type_ownership(parser, scope, data_type) ==
				PIGEN_SYNTAX_TYPE_PIGEN)
			return DECLARATION_NOT_RECOGNIZED;
	}
	if (!identifier(parser, name)) return DECLARATION_NOT_RECOGNIZED;
	id = add_declaration_node(parser, direction, data_type);
	if (has_prefix_transfer)
	{
		parser->tree->nodes[id.index].as.signal_declaration.has_transfer_type = 1;
		parser->tree->nodes[id.index].as.signal_declaration.transfer_type.
			transfer_type = prefix_transfer_type;
		parser->tree->nodes[id.index].as.signal_declaration.transfer_type.location =
			range_location(parser, prefix_transfer_at, prefix_transfer_at + 1);
	}
	if (!append_declarators(parser, id, name, limit))
		return DECLARATION_NOT_RECOGNIZED;
	*declaration = id;
	return DECLARATION_RECOGNIZED;
}

static declaration_parse_result parse_data_first_declaration(
	syntax_parser *parser, pigen_syntax_id scope, size_t start, size_t limit,
	int ansi,
	pigen_syntax_id *declaration, int *decisive)
{
	size_t at = start;
	size_t type_start;
	pigen_syntax_direction direction = parse_direction(parser, &at);
	pigen_syntax_type_id data_type;
	pigen_syntax_transfer_type_occurrence transfer = {0};
	pigen_transfer_type transfer_type;
	const pigen_transfer_type_descriptor *descriptor;
	pigen_syntax_type_ownership ownership;
	pigen_syntax_id id;
	int has_transfer_type = 0;

	*decisive = 0;
	type_start = at;
	if (!pigen_parse_type_prefix(parser->expanded, type_start, limit,
		&parser->tree->expressions, &parser->tree->types, &data_type, &at,
		parser->error)) return DECLARATION_NOT_RECOGNIZED;
	ownership = declaration_type_ownership(parser, scope, data_type);
	*decisive = ownership == PIGEN_SYNTAX_TYPE_PIGEN ||
		ownership == PIGEN_SYNTAX_TYPE_STRUCTURED;
	if (transfer_type_at(parser, at, &transfer_type))
	{
		descriptor = pigen_transfer_type_descriptor_get(transfer_type);
		*decisive = 1;
		has_transfer_type = 1;
		if (ansi && direction == PIGEN_DIRECTION_INTERNAL && descriptor &&
			!descriptor->is_static)
		{
			fail(parser, start,
				"ANSI dynamic signal port requires `input` or `output`");
			return DECLARATION_INVALID;
		}
		if (!parse_transfer_type_occurrence(parser, &at, limit, &transfer))
			return DECLARATION_INVALID;
	}
	if (!identifier(parser, at))
	{
		if (*decisive)
		{
			fail(parser, at, "declaration requires a signal name");
			return DECLARATION_INVALID;
		}
		return DECLARATION_NOT_RECOGNIZED;
	}
	if (!*decisive) return DECLARATION_NOT_RECOGNIZED;
	id = add_declaration_node(parser, direction, data_type);
	if (has_transfer_type)
	{
		parser->tree->nodes[id.index].as.signal_declaration.has_transfer_type = 1;
		parser->tree->nodes[id.index].as.signal_declaration.transfer_type = transfer;
	}
	if (!append_declarators(parser, id, at, limit))
		return *decisive ? DECLARATION_INVALID : DECLARATION_NOT_RECOGNIZED;
	*declaration = id;
	return DECLARATION_RECOGNIZED;
}

static void commit_declaration(syntax_parser *parser, pigen_syntax_id module,
	size_t start, size_t declaration_after, syntax_cursor *opaque_cursor,
	pigen_syntax_id declaration)
{
	const pigen_expanded_token *decl_tok;
	const pigen_expanded_token *start_tok;
	size_t opaque_start;
	size_t opaque_end;

	parser->tree->nodes[declaration.index].location = range_location(parser,
		start, declaration_after);
	/* Emit the OPAQUE span between the previous item's end (byte cursor) and
	 * this declaration's start, using byte-extent so whitespace is covered. */
	decl_tok = pigen_expanded_token_get(parser->expanded,
		(pigen_token_id){(uint32_t)(declaration_after - 1)});
	start_tok = pigen_expanded_token_get(parser->expanded,
		(pigen_token_id){(uint32_t)start});
	opaque_start = parser->opaque_byte_cursor;
	opaque_end = start_tok ?
		pigen_origin_expansion_span(parser->expanded,
			start_tok->origin).start : parser->opaque_byte_cursor;
	if (start_tok && decl_tok)
		parser->opaque_byte_cursor = pigen_origin_expansion_span(
			parser->expanded, decl_tok->origin).end;
	add_opaque_bytes(parser, module, opaque_start, opaque_end);
	add_child(parser, module, declaration);
	*opaque_cursor = declaration_after;
}

static int decisive_typedef_without_terminator(syntax_parser *parser,
	pigen_syntax_id parent, size_t start, size_t boundary)
{
	syntax_checkpoint checkpoint = save_checkpoint(parser);
	pigen_syntax_type_id type;
	pigen_syntax_type_ownership ownership;
	size_t name;
	int decisive = 0;

	if (pigen_parse_type_prefix(parser->expanded, start + 1, boundary,
		&parser->tree->expressions, &parser->tree->types, &type, &name,
		parser->error))
	{
		ownership = declaration_type_ownership(parser, parent, type);
		decisive = (ownership == PIGEN_SYNTAX_TYPE_PIGEN ||
			ownership == PIGEN_SYNTAX_TYPE_STRUCTURED) &&
			identifier(parser, name) && name + 1 == boundary;
	}
	restore_checkpoint(parser, checkpoint);
	return decisive;
}

static declaration_parse_result parse_typedef(syntax_parser *parser,
	pigen_syntax_id parent,
	size_t start, size_t semicolon, syntax_cursor *opaque_cursor)
{
	syntax_checkpoint checkpoint = save_checkpoint(parser);
	pigen_syntax_type_id type;
	pigen_syntax_type_ownership ownership;
	pigen_syntax_location declaration = range_location(parser, start,
		semicolon + 1);
	pigen_syntax_node node = {0};
	pigen_syntax_id id;
	size_t name;

	if (!pigen_parse_type_prefix(parser->expanded, start + 1, semicolon,
		&parser->tree->expressions, &parser->tree->types, &type, &name,
		parser->error)) return DECLARATION_INVALID;
	ownership = declaration_type_ownership(parser, parent, type);
	if (ownership == PIGEN_SYNTAX_TYPE_UNOWNED)
	{
		restore_checkpoint(parser, checkpoint);
		if (declarator_candidate(parser, name, semicolon))
			add_typedef_name_fact(parser, (pigen_token_id){(uint32_t)name},
				parent, TYPEDEF_NAME_OPAQUE_SHADOW);
		return DECLARATION_NOT_RECOGNIZED;
	}
	if (!identifier(parser, name))
	{
		fail(parser, name, "typedef requires a name");
		return DECLARATION_INVALID;
	}
	if (name + 1 != semicolon)
	{
		if (ownership == PIGEN_SYNTAX_TYPE_SYSTEMVERILOG)
		{
			int introduced_name = declarator_candidate(parser, name, semicolon);

			restore_checkpoint(parser, checkpoint);
			if (introduced_name)
				add_typedef_name_fact(parser,
					(pigen_token_id){(uint32_t)name}, parent,
					TYPEDEF_NAME_OPAQUE_SHADOW);
			return DECLARATION_NOT_RECOGNIZED;
		}
		fail(parser, name + 1, "typedef permits exactly one name");
		return DECLARATION_INVALID;
	}
	add_opaque(parser, parent, *opaque_cursor, start);
	node.kind = PIGEN_SYNTAX_TYPEDEF;
	node.location = declaration;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.type_definition.name = (pigen_token_id){(uint32_t)name};
	node.as.type_definition.type = type;
	id = add_node(parser, node);
	add_child(parser, parent, id);
	add_typedef_name_fact(parser, node.as.type_definition.name, parent,
		TYPEDEF_NAME_ELIGIBLE);
	advance_opaque_byte_cursor(parser, semicolon + 1);
	*opaque_cursor = semicolon + 1;
	return DECLARATION_RECOGNIZED;
}

static int block_opener(const syntax_parser *parser, size_t at)
{
	return token_is(parser, at, "begin") || token_is(parser, at, "case") ||
		token_is(parser, at, "casex") || token_is(parser, at, "casez") ||
		token_is(parser, at, "function") || token_is(parser, at, "task") ||
		token_is(parser, at, "generate") || token_is(parser, at, "class");
}

static int block_closer(const syntax_parser *parser, size_t at)
{
	return token_is(parser, at, "end") || token_is(parser, at, "endcase") ||
		token_is(parser, at, "endfunction") || token_is(parser, at, "endtask") ||
		token_is(parser, at, "endgenerate") || token_is(parser, at, "endclass");
}

static size_t matching_parenthesis(const syntax_parser *parser, size_t open,
	size_t limit)
{
	size_t at;
	size_t depth = 0;

	for (at = open; at < limit; at++)
	{
		if (token_is(parser, at, "(")) depth++;
		else if (token_is(parser, at, ")") && !--depth) return at;
	}
	return limit;
}

static size_t matching_begin_end(const syntax_parser *parser, size_t begin,
	size_t limit)
{
	size_t at;
	size_t depth = 0;

	for (at = begin; at < limit; at++)
	{
		if (token_is(parser, at, "begin")) depth++;
		else if (token_is(parser, at, "end") && !--depth) return at;
	}
	return limit;
}

static size_t ansi_item_end(const syntax_parser *parser, size_t start,
	size_t after)
{
	size_t at;
	size_t parens = 0;
	size_t brackets = 0;
	size_t braces = 0;

	for (at = start; at < after; at++)
	{
		if (token_is(parser, at, "(")) parens++;
		else if (token_is(parser, at, ")")) parens--;
		else if (token_is(parser, at, "[")) brackets++;
		else if (token_is(parser, at, "]")) brackets--;
		else if (token_is(parser, at, "{")) braces++;
		else if (token_is(parser, at, "}")) braces--;
		else if (!parens && !brackets && !braces && token_is(parser, at, ","))
			break;
	}
	return at;
}

static size_t top_level_token(const syntax_parser *parser, size_t start,
	size_t after, const char *wanted)
{
	size_t at;
	size_t parens = 0;
	size_t brackets = 0;
	size_t braces = 0;

	for (at = start; at < after; at++)
	{
		if (!parens && !brackets && !braces && token_is(parser, at, wanted))
			return at;
		if (token_is(parser, at, "(")) parens++;
		else if (token_is(parser, at, ")") && parens) parens--;
		else if (token_is(parser, at, "[")) brackets++;
		else if (token_is(parser, at, "]") && brackets) brackets--;
		else if (token_is(parser, at, "{")) braces++;
		else if (token_is(parser, at, "}") && braces) braces--;
	}
	return after;
}

static int simple_parameter_shape(const syntax_parser *parser, size_t start,
	size_t after, int has_keyword, size_t *name, size_t *equals)
{
	size_t at = start + (has_keyword ? 1 : 0);
	size_t equal = top_level_token(parser, at, after, "=");

	if (equal == after || equal + 1 == after || at + 1 != equal ||
		!identifier(parser, at))
		return 0;
	*name = at;
	*equals = equal;
	return 1;
}

static int parse_parameter(syntax_parser *parser, pigen_syntax_id module,
	size_t start, size_t after, int has_keyword, int is_local,
	syntax_cursor *opaque_cursor)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id id;
	size_t name;
	size_t equals;

	{
		size_t at = start + (has_keyword ? 1 : 0);
		size_t equal = top_level_token(parser, at, after, "=");
		pigen_syntax_type_id typed = INVALID_ID(pigen_syntax_type_id);
		size_t typed_name = 0;

		if (equal != after &&
			pigen_parse_type_prefix(parser->expanded, at, equal,
				&parser->tree->expressions, &parser->tree->types, &typed,
				&typed_name, parser->error) &&
			identifier(parser, typed_name) && typed_name > at &&
			typed_name + 1 == equal)
		{
			name = typed_name;
			equals = equal;
			node.as.parameter.has_type = 1;
			node.as.parameter.data_type = typed;
		}
		else if (!simple_parameter_shape(parser, start, after, has_keyword,
			&name, &equals))
			return 0;
	}
	add_opaque(parser, module, *opaque_cursor, start);
	node.kind = PIGEN_SYNTAX_PARAMETER;
	node.location = range_location(parser, start, after);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.parameter.name = (pigen_token_id){(uint32_t)name};
	node.as.parameter.is_local = is_local;
	if (!pigen_parse_expression(parser->expanded, equals + 1, after,
		&parser->tree->expressions, &parser->tree->types, &node.as.parameter.value, parser->error))
		return 0;
	id = add_node(parser, node);
	add_child(parser, module, id);
	advance_opaque_byte_cursor(parser, after);
	*opaque_cursor = after;
	return 1;
}

static int parse_parameter_items(syntax_parser *parser,
	pigen_syntax_id module, size_t first, size_t after,
	syntax_cursor *opaque_cursor)
{
	size_t at = first;
	int continuation = 0;
	int continuation_is_local = 0;

	while (at < after)
	{
		size_t item_end = ansi_item_end(parser, at, after);
		int has_keyword = token_is(parser, at, "parameter") ||
			token_is(parser, at, "localparam");
		int is_local = has_keyword ? token_is(parser, at, "localparam") :
			continuation_is_local;
		size_t ignored_name;
		size_t ignored_equals;
		int has_equal = top_level_token(parser, at, item_end, "=") !=
			item_end;
		int shaped = ((has_keyword && has_equal) ||
			(continuation && simple_parameter_shape(parser, at, item_end,
				has_keyword, &ignored_name, &ignored_equals)));

		if (shaped)
		{
			if (!parse_parameter(parser, module, at, item_end, has_keyword,
				is_local, opaque_cursor)) return 0;
			continuation = 1;
			continuation_is_local = is_local;
		}
		else
		{
			continuation = 0;
			continuation_is_local = 0;
		}
		at = item_end < after ? item_end + 1 : after;
	}
	return 1;
}

static int parse_ansi_ports(syntax_parser *parser,
	pigen_syntax_id module, size_t first, size_t after,
	syntax_cursor *opaque_cursor)
{
	size_t at = first;

	while (at < after)
	{
		size_t item_end = ansi_item_end(parser, at, after);
		size_t declaration_after = item_end;
		size_t continuation = item_end;
		size_t type_at = at;
		int has_direction = token_is(parser, at, "input") ||
			token_is(parser, at, "output") || token_is(parser, at, "inout");
		syntax_checkpoint checkpoint;
		declaration_parse_result result;
		pigen_syntax_id declaration = INVALID_SYNTAX;
		int decisive = 0;

		if (has_direction) type_at++;
		if (leading_transfer_first_declaration(parser, module, type_at, item_end))
			return fail(parser, type_at,
				"data type must precede the transfer type");
		while (continuation < after)
		{
			size_t next = continuation + 1;
			size_t next_end = ansi_item_end(parser, next, after);
			if (!declarator_candidate(parser, next, next_end)) break;
			declaration_after = next_end;
			continuation = next_end;
		}

		checkpoint = save_checkpoint(parser);
		result = parse_systemverilog_static_declaration(parser, module, at, item_end,
			&declaration);
		if (result == DECLARATION_RECOGNIZED)
		{
			continuation = item_end;
			while (continuation < declaration_after)
			{
				size_t next = continuation + 1;
				size_t next_end = ansi_item_end(parser, next, after);
				if (!append_declarators(parser, declaration, next, next_end))
					break;
				continuation = next_end;
			}
			if (continuation == declaration_after)
			{
				commit_declaration(parser, module, at, declaration_after,
					opaque_cursor, declaration);
				item_end = declaration_after;
				at = item_end < after ? item_end + 1 : after;
				continue;
			}
		}
		restore_checkpoint(parser, checkpoint);

		checkpoint = save_checkpoint(parser);
		result = parse_data_first_declaration(parser, module, at, item_end, 1,
			&declaration, &decisive);
		if (result == DECLARATION_INVALID) return 0;
		if (result == DECLARATION_RECOGNIZED)
		{
			continuation = item_end;
			while (continuation < declaration_after)
			{
				size_t next = continuation + 1;
				size_t next_end = ansi_item_end(parser, next, after);
				if (!append_declarators(parser, declaration, next, next_end))
				{
					if (decisive) return 0;
					break;
				}
				continuation = next_end;
			}
			if (continuation == declaration_after)
			{
				commit_declaration(parser, module, at, declaration_after,
					opaque_cursor, declaration);
				item_end = declaration_after;
				at = item_end < after ? item_end + 1 : after;
				continue;
			}
		}
		restore_checkpoint(parser, checkpoint);
		at = item_end < after ? item_end + 1 : after;
	}
	return 1;
}

static const char *opaque_unit_closer(const syntax_parser *parser, size_t at)
{
	if (token_is(parser, at, "package")) return "endpackage";
	if (token_is(parser, at, "interface")) return "endinterface";
	if (token_is(parser, at, "program")) return "endprogram";
	if (token_is(parser, at, "class")) return "endclass";
	if (token_is(parser, at, "checker")) return "endchecker";
	if (token_is(parser, at, "primitive")) return "endprimitive";
	if (token_is(parser, at, "config")) return "endconfig";
	return NULL;
}

static int parse_pipeline_reset(syntax_parser *parser,
	pigen_syntax_id parent, size_t start, size_t limit, size_t *after,
	size_t *assignment_count)
{
	const char *text;
	size_t length = 0;
	const pigen_expanded_token *token = token_at(parser, start);

	(void)parent;
	(void)limit;
	text = token ? pigen_expanded_token_text(parser->expanded, token, &length) :
		NULL;
	if (!text || !((length == 14 && !memcmp(text, "pipeline_reset", 14)) ||
		(length == 10 && !memcmp(text, "pipe_reset", 10))))
		return 0;
	if (!token_is(parser, start + 1, "(") || !identifier(parser, start + 2) ||
		!token_is(parser, start + 3, ")") || !token_is(parser, start + 4, ";"))
		return 0;
	{
		pigen_syntax_node node = {0};

		node.kind = PIGEN_SYNTAX_ACTION;
		node.location = range_location(parser, start, start + 5);
		node.parent = INVALID_SYNTAX;
		node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
		node.as.action.action = 4;
		node.as.action.target = (pigen_token_id){(uint32_t)(start + 2)};
		add_child(parser, parent, add_node(parser, node));
	}
	*after = start + 5;
	(*assignment_count)++;
	return 1;
}

static int parse_procedural_statement(syntax_parser *parser,
	pigen_syntax_id parent, size_t start, size_t limit, size_t *after_statement,
	size_t *assignment_count);

static int parse_pipeline(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t limit, size_t *after_statement,
	size_t *assignment_count);

static int parse_fsm(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t limit, size_t *after_statement);

static int parse_procedural_block(syntax_parser *parser,
	pigen_syntax_id parent, size_t start, size_t limit, size_t *after_statement,
	size_t *assignment_count)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id block;
	size_t at = start + 1;

	if (!token_is(parser, start, "begin") || token_is(parser, at, ":"))
		return 0;
	node.kind = PIGEN_SYNTAX_PROCEDURAL_BLOCK;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	block = add_node(parser, node);
	add_child(parser, parent, block);
	while (at < limit && !token_is(parser, at, "end"))
	{
		if (!parse_procedural_statement(parser, block, at, limit, &at,
			assignment_count))
		{
			if (parser->fsm_states)
				parser->fsm_body_failed = 1;
			return 0;
		}
	}
	if (at == limit || !token_is(parser, at, "end") ||
		token_is(parser, at + 1, ":")) return 0;
	*after_statement = at + 1;
	parser->tree->nodes[block.index].location = range_location(parser, start,
		*after_statement);
	return 1;
}

static int parse_if_statement(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t limit, size_t *after_statement,
	size_t *assignment_count)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id statement;
	size_t close;
	size_t at;

	if (!token_is(parser, start, "if") || !token_is(parser, start + 1, "("))
		return 0;
	close = matching_parenthesis(parser, start + 1, limit);
	if (close == limit || close == start + 2) return 0;
	node.kind = PIGEN_SYNTAX_IF_STATEMENT;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	if (!pigen_parse_expression(parser->expanded, start + 2, close,
		&parser->tree->expressions, &parser->tree->types, &node.as.if_statement.condition,
		parser->error)) return 0;
	statement = add_node(parser, node);
	add_child(parser, parent, statement);
	at = close + 1;
	if (!parse_procedural_statement(parser, statement, at, limit, &at,
		assignment_count))
	{
		if (parser->fsm_states)
			parser->fsm_body_failed = 1;
		return 0;
	}
	if (token_is(parser, at, "else"))
	{
		parser->tree->nodes[statement.index].as.if_statement.has_else = 1;
		if (!parse_procedural_statement(parser, statement, at + 1, limit, &at,
			assignment_count))
		{
			if (parser->fsm_states)
				parser->fsm_body_failed = 1;
			return 0;
		}
	}
	*after_statement = at;
	parser->tree->nodes[statement.index].location = range_location(parser, start,
		at);
	return 1;
}

static int parse_nonblocking_assignment(syntax_parser *parser,
	pigen_syntax_id parent, size_t start, size_t limit, size_t *after_statement,
	size_t *assignment_count)
{
	const size_t semicolon = top_level_token(parser, start, limit, ";");
	const size_t separator = top_level_token(parser, start, semicolon, "<=");
	pigen_syntax_node node = {0};

	if (semicolon == limit || separator == start || separator == semicolon ||
		separator + 1 == semicolon)
	{
		if (parser->fsm_states)
			return fail(parser, start,
				"fsm state body statement must be an ordinary statement");
		return 0;
	}
	node.kind = PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT;
	node.location = range_location(parser, start, semicolon + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	if (!pigen_parse_expression(parser->expanded, start, separator,
		&parser->tree->expressions, &parser->tree->types,
		&node.as.nonblocking_assignment.destination, parser->error) ||
		!pigen_parse_expression(parser->expanded, separator + 1, semicolon,
			&parser->tree->expressions, &parser->tree->types,
			&node.as.nonblocking_assignment.value, parser->error)) return 0;
	add_child(parser, parent, add_node(parser, node));
	(*assignment_count)++;
	*after_statement = semicolon + 1;
	return 1;
}

static int pipeline_scope_push(pipeline_scope_stack *stack)
{
	if (stack->depth >= MAX_PIPELINE_SCOPES) return 0;
	stack->scopes[stack->depth] = (pipeline_scope){0};
	stack->depth++;
	return 1;
}

static void pipeline_scope_pop(pipeline_scope_stack *stack)
{
	pipeline_scope *top = &stack->scopes[--stack->depth];
	free(top->fields);
	*top = (pipeline_scope){0};
}

static int pipeline_scope_add(pipeline_scope *scope, pigen_token_id name,
	pipeline_scope_kind kind)
{
	if (scope->count == scope->capacity)
	{
		scope->capacity = scope->capacity ? scope->capacity * 2 : 4;
		scope->fields = pigen_resize(scope->fields,
			scope->capacity * sizeof(*scope->fields));
	}
	scope->fields[scope->count].name = name;
	scope->fields[scope->count].kind = kind;
	scope->fields[scope->count].index = scope->count;
	scope->count++;
	return 1;
}

static int pipeline_scope_contains(const syntax_parser *parser,
	const pipeline_scope *scope, pigen_token_id name)
{
	size_t i;
	for (i = 0; i < scope->count; i++)
		if (tokens_equal(parser, scope->fields[i].name, name)) return 1;
	return 0;
}

static int parse_pipeline_field(syntax_parser *parser,
	pigen_syntax_id scope_id, size_t start, size_t semicolon)
{
	pigen_syntax_type_id data_type;
	pigen_transfer_type transfer_type;
	pigen_syntax_direction direction;
	pigen_syntax_type_ownership ownership;
	pigen_syntax_node node = {0};
	pigen_syntax_id declaration;
	size_t at = start;
	size_t name;
	size_t transfer_at = 0;
	int has_explicit_transfer = 0;
	pigen_token_id name_token;

	direction = parse_direction(parser, &at);
	if (transfer_type_at(parser, at, &transfer_type))
	{
		if (transfer_type != PIGEN_TRANSFER_TYPE_BUF)
		{
			fail(parser, at, "pipeline field transfer type must be buf");
			return 0;
		}
		transfer_at = at;
		has_explicit_transfer = 1;
		at++;
	}
	if (!pigen_parse_type_prefix(parser->expanded, at, semicolon,
		&parser->tree->expressions, &parser->tree->types, &data_type, &name,
		parser->error))
	{
		fail(parser, has_explicit_transfer ? at : start,
			"pipeline field declaration requires a data type");
		return 0;
	}
	if (!has_explicit_transfer && identifier(parser, name) &&
		token_is(parser, name, "buf") && identifier(parser, name + 1))
	{
		transfer_at = name;
		has_explicit_transfer = 1;
		name++;
	}
	if (!identifier(parser, name))
	{
		fail(parser, name, "pipeline field declaration requires a field name");
		return 0;
	}
	if (name + 1 != semicolon)
	{
		fail(parser, name + 1,
			"pipeline field declaration declares exactly one field");
		return 0;
	}
	ownership = declaration_type_ownership(parser, scope_id, data_type);
	if (ownership == PIGEN_SYNTAX_TYPE_UNOWNED)
	{
		fail(parser, at, "pipeline field declaration requires a data type");
		return 0;
	}
	name_token = (pigen_token_id){(uint32_t)name};
	if (parser->pipeline_scopes.depth)
	{
		const pipeline_scope *pipeline_scope =
			&parser->pipeline_scopes.scopes[0];
		size_t d;
		if (pipeline_scope_contains(parser, pipeline_scope, name_token))
		{
			fail(parser, name, "duplicate pipeline field name");
			return 0;
		}
		for (d = parser->pipeline_scopes.depth; d-- > 1;)
		{
			if (pipeline_scope_contains(parser, &parser->pipeline_scopes
					.scopes[d], name_token))
			{
				fail(parser, name,
					"pipeline field shadows a stage-local name");
				return 0;
			}
		}
	}
	node.kind = PIGEN_SYNTAX_SIGNAL_DECLARATION;
	node.location = range_location(parser, start, semicolon + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.signal_declaration.direction = direction;
	node.as.signal_declaration.data_type = data_type;
	node.as.signal_declaration.has_transfer_type = 1;
	node.as.signal_declaration.transfer_type.transfer_type =
		PIGEN_TRANSFER_TYPE_BUF;
	node.as.signal_declaration.transfer_type.argument =
		INVALID_ID(pigen_syntax_expr_id);
	if (has_explicit_transfer)
		node.as.signal_declaration.transfer_type.location =
			range_location(parser, transfer_at, transfer_at + 1);
	declaration = add_node(parser, node);
	if (add_signal_declarator(parser, declaration, name, semicolon,
		&at).index == PIGEN_INVALID_ID) return 0;
	add_child(parser, scope_id, declaration);
	if (parser->pipeline_scopes.depth)
	{
		pipeline_scope *top = &parser->pipeline_scopes
			.scopes[parser->pipeline_scopes.depth - 1];
		pipeline_scope_add(top, name_token, PIPELINE_SCOPE_PIPELINE);
	}
	return 1;
}

static int parse_pipeline_stage_item(syntax_parser *parser,
	pigen_syntax_id stage, size_t start, size_t limit, size_t *after_item);

static int parse_pipeline_stage(syntax_parser *parser, pigen_syntax_id pipeline,
	size_t start, size_t limit, size_t *after_stage)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id stage;
	size_t at = start + 1;
	size_t item_after;
	int saw_item = 0;
	int has_name = 0;

	if (!token_is(parser, start, "stage")) return 0;
	/* Compact stage form: `stage: statement;` (no begin/end). */
	if (token_is(parser, at, ":"))
	{
		size_t item_after;

		node.kind = PIGEN_SYNTAX_STAGE;
		node.as.stage.has_name = 0;
		node.parent = INVALID_SYNTAX;
		node.first_child = node.last_child = node.next_sibling =
			INVALID_SYNTAX;
		stage = add_node(parser, node);
		add_child(parser, pipeline, stage);
		if (!parse_pipeline_stage_item(parser, stage, at + 1, limit,
			&item_after))
			return 0;
		parser->tree->nodes[stage.index].location = range_location(parser,
			start, item_after);
		*after_stage = item_after;
		return 1;
	}
	if (at >= limit)
		return fail(parser, start, "pipeline stage requires `begin`");
	if (identifier(parser, at) && !token_is(parser, at, "begin"))
	{
		size_t begin = at + 1;
		while (begin < limit && (token_is(parser, begin, " ") ||
			token_is(parser, begin, "\t"))) begin++;
		if (begin >= limit || !token_is(parser, begin, "begin"))
			return fail(parser, at, "named pipeline stage requires `begin`");
		node.as.stage.name = (pigen_token_id){(uint32_t)at};
		has_name = 1;
		at = begin;
	}
	if (!token_is(parser, at, "begin"))
		return fail(parser, at, "pipeline stage requires `begin`");
	node.kind = PIGEN_SYNTAX_STAGE;
	node.as.stage.has_name = has_name;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	stage = add_node(parser, node);
	add_child(parser, pipeline, stage);
	if (!pipeline_scope_push(&parser->pipeline_scopes))
		return fail(parser, at, "too many pipeline scopes");
	at++;
	while (at < limit && !token_is(parser, at, "end"))
	{
		if (!parse_pipeline_stage_item(parser, stage, at, limit, &item_after))
		{
			pipeline_scope_pop(&parser->pipeline_scopes);
			return 0;
		}
		saw_item = 1;
		at = item_after;
	}
	if (!saw_item)
	{
		pipeline_scope_pop(&parser->pipeline_scopes);
		return fail(parser, start, "pipeline stage requires at least one item");
	}
	pipeline_scope_pop(&parser->pipeline_scopes);
	if (at == limit || !token_is(parser, at, "end"))
		return fail(parser, at, "pipeline stage requires `end`");
	node.location = range_location(parser, start, at + 1);
	parser->tree->nodes[stage.index].location = node.location;
	*after_stage = at + 1;
	return 1;
}

static int parse_pipeline_stage_item(syntax_parser *parser,
	pigen_syntax_id stage, size_t start, size_t limit, size_t *after_item)
{
	size_t semicolon = top_level_token(parser, start, limit, ";");
	size_t separator;
	pigen_syntax_type_id data_type;
	pigen_syntax_direction direction;
	pigen_syntax_type_ownership ownership;
	pigen_syntax_node node = {0};
	pigen_syntax_id declaration;
	size_t at = start;
	size_t name;
	int is_declaration;

	if (semicolon == limit)
		return fail(parser, start, "unterminated pipeline stage item");
	separator = top_level_token(parser, start, semicolon, "<=");
	if (separator > start && separator < semicolon)
	{
		size_t value_start = separator + 1;
		pigen_syntax_expr_id destination, value;

		if (value_start == semicolon)
			return fail(parser, separator,
				"pipeline stage assignment requires a value");
		if (!pigen_parse_expression(parser->expanded, start, separator,
			&parser->tree->expressions, &parser->tree->types, &destination,
			parser->error) ||
			!pigen_parse_expression(parser->expanded, value_start, semicolon,
				&parser->tree->expressions, &parser->tree->types, &value,
				parser->error)) return 0;
		node.kind = PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT;
		node.location = range_location(parser, start, semicolon + 1);
		node.parent = INVALID_SYNTAX;
		node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
		node.as.nonblocking_assignment.destination = destination;
		node.as.nonblocking_assignment.value = value;
		add_child(parser, stage, add_node(parser, node));
		*after_item = semicolon + 1;
		return 1;
	}
	if (token_is(parser, start, "yield"))
		return fail(parser, start,
			"yield is only after the final stage");
	at = start;
	direction = parse_direction(parser, &at);
	if (!pigen_parse_type_prefix(parser->expanded, at, semicolon,
		&parser->tree->expressions, &parser->tree->types, &data_type, &name,
		parser->error))
		return fail(parser, start, "unrecognized pipeline stage item");
	if (!identifier(parser, name))
		return fail(parser, start, "unrecognized pipeline stage item");
	ownership = declaration_type_ownership(parser, stage, data_type);
	is_declaration = (ownership == PIGEN_SYNTAX_TYPE_PIGEN ||
		ownership == PIGEN_SYNTAX_TYPE_STRUCTURED);
	if (!is_declaration)
		return fail(parser, start, "unrecognized pipeline stage item");
	node.kind = PIGEN_SYNTAX_SIGNAL_DECLARATION;
	node.location = range_location(parser, start, semicolon + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.signal_declaration.direction = direction;
	node.as.signal_declaration.data_type = data_type;
	node.as.signal_declaration.transfer_type.argument =
		INVALID_ID(pigen_syntax_expr_id);
	declaration = add_node(parser, node);
	if (add_signal_declarator(parser, declaration, name, semicolon,
		&at).index == PIGEN_INVALID_ID) return 0;
	add_child(parser, stage, declaration);
	{
		pigen_token_id name_token = (pigen_token_id){(uint32_t)name};
		pipeline_scope *stage_scope = &parser->pipeline_scopes
			.scopes[parser->pipeline_scopes.depth - 1];
		size_t d;
		if (pipeline_scope_contains(parser, stage_scope, name_token))
		{
			fail(parser, name, "duplicate stage-local name");
			return 0;
		}
		for (d = parser->pipeline_scopes.depth - 1; d-- > 0;)
		{
			if (pipeline_scope_contains(parser, &parser->pipeline_scopes
					.scopes[d], name_token))
			{
				fail(parser, name,
					"stage-local name shadows a pipeline field");
				return 0;
			}
		}
		pipeline_scope_add(stage_scope, name_token, PIPELINE_SCOPE_STAGE);
	}
	*after_item = semicolon + 1;
	return 1;
}

static int parse_pipeline(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t limit, size_t *after_statement,
	size_t *assignment_count)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id pipeline;
	size_t endpipeline;
	size_t name, at;
	int saw_stage = 0;
	int saw_yield = 0;

	if (!token_is(parser, start, "pipeline")) return 0;
	parser->pipeline_failed = 1;
	name = start + 1;
	if (name >= limit || !identifier(parser, name))
		return fail(parser, start, "pipeline requires a name");
	at = name + 1;
	while (at < limit && (token_is(parser, at, " ") || token_is(parser, at, "\t")))
		at++;
	if (at >= limit || !token_is(parser, at, "begin"))
		return fail(parser, at, "pipeline requires `begin`");
	endpipeline = top_level_token(parser, at, limit, "endpipeline");
	if (endpipeline == limit)
		return fail(parser, at, "pipeline requires `endpipeline`");
	if (pipeline_scope_push(&parser->pipeline_scopes))
	{
		syntax_checkpoint checkpoint = save_checkpoint(parser);
		size_t body_start = at + 1;
		size_t body_at = body_start;
		int ok = 1;

		node.kind = PIGEN_SYNTAX_PIPELINE;
		node.location = range_location(parser, start, endpipeline + 1);
		node.parent = INVALID_SYNTAX;
		node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
		node.as.pipeline.name = (pigen_token_id){(uint32_t)name};
		node.as.pipeline.yield = INVALID_ID(pigen_syntax_expr_id);
		pipeline = add_node(parser, node);
		add_child(parser, parent, pipeline);
		while (body_at < endpipeline)
		{
			size_t item_end;
			if (token_is(parser, body_at, "stage"))
			{
				if (!parse_pipeline_stage(parser, pipeline, body_at, endpipeline,
					&item_end)) { ok = 0; break; }
				saw_stage = 1;
				body_at = item_end;
				continue;
			}
			if (token_is(parser, body_at, "yield"))
			{
				size_t semicolon = top_level_token(parser, body_at, endpipeline, ";");
				size_t value_start = body_at + 1;
				pigen_syntax_expr_id value;

				if (saw_yield)
				{
					fail(parser, body_at, "pipeline permits exactly one yield");
					ok = 0; break;
				}
				if (!saw_stage)
				{
					fail(parser, body_at, "pipeline yield must follow the stages");
					ok = 0; break;
				}
				if (semicolon == endpipeline || value_start == semicolon)
				{
					fail(parser, body_at, "pipeline yield requires an expression");
					ok = 0; break;
				}
				if (!pigen_parse_expression(parser->expanded, value_start,
					semicolon, &parser->tree->expressions,
					&parser->tree->types, &value, parser->error))
				{
					ok = 0; break;
				}
				parser->tree->nodes[pipeline.index].as.pipeline.yield = value;
				saw_yield = 1;
				body_at = semicolon + 1;
				continue;
			}
			if (token_is(parser, body_at, "export"))
			{
				fail(parser, body_at,
					"export is reserved but not yet supported");
				ok = 0;
				break;
			}
			{
				size_t semicolon = top_level_token(parser, body_at, endpipeline, ";");

				if (semicolon == endpipeline)
				{
					fail(parser, body_at, "unterminated pipeline declaration");
					ok = 0; break;
				}
				if (top_level_token(parser, body_at, semicolon, "<=") > body_at &&
					top_level_token(parser, body_at, semicolon, "<=") < semicolon)
				{
					fail(parser, body_at, "pipeline field declaration cannot assign");
					ok = 0; break;
				}
				if (!parse_pipeline_field(parser, pipeline, body_at, semicolon))
				{ ok = 0; break; }
				body_at = semicolon + 1;
				continue;
			}
		}
		if (ok)
		{
			if (!saw_stage)
			{
				fail(parser, start, "pipeline requires at least one stage");
				ok = 0;
			}
			else if (!saw_yield)
			{
				fail(parser, endpipeline, "pipeline requires a final yield");
				ok = 0;
			}
		}
		if (ok)
		{
			parser->pipeline_failed = 0;
			*after_statement = endpipeline + 1;
			*assignment_count += 1;
			return 1;
		}
		{
			pigen_syntax_error preserved = {0};
			int has_error = parser->error && parser->error->message;
			if (has_error) preserved = *parser->error;
			restore_checkpoint(parser, checkpoint);
			if (has_error && parser->error) *parser->error = preserved;
			parser->pipeline_failed = 1;
		}
		pipeline_scope_pop(&parser->pipeline_scopes);
		return 0;
	}
	return 0;
}

static int fsm_state_names_add(fsm_state_names *states, pigen_token_id name)
{
	if (states->count == states->capacity)
	{
		states->capacity = states->capacity ? states->capacity * 2 : 4;
		states->names = pigen_resize(states->names,
			states->capacity * sizeof(*states->names));
	}
	states->names[states->count++] = name;
	return 1;
}

static int fsm_state_names_contains(const syntax_parser *parser,
	const fsm_state_names *states, pigen_token_id name)
{
	size_t i;
	for (i = 0; i < states->count; i++)
		if (tokens_equal(parser, states->names[i], name)) return 1;
	return 0;
}

static int parse_goto_statement(syntax_parser *parser, pigen_syntax_id parent,
	const fsm_state_names *declared, size_t start, size_t limit,
	size_t *after_statement)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id target;
	size_t semicolon;

	if (!token_is(parser, start, "goto")) return 0;
	semicolon = top_level_token(parser, start + 1, limit, ";");
	if (semicolon == limit)
		return fail(parser, start, "goto requires a state name");
	if (semicolon != start + 2 || !identifier(parser, start + 1))
		return fail(parser, start + 1, "goto requires a state name");
	if (!fsm_state_names_contains(parser, declared,
		(pigen_token_id){(uint32_t)(start + 1)}))
		return fail(parser, start + 1, "goto references an undeclared state");
	node.kind = PIGEN_SYNTAX_GOTO;
	node.location = range_location(parser, start, semicolon + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.goto_statement.target = (pigen_token_id){(uint32_t)(start + 1)};
	target = add_node(parser, node);
	add_child(parser, parent, target);
	*after_statement = semicolon + 1;
	return 1;
}

static size_t fsm_state_header(const syntax_parser *parser, size_t start,
	size_t limit, size_t *name, size_t *body_start)
{
	if (!token_is(parser, start, "state")) return 0;
	*name = start + 1;
	if (*name >= limit || !identifier(parser, *name)) return 0;
	if (*name + 1 >= limit || !token_is(parser, *name + 1, ":")) return 0;
	*body_start = *name + 2;
	return 1;
}

static size_t fsm_skip_statement(const syntax_parser *parser, size_t start,
	size_t limit)
{
	if (token_is(parser, start, "begin"))
		return matching_begin_end(parser, start, limit) + 1;
	if (token_is(parser, start, "if"))
	{
		size_t close = matching_parenthesis(parser, start + 1, limit);
		size_t at;
		if (close == limit || close == start + 2) return limit;
		at = fsm_skip_statement(parser, close + 1, limit);
		if (at == limit) return limit;
		if (token_is(parser, at, "else"))
			at = fsm_skip_statement(parser, at + 1, limit);
		return at;
	}
	{
		size_t semicolon = top_level_token(parser, start, limit, ";");
		return semicolon == limit ? limit : semicolon + 1;
	}
}

static int parse_fsm_state(syntax_parser *parser, pigen_syntax_id fsm,
	size_t start, size_t limit, size_t *after_state)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id state;
	size_t name = start + 1;
	size_t at;
	size_t assignment_count = 0;

	if (!fsm_state_header(parser, start, limit, &name, &at))
		return fail(parser, start, "fsm state requires a name");
	node.kind = PIGEN_SYNTAX_FSM_STATE;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.fsm_state.name = (pigen_token_id){(uint32_t)name};
	state = add_node(parser, node);
	add_child(parser, fsm, state);
	if (!parse_procedural_statement(parser, state, at, limit, &at,
		&assignment_count))
	{
		if (!(parser->error && parser->error->message))
			parser->fsm_body_failed = 1;
		return 0;
	}
	parser->tree->nodes[state.index].location = range_location(parser, start,
		at);
	*after_state = at;
	return 1;
}

static int parse_fsm(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t limit, size_t *after_statement)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id fsm;
	fsm_state_names declared = {0};
	syntax_checkpoint checkpoint;
	size_t name = start + 1;
	size_t at;
	size_t initial_at;
	size_t body_end;
	int saw_state = 0;
	int ok = 0;

	if (!token_is(parser, start, "fsm")) return 0;
	if (name >= limit || !identifier(parser, name))
		return fail(parser, start, "fsm requires a name");
	at = name + 1;
	if (at >= limit || !token_is(parser, at, "@"))
		return fail(parser, at, "fsm requires an event control");
	at++;
	if (at >= limit || !token_is(parser, at, "("))
		return fail(parser, at, "fsm event control must be one posedge");
	{
		size_t close = matching_parenthesis(parser, at, limit);
		size_t scan;
		if (close == limit)
			return fail(parser, at, "fsm event control must be one posedge");
		scan = at + 1;
		if (!token_is(parser, scan, "posedge"))
			return fail(parser, scan, "fsm event control must be one posedge");
		scan++;
		if (scan >= close || !identifier(parser, scan))
			return fail(parser, scan < close ? scan : close,
				"fsm event control must be one posedge");
		scan++;
		if (scan != close)
			return fail(parser, scan < close ? scan : close,
				"fsm event control must be one posedge");
		at = close;
	}
	at++;
	if (at >= limit || !token_is(parser, at, "reset"))
		return fail(parser, at, "fsm requires `reset (name)`");
	at++;
	if (at >= limit || !token_is(parser, at, "(") ||
		at + 2 >= limit || !identifier(parser, at + 1) ||
		!token_is(parser, at + 2, ")"))
		return fail(parser, at, "fsm requires `reset (name)`");
	at += 3;
	if (at >= limit || !token_is(parser, at, "initial"))
		return fail(parser, at, "fsm requires an initial state");
	at++;
	if (at >= limit || !identifier(parser, at))
		return fail(parser, at, "fsm requires an initial state");
	initial_at = at;
	at++;
	if (at >= limit || !token_is(parser, at, "begin"))
		return fail(parser, at, "fsm requires `begin`");
	body_end = matching_begin_end(parser, at, limit);
	if (body_end == limit)
		return fail(parser, at, "fsm requires `end`");
	checkpoint = save_checkpoint(parser);
	node.kind = PIGEN_SYNTAX_FSM;
	node.location = range_location(parser, start, body_end + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.fsm.name = (pigen_token_id){(uint32_t)name};
	node.as.fsm.clock = INVALID_ID(pigen_syntax_expr_id);
	node.as.fsm.reset = INVALID_ID(pigen_syntax_expr_id);
	node.as.fsm.initial = (pigen_token_id){(uint32_t)initial_at};
	fsm = add_node(parser, node);
	add_child(parser, parent, fsm);
	if (!pigen_parse_expression(parser->expanded, initial_at - 7, initial_at - 6,
		&parser->tree->expressions, &parser->tree->types,
		&node.as.fsm.clock, parser->error) ||
		!pigen_parse_expression(parser->expanded, initial_at - 3, initial_at - 2,
			&parser->tree->expressions, &parser->tree->types,
			&node.as.fsm.reset, parser->error))
		goto done;
	at++;
	parser->fsm_states = &declared;
	parser->fsm_body_failed = 0;
	{
		size_t scan = at;
		while (scan < body_end)
		{
			size_t sname, sbody;
			if (!fsm_state_header(parser, scan, body_end, &sname, &sbody))
				break;
			if (fsm_state_names_contains(parser, &declared,
				(pigen_token_id){(uint32_t)sname}))
			{
				fail(parser, sname, "fsm state declared more than once");
				goto done;
			}
			fsm_state_names_add(&declared, (pigen_token_id){(uint32_t)sname});
			scan = fsm_skip_statement(parser, sbody, body_end);
			if (scan == body_end) break;
		}
	}
	while (at < body_end)
	{
		size_t item_after;
		if (!parse_fsm_state(parser, fsm, at, body_end, &item_after))
			goto done;
		saw_state = 1;
		at = item_after;
	}
	if (!saw_state)
	{
		fail(parser, start, "fsm requires at least one state");
		goto done;
	}
	if (!fsm_state_names_contains(parser, &declared,
		(pigen_token_id){(uint32_t)initial_at}))
	{
		fail(parser, initial_at, "fsm initial state is not declared");
		goto done;
	}
	ok = 1;
done:
	parser->fsm_states = NULL;
	if (!ok && !(parser->error && parser->error->message) &&
		parser->fsm_body_failed)
	{
		fail(parser, start,
			"fsm state body statement must be an ordinary statement");
		parser->fsm_body_failed = 0;
	}
	if (ok)
	{
		parser->tree->nodes[fsm.index].as.fsm.clock = node.as.fsm.clock;
		parser->tree->nodes[fsm.index].as.fsm.reset = node.as.fsm.reset;
		*after_statement = body_end + 1;
	}
	else
	{
		pigen_syntax_error preserved = {0};
		int has_error = parser->error && parser->error->message;
		if (has_error) preserved = *parser->error;
		restore_checkpoint(parser, checkpoint);
		if (has_error && parser->error) *parser->error = preserved;
	}
	free(declared.names);
	return ok ? 1 : 0;
}

static int parse_validity_action(syntax_parser *parser,
	pigen_syntax_id parent, size_t start, size_t limit, size_t *after,
	size_t *assignment_count)
{
	pigen_syntax_node node = {0};
	int action;
	size_t length = 0;
	const char *text = pigen_expanded_token_text(parser->expanded,
		token_at(parser, start), &length);

	(void)limit;
	if (!text) return 0;
	if (length == 8 && !memcmp(text, "validate", 8)) action = 1;
	else if (length == 10 && !memcmp(text, "invalidate", 10)) action = 2;
	else if (length == 5 && !memcmp(text, "flush", 5)) action = 3;
	else return 0;
	if (!token_is(parser, start + 1, "(") || !identifier(parser, start + 2) ||
		!token_is(parser, start + 3, ")") || !token_is(parser, start + 4, ";"))
		return 0;
	node.kind = PIGEN_SYNTAX_ACTION;
	node.location = range_location(parser, start, start + 5);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.action.action = action;
	node.as.action.target = (pigen_token_id){(uint32_t)(start + 2)};
	add_child(parser, parent, add_node(parser, node));
	(*assignment_count)++;
	*after = start + 5;
	return 1;
}

static int parse_procedural_statement(syntax_parser *parser,
	pigen_syntax_id parent, size_t start, size_t limit, size_t *after_statement,
	size_t *assignment_count)
{
	if (parse_pipeline_reset(parser, parent, start, limit, after_statement,
		assignment_count))
		return 1;
	if (parse_validity_action(parser, parent, start, limit, after_statement,
		assignment_count))
		return 1;
	if (token_is(parser, start, "begin"))
		return parse_procedural_block(parser, parent, start, limit,
			after_statement, assignment_count);
	if (token_is(parser, start, "if"))
		return parse_if_statement(parser, parent, start, limit, after_statement,
			assignment_count);
	if (token_is(parser, start, "pipeline"))
		return parse_pipeline(parser, parent, start, limit, after_statement,
			assignment_count);
	if (parser->fsm_states && token_is(parser, start, "goto"))
		return parse_goto_statement(parser, parent, parser->fsm_states, start,
			limit, after_statement);
	return parse_nonblocking_assignment(parser, parent, start, limit,
		after_statement, assignment_count);
}

static int parse_clocked_process(syntax_parser *parser,
	pigen_syntax_id module, size_t start, size_t limit,
	syntax_cursor *opaque_cursor, size_t *after_process)
{
	pigen_syntax_expr_arena *expressions = &parser->tree->expressions;
	syntax_checkpoint checkpoint = save_checkpoint(parser);
	size_t event_open = start + 2;
	size_t event_close;
	size_t process_after;
	pigen_syntax_expr_id clock;
	size_t assignment_count = 0;
	pigen_syntax_node node = {0};
	pigen_syntax_id process;

	if ((!token_is(parser, start, "always") &&
		!token_is(parser, start, "always_ff")) ||
		!token_is(parser, start + 1, "@") ||
		!token_is(parser, event_open, "(")) return 0;
	event_close = matching_parenthesis(parser, event_open, limit);
	if (event_close == limit || !token_is(parser, event_open + 1, "posedge") ||
		event_open + 3 != event_close || !identifier(parser, event_open + 2))
		return 0;
	if (!pigen_parse_expression(parser->expanded, event_open + 2, event_close,
		expressions, &parser->tree->types, &clock, parser->error))
	{
		restore_checkpoint(parser, checkpoint);
		return 0;
	}
	node.kind = PIGEN_SYNTAX_CLOCKED_PROCESS;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.clocked_process.edge = PIGEN_EDGE_POSEDGE;
	node.as.clocked_process.clock = clock;
	process = add_node(parser, node);
	if (!parse_procedural_statement(parser, process, event_close + 1, limit,
		&process_after, &assignment_count) || !assignment_count)
	{
		pigen_syntax_error preserved = {0};
		int pipeline_failure = parser->pipeline_failed;
		if (pipeline_failure) preserved = *parser->error;
		restore_checkpoint(parser, checkpoint);
		if (pipeline_failure && parser->error) *parser->error = preserved;
		return 0;
	}
	parser->tree->nodes[process.index].location = range_location(parser, start,
		process_after);
	if (node_has_function_call(parser, process, 0))
	{
		if (node_has_structured_construct(parser, process, 0))
			return fail(parser, start,
				"function call in a structured clocked process is not supported");
		restore_checkpoint(parser, checkpoint);
		return 0;
	}
	add_opaque(parser, module, *opaque_cursor, start);
	add_child(parser, module, process);
	advance_opaque_byte_cursor(parser, process_after);
	*opaque_cursor = process_after;
	*after_process = process_after;
	return 1;
}

static int looks_like_plain_instance(const syntax_parser *parser, size_t at,
	size_t limit)
{
	size_t after_name, close;

	if (at + 1 >= limit || !identifier(parser, at) ||
		!identifier(parser, at + 1)) return 0;
	after_name = at + 2;
	if (token_is(parser, after_name, "#"))
	{
		if (!token_is(parser, after_name + 1, "(")) return 0;
		close = matching_parenthesis(parser, after_name + 1, limit);
		if (close == limit) return 0;
		after_name = close + 1;
	}
	if (!token_is(parser, after_name, "(")) return 0;
	close = matching_parenthesis(parser, after_name, limit);
	return close < limit && token_is(parser, close + 1, ";");
}

static int looks_like_parameterized_instance(const syntax_parser *parser,
	size_t at, size_t limit)
{
	size_t close, after_name;

	if (at + 4 >= limit || !identifier(parser, at) ||
		!token_is(parser, at + 1, "#") || !token_is(parser, at + 2, "("))
		return 0;
	close = matching_parenthesis(parser, at + 2, limit);
	if (close == limit) return 0;
	after_name = close + 1;
	if (!identifier(parser, after_name) ||
		!token_is(parser, after_name + 1, "(")) return 0;
	close = matching_parenthesis(parser, after_name + 1, limit);
	return close < limit && token_is(parser, close + 1, ";");
}

static int parse_structural_instance(syntax_parser *parser,
	pigen_syntax_id module, size_t start, size_t limit, size_t *after)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id instance;
	size_t at = start + 2, close, semicolon, item, opaque_start, opaque_end;
	const pigen_expanded_token *start_tok = token_at(parser, start);
	const pigen_expanded_token *last_tok;

	if (token_is(parser, at, "#"))
	{
		close = matching_parenthesis(parser, at + 1, limit);
		if (close == limit) return 0;
		at = close + 1;
	}
	if (!token_is(parser, at, "(")) return 0;
	close = matching_parenthesis(parser, at, limit);
	if (close == limit || !token_is(parser, close + 1, ";")) return 0;
	semicolon = close + 1;
	opaque_start = parser->opaque_byte_cursor;
	opaque_end = start_tok ? pigen_origin_expansion_span(parser->expanded,
		start_tok->origin).start : opaque_start;
	add_opaque_bytes(parser, module, opaque_start, opaque_end);
	node.kind = PIGEN_SYNTAX_INSTANCE;
	node.location = range_location(parser, start, semicolon + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.instance.module = (pigen_token_id){(uint32_t)start};
	node.as.instance.name = (pigen_token_id){(uint32_t)(start + 1)};
	instance = add_node(parser, node);
	add_child(parser, module, instance);
	for (item = at + 1; item < close; )
	{
		pigen_syntax_node connection = {0};
		pigen_token_id port = INVALID_ID(pigen_token_id);
		pigen_syntax_expr_id value = INVALID_ID(pigen_syntax_expr_id);
		size_t value_close;

		if (token_is(parser, item, "."))
		{
			if (!identifier(parser, item + 1) ||
				!token_is(parser, item + 2, "("))
				return fail(parser, item,
					"named instance connection requires .port(expression)");
			port = (pigen_token_id){(uint32_t)(item + 1)};
			value_close = matching_parenthesis(parser, item + 2, close);
			if (value_close == close)
				return fail(parser, item + 2,
					"unterminated instance connection");
			if (value_close > item + 3 &&
				!pigen_parse_expression(parser->expanded, item + 3,
					value_close, &parser->tree->expressions,
					&parser->tree->types, &value, parser->error))
				return 0;
		}
		else
		{
			size_t next_comma = item;
			while (next_comma < close && !token_is(parser, next_comma, ","))
				next_comma++;
			value_close = next_comma - 1;
			if (next_comma > item &&
				!pigen_parse_expression(parser->expanded, item, next_comma,
					&parser->tree->expressions, &parser->tree->types,
					&value, parser->error))
				return 0;
		}
		connection.kind = PIGEN_SYNTAX_CONNECTION;
		connection.location = range_location(parser, item, value_close + 1);
		connection.parent = INVALID_SYNTAX;
		connection.first_child = connection.last_child =
			connection.next_sibling = INVALID_SYNTAX;
		connection.as.connection.port = port;
		connection.as.connection.value = value;
		add_child(parser, instance, add_node(parser, connection));
		item = value_close + 1;
		if (item < close)
		{
			if (!token_is(parser, item, ","))
				return fail(parser, item, "instance connections require `,`");
			item++;
		}
	}
	last_tok = token_at(parser, semicolon);
	if (last_tok)
		parser->opaque_byte_cursor = pigen_origin_expansion_span(
			parser->expanded, last_tok->origin).end;
	*after = semicolon + 1;
	return 1;
}

static int retained_body_end(syntax_parser *parser, size_t body, size_t limit,
	size_t *end_out)
{
	size_t end;

	if (token_is(parser, body, "begin"))
	{
		size_t depth = 0;

		for (end = body; end < limit; end++)
		{
			if (token_is(parser, end, "begin")) depth++;
			else if (token_is(parser, end, "end") && !--depth) break;
		}
		if (end == limit) return 0;
	}
	else
	{
		end = top_level_token(parser, body, limit, ";");
		if (end == limit) return 0;
	}
	*end_out = end;
	return 1;
}

static int parse_retained_unknown_item(syntax_parser *parser,
	pigen_syntax_id module, size_t start, size_t limit, size_t *after_item)
{
	static const char *const pigen_words[] = {
		"buf", "pipeline", "fsm", "transfer", "validate", "invalidate",
		"flush", "peek", "pipeline_reset",
	};
	size_t end, body;
	pigen_syntax_node node = {0};
	size_t opaque_start, opaque_end;
	const pigen_expanded_token *start_tok = token_at(parser, start);
	const pigen_expanded_token *last_tok;

	if (token_is(parser, start, "generate"))
	{
		size_t depth = 0;

		for (end = start; end < limit; end++)
		{
			if (token_is(parser, end, "generate")) depth++;
			else if (token_is(parser, end, "endgenerate") && !--depth) break;
		}
		if (end == limit) return 0;
	}
	else if (token_is(parser, start, "initial") || token_is(parser, start, "final") ||
		token_is(parser, start, "always_comb") || token_is(parser, start, "always_latch"))
	{
		body = start + 1;
		if (!retained_body_end(parser, body, limit, &end)) return 0;
	}
	else
	{
		end = top_level_token(parser, start, limit, ";");
		if (end == limit) return 0;
	}
	for (size_t at = start; at <= end; at++)
		for (size_t k = 0; k < sizeof pigen_words / sizeof *pigen_words; k++)
			if (token_is(parser, at, pigen_words[k]))
				return fail(parser, at,
					"structured construct in an unsupported module item is not supported");
	opaque_start = parser->opaque_byte_cursor;
	opaque_end = start_tok ? pigen_origin_expansion_span(parser->expanded,
		start_tok->origin).start : opaque_start;
	add_opaque_bytes(parser, module, opaque_start, opaque_end);
	node.kind = PIGEN_SYNTAX_RETAINED;
	node.location = range_location(parser, start, end + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	add_child(parser, module, add_node(parser, node));
	last_tok = token_at(parser, end);
	if (last_tok)
		parser->opaque_byte_cursor = pigen_origin_expansion_span(
			parser->expanded, last_tok->origin).end;
	*after_item = end + 1;
	return 1;
}

static void add_retained_range(syntax_parser *parser, pigen_syntax_id module,
	size_t start, size_t after)
{
	pigen_syntax_node node = {0};

	node.kind = PIGEN_SYNTAX_RETAINED;
	node.location = range_location(parser, start, after);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	add_child(parser, module, add_node(parser, node));
}

static int parse_retained_module_item(syntax_parser *parser,
	pigen_syntax_id module, size_t start, size_t limit, size_t *after_item)
{
	size_t semicolon = top_level_token(parser, start, limit, ";");
	pigen_syntax_node node = {0};
	size_t opaque_start;
	size_t opaque_end;
	const pigen_expanded_token *start_tok = token_at(parser, start);
	const pigen_expanded_token *last_tok;

	if (semicolon == limit) return 0;
	if (token_is(parser, start, "assign"))
		for (size_t at = start + 1; at < semicolon; at++)
			if (token_is(parser, at, "<="))
				return fail(parser, at,
					"transfers are allowed only in clocked always blocks");
	opaque_start = parser->opaque_byte_cursor;
	opaque_end = start_tok ? pigen_origin_expansion_span(parser->expanded,
		start_tok->origin).start : opaque_start;
	add_opaque_bytes(parser, module, opaque_start, opaque_end);
	node.kind = PIGEN_SYNTAX_RETAINED;
	node.location = range_location(parser, start, semicolon + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	add_child(parser, module, add_node(parser, node));
	last_tok = token_at(parser, semicolon);
	if (last_tok)
		parser->opaque_byte_cursor = pigen_origin_expansion_span(
			parser->expanded, last_tok->origin).end;
	*after_item = semicolon + 1;
	return 1;
}

static int retained_procedural_end(syntax_parser *parser, size_t start,
	size_t limit, size_t *end_out)
{
	size_t event_close = matching_parenthesis(parser, start + 2, limit);
	size_t body, end;

	if (event_close == limit) return 0;
	body = event_close + 1;
	if (token_is(parser, body, "begin"))
	{
		size_t depth = 0;
		end = body;
		while (end < limit)
		{
			if (token_is(parser, end, "begin")) depth++;
			else if (token_is(parser, end, "end") && !--depth) break;
			end++;
		}
		if (end == limit) return 0;
	}
	else
	{
		end = top_level_token(parser, body, limit, ";");
		if (end == limit) return 0;
	}
	*end_out = end;
	return 1;
}

static int retained_process_has_structured(syntax_parser *parser,
	size_t start, size_t end)
{
	size_t at;

	for (at = start; at <= end; at++)
		if (token_is(parser, at, "pipeline") ||
			token_is(parser, at, "fsm") ||
			token_is(parser, at, "validate") ||
			token_is(parser, at, "invalidate") ||
			token_is(parser, at, "flush") ||
			token_is(parser, at, "pipeline_reset") ||
			token_is(parser, at, "pipe_reset"))
			return 1;
	return 0;
}

static int parse_retained_procedural(syntax_parser *parser,
	pigen_syntax_id module, size_t start, size_t limit, size_t *after_item)
{
	size_t event_open = start + 2;
	size_t event_close = matching_parenthesis(parser, event_open, limit);
	size_t body;
	size_t end;
	pigen_syntax_node node = {0};
	size_t opaque_start, opaque_end;
	const pigen_expanded_token *start_tok = token_at(parser, start);
	const pigen_expanded_token *last_tok;

	if (event_close == limit) return 0;
	body = event_close + 1;
	if (token_is(parser, body, "begin"))
	{
		size_t depth = 0;
		end = body;
		while (end < limit)
		{
			if (token_is(parser, end, "begin")) depth++;
			else if (token_is(parser, end, "end") && !--depth) break;
			end++;
		}
		if (end == limit) return 0;
	}
	else
	{
		end = top_level_token(parser, body, limit, ";");
		if (end == limit) return 0;
	}
	opaque_start = parser->opaque_byte_cursor;
	opaque_end = start_tok ? pigen_origin_expansion_span(parser->expanded,
		start_tok->origin).start : opaque_start;
	add_opaque_bytes(parser, module, opaque_start, opaque_end);
	node.kind = PIGEN_SYNTAX_RETAINED;
	node.location = range_location(parser, start, end + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	add_child(parser, module, add_node(parser, node));
	last_tok = token_at(parser, end);
	if (last_tok)
		parser->opaque_byte_cursor = pigen_origin_expansion_span(
			parser->expanded, last_tok->origin).end;
	*after_item = end + 1;
	return 1;
}

static void remember_function_name(syntax_parser *parser, size_t start,
	size_t end)
{
	size_t at;

	for (at = start; at + 1 < end; at++)
		if (token_is(parser, at + 1, "(") && identifier(parser, at))
		{
			if (parser->function_name_count ==
				parser->function_name_capacity)
			{
				parser->function_name_capacity =
					parser->function_name_capacity ?
					parser->function_name_capacity * 2 : 8;
				parser->function_names = pigen_resize(
					parser->function_names,
					parser->function_name_capacity *
					sizeof(*parser->function_names));
			}
			parser->function_names[parser->function_name_count++] =
				(pigen_token_id){(uint32_t)at};
			return;
		}
}

static int token_is_function_name(syntax_parser *parser, pigen_token_id token)
{
	size_t i;

	for (i = 0; i < parser->function_name_count; i++)
		if (tokens_equal(parser, parser->function_names[i], token)) return 1;
	return 0;
}

static int expression_has_function_call(syntax_parser *parser,
	pigen_syntax_expr_id expression, size_t depth)
{
	const pigen_syntax_expr *known = pigen_syntax_expr_get(
		&parser->tree->expressions, expression);
	const pigen_syntax_expr_id *children;
	size_t i;

	if (!known || depth > 64) return 0;
	switch (known->kind)
	{
		case PIGEN_SYNTAX_EXPR_CALL:
		{
			const pigen_syntax_expr *callee = pigen_syntax_expr_get(
				&parser->tree->expressions, known->as.call.callee);

			if (callee && callee->kind == PIGEN_SYNTAX_EXPR_NAME &&
				token_is_function_name(parser, callee->as.atom.token))
				return 1;
			children = pigen_syntax_expr_children(&parser->tree->expressions,
				known->as.call.first_argument, known->as.call.argument_count);
			for (i = 0; children && i < known->as.call.argument_count; i++)
				if (expression_has_function_call(parser, children[i], depth + 1))
					return 1;
			return 0;
		}
		case PIGEN_SYNTAX_EXPR_GROUP:
			return expression_has_function_call(parser,
				known->as.group.operand, depth + 1);
		case PIGEN_SYNTAX_EXPR_UNARY:
			return expression_has_function_call(parser,
				known->as.unary.operand, depth + 1);
		case PIGEN_SYNTAX_EXPR_BINARY:
			return expression_has_function_call(parser,
				known->as.binary.left, depth + 1) ||
				expression_has_function_call(parser,
					known->as.binary.right, depth + 1);
		case PIGEN_SYNTAX_EXPR_CONDITIONAL:
			return expression_has_function_call(parser,
				known->as.conditional.condition, depth + 1) ||
				expression_has_function_call(parser,
					known->as.conditional.when_true, depth + 1) ||
				expression_has_function_call(parser,
					known->as.conditional.when_false, depth + 1);
		case PIGEN_SYNTAX_EXPR_INDEX:
			return expression_has_function_call(parser, known->as.index.base,
				depth + 1) || expression_has_function_call(parser,
					known->as.index.index, depth + 1);
		case PIGEN_SYNTAX_EXPR_SELECT:
			return expression_has_function_call(parser, known->as.select.base,
				depth + 1) || expression_has_function_call(parser,
					known->as.select.left, depth + 1) ||
				expression_has_function_call(parser, known->as.select.right,
					depth + 1);
		case PIGEN_SYNTAX_EXPR_MEMBER:
			return expression_has_function_call(parser, known->as.member.base,
				depth + 1);
		case PIGEN_SYNTAX_EXPR_CAST:
			return expression_has_function_call(parser, known->as.cast.value,
				depth + 1);
		case PIGEN_SYNTAX_EXPR_CONCATENATION:
		case PIGEN_SYNTAX_EXPR_REPLICATION:
			children = pigen_syntax_expr_children(&parser->tree->expressions,
				known->as.sequence.first_child,
				known->as.sequence.child_count);
			for (i = 0; children && i < known->as.sequence.child_count; i++)
				if (expression_has_function_call(parser, children[i], depth + 1))
					return 1;
			return 0;
		default:
			return 0;
	}
}

static int node_has_structured_construct(syntax_parser *parser,
	pigen_syntax_id node_id, size_t depth)
{
	const pigen_syntax_node *node = pigen_syntax_get(parser->tree, node_id);
	pigen_syntax_id child;

	if (!node || depth > 64) return 0;
	if (node->kind == PIGEN_SYNTAX_PIPELINE ||
		node->kind == PIGEN_SYNTAX_FSM ||
		node->kind == PIGEN_SYNTAX_ACTION)
		return 1;
	for (child = node->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *known = pigen_syntax_get(parser->tree, child);

		if (!known) break;
		if (node_has_structured_construct(parser, child, depth + 1)) return 1;
		child = known->next_sibling;
	}
	return 0;
}

static int node_has_function_call(syntax_parser *parser,
	pigen_syntax_id node_id, size_t depth)
{
	const pigen_syntax_node *node = pigen_syntax_get(parser->tree, node_id);
	pigen_syntax_id child;

	if (!node || depth > 64) return 0;
	if (node->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT &&
		expression_has_function_call(parser,
			node->as.nonblocking_assignment.value, 0))
		return 1;
	if (node->kind == PIGEN_SYNTAX_IF_STATEMENT &&
		expression_has_function_call(parser, node->as.if_statement.condition, 0))
		return 1;
	for (child = node->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *known = pigen_syntax_get(parser->tree, child);

		if (!known) break;
		if (node_has_function_call(parser, child, depth + 1)) return 1;
		child = known->next_sibling;
	}
	return 0;
}

static int parse_retained_block_item(syntax_parser *parser,
	pigen_syntax_id module, size_t start, size_t limit, const char *closer,
	size_t *after_item)
{
	size_t end = start;
	pigen_syntax_node node = {0};
	size_t opaque_start;
	size_t opaque_end;
	const pigen_expanded_token *start_tok = token_at(parser, start);
	const pigen_expanded_token *last_tok;

	while (end < limit && !token_is(parser, end, closer)) end++;
	if (end == limit) return 0;
	opaque_start = parser->opaque_byte_cursor;
	opaque_end = start_tok ? pigen_origin_expansion_span(parser->expanded,
		start_tok->origin).start : opaque_start;
	add_opaque_bytes(parser, module, opaque_start, opaque_end);
	node.kind = PIGEN_SYNTAX_RETAINED;
	node.location = range_location(parser, start, end + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	add_child(parser, module, add_node(parser, node));
	last_tok = token_at(parser, end);
	if (last_tok)
		parser->opaque_byte_cursor = pigen_origin_expansion_span(
			parser->expanded, last_tok->origin).end;
	if (closer && !strcmp(closer, "endfunction"))
		remember_function_name(parser, start, end);
	*after_item = end + 1;
	return 1;
}

static int parse_fabric_block(syntax_parser *parser, pigen_syntax_id module,
	size_t start, size_t limit, size_t *after_fabric)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id fabric;
	size_t at = start + 1, endfabric;
	size_t opaque_start, opaque_end;
	const pigen_expanded_token *start_tok = token_at(parser, start);
	const pigen_expanded_token *last_tok;

	if (!identifier(parser, at))
		return fail(parser, at, "fabric requires a name");
	node.kind = PIGEN_SYNTAX_FABRIC;
	node.as.fabric.name = (pigen_token_id){(uint32_t)at};
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	at++;
	if (at >= limit || !token_is(parser, at, "begin"))
		return fail(parser, at, "fabric requires `begin`");
	at++;
	opaque_start = parser->opaque_byte_cursor;
	opaque_end = start_tok ? pigen_origin_expansion_span(parser->expanded,
		start_tok->origin).start : opaque_start;
	add_opaque_bytes(parser, module, opaque_start, opaque_end);
	fabric = add_node(parser, node);
	add_child(parser, module, fabric);
	while (at < limit && !token_is(parser, at, "endfabric"))
	{
		pigen_syntax_node edge = {0};
		pigen_token_id left_instance, left_port, right_instance, right_port;
		int direct = 1, tier = 0;
		size_t arrow;

		if (!identifier(parser, at) || !token_is(parser, at + 1, ".") ||
			!identifier(parser, at + 2))
			return fail(parser, at, "fabric endpoint requires instance.port");
		left_instance = (pigen_token_id){(uint32_t)at};
		left_port = (pigen_token_id){(uint32_t)(at + 2)};
		arrow = at + 3;
		if (token_is(parser, arrow, ">"))
			arrow++;
		else if (token_is(parser, arrow, "->"))
		{
			direct = 0;
			tier = 1;
			arrow++;
		}
		else if (token_is(parser, arrow, "-->"))
		{
			direct = 0;
			tier = 2;
			arrow++;
		}
		else
		{
			direct = 0;
			while (token_is(parser, arrow, "-")) { tier++; arrow++; }
			if (!token_is(parser, arrow, ">"))
				return fail(parser, arrow,
					"fabric connection requires `>` or routed `->`");
			tier++;
			arrow++;
		}
		if (!identifier(parser, arrow) || !token_is(parser, arrow + 1, ".") ||
			!identifier(parser, arrow + 2) ||
			!token_is(parser, arrow + 3, ";"))
			return fail(parser, arrow,
				"fabric destination requires instance.port;");
		right_instance = (pigen_token_id){(uint32_t)arrow};
		right_port = (pigen_token_id){(uint32_t)(arrow + 2)};
		edge.kind = PIGEN_SYNTAX_FABRIC_CONNECTION;
		edge.location = range_location(parser, at, arrow + 4);
		edge.parent = INVALID_SYNTAX;
		edge.first_child = edge.last_child = edge.next_sibling =
			INVALID_SYNTAX;
		edge.as.fabric_connection.left_instance = left_instance;
		edge.as.fabric_connection.left_port = left_port;
		edge.as.fabric_connection.right_instance = right_instance;
		edge.as.fabric_connection.right_port = right_port;
		edge.as.fabric_connection.direct = direct;
		edge.as.fabric_connection.tier = tier;
		add_child(parser, fabric, add_node(parser, edge));
		at = arrow + 4;
	}
	if (at >= limit || !token_is(parser, at, "endfabric"))
		return fail(parser, at, "fabric requires `endfabric`");
	endfabric = at;
	node.location = range_location(parser, start, endfabric + 1);
	parser->tree->nodes[fabric.index].location = node.location;
	last_tok = token_at(parser, endfabric);
	if (last_tok)
		parser->opaque_byte_cursor = pigen_origin_expansion_span(
			parser->expanded, last_tok->origin).end;
	*after_fabric = endfabric + 1;
	return 1;
}

static int parse_module_items(syntax_parser *parser, pigen_syntax_id module,
	size_t first, size_t after, syntax_cursor *opaque_cursor)
{
	size_t at = first;
	size_t depth = 0;
	size_t brace_depth = 0;
	int item_start = 1;

	while (at < after)
	{
		int retained_keyword = token_is(parser, at, "initial") ||
			token_is(parser, at, "final") ||
			token_is(parser, at, "generate") ||
			token_is(parser, at, "genvar") ||
			token_is(parser, at, "always_comb") ||
			token_is(parser, at, "always_latch");

		if (item_start && !retained_keyword &&
			looks_like_parameterized_instance(parser, at, after))
		{
			size_t retained_after;

			if (!parse_retained_module_item(parser, module, at, after,
				&retained_after)) return 0;
			at = retained_after;
			item_start = 1;
			continue;
		}
		if (item_start && !retained_keyword &&
			looks_like_plain_instance(parser, at, after))
		{
			size_t instance_after;

			if (!parse_structural_instance(parser, module, at, after,
				&instance_after)) return 0;
			at = instance_after;
			item_start = 1;
			continue;
		}
		if (item_start && token_is(parser, at, "assign"))
		{
			size_t retained_after;

			if (!parse_retained_module_item(parser, module, at, after,
				&retained_after)) return 0;
			at = retained_after;
			item_start = 1;
			continue;
		}
		if (item_start && token_is(parser, at, "fabric"))
		{
			size_t fabric_after;
			if (!parse_fabric_block(parser, module, at, after,
				&fabric_after)) return 0;
			at = fabric_after;
			item_start = 1;
			continue;
		}
		if (item_start && (token_is(parser, at, "always") ||
			token_is(parser, at, "always_ff")))
		{
			size_t process_after;
			if (parse_clocked_process(parser, module, at, after, opaque_cursor,
				&process_after))
			{
				at = process_after;
				item_start = 1;
				continue;
			}
			if (parser->pipeline_failed) return 0;
			if (token_is(parser, at, "always_ff"))
			{
				size_t open = at + 2;
				size_t close = token_is(parser, at + 1, "@") &&
					token_is(parser, open, "(") ?
					matching_parenthesis(parser, open, after) : after;
				size_t retained_after;

				if (close == after || close != open + 3 ||
					!token_is(parser, open + 1, "posedge") ||
					!identifier(parser, open + 2))
					return fail(parser, at + 2,
						"exactly one posedge event control");
				/* Valid-edge ordinary SV that the structured subset does not
				 * model is preserved verbatim, but it cannot also carry
				 * Pigen pipeline/FSM/actions that would be emitted raw. */
				{
					size_t process_end;

					if (!retained_procedural_end(parser, at, after,
						&process_end)) return 0;
					if (retained_process_has_structured(parser, at,
						process_end))
						return fail(parser, at,
							"structured construct in an unsupported clocked "
							"process is not supported");
				}
				if (!parse_retained_procedural(parser, module, at, after,
					&retained_after)) return 0;
				at = retained_after;
				item_start = 1;
				continue;
			}
			/* A non-clocked `always` block is ordinary SV retained as-is.
			 * A failed edge-clocked block must roll back, not become opaque. */
			if (token_is(parser, at, "always") &&
				!(token_is(parser, at + 2, "(") &&
					(token_is(parser, at + 3, "posedge") ||
					 token_is(parser, at + 3, "negedge") ||
					 token_is(parser, at + 3, "edge"))))
			{
				size_t retained_after;

				if (parse_retained_module_item(parser, module, at, after,
					&retained_after))
				{
					at = retained_after;
					item_start = 1;
					continue;
				}
			}
		}
		if (item_start && token_is(parser, at, "fsm"))
		{
			size_t fsm_after;
			if (parse_fsm(parser, module, at, after, &fsm_after))
			{
				advance_opaque_byte_cursor(parser, fsm_after);
				at = fsm_after;
				item_start = 1;
				continue;
			}
			return 0;
		}
		if (item_start && (token_is(parser, at, "parameter") ||
			token_is(parser, at, "localparam")))
		{
			size_t semicolon = at;
			while (semicolon < after && !token_is(parser, semicolon, ";")) semicolon++;
			if (semicolon == after)
				return fail(parser, at, "unterminated parameter declaration");
			if (!parse_parameter_items(parser, module, at, semicolon,
				opaque_cursor)) return 0;
			add_retained_range(parser, module, at, semicolon + 1);
			at = semicolon + 1;
			item_start = 1;
			continue;
		}
		if (item_start && (token_is(parser, at, "function") ||
			token_is(parser, at, "task")))
		{
			size_t retained_after;
			const char *closer = token_is(parser, at, "function") ?
				"endfunction" : "endtask";

			if (!parse_retained_block_item(parser, module, at, after, closer,
				&retained_after)) return 0;
			at = retained_after;
			item_start = 1;
			continue;
		}
		if (item_start && token_is(parser, at, "typedef"))
		{
			size_t semicolon = top_level_token(parser, at, after, ";");
			declaration_parse_result result;
			if (semicolon == after)
			{
				if (decisive_typedef_without_terminator(parser, module, at, after))
					return fail(parser, after,
						"typedef declaration requires `;`");
				return fail(parser, at, "unterminated typedef declaration");
			}
			result = parse_typedef(parser, module, at, semicolon, opaque_cursor);
			if (result == DECLARATION_INVALID) return 0;
			add_retained_range(parser, module, at, semicolon + 1);
			at = semicolon + 1;
			item_start = 1;
			continue;
		}
		if (item_start)
		{
			size_t semicolon = top_level_token(parser, at, after, ";");
			size_t type_at = at;
			int has_direction = token_is(parser, at, "input") ||
				token_is(parser, at, "output") || token_is(parser, at, "inout");
			syntax_checkpoint checkpoint;
			declaration_parse_result result;
			pigen_syntax_id declaration = INVALID_SYNTAX;
			int decisive = 0;

			if (has_direction) type_at++;
			if (semicolon < after)
			{
				if (leading_transfer_first_declaration(parser, module, type_at,
					semicolon))
					return fail(parser, type_at,
						"data type must precede the transfer type");
				checkpoint = save_checkpoint(parser);
				result = parse_systemverilog_static_declaration(parser, module, at,
					semicolon, &declaration);
				if (result == DECLARATION_RECOGNIZED)
				{
					commit_declaration(parser, module, at, semicolon + 1,
						opaque_cursor, declaration);
					at = semicolon + 1;
					item_start = 1;
					continue;
				}
				restore_checkpoint(parser, checkpoint);

				checkpoint = save_checkpoint(parser);
				result = parse_data_first_declaration(parser, module, at, semicolon,
					0,
					&declaration, &decisive);
				if (result == DECLARATION_INVALID) return 0;
				if (result == DECLARATION_RECOGNIZED)
				{
					commit_declaration(parser, module, at, semicolon + 1,
						opaque_cursor, declaration);
					at = semicolon + 1;
					item_start = 1;
					continue;
				}
				restore_checkpoint(parser, checkpoint);
			}
			else
			{
				if (leading_transfer_first_declaration(parser, module, type_at,
					after))
					return fail(parser, type_at,
						"data type must precede the transfer type");
				checkpoint = save_checkpoint(parser);
				result = parse_data_first_declaration(parser, module, at, after, 0,
					&declaration, &decisive);
				if (result == DECLARATION_INVALID) return 0;
				if (result == DECLARATION_RECOGNIZED && decisive)
				{
					restore_checkpoint(parser, checkpoint);
					return fail(parser, after,
						"signal declaration requires `;`");
				}
				restore_checkpoint(parser, checkpoint);
			}
		}
		if (item_start && !token_is(parser, at, "always") &&
			!token_is(parser, at, "always_ff"))
		{
			size_t retained_after;

			if (parse_retained_unknown_item(parser, module, at, after,
				&retained_after))
			{
				at = retained_after;
				item_start = 1;
				continue;
			}
		}
		if (token_is(parser, at, "{")) brace_depth++;
		else if (token_is(parser, at, "}") && brace_depth) brace_depth--;
		else if (block_opener(parser, at)) depth++;
		else if (block_closer(parser, at))
		{
			if (depth) depth--;
			if (!depth) item_start = 1;
		}
		else if (!depth && !brace_depth && token_is(parser, at, ";"))
			item_start = 1;
		else if (item_start) item_start = 0;
		at++;
	}
	return 1;
}

static int module_header_has_directionless_port(const syntax_parser *parser,
	size_t start)
{
	size_t header_end, at, close, item;
	size_t paren_depth = 0;

	for (header_end = start + 2;
		header_end < parser->expanded->token_count; header_end++)
	{
		if (token_is(parser, header_end, "(")) paren_depth++;
		else if (token_is(parser, header_end, ")") && paren_depth) paren_depth--;
		else if (!paren_depth && token_is(parser, header_end, ";")) break;
	}
	if (header_end == parser->expanded->token_count) return 0;
	for (at = start + 2; at < header_end; at++)
		if (token_is(parser, at, "(")) break;
	if (at == header_end) return 0;
	close = matching_parenthesis(parser, at, header_end);
	if (close == header_end) return 0;
	for (item = at + 1; item < close; )
	{
		size_t next = item;

		while (next < close && !token_is(parser, next, ",")) next++;
		if (item < next && !token_is(parser, item, "input") &&
			!token_is(parser, item, "output") && !token_is(parser, item, "inout"))
			return 1;
		item = next + 1;
	}
	return 0;
}

static int parameterized_module(const syntax_parser *parser, size_t start)
{
	return token_is(parser, start, "module") &&
		identifier(parser, start + 1) && token_is(parser, start + 2, "#");
}

static int parameterized_module_has_pigen(const syntax_parser *parser,
	size_t start)
{
	static const char *const pigen_words[] = {
		"buf", "pipeline", "fsm", "transfer", "validate", "invalidate",
		"flush", "peek", "pipeline_reset",
	};
	size_t header_end, endmodule, at;
	size_t paren_depth = 0;

	for (header_end = start + 2;
		header_end < parser->expanded->token_count; header_end++)
	{
		if (token_is(parser, header_end, "(")) paren_depth++;
		else if (token_is(parser, header_end, ")") && paren_depth) paren_depth--;
		else if (!paren_depth && token_is(parser, header_end, ";")) break;
	}
	if (header_end == parser->expanded->token_count) return 0;
	for (endmodule = header_end + 1;
		endmodule < parser->expanded->token_count; endmodule++)
		if (token_is(parser, endmodule, "endmodule")) break;
	if (endmodule == parser->expanded->token_count) return 0;
	for (at = start; at < endmodule; at++)
		for (size_t k = 0; k < sizeof pigen_words / sizeof *pigen_words; k++)
			if (token_is(parser, at, pigen_words[k])) return 1;
	return 0;
}

static int parse_parameterized_module_opaque(syntax_parser *parser,
	pigen_syntax_id root, size_t start, size_t *after_module)
{
	size_t header_end, endmodule;
	pigen_syntax_node node = {0};
	size_t paren_depth = 0;

	for (header_end = start + 2;
		header_end < parser->expanded->token_count; header_end++)
	{
		if (token_is(parser, header_end, "(")) paren_depth++;
		else if (token_is(parser, header_end, ")") && paren_depth) paren_depth--;
		else if (!paren_depth && token_is(parser, header_end, ";")) break;
	}
	if (header_end == parser->expanded->token_count)
		return fail(parser, start, "unterminated module header");
	for (endmodule = header_end + 1;
		endmodule < parser->expanded->token_count; endmodule++)
		if (token_is(parser, endmodule, "endmodule")) break;
	if (endmodule == parser->expanded->token_count)
		return fail(parser, start, "module requires `endmodule`");
	node.kind = PIGEN_SYNTAX_RETAINED;
	node.location = range_location(parser, start, endmodule + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	add_child(parser, root, add_node(parser, node));
	advance_opaque_byte_cursor(parser, endmodule + 1);
	*after_module = endmodule + 1;
	return 1;
}

static int parse_module(syntax_parser *parser, pigen_syntax_id root,
	size_t start, size_t *after_module)
{
	size_t name = start + 1;
	size_t header_end;
	size_t endmodule;
	size_t port_open = 0;
	size_t port_close = 0;
	size_t header_at;
	syntax_cursor opaque_cursor;
	size_t paren_depth = 0;
	pigen_syntax_node node = {0};
	pigen_syntax_id module;

	if (!identifier(parser, name))
		return fail(parser, name, "module requires a name");
	for (header_end = name + 1;
		header_end < parser->expanded->token_count; header_end++)
	{
		if (token_is(parser, header_end, "(")) paren_depth++;
		else if (token_is(parser, header_end, ")") && paren_depth) paren_depth--;
		else if (!paren_depth && token_is(parser, header_end, ";")) break;
	}
	if (header_end == parser->expanded->token_count)
		return fail(parser, start, "unterminated module header");
	for (endmodule = header_end + 1;
		endmodule < parser->expanded->token_count; endmodule++)
		if (token_is(parser, endmodule, "endmodule")) break;
	if (endmodule == parser->expanded->token_count)
		return fail(parser, start, "module requires `endmodule`");
	node.kind = PIGEN_SYNTAX_MODULE;
	node.location = range_location(parser, start, endmodule + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.module.name = (pigen_token_id){(uint32_t)name};
	module = add_node(parser, node);
	add_child(parser, root, module);
	opaque_cursor = start;
	header_at = name + 1;
	if (token_is(parser, header_at, "#"))
	{
		size_t parameter_close;
		if (!token_is(parser, header_at + 1, "("))
			return fail(parser, header_at, "module parameter list requires `(`");
		parameter_close = matching_parenthesis(parser, header_at + 1, header_end);
		if (parameter_close == header_end)
			return fail(parser, header_at + 1,
				"unterminated module parameter list");
		if (!parse_parameter_items(parser, module, header_at + 2,
			parameter_close, &opaque_cursor)) return 0;
		header_at = parameter_close + 1;
	}
	if (token_is(parser, header_at, "("))
	{
		port_open = header_at;
		port_close = matching_parenthesis(parser, port_open, header_end);
		if (port_close == header_end)
			return fail(parser, port_open, "unterminated module port list");
		if (!parse_ansi_ports(parser, module, port_open + 1,
			port_close, &opaque_cursor)) return 0;
	}
	if (!parse_module_items(parser, module, header_end + 1, endmodule,
		&opaque_cursor)) return 0;
	{
		size_t module_end = pigen_origin_expansion_span(parser->expanded,
			parser->expanded->tokens[endmodule].origin).end;

		add_opaque_bytes(parser, module, parser->opaque_byte_cursor,
			module_end);
		advance_opaque_byte_cursor(parser, endmodule + 1);
	}
	*after_module = endmodule + 1;
	return 1;
}

int pigen_parse_syntax(const pigen_expanded_source *source,
	pigen_syntax_tree *tree, pigen_syntax_error *error)
{
	syntax_parser parser = {0};
	pigen_syntax_node root_node = {0};
	pigen_syntax_id root;
	size_t at = 0;
	syntax_cursor opaque_cursor;

	memset(tree, 0, sizeof(*tree));
	if (error) *error = (pigen_syntax_error){
		INVALID_ID(pigen_origin_id), invalid_source_span(), NULL};
	parser.expanded = source;
	parser.tree = tree;
	parser.error = error;
	parser.pipeline_failed = 0;
	parser.fsm_states = NULL;
	parser.source_length = 0;
	parser.opaque_byte_cursor = 0;
	if (!source || !pigen_source_get(source->sources, source->root_source) ||
		!source->token_count) return 0;
	parser.source_length = pigen_source_get(source->sources,
		source->root_source)->length;
	opaque_cursor = 0;
	tree->expanded = source;
	root_node.kind = PIGEN_SYNTAX_COMPILATION_UNIT;
	root_node.location = range_location(&parser, 0, source->token_count - 1);
	root_node.parent = root_node.first_child = root_node.last_child =
		root_node.next_sibling = INVALID_SYNTAX;
	root = add_node(&parser, root_node);
	while (token_at(&parser, at)->kind != PIGEN_TOKEN_EOF)
	{
		const char *closer = opaque_unit_closer(&parser, at);
		if (closer)
		{
			while (token_at(&parser, at)->kind != PIGEN_TOKEN_EOF &&
				!token_is(&parser, at, closer)) at++;
			if (token_at(&parser, at)->kind != PIGEN_TOKEN_EOF) at++;
			continue;
		}
		if (token_is(&parser, at, "typedef"))
		{
			size_t semicolon = top_level_token(&parser, at,
				source->token_count, ";");
			declaration_parse_result result;
			if (semicolon == source->token_count ||
				token_at(&parser, semicolon)->kind == PIGEN_TOKEN_EOF)
			{
				size_t boundary = source->token_count - 1;
				int decisive = decisive_typedef_without_terminator(&parser, root,
					at, boundary);

				if (decisive)
					fail(&parser, boundary,
						"typedef declaration requires `;`");
				free(parser.typedef_name_facts);
				return 0;
			}
			result = parse_typedef(&parser, root, at, semicolon, &opaque_cursor);
			if (result == DECLARATION_INVALID)
			{
				free(parser.typedef_name_facts);
				return 0;
			}
			at = semicolon + 1;
			continue;
		}
		if (!token_is(&parser, at, "module")) { at++; continue; }
		add_opaque(&parser, root, opaque_cursor, at);
		if ((parameterized_module(&parser, at) ||
			module_header_has_directionless_port(&parser, at)) &&
			!parameterized_module_has_pigen(&parser, at))
		{
			if (!parse_parameterized_module_opaque(&parser, root, at, &at))
			{
				free(parser.typedef_name_facts);
				return 0;
			}
		}
		else if (!parse_module(&parser, root, at, &at))
		{
			free(parser.typedef_name_facts);
			return 0;
		}
		opaque_cursor = at;
	}
	/* Trailing bytes after the last design unit belong to the last module's
	 * nested scope when that is the most recent top-level item, so a module's
	 * owner-managed layout covers the complete remaining source. */
	{
		pigen_syntax_id owner = root;
		pigen_syntax_node *last = parser.tree->nodes[0].last_child.index ==
			PIGEN_INVALID_ID ? NULL :
			&parser.tree->nodes[parser.tree->nodes[0].last_child.index];

		if (last && last->kind == PIGEN_SYNTAX_MODULE)
			owner = parser.tree->nodes[0].last_child;
		add_opaque_bytes(&parser, owner, parser.opaque_byte_cursor,
			parser.source_length);
	}
	free(parser.typedef_name_facts);
	return 1;
}

const pigen_syntax_node *pigen_syntax_get(const pigen_syntax_tree *tree,
	pigen_syntax_id node)
{
	if (node.index == PIGEN_INVALID_ID || node.index >= tree->node_count)
		return NULL;
	return &tree->nodes[node.index];
}

const pigen_syntax_shape_dimension *pigen_syntax_declarator_shape_dimensions(
	const pigen_syntax_tree *tree, const pigen_syntax_node *declarator)
{
	const pigen_syntax_signal_declarator *known;

	if (!tree || !declarator) return NULL;
	if (declarator->kind != PIGEN_SYNTAX_SIGNAL_DECLARATOR)
		return NULL;
	known = &declarator->as.signal_declarator;
	if (!known->dimension_count ||
		known->first_shape_dimension + known->dimension_count >
			tree->shape_dimension_count) return NULL;
	return tree->shape_dimensions + known->first_shape_dimension;
}

void pigen_free_syntax_tree(pigen_syntax_tree *tree)
{
	free(tree->nodes);
	free(tree->shape_dimensions);
	pigen_free_syntax_expr_arena(&tree->expressions);
	pigen_free_syntax_type_arena(&tree->types);
	*tree = (pigen_syntax_tree){0};
}
