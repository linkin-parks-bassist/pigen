/* Structural parsing for expressions used by Pigen semantic constructs. */
#include <stdlib.h>
#include <string.h>

#include "pigen/expression.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

typedef struct {
	const pigen_expanded_source *source;
	pigen_syntax_expr_arena *arena;
	pigen_syntax_type_arena *types;
	pigen_syntax_error *error;
	size_t at;
	size_t after;
} expression_parser;

typedef struct {
	pigen_syntax_operator operator;
	unsigned precedence;
	int right_associative;
} binary_operator;

static pigen_source_span invalid_span(void)
{
	return (pigen_source_span){INVALID_ID(pigen_source_id), 0, 0};
}

static const pigen_expanded_token *token_at(const expression_parser *parser,
	size_t at)
{
	return pigen_expanded_token_get(parser->source,
		(pigen_token_id){(uint32_t)at});
}

static int token_is(const expression_parser *parser, size_t at,
	const char *expected)
{
	const pigen_expanded_token *token = token_at(parser, at);
	const char *text;
	size_t length;
	if (at >= parser->after || !token) return 0;
	text = pigen_expanded_token_text(parser->source, token, &length);
	return text && length == strlen(expected) &&
		!memcmp(text, expected, length);
}

pigen_syntax_location pigen_syntax_location_from_extent(
	const pigen_expanded_source *source, size_t first, size_t after)
{
	pigen_syntax_location location = {
		{(pigen_token_id){(uint32_t)first},
			(pigen_token_id){(uint32_t)after}},
		INVALID_ID(pigen_origin_id), invalid_span()};
	pigen_source_span combined;
	size_t at;
	size_t previous_start;
	const pigen_expanded_token *token;

	if (!source || first >= after || after > source->token_count)
		return location;
	token = pigen_expanded_token_get(source,
		(pigen_token_id){(uint32_t)first});
	if (!token) return location;
	location.origin = token->origin;
	combined = pigen_origin_expansion_span(source, token->origin);
	if (!pigen_source_span_valid(source->sources, combined)) return location;
	previous_start = combined.start;
	for (at = first + 1; at < after; at++)
	{
		pigen_source_span span;
		token = pigen_expanded_token_get(source,
			(pigen_token_id){(uint32_t)at});
		if (!token) return location;
		span = pigen_origin_expansion_span(source, token->origin);
		if (!pigen_source_span_valid(source->sources, span) ||
			span.source.index != combined.source.index ||
			span.start < previous_start)
			return location;
		if (span.end > combined.end) combined.end = span.end;
		previous_start = span.start;
	}
	location.source_span = combined;
	return location;
}

static pigen_syntax_expr_id fail(expression_parser *parser, size_t at,
	const char *message)
{
	const pigen_expanded_token *token = token_at(parser, at);
	if (parser->error)
	{
		parser->error->origin = token ? token->origin :
			INVALID_ID(pigen_origin_id);
		parser->error->span = token ? pigen_origin_expansion_span(
			parser->source, token->origin) : invalid_span();
		parser->error->message = message;
	}
	return INVALID_ID(pigen_syntax_expr_id);
}

static pigen_syntax_expr_id add_node(expression_parser *parser,
	pigen_syntax_expr node)
{
	pigen_syntax_expr_arena *arena = parser->arena;
	pigen_syntax_expr_id result;
	if (arena->node_count == PIGEN_INVALID_ID)
		pigen_fail("too many syntax expressions");
	if (arena->node_count == arena->node_capacity)
	{
		arena->node_capacity = arena->node_capacity ?
			arena->node_capacity * 2 : 32;
		arena->nodes = pigen_resize(arena->nodes,
			arena->node_capacity * sizeof(*arena->nodes));
	}
	result = (pigen_syntax_expr_id){(uint32_t)arena->node_count};
	arena->nodes[arena->node_count++] = node;
	return result;
}

static size_t append_children(expression_parser *parser,
	const pigen_syntax_expr_id *children, size_t count)
{
	pigen_syntax_expr_arena *arena = parser->arena;
	size_t first = arena->child_count;
	if (arena->child_count + count > arena->child_capacity)
	{
		size_t capacity = arena->child_capacity ? arena->child_capacity * 2 : 32;
		while (capacity < arena->child_count + count) capacity *= 2;
		arena->children = pigen_resize(arena->children,
			capacity * sizeof(*arena->children));
		arena->child_capacity = capacity;
	}
	if (count)
		memcpy(arena->children + arena->child_count, children,
			count * sizeof(*children));
	arena->child_count += count;
	return first;
}

