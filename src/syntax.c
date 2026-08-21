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

typedef size_t syntax_cursor;

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
	if (token_is(parser, at, "buf")) *transfer_type = PIGEN_TRANSFER_TYPE_BUF;
	else if (token_is(parser, at, "port")) *transfer_type = PIGEN_TRANSFER_TYPE_PORT;
	else if (token_is(parser, at, "skid")) *transfer_type = PIGEN_TRANSFER_TYPE_SKID;
	else if (token_is(parser, at, "fifo")) *transfer_type = PIGEN_TRANSFER_TYPE_FIFO;
	else return 0;
	return 1;
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

static int add_dimension(syntax_parser *parser, size_t open, size_t close)
{
	pigen_syntax_tree *tree = parser->tree;
	pigen_syntax_dimension dimension;
	size_t colon = open + 1;
	size_t depth = 0;

	for (; colon < close; colon++)
	{
		if (token_is(parser, colon, "(") || token_is(parser, colon, "[") ||
			token_is(parser, colon, "{")) depth++;
		else if (token_is(parser, colon, ")") || token_is(parser, colon, "]") ||
			token_is(parser, colon, "}")) depth--;
		else if (!depth && token_is(parser, colon, ":")) break;
	}
	if (colon == open + 1 || colon == close || colon + 1 == close)
		return fail(parser, open, "packed dimension requires `left:right`");
	dimension.location = range_location(parser, open, close + 1);
	if (!pigen_parse_expression(parser->expanded, open + 1, colon,
		&tree->expressions, &dimension.left, parser->error) ||
		!pigen_parse_expression(parser->expanded, colon + 1, close,
			&tree->expressions, &dimension.right, parser->error))
		return 0;
	if (tree->dimension_count == tree->dimension_capacity)
	{
		tree->dimension_capacity = tree->dimension_capacity ?
			tree->dimension_capacity * 2 : 16;
		tree->dimensions = pigen_resize(tree->dimensions,
			tree->dimension_capacity * sizeof(*tree->dimensions));
	}
	tree->dimensions[tree->dimension_count++] = dimension;
	return 1;
}

static int parse_type(syntax_parser *parser, size_t start, size_t limit,
	int final_group_is_depth, int allow_implicit_scalar,
	pigen_syntax_type *type, size_t *name_at,
	pigen_syntax_expr_id *depth)
{
	size_t at = start;
	size_t groups_start;
	size_t last_group_open = 0;
	size_t last_group_close = 0;
	size_t group_count = 0;
	size_t payload_groups;

	*type = (pigen_syntax_type){0};
	*depth = INVALID_ID(pigen_syntax_expr_id);
	type->signedness = PIGEN_SYNTAX_SIGN_IMPLICIT;
	type->base = PIGEN_SYNTAX_TYPE_IMPLICIT_LOGIC;
	type->base_name = INVALID_ID(pigen_token_id);
	if (token_is(parser, at, "signed"))
	{
		type->signedness = PIGEN_SYNTAX_SIGN_SIGNED;
		at++;
	}
	else if (token_is(parser, at, "unsigned"))
	{
		type->signedness = PIGEN_SYNTAX_SIGN_UNSIGNED;
		at++;
	}
	if (identifier(parser, at) && (!allow_implicit_scalar || at + 1 < limit) &&
		!token_is(parser, at, "signed") &&
		!token_is(parser, at, "unsigned"))
	{
		if (token_is(parser, at, "logic")) type->base = PIGEN_SYNTAX_TYPE_LOGIC;
		else if (token_is(parser, at, "bit")) type->base = PIGEN_SYNTAX_TYPE_BIT;
		else
		{
			type->base = PIGEN_SYNTAX_TYPE_NAMED;
			type->base_name = (pigen_token_id){(uint32_t)at};
		}
		at++;
		if (token_is(parser, at, "signed"))
		{
			type->signedness = PIGEN_SYNTAX_SIGN_SIGNED;
			at++;
		}
		else if (token_is(parser, at, "unsigned"))
		{
			type->signedness = PIGEN_SYNTAX_SIGN_UNSIGNED;
			at++;
		}
	}
	groups_start = at;
	while (at < limit && token_is(parser, at, "["))
	{
		size_t close = matching_bracket(parser, at, limit);
		if (close == limit)
			return fail(parser, at, "unterminated packed dimension");
		last_group_open = at;
		last_group_close = close;
		group_count++;
		at = close + 1;
	}
	*name_at = at;
	if (!identifier(parser, *name_at))
		return fail(parser, *name_at, "declaration requires a name");
	if (final_group_is_depth)
	{
		if (!group_count)
			return fail(parser, *name_at, "fifo declaration requires a depth");
		if (last_group_close == last_group_open + 1)
			return fail(parser, last_group_open, "fifo depth requires an expression");
		if (!pigen_parse_expression(parser->expanded, last_group_open + 1,
			last_group_close, &parser->tree->expressions, depth,
			parser->error))
			return fail(parser, last_group_open, "invalid fifo depth expression");
		payload_groups = group_count - 1;
	}
	else
		payload_groups = group_count;
	if (final_group_is_depth && !payload_groups &&
		type->base == PIGEN_SYNTAX_TYPE_IMPLICIT_LOGIC)
		return fail(parser, *name_at,
			"fifo declaration requires a data type before its depth");
	type->first_dimension = parser->tree->dimension_count;
	type->dimension_count = payload_groups;
	at = groups_start;
	for (size_t i = 0; i < payload_groups; i++)
	{
		size_t close = matching_bracket(parser, at, limit);
		if (!add_dimension(parser, at, close)) return 0;
		at = close + 1;
	}
	if (payload_groups)
		type->location = range_location(parser, start, at);
	else if (type->base != PIGEN_SYNTAX_TYPE_IMPLICIT_LOGIC)
	{
		size_t type_end = final_group_is_depth ? last_group_open : *name_at;
		type->location = range_location(parser, start, type_end);
	}
	else if (!allow_implicit_scalar)
		return fail(parser, *name_at, "declaration requires a packed type");
	else
		type->location = range_location(parser, start, *name_at);
	return 1;
}

