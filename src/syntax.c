/* Source-spanned syntax nodes for modules and internal transport declarations. */
#include <stdlib.h>
#include <string.h>

#include "pigen/lexer.h"
#include "pigen/syntax.h"
#include "pigen/util.h"

#define INVALID_SYNTAX ((pigen_syntax_id){PIGEN_INVALID_ID})

typedef struct {
	const pigen_source_manager *sources;
	const pigen_source_file *file;
	pigen_source_id source;
	pigen_tokens tokens;
	pigen_syntax_tree *tree;
	pigen_syntax_error *error;
} syntax_parser;

static pigen_source_span token_span(const syntax_parser *parser, size_t at)
{
	pigen_span span = parser->tokens.items[at].span;
	return (pigen_source_span){parser->source, span.start, span.end};
}

static pigen_source_span range_span(const syntax_parser *parser, size_t first,
	size_t after)
{
	return (pigen_source_span){parser->source,
		parser->tokens.items[first].span.start,
		parser->tokens.items[after - 1].span.end};
}

static int token_is(const syntax_parser *parser, size_t at, const char *text)
{
	return at < parser->tokens.count &&
		pigen_token_is(parser->file->text, &parser->tokens.items[at], text);
}

static int identifier(const syntax_parser *parser, size_t at)
{
	return at < parser->tokens.count &&
		parser->tokens.items[at].kind == PIGEN_TOKEN_IDENTIFIER;
}

static int fail(syntax_parser *parser, size_t at, const char *message)
{
	if (parser->error)
	{
		parser->error->span = token_span(parser, at);
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
	size_t start, size_t end)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id id;

	if (start == end) return;
	node.kind = PIGEN_SYNTAX_OPAQUE;
	node.span = (pigen_source_span){parser->source, start, end};
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	id = add_node(parser, node);
	add_child(parser, parent, id);
}