static int unary_operator_at(const expression_parser *parser, size_t at,
	pigen_syntax_operator *operator)
{
	if (token_is(parser, at, "+")) *operator = PIGEN_SYNTAX_OP_POSITIVE;
	else if (token_is(parser, at, "-")) *operator = PIGEN_SYNTAX_OP_NEGATE;
	else if (token_is(parser, at, "!")) *operator = PIGEN_SYNTAX_OP_LOGICAL_NOT;
	else if (token_is(parser, at, "~")) *operator = PIGEN_SYNTAX_OP_BITWISE_NOT;
	else if (token_is(parser, at, "&")) *operator = PIGEN_SYNTAX_OP_REDUCTION_AND;
	else if (token_is(parser, at, "~&")) *operator = PIGEN_SYNTAX_OP_REDUCTION_NAND;
	else if (token_is(parser, at, "|")) *operator = PIGEN_SYNTAX_OP_REDUCTION_OR;
	else if (token_is(parser, at, "~|")) *operator = PIGEN_SYNTAX_OP_REDUCTION_NOR;
	else if (token_is(parser, at, "^")) *operator = PIGEN_SYNTAX_OP_REDUCTION_XOR;
	else if (token_is(parser, at, "~^") || token_is(parser, at, "^~"))
		*operator = PIGEN_SYNTAX_OP_REDUCTION_XNOR;
	else return 0;
	return 1;
}

static int binary_operator_at(const expression_parser *parser, size_t at,
	binary_operator *known)
{
#define BINARY(text, kind, precedence, right) \
	if (token_is(parser, at, text)) \
	{ *known = (binary_operator){kind, precedence, right}; return 1; }
	BINARY("||", PIGEN_SYNTAX_OP_LOGICAL_OR, 2, 0)
	BINARY("&&", PIGEN_SYNTAX_OP_LOGICAL_AND, 3, 0)
	BINARY("|", PIGEN_SYNTAX_OP_BITWISE_OR, 4, 0)
	BINARY("^", PIGEN_SYNTAX_OP_BITWISE_XOR, 5, 0)
	BINARY("~^", PIGEN_SYNTAX_OP_BITWISE_XNOR, 5, 0)
	BINARY("^~", PIGEN_SYNTAX_OP_BITWISE_XNOR, 5, 0)
	BINARY("&", PIGEN_SYNTAX_OP_BITWISE_AND, 6, 0)
	BINARY("==", PIGEN_SYNTAX_OP_EQUAL, 7, 0)
	BINARY("!=", PIGEN_SYNTAX_OP_NOT_EQUAL, 7, 0)
	BINARY("===", PIGEN_SYNTAX_OP_CASE_EQUAL, 7, 0)
	BINARY("!==", PIGEN_SYNTAX_OP_CASE_NOT_EQUAL, 7, 0)
	BINARY("==?", PIGEN_SYNTAX_OP_WILDCARD_EQUAL, 7, 0)
	BINARY("!=?", PIGEN_SYNTAX_OP_WILDCARD_NOT_EQUAL, 7, 0)
	BINARY("<", PIGEN_SYNTAX_OP_LESS, 8, 0)
	BINARY("<=", PIGEN_SYNTAX_OP_LESS_EQUAL, 8, 0)
	BINARY(">", PIGEN_SYNTAX_OP_GREATER, 8, 0)
	BINARY(">=", PIGEN_SYNTAX_OP_GREATER_EQUAL, 8, 0)
	BINARY("<<", PIGEN_SYNTAX_OP_SHIFT_LEFT, 9, 0)
	BINARY(">>", PIGEN_SYNTAX_OP_SHIFT_RIGHT, 9, 0)
	BINARY("<<<", PIGEN_SYNTAX_OP_ARITH_SHIFT_LEFT, 9, 0)
	BINARY(">>>", PIGEN_SYNTAX_OP_ARITH_SHIFT_RIGHT, 9, 0)
	BINARY("+", PIGEN_SYNTAX_OP_ADD, 10, 0)
	BINARY("-", PIGEN_SYNTAX_OP_SUBTRACT, 10, 0)
	BINARY("*", PIGEN_SYNTAX_OP_MULTIPLY, 11, 0)
	BINARY("/", PIGEN_SYNTAX_OP_DIVIDE, 11, 0)
	BINARY("%", PIGEN_SYNTAX_OP_MODULO, 11, 0)
	BINARY("**", PIGEN_SYNTAX_OP_POWER, 12, 1)
#undef BINARY
	return 0;
}