static pigen_syntax_id add_signal_declarator(syntax_parser *parser,
	pigen_syntax_id declaration, size_t name)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id id;

	node.kind = PIGEN_SYNTAX_SIGNAL_DECLARATOR;
	node.location = range_location(parser, name, name + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.signal_declarator.name = (pigen_token_id){(uint32_t)name};
	id = add_node(parser, node);
	add_child(parser, declaration, id);
	return id;
}

static pigen_syntax_id add_static_signal_declarator(syntax_parser *parser,
	pigen_syntax_id declaration, size_t name)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id id;

	node.kind = PIGEN_SYNTAX_STATIC_SIGNAL_DECLARATOR;
	node.location = range_location(parser, name, name + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.static_signal_declarator.name = (pigen_token_id){(uint32_t)name};
	id = add_node(parser, node);
	add_child(parser, declaration, id);
	return id;
}

static int value_candidate(const syntax_parser *parser, size_t start,
	size_t after)
{
	size_t at = start;
	int has_direction = token_is(parser, at, "input") ||
		token_is(parser, at, "output") || token_is(parser, at, "inout");
	int recognized;

	if (has_direction) at++;
	recognized = token_is(parser, at, "wire") || token_is(parser, at, "reg") ||
		token_is(parser, at, "logic") || token_is(parser, at, "bit") ||
		(has_direction && (token_is(parser, at, "signed") ||
			token_is(parser, at, "unsigned") || token_is(parser, at, "[") ||
			(at + 1 == after && identifier(parser, at))));
	if (!recognized) return 0;
	if (token_is(parser, at, "wire") || token_is(parser, at, "reg")) at++;
	if (token_is(parser, at, "logic") || token_is(parser, at, "bit")) at++;
	if (token_is(parser, at, "signed") || token_is(parser, at, "unsigned")) at++;
	while (at < after && token_is(parser, at, "["))
	{
		size_t close = matching_bracket(parser, at, after);
		if (close == after) return 0;
		at = close + 1;
	}
	if (at == after || !identifier(parser, at)) return 0;
	at++;
	while (at < after)
	{
		if (!token_is(parser, at, ",") || at + 1 == after ||
			!identifier(parser, at + 1)) return 0;
		at += 2;
	}
	return 1;
}