static int transport_kind(const syntax_parser *parser, size_t at,
	pigen_syntax_transport_kind *kind)
{
	if (token_is(parser, at, "buf")) *kind = PIGEN_TRANSPORT_BUF;
	else if (token_is(parser, at, "port")) *kind = PIGEN_TRANSPORT_PORT;
	else if (token_is(parser, at, "skid")) *kind = PIGEN_TRANSPORT_SKID;
	else if (token_is(parser, at, "fifo")) *kind = PIGEN_TRANSPORT_FIFO;
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
	dimension.span = range_span(parser, open, close + 1);
	dimension.left = range_span(parser, open + 1, colon);
	dimension.right = range_span(parser, colon + 1, close);
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
	int final_group_is_depth, pigen_syntax_type *type, size_t *name_at,
	pigen_source_span *depth)
{
	size_t at = start;
	size_t groups_start;
	size_t last_group_open = 0;
	size_t last_group_close = 0;
	size_t group_count = 0;
	size_t payload_groups;

	*type = (pigen_syntax_type){0};
	type->signedness = PIGEN_SYNTAX_SIGN_IMPLICIT;
	type->base = PIGEN_SYNTAX_TYPE_IMPLICIT_LOGIC;
	type->base_name = (pigen_source_span){parser->source, 0, 0};
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
	if (identifier(parser, at) && !token_is(parser, at, "signed") &&
		!token_is(parser, at, "unsigned"))
	{
		if (token_is(parser, at, "logic")) type->base = PIGEN_SYNTAX_TYPE_LOGIC;
		else if (token_is(parser, at, "bit")) type->base = PIGEN_SYNTAX_TYPE_BIT;
		else
		{
			type->base = PIGEN_SYNTAX_TYPE_NAMED;
			type->base_name = token_span(parser, at);
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
		*depth = range_span(parser, last_group_open + 1, last_group_close);
		payload_groups = group_count - 1;
	}
	else
		payload_groups = group_count;
	if (final_group_is_depth && !payload_groups &&
		type->base == PIGEN_SYNTAX_TYPE_IMPLICIT_LOGIC)
		return fail(parser, *name_at,
			"fifo declaration requires a payload type before its depth");
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
		type->span = (pigen_source_span){parser->source,
			parser->tokens.items[start].span.start,
			parser->tokens.items[at - 1].span.end};
	else if (type->base != PIGEN_SYNTAX_TYPE_IMPLICIT_LOGIC)
	{
		size_t type_end = final_group_is_depth ? last_group_open : *name_at;
		type->span = range_span(parser, start, type_end);
	}
	else
		return fail(parser, *name_at, "declaration requires a packed type");
	return 1;
}

static int parse_transport(syntax_parser *parser, pigen_syntax_id module,
	size_t start, size_t semicolon, size_t *opaque_cursor)
{
	size_t at = start;
	size_t name_start;
	pigen_syntax_direction direction = PIGEN_DIRECTION_INTERNAL;
	pigen_syntax_transport_kind kind;
	pigen_syntax_type payload = {0};
	pigen_source_span fifo_depth = {parser->source, 0, 0};
	pigen_source_span declaration = range_span(parser, start, semicolon + 1);

	if (token_is(parser, at, "input")) { direction = PIGEN_DIRECTION_INPUT; at++; }
	else if (token_is(parser, at, "output")) { direction = PIGEN_DIRECTION_OUTPUT; at++; }
	else if (token_is(parser, at, "inout")) { direction = PIGEN_DIRECTION_INOUT; at++; }
	if (!transport_kind(parser, at, &kind))
		return 0;
	at++;
	if (!parse_type(parser, at, semicolon, kind == PIGEN_TRANSPORT_FIFO,
		&payload, &name_start, &fifo_depth)) return 0;

	add_opaque(parser, module, *opaque_cursor, declaration.start);
	for (at = name_start; at < semicolon; )
	{
		pigen_syntax_node node = {0};
		pigen_syntax_id id;
		if (!identifier(parser, at))
			return fail(parser, at, "expected transport name");
		node.kind = PIGEN_SYNTAX_TRANSPORT;
		node.span = declaration;
		node.parent = INVALID_SYNTAX;
		node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
		node.as.transport.kind = kind;
		node.as.transport.direction = direction;
		node.as.transport.name = token_span(parser, at++);
		node.as.transport.payload = payload;
		node.as.transport.fifo_depth = fifo_depth;
		id = add_node(parser, node);
		add_child(parser, module, id);
		if (at == semicolon) break;
		if (!token_is(parser, at, ","))
			return fail(parser, at, "expected `,` or `;` after transport name");
		at++;
	}
	*opaque_cursor = declaration.end;
	return 1;
}

static int parse_typedef(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t semicolon, size_t *opaque_cursor)
{
	pigen_syntax_type type;
	pigen_source_span unused_depth = {parser->source, 0, 0};
	pigen_source_span declaration = range_span(parser, start, semicolon + 1);
	pigen_syntax_node node = {0};
	pigen_syntax_id id;
	size_t name;

	if (!parse_type(parser, start + 1, semicolon, 0, &type, &name,
		&unused_depth)) return 0;
	if (name + 1 != semicolon)
		return fail(parser, name + 1, "typedef permits exactly one name");
	add_opaque(parser, parent, *opaque_cursor, declaration.start);
	node.kind = PIGEN_SYNTAX_TYPEDEF;
	node.span = declaration;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.type_definition.name = token_span(parser, name);
	node.as.type_definition.type = type;
	id = add_node(parser, node);
	add_child(parser, parent, id);
	*opaque_cursor = declaration.end;
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

static int parse_module_items(syntax_parser *parser, pigen_syntax_id module,
	size_t first, size_t after)
{
	size_t at = first;
	size_t opaque_cursor = parser->tokens.items[first].span.start;
	size_t depth = 0;
	int item_start = 1;

	while (at < after)
	{
		pigen_syntax_transport_kind ignored;
		size_t kind_at = at;
		int candidate;
		if (item_start && token_is(parser, at, "typedef"))
		{
			size_t semicolon = at;
			while (semicolon < after && !token_is(parser, semicolon, ";")) semicolon++;
			if (semicolon == after)
				return fail(parser, at, "unterminated typedef declaration");
			if (!parse_typedef(parser, module, at, semicolon, &opaque_cursor)) return 0;
			at = semicolon + 1;
			item_start = 1;
			continue;
		}
		if (item_start && (token_is(parser, at, "input") ||
			token_is(parser, at, "output") || token_is(parser, at, "inout")))
			kind_at++;
		candidate = item_start && transport_kind(parser, kind_at, &ignored);
		if (candidate)
		{
			size_t semicolon = at;
			while (semicolon < after && !token_is(parser, semicolon, ";")) semicolon++;
			if (semicolon == after)
				return fail(parser, at, "unterminated transport declaration");
			if (!parse_transport(parser, module, at, semicolon, &opaque_cursor)) return 0;
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
	add_opaque(parser, module, opaque_cursor,
		parser->tokens.items[after].span.start);
	return 1;
}

static int parse_module(syntax_parser *parser, pigen_syntax_id root,
	size_t start, size_t *after_module)
{
	size_t name = start + 1;
	size_t header_end;
	size_t endmodule;
	size_t paren_depth = 0;
	pigen_syntax_node node = {0};
	pigen_syntax_id module;

	if (!identifier(parser, name))
		return fail(parser, name, "module requires a name");
	for (header_end = name + 1; header_end < parser->tokens.count; header_end++)
	{
		if (token_is(parser, header_end, "(")) paren_depth++;
		else if (token_is(parser, header_end, ")") && paren_depth) paren_depth--;
		else if (!paren_depth && token_is(parser, header_end, ";")) break;
	}
	if (header_end == parser->tokens.count)
		return fail(parser, start, "unterminated module header");
	for (endmodule = header_end + 1; endmodule < parser->tokens.count; endmodule++)
		if (token_is(parser, endmodule, "endmodule")) break;
	if (endmodule == parser->tokens.count)
		return fail(parser, start, "module requires `endmodule`");
	node.kind = PIGEN_SYNTAX_MODULE;
	node.span = range_span(parser, start, endmodule + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.module.name = token_span(parser, name);
	module = add_node(parser, node);
	add_child(parser, root, module);
	if (!parse_module_items(parser, module, header_end + 1, endmodule)) return 0;
	*after_module = endmodule + 1;
	return 1;
}

int pigen_parse_syntax(const pigen_source_manager *sources,
	pigen_source_id source, pigen_syntax_tree *tree, pigen_syntax_error *error)
{
	syntax_parser parser = {0};
	pigen_syntax_node root_node = {0};
	pigen_syntax_id root;
	size_t at = 0;
	size_t opaque_cursor = 0;

	memset(tree, 0, sizeof(*tree));
	if (error) *error = (pigen_syntax_error){0};
	parser.sources = sources;
	parser.file = pigen_source_get(sources, source);
	parser.source = source;
	parser.tree = tree;
	parser.error = error;
	if (!parser.file) return 0;
	tree->source = source;
	pigen_lex_source(parser.file->text, parser.file->length, &parser.tokens);
	root_node.kind = PIGEN_SYNTAX_COMPILATION_UNIT;
	root_node.span = (pigen_source_span){source, 0, parser.file->length};
	root_node.parent = root_node.first_child = root_node.last_child =
		root_node.next_sibling = INVALID_SYNTAX;
	root = add_node(&parser, root_node);
	while (parser.tokens.items[at].kind != PIGEN_TOKEN_EOF)
	{
		const char *closer = opaque_unit_closer(&parser, at);
		if (closer)
		{
			while (parser.tokens.items[at].kind != PIGEN_TOKEN_EOF &&
				!token_is(&parser, at, closer)) at++;
			if (parser.tokens.items[at].kind != PIGEN_TOKEN_EOF) at++;
			continue;
		}
		if (token_is(&parser, at, "typedef"))
		{
			size_t semicolon = at;
			while (parser.tokens.items[semicolon].kind != PIGEN_TOKEN_EOF &&
				!token_is(&parser, semicolon, ";")) semicolon++;
			if (parser.tokens.items[semicolon].kind == PIGEN_TOKEN_EOF ||
				!parse_typedef(&parser, root, at, semicolon, &opaque_cursor))
			{
				pigen_free_tokens(&parser.tokens);
				return 0;
			}
			at = semicolon + 1;
			continue;
		}
		if (!token_is(&parser, at, "module")) { at++; continue; }
		add_opaque(&parser, root, opaque_cursor,
			parser.tokens.items[at].span.start);
		if (!parse_module(&parser, root, at, &at))
		{
			pigen_free_tokens(&parser.tokens);
			return 0;
		}
		opaque_cursor = parser.tokens.items[at - 1].span.end;
	}
	add_opaque(&parser, root, opaque_cursor, parser.file->length);
	pigen_free_tokens(&parser.tokens);
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
	*tree = (pigen_syntax_tree){0};
}