static pigen_syntax_expr_id parse_precedence(expression_parser *parser,
	unsigned minimum);

static int collect_sequence(expression_parser *parser, const char *closer,
	pigen_syntax_expr_id **items, size_t *count)
{
	size_t capacity = 0;
	*items = NULL;
	*count = 0;
	if (token_is(parser, parser->at, closer))
		return 1;
	for (;;)
	{
		pigen_syntax_expr_id item = parse_precedence(parser, 1);
		if (item.index == PIGEN_INVALID_ID)
		{
			free(*items);
			return 0;
		}
		if (*count == capacity)
		{
			capacity = capacity ? capacity * 2 : 4;
			*items = pigen_resize(*items, capacity * sizeof(**items));
		}
		(*items)[(*count)++] = item;
		if (!token_is(parser, parser->at, ",")) break;
		parser->at++;
		if (token_is(parser, parser->at, closer))
		{
			free(*items);
			*items = NULL;
			*count = 0;
			fail(parser, parser->at, "trailing comma in expression list");
			return 0;
		}
	}
	return 1;
}

static pigen_syntax_expr_id parse_concatenation(expression_parser *parser)
{
	size_t start = parser->at++;
	pigen_syntax_expr_id first;
	pigen_syntax_expr_id *items = NULL;
	size_t count = 0;
	pigen_syntax_expr node = {0};

	if (parser->at >= parser->after || token_is(parser, parser->at, "}"))
		return fail(parser, parser->at, "empty concatenation");
	first = parse_precedence(parser, 1);
	if (first.index == PIGEN_INVALID_ID) return first;
	if (token_is(parser, parser->at, "{"))
	{
		parser->at++;
		if (!collect_sequence(parser, "}", &items, &count)) return INVALID_ID(pigen_syntax_expr_id);
		if (!count || !token_is(parser, parser->at, "}"))
		{
			free(items);
			return fail(parser, parser->at, "unterminated replication body");
		}
		parser->at++;
		if (!token_is(parser, parser->at, "}"))
		{
			free(items);
			return fail(parser, parser->at, "unterminated replication");
		}
		parser->at++;
		node.kind = PIGEN_SYNTAX_EXPR_REPLICATION;
		node.location = pigen_syntax_location_from_extent(parser->source,
			start, parser->at);
		node.as.replication.count = first;
		node.as.replication.first_child = append_children(parser, items, count);
		node.as.replication.child_count = count;
		free(items);
		return add_node(parser, node);
	}
	items = pigen_resize(NULL, sizeof(*items));
	items[count++] = first;
	while (token_is(parser, parser->at, ","))
	{
		pigen_syntax_expr_id item;
		parser->at++;
		item = parse_precedence(parser, 1);
		if (item.index == PIGEN_INVALID_ID)
		{
			free(items);
			return item;
		}
		items = pigen_resize(items, (count + 1) * sizeof(*items));
		items[count++] = item;
	}
	if (!token_is(parser, parser->at, "}"))
	{
		free(items);
		return fail(parser, parser->at, "unterminated concatenation");
	}
	parser->at++;
	node.kind = PIGEN_SYNTAX_EXPR_CONCATENATION;
	node.location = pigen_syntax_location_from_extent(parser->source,
		start, parser->at);
	node.as.sequence.first_child = append_children(parser, items, count);
	node.as.sequence.child_count = count;
	free(items);
	return add_node(parser, node);
}