static int parse_value(syntax_parser *parser, pigen_syntax_id module,
	size_t start, size_t type_limit, size_t declaration_after,
	syntax_cursor *opaque_cursor, pigen_syntax_id *declaration)
{
	size_t at = start;
	size_t name_start;
	int explicit_data_type = 0;
	pigen_syntax_direction direction = PIGEN_DIRECTION_INTERNAL;
	pigen_transfer_type transfer_type;
	pigen_syntax_type type = {0};
	pigen_syntax_expr_id unused_depth = INVALID_ID(pigen_syntax_expr_id);
	pigen_syntax_node node = {0};
	pigen_syntax_id declaration_id;

	if (token_is(parser, at, "input")) { direction = PIGEN_DIRECTION_INPUT; at++; }
	else if (token_is(parser, at, "output")) { direction = PIGEN_DIRECTION_OUTPUT; at++; }
	else if (token_is(parser, at, "inout")) { direction = PIGEN_DIRECTION_INOUT; at++; }
	if (token_is(parser, at, "wire"))
	{
		transfer_type = PIGEN_TRANSFER_TYPE_WIRE;
		at++;
	}
	else if (token_is(parser, at, "reg"))
	{
		transfer_type = PIGEN_TRANSFER_TYPE_REG;
		explicit_data_type = 1;
		at++;
	}
	else
	{
		explicit_data_type = token_is(parser, at, "logic") ||
			token_is(parser, at, "bit");
		transfer_type = direction == PIGEN_DIRECTION_INTERNAL ||
			(direction == PIGEN_DIRECTION_OUTPUT && explicit_data_type) ?
			PIGEN_TRANSFER_TYPE_LOGIC : PIGEN_TRANSFER_TYPE_WIRE;
	}
	if (!parse_type(parser, at, type_limit, 0, 1, &type, &name_start,
		&unused_depth)) return 0;

	add_opaque(parser, module, *opaque_cursor, start);
	node.kind = PIGEN_SYNTAX_STATIC_SIGNAL_DECLARATION;
	node.location = range_location(parser, start, declaration_after);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.static_signal_declaration.direction = direction;
	node.as.static_signal_declaration.transfer_type = transfer_type;
	node.as.static_signal_declaration.type = type;
	declaration_id = add_node(parser, node);
	add_child(parser, module, declaration_id);
	for (at = name_start; at < type_limit; )
	{
		if (!identifier(parser, at))
			return fail(parser, at, "expected signal name");
		add_static_signal_declarator(parser, declaration_id, at++);
		if (at == type_limit) break;
		if (!token_is(parser, at, ","))
			return fail(parser, at, "unsupported signal declarator");
		at++;
	}
	*opaque_cursor = declaration_after;
	if (declaration) *declaration = declaration_id;
	return 1;
}

static int parse_signal(syntax_parser *parser, pigen_syntax_id module,
	size_t start, size_t type_limit, size_t declaration_after,
	syntax_cursor *opaque_cursor, pigen_syntax_id *declaration)
{
	size_t at = start;
	size_t name_start;
	pigen_syntax_direction direction = PIGEN_DIRECTION_INTERNAL;
	pigen_transfer_type transfer_type;
	pigen_syntax_type payload = {0};
	pigen_syntax_expr_id fifo_depth = INVALID_ID(pigen_syntax_expr_id);
	pigen_syntax_location declaration_location = range_location(parser, start,
		declaration_after);
	pigen_syntax_node node = {0};
	pigen_syntax_id declaration_id;

	if (token_is(parser, at, "input")) { direction = PIGEN_DIRECTION_INPUT; at++; }
	else if (token_is(parser, at, "output")) { direction = PIGEN_DIRECTION_OUTPUT; at++; }
	else if (token_is(parser, at, "inout")) { direction = PIGEN_DIRECTION_INOUT; at++; }
	if (!transfer_type_at(parser, at, &transfer_type))
		return 0;
	at++;
	if (!parse_type(parser, at, type_limit, transfer_type == PIGEN_TRANSFER_TYPE_FIFO, 0,
		&payload, &name_start, &fifo_depth)) return 0;

	add_opaque(parser, module, *opaque_cursor, start);
	node.kind = PIGEN_SYNTAX_SIGNAL_DECLARATION;
	node.location = declaration_location;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.signal_declaration.transfer_type = transfer_type;
	node.as.signal_declaration.direction = direction;
	node.as.signal_declaration.payload = payload;
	node.as.signal_declaration.fifo_depth = fifo_depth;
	declaration_id = add_node(parser, node);
	add_child(parser, module, declaration_id);
	for (at = name_start; at < type_limit; )
	{
		if (!identifier(parser, at))
			return fail(parser, at, "expected signal name");
		add_signal_declarator(parser, declaration_id, at++);
		if (at == type_limit) break;
		if (!token_is(parser, at, ","))
			return fail(parser, at, "expected `,` or `;` after signal name");
		at++;
	}
	*opaque_cursor = declaration_after;
	if (declaration) *declaration = declaration_id;
	return 1;
}

