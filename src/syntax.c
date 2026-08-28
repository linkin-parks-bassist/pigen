/* Source-spanned syntax nodes for modules and internal signal declarations. */
#include <stdlib.h>
#include <string.h>

#include "pigen/lexer.h"
#include "pigen/syntax.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})
#define INVALID_SYNTAX ((pigen_syntax_id){PIGEN_INVALID_ID})

typedef struct {
	const pigen_expanded_source *expanded;
	pigen_syntax_tree *tree;
	pigen_syntax_error *error;
} syntax_parser;

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

static void add_opaque(syntax_parser *parser, pigen_syntax_id parent,
	syntax_cursor start, syntax_cursor end)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id id;

	if (start == end) return;
	node.kind = PIGEN_SYNTAX_OPAQUE;
	node.location = range_location(parser, start, end);
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

static int type_has_count_argument(const syntax_parser *parser,
	pigen_syntax_type_id type)
{
	const pigen_syntax_type *known = pigen_syntax_type_get(&parser->tree->types,
		type);
	const pigen_syntax_type_argument *arguments;
	size_t i;

	if (!known || !known->argument_count) return 0;
	arguments = pigen_syntax_type_arguments(&parser->tree->types,
		known->first_argument, known->argument_count);
	if (!arguments) return 0;
	for (i = 0; i < known->argument_count; i++)
		if (arguments[i].kind == PIGEN_SYNTAX_TYPE_COUNT) return 1;
	return 0;
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
	syntax_parser *parser, size_t start, size_t limit,
	pigen_syntax_id *declaration)
{
	size_t at = start;
	size_t name;
	size_t parsed_after;
	pigen_syntax_direction direction = parse_direction(parser, &at);
	pigen_syntax_type_id data_type;
	pigen_transfer_type prefix_transfer_type;
	size_t prefix_transfer_at = at;
	int has_prefix_transfer = token_is(parser, at, "wire") ||
		token_is(parser, at, "reg");
	int has_static_prefix = has_prefix_transfer || token_is(parser, at, "logic") ||
		token_is(parser, at, "bit") || token_is(parser, at, "signed") ||
		token_is(parser, at, "unsigned") || token_is(parser, at, "[") ||
		(direction != PIGEN_DIRECTION_INTERNAL &&
			declarator_candidate(parser, at, limit));
	pigen_syntax_id id;

	if (!has_static_prefix) return DECLARATION_NOT_RECOGNIZED;
	if (has_prefix_transfer)
	{
		if (!transfer_type_at(parser, at, &prefix_transfer_type))
			return DECLARATION_NOT_RECOGNIZED;
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
		if (transfer_type_at(parser, name, &prefix_transfer_type))
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
	syntax_parser *parser, size_t start, size_t limit, int ansi,
	pigen_syntax_id *declaration, int *decisive)
{
	size_t at = start;
	size_t type_start;
	pigen_syntax_direction direction = parse_direction(parser, &at);
	pigen_syntax_type_id data_type;
	pigen_syntax_transfer_type_occurrence transfer = {0};
	pigen_transfer_type transfer_type;
	const pigen_transfer_type_descriptor *descriptor;
	const pigen_syntax_type *known_type;
	pigen_syntax_id id;
	int has_transfer_type = 0;

	*decisive = 0;
	type_start = at;
	if (!pigen_parse_type_prefix(parser->expanded, type_start, limit,
		&parser->tree->expressions, &parser->tree->types, &data_type, &at,
		parser->error)) return DECLARATION_NOT_RECOGNIZED;
	known_type = pigen_syntax_type_get(&parser->tree->types, data_type);
	*decisive = type_has_count_argument(parser, data_type) ||
		(direction == PIGEN_DIRECTION_INPUT && known_type &&
			token_is(parser, known_type->base.index, "bit"));
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
	else if (known_type && token_is(parser, known_type->base.index, "byte"))
		return DECLARATION_NOT_RECOGNIZED;
	if (!identifier(parser, at))
	{
		if (*decisive)
		{
			fail(parser, at, "declaration requires a signal name");
			return DECLARATION_INVALID;
		}
		return DECLARATION_NOT_RECOGNIZED;
	}
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
	parser->tree->nodes[declaration.index].location = range_location(parser,
		start, declaration_after);
	add_opaque(parser, module, *opaque_cursor, start);
	add_child(parser, module, declaration);
	*opaque_cursor = declaration_after;
}

static int parse_typedef(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t semicolon, syntax_cursor *opaque_cursor)
{
	pigen_syntax_type_id type;
	pigen_syntax_location declaration = range_location(parser, start,
		semicolon + 1);
	pigen_syntax_node node = {0};
	pigen_syntax_id id;
	size_t name;

	if (!pigen_parse_type_prefix(parser->expanded, start + 1, semicolon,
		&parser->tree->expressions, &parser->tree->types, &type, &name,
		parser->error)) return 0;
	if (name + 1 != semicolon)
		return fail(parser, name + 1, "typedef permits exactly one name");
	add_opaque(parser, parent, *opaque_cursor, start);
	node.kind = PIGEN_SYNTAX_TYPEDEF;
	node.location = declaration;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.type_definition.name = (pigen_token_id){(uint32_t)name};
	node.as.type_definition.type = type;
	id = add_node(parser, node);
	add_child(parser, parent, id);
	*opaque_cursor = semicolon + 1;
	return 1;
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

	if (!simple_parameter_shape(parser, start, after, has_keyword, &name,
		&equals))
		return 0;
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
		int shaped = (has_keyword || continuation) &&
			simple_parameter_shape(parser, at, item_end, has_keyword,
				&ignored_name, &ignored_equals);

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
		pigen_transfer_type leading_transfer;
		const pigen_transfer_type_descriptor *leading_descriptor;
		syntax_checkpoint checkpoint;
		declaration_parse_result result;
		pigen_syntax_id declaration = INVALID_SYNTAX;
		int decisive = 0;

		if (has_direction) type_at++;
		if (transfer_type_at(parser, type_at, &leading_transfer))
		{
			leading_descriptor = pigen_transfer_type_descriptor_get(leading_transfer);
			if (leading_descriptor && !leading_descriptor->is_static)
			{
				if (!has_direction)
					return fail(parser, at,
						"ANSI dynamic signal port requires `input` or `output`");
				return fail(parser, type_at,
					"data type must precede the transfer type");
			}
		}
		while (continuation < after)
		{
			size_t next = continuation + 1;
			size_t next_end = ansi_item_end(parser, next, after);
			if (!declarator_candidate(parser, next, next_end)) break;
			declaration_after = next_end;
			continuation = next_end;
		}

		checkpoint = save_checkpoint(parser);
		result = parse_systemverilog_static_declaration(parser, at, item_end,
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
		result = parse_data_first_declaration(parser, at, item_end, 1,
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

static int parse_procedural_statement(syntax_parser *parser,
	pigen_syntax_id parent, size_t start, size_t limit, size_t *after_statement,
	size_t *assignment_count);

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
		if (!parse_procedural_statement(parser, block, at, limit, &at,
			assignment_count)) return 0;
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
		assignment_count)) return 0;
	if (token_is(parser, at, "else"))
	{
		parser->tree->nodes[statement.index].as.if_statement.has_else = 1;
		if (!parse_procedural_statement(parser, statement, at + 1, limit, &at,
			assignment_count)) return 0;
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
		separator + 1 == semicolon) return 0;
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

static int parse_procedural_statement(syntax_parser *parser,
	pigen_syntax_id parent, size_t start, size_t limit, size_t *after_statement,
	size_t *assignment_count)
{
	if (token_is(parser, start, "begin"))
		return parse_procedural_block(parser, parent, start, limit,
			after_statement, assignment_count);
	if (token_is(parser, start, "if"))
		return parse_if_statement(parser, parent, start, limit, after_statement,
			assignment_count);
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
		restore_checkpoint(parser, checkpoint);
		return 0;
	}
	parser->tree->nodes[process.index].location = range_location(parser, start,
		process_after);
	add_opaque(parser, module, *opaque_cursor, start);
	add_child(parser, module, process);
	*opaque_cursor = process_after;
	*after_process = process_after;
	return 1;
}

static int parse_module_items(syntax_parser *parser, pigen_syntax_id module,
	size_t first, size_t after, syntax_cursor *opaque_cursor)
{
	size_t at = first;
	size_t depth = 0;
	int item_start = 1;

	while (at < after)
	{
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
			at = semicolon + 1;
			item_start = 1;
			continue;
		}
		if (item_start && token_is(parser, at, "typedef"))
		{
			size_t semicolon = at;
			while (semicolon < after && !token_is(parser, semicolon, ";")) semicolon++;
			if (semicolon == after)
				return fail(parser, at, "unterminated typedef declaration");
			if (!parse_typedef(parser, module, at, semicolon, opaque_cursor)) return 0;
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
			pigen_transfer_type leading_transfer;
			const pigen_transfer_type_descriptor *leading_descriptor;
			syntax_checkpoint checkpoint;
			declaration_parse_result result;
			pigen_syntax_id declaration = INVALID_SYNTAX;
			int decisive = 0;

			if (has_direction) type_at++;
			if (transfer_type_at(parser, type_at, &leading_transfer))
			{
				leading_descriptor = pigen_transfer_type_descriptor_get(
					leading_transfer);
				if (leading_descriptor && !leading_descriptor->is_static)
					return fail(parser, type_at,
						"data type must precede the transfer type");
			}
			if (semicolon < after)
			{
				checkpoint = save_checkpoint(parser);
				result = parse_systemverilog_static_declaration(parser, at,
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
				result = parse_data_first_declaration(parser, at, semicolon, 0,
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
		}
		if (block_opener(parser, at)) depth++;
		else if (block_closer(parser, at))
		{
			if (depth) depth--;
			if (!depth) item_start = 1;
		}
		else if (!depth && token_is(parser, at, ";")) item_start = 1;
		else if (item_start) item_start = 0;
		at++;
	}
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
	add_opaque(parser, module, opaque_cursor, endmodule + 1);
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
	if (!source || !pigen_source_get(source->sources, source->root_source) ||
		!source->token_count) return 0;
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
			size_t semicolon = at;
			while (token_at(&parser, semicolon)->kind != PIGEN_TOKEN_EOF &&
				!token_is(&parser, semicolon, ";")) semicolon++;
			if (token_at(&parser, semicolon)->kind == PIGEN_TOKEN_EOF ||
				!parse_typedef(&parser, root, at, semicolon, &opaque_cursor))
				return 0;
			at = semicolon + 1;
			continue;
		}
		if (!token_is(&parser, at, "module")) { at++; continue; }
		add_opaque(&parser, root, opaque_cursor, at);
		if (!parse_module(&parser, root, at, &at))
			return 0;
		opaque_cursor = at;
	}
	add_opaque(&parser, root, opaque_cursor, at);
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