static pigen_syntax_expr_id parse_record(expression_parser *parser)
{
	size_t start=parser->at;
	parser->at+=2;
	pigen_syntax_expr_id *fields=NULL;
	size_t count=0;int keyed=-1;
	while(parser->at<parser->after && !token_is(parser,parser->at,"}"))
	{
		size_t field_start=parser->at;
		pigen_syntax_expr_id key=parse_precedence(parser,1);
		if(key.index==PIGEN_INVALID_ID) {free(fields);return key;}
		if(!count && token_is(parser,parser->at,"{"))
		{
			parser->at++;
			if(!collect_sequence(parser,"}",&fields,&count)) return INVALID_ID(pigen_syntax_expr_id);
			if(!count || !token_is(parser,parser->at,"}"))
			{free(fields);return fail(parser,parser->at,"unterminated assignment pattern replication body");}
			parser->at++;
			if(!token_is(parser,parser->at,"}"))
			{free(fields);return fail(parser,parser->at,"assignment pattern replication must cover a whole dimension");}
			parser->at++;
			pigen_syntax_expr repeat={.kind=PIGEN_SYNTAX_EXPR_ARRAY_PATTERN_REPLICATION};
			repeat.location=pigen_syntax_location_from_extent(parser->source,start,parser->at);
			repeat.as.replication.count=key;
			repeat.as.replication.first_child=append_children(parser,fields,count);
			repeat.as.replication.child_count=count;
			free(fields);return add_node(parser,repeat);
		}
		int has_key=token_is(parser,parser->at,":");
		if(keyed<0) keyed=has_key;
		if(keyed!=has_key)
		{free(fields);return fail(parser,parser->at,"assignment pattern cannot mix keyed and positional entries");}
		pigen_syntax_expr_id value=key;
		if(keyed)
		{
			parser->at++;
			value=parse_precedence(parser,1);
			if(value.index==PIGEN_INVALID_ID) {free(fields);return value;}
			const pigen_syntax_expr *name=pigen_syntax_expr_get(parser->arena,key);
			pigen_syntax_expr field={.kind=PIGEN_SYNTAX_EXPR_RECORD_FIELD};
			field.location=pigen_syntax_location_from_extent(parser->source,field_start,parser->at);
			field.as.member.member=name && name->kind==PIGEN_SYNTAX_EXPR_NAME ? name->as.atom.token : INVALID_ID(pigen_token_id);
			field.as.member.base=value;field.as.member.pattern_key=key;
			value=add_node(parser,field);
		}
		fields=pigen_resize(fields,(count+1)*sizeof(*fields));fields[count++]=value;
		if(!token_is(parser,parser->at,",")) break;
		parser->at++;
		if(token_is(parser,parser->at,"}")) {free(fields);return fail(parser,parser->at,"record construction has a trailing comma");}
	}
	if(!count || !token_is(parser,parser->at,"}"))
	{free(fields);return fail(parser,parser->at,"empty or unterminated record construction");}
	parser->at++;
	pigen_syntax_expr record={.kind=keyed ? PIGEN_SYNTAX_EXPR_RECORD : PIGEN_SYNTAX_EXPR_ARRAY_PATTERN};
	record.location=pigen_syntax_location_from_extent(parser->source,start,parser->at);
	record.as.sequence.first_child=append_children(parser,fields,count);record.as.sequence.child_count=count;
	free(fields);return add_node(parser,record);
}