static int parse_typedef(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t semicolon, syntax_cursor *opaque_cursor)
{
	pigen_syntax_type type;
	pigen_syntax_expr_id unused_depth = INVALID_ID(pigen_syntax_expr_id);
	pigen_syntax_location declaration = range_location(parser, start,
		semicolon + 1);
	pigen_syntax_node node = {0};
	pigen_syntax_id id;
	size_t name;

	if (!parse_type(parser, start + 1, semicolon, 0, 0, &type, &name,
		&unused_depth)) return 0;
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
		&parser->tree->expressions, &node.as.parameter.value, parser->error))
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
		pigen_transfer_type ignored;
		size_t item_end = ansi_item_end(parser, at, after);
		size_t type_at = at;
		int has_direction = token_is(parser, at, "input") ||
			token_is(parser, at, "output") || token_is(parser, at, "inout");

		if (has_direction) type_at++;
		if (transfer_type_at(parser, type_at, &ignored))
		{
			pigen_syntax_id declaration;
			size_t declaration_after = item_end;
			size_t continuation = item_end;

			if (!has_direction)
				return fail(parser, at,
					"ANSI signal port requires `input` or `output`");
			while (continuation < after)
			{
				size_t next = continuation + 1;
				size_t next_end = ansi_item_end(parser, next, after);
				if (next + 1 != next_end || !identifier(parser, next)) break;
				declaration_after = next_end;
				continuation = next_end;
			}
			if (!parse_signal(parser, module, at, item_end,
				declaration_after, opaque_cursor, &declaration)) return 0;
			continuation = item_end;
			while (continuation < declaration_after)
			{
				size_t next = continuation + 1;
				add_signal_declarator(parser, declaration, next);
				continuation = ansi_item_end(parser, next, after);
			}
			item_end = declaration_after;
		}
		else if (value_candidate(parser, at, item_end))
		{
			pigen_syntax_id declaration;
			size_t declaration_after = item_end;
			size_t continuation = item_end;

			while (continuation < after)
			{
				size_t next = continuation + 1;
				size_t next_end = ansi_item_end(parser, next, after);
				if (next + 1 != next_end || !identifier(parser, next)) break;
				declaration_after = next_end;
				continuation = next_end;
			}
			if (!parse_value(parser, module, at, item_end,
				declaration_after, opaque_cursor, &declaration)) return 0;
			continuation = item_end;
			while (continuation < declaration_after)
			{
				size_t next = continuation + 1;
				add_static_signal_declarator(parser, declaration, next);
				continuation = ansi_item_end(parser, next, after);
			}
			item_end = declaration_after;
		}
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

static void abandon_process_parse(syntax_parser *parser,
	size_t syntax_node_count, size_t expression_node_count,
	size_t expression_child_count, pigen_syntax_error saved_error)
{
	parser->tree->node_count = syntax_node_count;
	parser->tree->expressions.node_count = expression_node_count;
	parser->tree->expressions.child_count = expression_child_count;
	if (parser->error) *parser->error = saved_error;
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
		&parser->tree->expressions, &node.as.if_statement.condition,
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
		&parser->tree->expressions,
		&node.as.nonblocking_assignment.destination, parser->error) ||
		!pigen_parse_expression(parser->expanded, separator + 1, semicolon,
			&parser->tree->expressions,
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
	size_t saved_syntax_node_count = parser->tree->node_count;
	size_t saved_expression_node_count = expressions->node_count;
	size_t saved_expression_child_count = expressions->child_count;
	pigen_syntax_error saved_error = parser->error ? *parser->error :
		(pigen_syntax_error){0};
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
		expressions, &clock, parser->error))
	{
		abandon_process_parse(parser, saved_syntax_node_count,
			saved_expression_node_count, saved_expression_child_count, saved_error);
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
		abandon_process_parse(parser, saved_syntax_node_count,
			saved_expression_node_count, saved_expression_child_count, saved_error);
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
		pigen_transfer_type ignored;
		size_t type_at = at;
		int candidate;
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
		if (item_start && (token_is(parser, at, "input") ||
			token_is(parser, at, "output") || token_is(parser, at, "inout")))
			type_at++;
		candidate = item_start && transfer_type_at(parser, type_at, &ignored);
		if (candidate)
		{
			size_t semicolon = at;
			while (semicolon < after && !token_is(parser, semicolon, ";")) semicolon++;
			if (semicolon == after)
				return fail(parser, at, "unterminated signal declaration");
			if (!parse_signal(parser, module, at, semicolon, semicolon + 1,
				opaque_cursor, NULL)) return 0;
			at = semicolon + 1;
			item_start = 1;
			continue;
		}
		if (item_start && value_candidate(parser, at,
			top_level_token(parser, at, after, ";")))
		{
			size_t semicolon = top_level_token(parser, at, after, ";");
			if (semicolon == after)
				return fail(parser, at, "unterminated signal declaration");
			if (!parse_value(parser, module, at, semicolon, semicolon + 1,
				opaque_cursor, NULL)) return 0;
			at = semicolon + 1;
			item_start = 1;
			continue;
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

const pigen_syntax_dimension *pigen_syntax_type_dimensions(
	const pigen_syntax_tree *tree, const pigen_syntax_type *type)
{
	if (!type || !type->dimension_count ||
		type->first_dimension + type->dimension_count > tree->dimension_count)
		return NULL;
	return tree->dimensions + type->first_dimension;
}

void pigen_free_syntax_tree(pigen_syntax_tree *tree)
{
	free(tree->nodes);
	free(tree->dimensions);
	pigen_free_syntax_expr_arena(&tree->expressions);
	*tree = (pigen_syntax_tree){0};
}