static pigen_syntax_expr_id parse_primary(expression_parser *parser)
{
	size_t start = parser->at;
	const pigen_expanded_token *token;
	pigen_syntax_expr node = {0};
	pigen_syntax_expr_id result;
	pigen_syntax_type_id cast_type;
	size_t type_after;
	size_t original_node_count;
	size_t original_child_count;
	size_t original_type_count;
	size_t original_type_argument_count;

	if (parser->at >= parser->after)
		return fail(parser, parser->at, "expected expression");
	if (token_is(parser, parser->at, "'") && token_is(parser, parser->at+1, "{"))
		return parse_record(parser);
	original_node_count = parser->arena->node_count;
	original_child_count = parser->arena->child_count;
	original_type_count = parser->types->node_count;
	original_type_argument_count = parser->types->argument_count;
	if (pigen_parse_type_prefix(parser->source, parser->at, parser->after,
		parser->arena, parser->types, &cast_type, &type_after, NULL) &&
		pigen_syntax_type_get(parser->types, cast_type)->base.index !=
			PIGEN_INVALID_ID &&
		token_is(parser, type_after, "'") && token_is(parser, type_after + 1, "("))
	{
		parser->at = type_after + 2;
		result = parse_precedence(parser, 1);
		if (result.index == PIGEN_INVALID_ID) return result;
		if (!token_is(parser, parser->at, ")"))
			return fail(parser, parser->at, "unterminated cast");
		parser->at++;
		node.kind = PIGEN_SYNTAX_EXPR_CAST;
		node.location = pigen_syntax_location_from_extent(parser->source,
			start, parser->at);
		node.as.cast.type = cast_type;
		node.as.cast.value = result;
		return add_node(parser, node);
	}
	parser->arena->node_count = original_node_count;
	parser->arena->child_count = original_child_count;
	parser->types->node_count = original_type_count;
	parser->types->argument_count = original_type_argument_count;
	if (token_is(parser, parser->at, "("))
	{
		parser->at++;
		result = parse_precedence(parser, 1);
		if (result.index == PIGEN_INVALID_ID) return result;
		if (!token_is(parser, parser->at, ")"))
			return fail(parser, parser->at, "unterminated parenthesized expression");
		parser->at++;
		node.kind = PIGEN_SYNTAX_EXPR_GROUP;
		node.location = pigen_syntax_location_from_extent(parser->source,
			start, parser->at);
		node.as.group.operand = result;
		return add_node(parser, node);
	}
	if (token_is(parser, parser->at, "{"))
		return parse_concatenation(parser);
	token = token_at(parser, parser->at);
	if (token && (token->kind == PIGEN_TOKEN_NUMBER ||
		token->kind == PIGEN_TOKEN_STRING))
		node.kind = PIGEN_SYNTAX_EXPR_LITERAL;
	else if (token && token->kind == PIGEN_TOKEN_IDENTIFIER)
		node.kind = PIGEN_SYNTAX_EXPR_NAME;
	else if (token_is(parser, parser->at, "'") &&
		parser->at + 1 < parser->after &&
		(token_at(parser, parser->at + 1)->kind == PIGEN_TOKEN_NUMBER ||
		 token_at(parser, parser->at + 1)->kind == PIGEN_TOKEN_IDENTIFIER))
	{
		node.kind = PIGEN_SYNTAX_EXPR_LITERAL;
		parser->at++;
	}
	else
		return fail(parser, parser->at, "unsupported expression primary");
	node.as.atom.token = (pigen_token_id){(uint32_t)start};
	parser->at++;
	node.location = pigen_syntax_location_from_extent(parser->source,
		start, parser->at);
	return add_node(parser, node);
}

static int inquiry_type_argument(expression_parser *parser,pigen_syntax_expr_id callee,
 pigen_syntax_type_id *type)
{
 const pigen_syntax_expr *name=pigen_syntax_expr_get(parser->arena,callee);
 if(!name || name->kind!=PIGEN_SYNTAX_EXPR_NAME) return 0;
 const char *queries[]={"$bits","$left","$right","$low","$high","$increment","$size","$dimensions","$unpacked_dimensions"};
 int inquiry=0;
 for(size_t i=0;i<sizeof queries/sizeof *queries;i++)
  if(token_is(parser,name->as.atom.token.index,queries[i])) inquiry=1;
 if(!inquiry) return 0;
 size_t nodes=parser->arena->node_count,children=parser->arena->child_count;
 size_t types=parser->types->node_count,arguments=parser->types->argument_count,members=parser->types->member_count,after;
 pigen_syntax_type_id candidate;
 if(pigen_parse_type_prefix(parser->source,parser->at,parser->after,parser->arena,parser->types,&candidate,&after,NULL) && (token_is(parser,after,")") || token_is(parser,after,",")))
 {
  pigen_syntax_type *known=&parser->types->nodes[candidate.index];
  int ownership=pigen_syntax_type_declaration_ownership(parser->source,parser->types,candidate,NULL,NULL);
  if(known->base.index!=PIGEN_INVALID_ID && token_is(parser,known->base.index,"int") && !known->argument_count)
  {known->is_systemverilog_int=1;ownership=PIGEN_SYNTAX_TYPE_SYSTEMVERILOG;}
  /* A count-shaped suffix alone cannot distinguish T[2] from value[2]. */
  if(ownership==PIGEN_SYNTAX_TYPE_PIGEN && known->base.index!=PIGEN_INVALID_ID)
  {
   const char *primitives[]={"int","uint","bit","logic","byte","integer","shortint","longint","time"};
   int primitive=0;
   for(size_t i=0;i<sizeof primitives/sizeof *primitives;i++)
    if(token_is(parser,known->base.index,primitives[i])) primitive=1;
   if(!primitive) ownership=PIGEN_SYNTAX_TYPE_UNOWNED;
  }
  if(ownership!=PIGEN_SYNTAX_TYPE_UNOWNED)
  {*type=candidate;parser->at=after;return 1;}
  if(known->signedness!=PIGEN_SYNTAX_SIGN_IMPLICIT)
  {*type=candidate;parser->at=after;return 1;}
  if(known->argument_count)
  {*type=candidate;return 2;}
 }
 parser->arena->node_count=nodes;parser->arena->child_count=children;
 parser->types->node_count=types;parser->types->argument_count=arguments;parser->types->member_count=members;
 return 0;
}

static pigen_syntax_expr_id parse_postfix(expression_parser *parser)
{
	pigen_syntax_expr_id base = parse_primary(parser);
	if (base.index == PIGEN_INVALID_ID) return base;
	for (;;)
	{
		size_t start = parser->arena->nodes[base.index].location.extent.first.index;
		pigen_syntax_expr node = {0};
		if (token_is(parser, parser->at, "'") && token_is(parser, parser->at + 1, "("))
		{
			parser->at += 2;
			pigen_syntax_expr_id value = parse_precedence(parser, 1);
			if (value.index == PIGEN_INVALID_ID) return value;
			if (!token_is(parser, parser->at, ")"))
				return fail(parser, parser->at, "unterminated size cast");
			parser->at++;
			node.kind = PIGEN_SYNTAX_EXPR_CAST;
			node.as.cast.type = INVALID_ID(pigen_syntax_type_id);
			node.as.cast.value = value;
			node.as.cast.is_size = 1;
			node.as.cast.size = base;
		}
		else if (token_is(parser, parser->at, "("))
		{
			pigen_syntax_expr_id *arguments = NULL;
			size_t count = 0;
			parser->at++;
			node.as.call.type_argument_kind=inquiry_type_argument(parser,base,&node.as.call.type_argument);
			if(node.as.call.type_argument_kind==1 && token_is(parser,parser->at,","))
   {
    parser->at++;
    if(token_is(parser,parser->at,")")) return fail(parser,parser->at,"missing inquiry dimension after comma");
    if(!collect_sequence(parser,")",&arguments,&count)) return INVALID_ID(pigen_syntax_expr_id);
   }
   else if (node.as.call.type_argument_kind!=1 && !collect_sequence(parser, ")", &arguments, &count)) return INVALID_ID(pigen_syntax_expr_id);
			if (!token_is(parser, parser->at, ")"))
			{
				free(arguments);
				return fail(parser, parser->at, "unterminated call");
			}
			parser->at++;
			node.kind = PIGEN_SYNTAX_EXPR_CALL;
			node.as.call.callee = base;
			node.as.call.first_argument = append_children(parser, arguments, count);
			node.as.call.argument_count = count;
			free(arguments);
		}
		else if (token_is(parser, parser->at, ".") ||
			token_is(parser, parser->at, "::"))
		{
			int scoped = token_is(parser, parser->at, "::");
			parser->at++;
			if (parser->at >= parser->after ||
				token_at(parser, parser->at)->kind != PIGEN_TOKEN_IDENTIFIER)
				return fail(parser, parser->at, "member access requires a name");
			node.kind = PIGEN_SYNTAX_EXPR_MEMBER;
			node.as.member.base = base;
			node.as.member.member = (pigen_token_id){(uint32_t)parser->at++};
			node.as.member.scoped = scoped;
		}
		else if (token_is(parser, parser->at, "["))
		{
			pigen_syntax_expr_id left;
			parser->at++;
			left = parse_precedence(parser, 1);
			if (left.index == PIGEN_INVALID_ID) return left;
			if (token_is(parser, parser->at, "]"))
			{
				node.kind = PIGEN_SYNTAX_EXPR_INDEX;
				node.as.index.base = base;
				node.as.index.index = left;
			}
			else
			{
				pigen_syntax_select_kind kind;
				pigen_syntax_expr_id right;
				if (token_is(parser, parser->at, ":")) kind = PIGEN_SELECT_RANGE;
				else if (token_is(parser, parser->at, "+:")) kind = PIGEN_SELECT_INDEXED_UP;
				else if (token_is(parser, parser->at, "-:")) kind = PIGEN_SELECT_INDEXED_DOWN;
				else return fail(parser, parser->at, "expected select operator or `]`");
				parser->at++;
				right = parse_precedence(parser, 1);
				if (right.index == PIGEN_INVALID_ID) return right;
				node.kind = PIGEN_SYNTAX_EXPR_SELECT;
				node.as.select.base = base;
				node.as.select.left = left;
				node.as.select.right = right;
				node.as.select.kind = kind;
			}
			if (!token_is(parser, parser->at, "]"))
				return fail(parser, parser->at, "unterminated select");
			parser->at++;
		}
		else break;
		node.location = pigen_syntax_location_from_extent(parser->source,
			start, parser->at);
		base = add_node(parser, node);
	}
	return base;
}

static pigen_syntax_expr_id parse_prefix(expression_parser *parser)
{
	pigen_syntax_operator operator;
	if (unary_operator_at(parser, parser->at, &operator))
	{
		size_t start = parser->at;
		pigen_syntax_location operator_location =
			pigen_syntax_location_from_extent(parser->source,
				parser->at, parser->at + 1);
		pigen_syntax_expr node = {0};
		pigen_syntax_expr_id operand;
		parser->at++;
		operand = parse_prefix(parser);
		if (operand.index == PIGEN_INVALID_ID) return operand;
		node.kind = PIGEN_SYNTAX_EXPR_UNARY;
		node.location = pigen_syntax_location_from_extent(parser->source,
			start, parser->at);
		node.as.unary.operator = operator;
		node.as.unary.operator_location = operator_location;
		node.as.unary.operand = operand;
		return add_node(parser, node);
	}
	return parse_postfix(parser);
}

static pigen_syntax_expr_id parse_precedence(expression_parser *parser,
	unsigned minimum)
{
	pigen_syntax_expr_id left = parse_prefix(parser);
	if (left.index == PIGEN_INVALID_ID) return left;
	for (;;)
	{
		binary_operator operator;
		if (binary_operator_at(parser, parser->at, &operator) &&
			operator.precedence >= minimum)
		{
			size_t start = parser->arena->nodes[left.index].location.extent.first.index;
			pigen_syntax_location operator_location =
				pigen_syntax_location_from_extent(parser->source,
					parser->at, parser->at + 1);
			pigen_syntax_expr node = {0};
			pigen_syntax_expr_id right;
			parser->at++;
			right = parse_precedence(parser, operator.precedence +
				(operator.right_associative ? 0 : 1));
			if (right.index == PIGEN_INVALID_ID) return right;
			node.kind = PIGEN_SYNTAX_EXPR_BINARY;
			node.location = pigen_syntax_location_from_extent(parser->source,
				start, parser->at);
			node.as.binary.operator = operator.operator;
			node.as.binary.operator_location = operator_location;
			node.as.binary.left = left;
			node.as.binary.right = right;
			left = add_node(parser, node);
			continue;
		}
		if (minimum <= 1 && token_is(parser, parser->at, "?"))
		{
			size_t start = parser->arena->nodes[left.index].location.extent.first.index;
			pigen_syntax_expr node = {0};
			pigen_syntax_expr_id when_true;
			pigen_syntax_expr_id when_false;
			parser->at++;
			when_true = parse_precedence(parser, 1);
			if (when_true.index == PIGEN_INVALID_ID) return when_true;
			if (!token_is(parser, parser->at, ":"))
				return fail(parser, parser->at, "conditional expression requires `:`");
			parser->at++;
			when_false = parse_precedence(parser, 1);
			if (when_false.index == PIGEN_INVALID_ID) return when_false;
			node.kind = PIGEN_SYNTAX_EXPR_CONDITIONAL;
			node.location = pigen_syntax_location_from_extent(parser->source,
				start, parser->at);
			node.as.conditional.condition = left;
			node.as.conditional.when_true = when_true;
			node.as.conditional.when_false = when_false;
			left = add_node(parser, node);
			continue;
		}
		break;
	}
	return left;
}

int pigen_parse_expression(const pigen_expanded_source *source,
	size_t first, size_t after, pigen_syntax_expr_arena *arena,
	pigen_syntax_type_arena *types,
	pigen_syntax_expr_id *expression, pigen_syntax_error *error)
{
	expression_parser parser = {source, arena, types, error, first, after};
	pigen_syntax_expr_id result;
	size_t original_node_count;
	size_t original_child_count;
	size_t original_type_count;
	size_t original_type_argument_count;
	size_t original_type_member_count;
	if (expression) *expression = INVALID_ID(pigen_syntax_expr_id);
	if (!source || !arena || !types || first >= after ||
		after > source->token_count)
		return 0;
	original_node_count = arena->node_count;
	original_child_count = arena->child_count;
	original_type_count = types->node_count;
	original_type_argument_count = types->argument_count;
	original_type_member_count = types->member_count;
	result = parse_precedence(&parser, 1);
	if (result.index == PIGEN_INVALID_ID) goto fail_parse;
	if (parser.at != after)
	{
		fail(&parser, parser.at, "unsupported trailing expression syntax");
		goto fail_parse;
	}
	if (expression) *expression = result;
	return 1;
fail_parse:
	arena->node_count = original_node_count;
	arena->child_count = original_child_count;
	types->node_count = original_type_count;
	types->argument_count = original_type_argument_count;
	types->member_count = original_type_member_count;
	return 0;
}

const pigen_syntax_expr *pigen_syntax_expr_get(
	const pigen_syntax_expr_arena *arena, pigen_syntax_expr_id expression)
{
	if (!arena || expression.index == PIGEN_INVALID_ID ||
		expression.index >= arena->node_count) return NULL;
	return &arena->nodes[expression.index];
}

const pigen_syntax_expr_id *pigen_syntax_expr_children(
	const pigen_syntax_expr_arena *arena, size_t first, size_t count)
{
	if (!arena || !count || first + count > arena->child_count) return NULL;
	return arena->children + first;
}

void pigen_free_syntax_expr_arena(pigen_syntax_expr_arena *arena)
{
	if (!arena) return;
	free(arena->nodes);
	free(arena->children);
	*arena = (pigen_syntax_expr_arena){0};
}

static int decimal_constant(const pigen_expanded_source *source,
	const pigen_syntax_expr_arena *arena,pigen_syntax_expr_id id,size_t depth)
{
	const pigen_syntax_expr *node=pigen_syntax_expr_get(arena,id);
	if (!node || depth>128) return 0;
	if (node->kind==PIGEN_SYNTAX_EXPR_LITERAL)
	{
		const pigen_expanded_token *token=pigen_expanded_token_get(source,node->as.atom.token);
		size_t length=0;const char *text=token ? pigen_expanded_token_text(source,token,&length) : NULL;
		return token && token->kind==PIGEN_TOKEN_NUMBER && length && text[0]>='0' && text[0]<='9' && !memchr(text,'\'',length);
	}
	if (node->kind==PIGEN_SYNTAX_EXPR_GROUP) return decimal_constant(source,arena,node->as.group.operand,depth+1);
	if (node->kind==PIGEN_SYNTAX_EXPR_UNARY) return decimal_constant(source,arena,node->as.unary.operand,depth+1);
	if (node->kind==PIGEN_SYNTAX_EXPR_BINARY) return decimal_constant(source,arena,node->as.binary.left,depth+1) && decimal_constant(source,arena,node->as.binary.right,depth+1);
	if (node->kind==PIGEN_SYNTAX_EXPR_CONDITIONAL) return decimal_constant(source,arena,node->as.conditional.condition,depth+1) &&
		decimal_constant(source,arena,node->as.conditional.when_true,depth+1) && decimal_constant(source,arena,node->as.conditional.when_false,depth+1);
	return 0;
}

int pigen_syntax_expr_is_decimal_constant(const pigen_expanded_source *source,
	const pigen_syntax_expr_arena *arena,pigen_syntax_expr_id expression)
{
	return source && decimal_constant(source,arena,expression,0);
}
