/* Intrinsic expression analysis without semantic-expression construction. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/expression_analysis.h"
#include "pigen/type_resolve.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

typedef struct {
	const pigen_syntax_tree *syntax;
	pigen_semantic_model *model;
	pigen_scope_id scope;
	int constant_only;
	pigen_analyzed_expr_arena *arena;
	pigen_semantic_error *error;
} expression_analyzer;

static pigen_analyzed_expr_id fail(expression_analyzer *analyzer,
	pigen_syntax_location location, const char *message)
{
	if (analyzer->error)
	{
		analyzer->error->origin = location.origin;
		analyzer->error->span = location.source_span;
		analyzer->error->message = message;
	}
	return INVALID_ID(pigen_analyzed_expr_id);
}

static pigen_analyzed_expr_id append_node(expression_analyzer *analyzer,
	pigen_analyzed_expr node)
{
	pigen_analyzed_expr_id result;

	if (analyzer->arena->node_count == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_analyzed_expr_id);
	if (analyzer->arena->node_count == analyzer->arena->node_capacity)
	{
		analyzer->arena->node_capacity = analyzer->arena->node_capacity ?
			analyzer->arena->node_capacity * 2 : 16;
		analyzer->arena->nodes = pigen_resize(analyzer->arena->nodes,
			analyzer->arena->node_capacity * sizeof(*analyzer->arena->nodes));
	}
	result = (pigen_analyzed_expr_id){
		(uint32_t)analyzer->arena->node_count};
	analyzer->arena->nodes[analyzer->arena->node_count++] = node;
	return result;
}

static size_t append_children(expression_analyzer *analyzer,
	const pigen_analyzed_expr_id *children, size_t count)
{
	size_t first = analyzer->arena->child_count;
	size_t needed;
	size_t capacity;

	if (!count || count > SIZE_MAX - first) return SIZE_MAX;
	needed = first + count;
	if (needed > analyzer->arena->child_capacity)
	{
		capacity = analyzer->arena->child_capacity ?
			analyzer->arena->child_capacity * 2 : 32;
		while (capacity < needed) capacity *= 2;
		analyzer->arena->children = pigen_resize(analyzer->arena->children,
			capacity * sizeof(*analyzer->arena->children));
		analyzer->arena->child_capacity = capacity;
	}
	memcpy(analyzer->arena->children + first, children,
		count * sizeof(*children));
	analyzer->arena->child_count = needed;
	return first;
}

static size_t append_states(expression_analyzer *analyzer,
	const pigen_bit_state *states, size_t count)
{
	size_t first = analyzer->arena->literal_state_count;
	size_t needed;
	size_t capacity;

	if (!count || count > SIZE_MAX - first) return SIZE_MAX;
	needed = first + count;
	if (needed > analyzer->arena->literal_state_capacity)
	{
		capacity = analyzer->arena->literal_state_capacity ?
			analyzer->arena->literal_state_capacity * 2 : 32;
		while (capacity < needed) capacity *= 2;
		analyzer->arena->literal_states = pigen_resize(
			analyzer->arena->literal_states,
			capacity * sizeof(*analyzer->arena->literal_states));
		analyzer->arena->literal_state_capacity = capacity;
	}
	memcpy(analyzer->arena->literal_states + first, states,
		count * sizeof(*states));
	analyzer->arena->literal_state_count = needed;
	return first;
}

static int decimal_size(const char *text, size_t length, size_t *value)
{
	size_t i;
	int any = 0;

	*value = 0;
	for (i = 0; i < length; i++)
	{
		unsigned digit;
		if (text[i] == '_') continue;
		if (text[i] < '0' || text[i] > '9') return 0;
		digit = (unsigned)(text[i] - '0');
		if (*value > (SIZE_MAX - digit) / 10) return 0;
		*value = *value * 10 + digit;
		any = 1;
	}
	return any && *value;
}

static int based_digit(char character, unsigned base, unsigned *value,
	pigen_bit_state *special)
{
	unsigned digit;

	if (character >= '0' && character <= '9')
		digit = (unsigned)(character - '0');
	else if (character >= 'a' && character <= 'f')
		digit = (unsigned)(character - 'a') + 10;
	else if (character >= 'A' && character <= 'F')
		digit = (unsigned)(character - 'A') + 10;
	else if (character == 'x' || character == 'X')
	{
		*special = PIGEN_BIT_X;
		return 1;
	}
	else if (character == 'z' || character == 'Z')
	{
		*special = PIGEN_BIT_Z;
		return 1;
	}
	else
		return 0;
	if (digit >= base) return 0;
	*value = digit;
	*special = PIGEN_BIT_ZERO;
	return 1;
}

static int fill_power_of_two_literal(const char *digits, size_t length,
	unsigned base, pigen_bit_state *states, size_t width)
{
	unsigned group = base == 2 ? 1 : base == 8 ? 3 : 4;
	size_t position = length;
	size_t bit = 0;
	pigen_bit_state padding = PIGEN_BIT_ZERO;

	while (position && digits[position - 1] == '_') position--;
	if (!position) return 0;
	{
		unsigned ignored = 0;
		pigen_bit_state leading;
		if (!based_digit(digits[0], base, &ignored, &leading)) return 0;
		if (leading == PIGEN_BIT_X || leading == PIGEN_BIT_Z)
			padding = leading;
	}
	for (bit = 0; bit < width; bit++) states[bit] = padding;
	bit = 0;
	while (position)
	{
		unsigned value = 0;
		pigen_bit_state special;
		unsigned i;
		char character = digits[--position];

		if (character == '_') continue;
		if (!based_digit(character, base, &value, &special)) return 0;
		for (i = 0; i < group && bit < width; i++, bit++)
			states[bit] = special == PIGEN_BIT_X || special == PIGEN_BIT_Z ?
				special : ((value >> i) & 1 ? PIGEN_BIT_ONE : PIGEN_BIT_ZERO);
	}
	return 1;
}

static int fill_decimal_literal(const char *digits, size_t length,
	pigen_bit_state *states, size_t width)
{
	size_t at;
	size_t bit;
	int any = 0;

	for (bit = 0; bit < width; bit++) states[bit] = PIGEN_BIT_ZERO;
	for (at = 0; at < length; at++)
	{
		unsigned carry;
		if (digits[at] == '_') continue;
		if (digits[at] < '0' || digits[at] > '9') return 0;
		carry = (unsigned)(digits[at] - '0');
		for (bit = 0; bit < width; bit++)
		{
			unsigned value =
				(states[bit] == PIGEN_BIT_ONE ? 1U : 0U) * 10U + carry;
			states[bit] = value & 1U ? PIGEN_BIT_ONE : PIGEN_BIT_ZERO;
			carry = value >> 1;
		}
		any = 1;
	}
	return any;
}

static int based_literal(expression_analyzer *analyzer,
	const pigen_syntax_expr *expression, pigen_bit_state **states,
	size_t *state_count, pigen_data_type_id *type)
{
	const pigen_expanded_token *token;
	const char *text;
	const char *apostrophe;
	size_t length;
	size_t width;
	size_t at;
	unsigned base;
	int is_signed = 0;

	*states = NULL;
	*state_count = 0;
	*type = INVALID_ID(pigen_data_type_id);
	if (expression->location.extent.after.index !=
		expression->location.extent.first.index + 1)
		return 0;
	token = pigen_expanded_token_get(analyzer->syntax->expanded,
		expression->as.atom.token);
	text = pigen_expanded_token_text(analyzer->syntax->expanded, token,
		&length);
	apostrophe = text ? memchr(text, '\'', length) : NULL;
	if (!apostrophe || apostrophe == text ||
		!decimal_size(text, (size_t)(apostrophe - text), &width))
		return 0;
	at = (size_t)(apostrophe - text) + 1;
	if (at < length && (text[at] == 's' || text[at] == 'S'))
	{
		is_signed = 1;
		at++;
	}
	if (at == length) return 0;
	switch (text[at++])
	{
		case 'b': case 'B': base = 2; break;
		case 'o': case 'O': base = 8; break;
		case 'd': case 'D': base = 10; break;
		case 'h': case 'H': base = 16; break;
		default: return 0;
	}
	if (at == length || width > SIZE_MAX / sizeof(**states)) return 0;
	*states = pigen_resize(NULL, width * sizeof(**states));
	if (base == 10 ? !fill_decimal_literal(text + at, length - at,
			*states, width) :
		!fill_power_of_two_literal(text + at, length - at, base,
			*states, width))
	{
		free(*states);
		*states = NULL;
		return 0;
	}
	*type = pigen_data_type_sized_logic(analyzer->model, width,
		is_signed ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED);
	if (type->index == PIGEN_INVALID_ID)
	{
		free(*states);
		*states = NULL;
		return 0;
	}
	*state_count = width;
	return 1;
}

static const char *token_text(const expression_analyzer *analyzer,
	const pigen_syntax_expr *expression, size_t *length)
{
	const pigen_expanded_token *token;

	if (expression->location.extent.after.index !=
		expression->location.extent.first.index + 1)
		return NULL;
	token = pigen_expanded_token_get(analyzer->syntax->expanded,
		expression->as.atom.token);
	return pigen_expanded_token_text(analyzer->syntax->expanded, token, length);
}

static pigen_source_span token_spelling(const expression_analyzer *analyzer,
	pigen_token_id token)
{
	const pigen_expanded_token *known = pigen_expanded_token_get(
		analyzer->syntax->expanded, token);
	return known ? pigen_origin_spelling_span(analyzer->syntax->expanded,
		known->origin) :
		(pigen_source_span){INVALID_ID(pigen_source_id), 0, 0};
}

static int literal_u64(const char *text, size_t length, uint64_t *value)
{
	size_t i;
	int any = 0;

	*value = 0;
	for (i = 0; i < length; i++)
	{
		unsigned digit;
		if (text[i] == '_') continue;
		if (text[i] < '0' || text[i] > '9') return 0;
		digit = (unsigned)(text[i] - '0');
		if (*value > (UINT64_MAX - digit) / 10) return 0;
		*value = *value * 10 + digit;
		any = 1;
	}
	return any;
}

static int unary_operator(pigen_syntax_operator syntax,
	pigen_unary_operator *semantic)
{
	switch (syntax)
	{
		case PIGEN_SYNTAX_OP_POSITIVE: *semantic = PIGEN_UNARY_POSITIVE; break;
		case PIGEN_SYNTAX_OP_NEGATE: *semantic = PIGEN_UNARY_NEGATE; break;
		case PIGEN_SYNTAX_OP_LOGICAL_NOT:
			*semantic = PIGEN_UNARY_LOGICAL_NOT; break;
		case PIGEN_SYNTAX_OP_BITWISE_NOT:
			*semantic = PIGEN_UNARY_BITWISE_NOT; break;
		case PIGEN_SYNTAX_OP_REDUCTION_AND:
			*semantic = PIGEN_UNARY_REDUCTION_AND; break;
		case PIGEN_SYNTAX_OP_REDUCTION_NAND:
			*semantic = PIGEN_UNARY_REDUCTION_NAND; break;
		case PIGEN_SYNTAX_OP_REDUCTION_OR:
			*semantic = PIGEN_UNARY_REDUCTION_OR; break;
		case PIGEN_SYNTAX_OP_REDUCTION_NOR:
			*semantic = PIGEN_UNARY_REDUCTION_NOR; break;
		case PIGEN_SYNTAX_OP_REDUCTION_XOR:
			*semantic = PIGEN_UNARY_REDUCTION_XOR; break;
		case PIGEN_SYNTAX_OP_REDUCTION_XNOR:
			*semantic = PIGEN_UNARY_REDUCTION_XNOR; break;
		default: return 0;
	}
	return 1;
}

static int binary_operator(pigen_syntax_operator syntax,
	pigen_binary_operator *semantic)
{
#define MAP(from, to) case from: *semantic = to; break
	switch (syntax)
	{
		MAP(PIGEN_SYNTAX_OP_POWER, PIGEN_BINARY_POWER);
		MAP(PIGEN_SYNTAX_OP_MULTIPLY, PIGEN_BINARY_MULTIPLY);
		MAP(PIGEN_SYNTAX_OP_DIVIDE, PIGEN_BINARY_DIVIDE);
		MAP(PIGEN_SYNTAX_OP_MODULO, PIGEN_BINARY_MODULO);
		MAP(PIGEN_SYNTAX_OP_ADD, PIGEN_BINARY_ADD);
		MAP(PIGEN_SYNTAX_OP_SUBTRACT, PIGEN_BINARY_SUBTRACT);
		MAP(PIGEN_SYNTAX_OP_SHIFT_LEFT, PIGEN_BINARY_SHIFT_LEFT);
		MAP(PIGEN_SYNTAX_OP_SHIFT_RIGHT, PIGEN_BINARY_SHIFT_RIGHT);
		MAP(PIGEN_SYNTAX_OP_ARITH_SHIFT_LEFT, PIGEN_BINARY_ARITH_SHIFT_LEFT);
		MAP(PIGEN_SYNTAX_OP_ARITH_SHIFT_RIGHT, PIGEN_BINARY_ARITH_SHIFT_RIGHT);
		MAP(PIGEN_SYNTAX_OP_LESS, PIGEN_BINARY_LESS);
		MAP(PIGEN_SYNTAX_OP_LESS_EQUAL, PIGEN_BINARY_LESS_EQUAL);
		MAP(PIGEN_SYNTAX_OP_GREATER, PIGEN_BINARY_GREATER);
		MAP(PIGEN_SYNTAX_OP_GREATER_EQUAL, PIGEN_BINARY_GREATER_EQUAL);
		MAP(PIGEN_SYNTAX_OP_EQUAL, PIGEN_BINARY_EQUAL);
		MAP(PIGEN_SYNTAX_OP_NOT_EQUAL, PIGEN_BINARY_NOT_EQUAL);
		MAP(PIGEN_SYNTAX_OP_CASE_EQUAL, PIGEN_BINARY_CASE_EQUAL);
		MAP(PIGEN_SYNTAX_OP_CASE_NOT_EQUAL, PIGEN_BINARY_CASE_NOT_EQUAL);
		MAP(PIGEN_SYNTAX_OP_WILDCARD_EQUAL, PIGEN_BINARY_WILDCARD_EQUAL);
		MAP(PIGEN_SYNTAX_OP_WILDCARD_NOT_EQUAL,
			PIGEN_BINARY_WILDCARD_NOT_EQUAL);
		MAP(PIGEN_SYNTAX_OP_BITWISE_AND, PIGEN_BINARY_BITWISE_AND);
		MAP(PIGEN_SYNTAX_OP_BITWISE_XOR, PIGEN_BINARY_BITWISE_XOR);
		MAP(PIGEN_SYNTAX_OP_BITWISE_XNOR, PIGEN_BINARY_BITWISE_XNOR);
		MAP(PIGEN_SYNTAX_OP_BITWISE_OR, PIGEN_BINARY_BITWISE_OR);
		MAP(PIGEN_SYNTAX_OP_LOGICAL_AND, PIGEN_BINARY_LOGICAL_AND);
		MAP(PIGEN_SYNTAX_OP_LOGICAL_OR, PIGEN_BINARY_LOGICAL_OR);
		default: return 0;
	}
#undef MAP
	return 1;
}

static pigen_select_kind select_kind(pigen_syntax_select_kind kind)
{
	switch (kind)
	{
		case PIGEN_SELECT_RANGE: return PIGEN_SEMANTIC_SELECT_RANGE;
		case PIGEN_SELECT_INDEXED_UP: return PIGEN_SEMANTIC_SELECT_INDEXED_UP;
		case PIGEN_SELECT_INDEXED_DOWN:
			return PIGEN_SEMANTIC_SELECT_INDEXED_DOWN;
	}
	pigen_fail("invalid syntax select kind");
	return PIGEN_SEMANTIC_SELECT_RANGE;
}

static pigen_const_expr_id converted_constant(expression_analyzer *analyzer,
	pigen_const_expr_id constant, pigen_conversion conversion)
{
	if (constant.index == PIGEN_INVALID_ID ||
		conversion.kind == PIGEN_CONVERSION_IDENTITY)
		return constant;
	return pigen_const_expr_intern_conversion(analyzer->model, conversion,
		constant);
}

static pigen_analyzed_expr_id analyze(expression_analyzer *analyzer,
	pigen_syntax_expr_id syntax_id)
{
	const pigen_syntax_expr *syntax = pigen_syntax_expr_get(
		&analyzer->syntax->expressions, syntax_id);
	pigen_analyzed_expr node = {0};
	pigen_analyzed_expr_id operand;
	pigen_analyzed_expr_id left;
	pigen_analyzed_expr_id right;
	pigen_analyzed_expr_id condition;
	pigen_analyzed_expr_id when_true;
	pigen_analyzed_expr_id when_false;

	if (!syntax) return INVALID_ID(pigen_analyzed_expr_id);
	node.syntax = syntax_id;
	node.kind = syntax->kind;
	node.span = syntax->location.source_span;
	node.constant = INVALID_ID(pigen_const_expr_id);
	switch (syntax->kind)
	{
		case PIGEN_SYNTAX_EXPR_LITERAL:
		{
			const char *text;
			size_t length;
			uint64_t value;
			pigen_bit_state *states;
			size_t state_count;
			pigen_data_type_id type;

			text = token_text(analyzer, syntax, &length);
			if (!text) return fail(analyzer, syntax->location,
				"invalid integer literal");
			if (!memchr(text, '\'', length))
			{
				if (analyzer->constant_only && literal_u64(text, length, &value))
				{
					node.literal_kind = PIGEN_ANALYZED_LITERAL_INTEGER;
					node.data_type = pigen_data_type_unsized_integer(analyzer->model);
					node.constant = pigen_const_expr_intern_integer(analyzer->model,
						value, node.data_type);
					node.as.integer = value;
				}
				else
				{
					node.literal_kind = PIGEN_ANALYZED_LITERAL_EXACT_INTEGER;
					node.as.exact_integer = pigen_integer_intern_decimal(
						analyzer->model, text, length);
					node.data_type = pigen_data_type_exact_integer(analyzer->model,
						node.as.exact_integer);
					node.constant = pigen_const_expr_intern_exact_integer(
						analyzer->model, node.as.exact_integer, node.data_type);
				}
				node.shape = pigen_semantic_scalar_shape(analyzer->model);
				if (node.constant.index == PIGEN_INVALID_ID)
					return fail(analyzer, syntax->location,
						"invalid integer literal");
				return append_node(analyzer, node);
			}
			if (!based_literal(analyzer, syntax, &states, &state_count, &type))
				return fail(analyzer, syntax->location, "invalid based literal");
			node.literal_kind = PIGEN_ANALYZED_LITERAL_BITS;
			node.as.bits.first_state = append_states(analyzer, states,
				state_count);
			node.as.bits.state_count = state_count;
			node.data_type = type;
			node.shape = pigen_semantic_scalar_shape(analyzer->model);
			node.constant = pigen_const_expr_intern_bits(analyzer->model, states,
				state_count, type);
			free(states);
			if (node.as.bits.first_state == SIZE_MAX ||
				node.constant.index == PIGEN_INVALID_ID)
				return fail(analyzer, syntax->location, "invalid based literal");
			return append_node(analyzer, node);
		}
		case PIGEN_SYNTAX_EXPR_NAME:
		{
			pigen_symbol_id symbol = pigen_symbol_lookup(analyzer->model,
				analyzer->scope, token_spelling(analyzer, syntax->as.atom.token));
			const pigen_symbol *known = pigen_symbol_get(analyzer->model, symbol);
			const pigen_semantic_signal *signal;

			if (!known || (analyzer->constant_only &&
				known->kind != PIGEN_SYMBOL_PARAMETER) ||
				(!analyzer->constant_only &&
				known->kind != PIGEN_SYMBOL_PARAMETER &&
				known->kind != PIGEN_SYMBOL_SIGNAL))
				return fail(analyzer, syntax->location, "unknown value name");
			node.as.symbol = symbol;
			node.data_type = known->data_type;
			signal = known->kind == PIGEN_SYMBOL_SIGNAL ?
				pigen_signal_get(analyzer->model,
					pigen_symbol_signal(analyzer->model, symbol)) : NULL;
			if (known->kind == PIGEN_SYMBOL_SIGNAL && !signal)
				return fail(analyzer, syntax->location, "invalid signal name");
			node.shape = signal ? signal->shape :
				pigen_semantic_scalar_shape(analyzer->model);
			node.constant = known->kind == PIGEN_SYMBOL_PARAMETER ?
				pigen_const_expr_intern_symbol(analyzer->model, symbol,
					known->data_type) : INVALID_ID(pigen_const_expr_id);
			if (known->kind == PIGEN_SYMBOL_PARAMETER &&
				node.constant.index == PIGEN_INVALID_ID)
				return fail(analyzer, syntax->location,
					"invalid constant symbol");
			return append_node(analyzer, node);
		}
		case PIGEN_SYNTAX_EXPR_GROUP:
			operand = analyze(analyzer, syntax->as.group.operand);
			if (operand.index == PIGEN_INVALID_ID) return operand;
			node.as.group.operand = operand;
			node.data_type = analyzer->arena->nodes[operand.index].data_type;
			node.shape = analyzer->arena->nodes[operand.index].shape;
			node.constant = analyzer->arena->nodes[operand.index].constant;
			return append_node(analyzer, node);
		case PIGEN_SYNTAX_EXPR_UNARY:
		{
			pigen_unary_operator operator;
			pigen_const_expr_id constant;

			if (!unary_operator(syntax->as.unary.operator, &operator))
				return fail(analyzer, syntax->as.unary.operator_location,
					"unsupported unary operator");
			operand = analyze(analyzer, syntax->as.unary.operand);
			if (operand.index == PIGEN_INVALID_ID) return operand;
			if (!pigen_data_type_resolve_unary_operation(analyzer->model,
				operator, analyzer->arena->nodes[operand.index].data_type,
				&node.as.unary.resolution))
				return fail(analyzer, syntax->as.unary.operator_location,
					"invalid operand for unary operator");
			node.as.unary.operand = operand;
			node.data_type = node.as.unary.resolution.operation.result_data_type;
			node.shape = analyzer->arena->nodes[operand.index].shape;
			constant = converted_constant(analyzer,
				analyzer->arena->nodes[operand.index].constant,
				node.as.unary.resolution.operand_conversion);
			node.constant = constant.index == PIGEN_INVALID_ID ? constant :
				pigen_const_expr_intern_unary(analyzer->model,
					node.as.unary.resolution.operation, constant);
			return append_node(analyzer, node);
		}
		case PIGEN_SYNTAX_EXPR_BINARY:
		{
			pigen_binary_operator operator;
			pigen_const_expr_id left_constant;
			pigen_const_expr_id right_constant;

			if (!binary_operator(syntax->as.binary.operator, &operator))
				return fail(analyzer, syntax->as.binary.operator_location,
					"unsupported binary operator");
			left = analyze(analyzer, syntax->as.binary.left);
			right = analyze(analyzer, syntax->as.binary.right);
			if (left.index == PIGEN_INVALID_ID || right.index == PIGEN_INVALID_ID)
				return INVALID_ID(pigen_analyzed_expr_id);
			if (analyzer->arena->nodes[left.index].shape.index !=
				analyzer->arena->nodes[right.index].shape.index)
				return fail(analyzer, syntax->as.binary.operator_location,
					"binary operands have different shapes");
			if (!pigen_data_type_resolve_binary_operation(analyzer->model,
				operator, analyzer->arena->nodes[left.index].data_type,
				analyzer->arena->nodes[right.index].data_type,
				&node.as.binary.resolution))
				return fail(analyzer, syntax->as.binary.operator_location,
					"invalid operands for binary operator");
			node.as.binary.left = left;
			node.as.binary.right = right;
			node.data_type = node.as.binary.resolution.operation.result_data_type;
			node.shape = analyzer->arena->nodes[left.index].shape;
			left_constant = converted_constant(analyzer,
				analyzer->arena->nodes[left.index].constant,
				node.as.binary.resolution.left_conversion);
			right_constant = converted_constant(analyzer,
				analyzer->arena->nodes[right.index].constant,
				node.as.binary.resolution.right_conversion);
			node.constant = left_constant.index == PIGEN_INVALID_ID ||
				right_constant.index == PIGEN_INVALID_ID ?
				INVALID_ID(pigen_const_expr_id) :
				pigen_const_expr_intern_binary(analyzer->model,
					node.as.binary.resolution.operation, left_constant,
					right_constant);
			return append_node(analyzer, node);
		}
		case PIGEN_SYNTAX_EXPR_CONDITIONAL:
		{
			pigen_const_expr_id condition_constant;
			pigen_const_expr_id true_constant;
			pigen_const_expr_id false_constant;

			condition = analyze(analyzer, syntax->as.conditional.condition);
			when_true = analyze(analyzer, syntax->as.conditional.when_true);
			when_false = analyze(analyzer, syntax->as.conditional.when_false);
			if (condition.index == PIGEN_INVALID_ID ||
				when_true.index == PIGEN_INVALID_ID ||
				when_false.index == PIGEN_INVALID_ID)
				return INVALID_ID(pigen_analyzed_expr_id);
			if (analyzer->arena->nodes[when_true.index].shape.index !=
				analyzer->arena->nodes[when_false.index].shape.index)
				return fail(analyzer, syntax->location,
					"conditional alternatives have different shapes");
			if (!pigen_data_type_resolve_conditional_operation(analyzer->model,
				analyzer->arena->nodes[condition.index].data_type,
				analyzer->arena->nodes[when_true.index].data_type,
				analyzer->arena->nodes[when_false.index].data_type,
				&node.as.conditional.resolution))
				return fail(analyzer, syntax->location,
					"invalid conditional operands");
			node.as.conditional.condition = condition;
			node.as.conditional.when_true = when_true;
			node.as.conditional.when_false = when_false;
			node.data_type =
				node.as.conditional.resolution.operation.result_data_type;
			node.shape = analyzer->arena->nodes[when_true.index].shape;
			condition_constant = converted_constant(analyzer,
				analyzer->arena->nodes[condition.index].constant,
				node.as.conditional.resolution.condition_conversion);
			true_constant = converted_constant(analyzer,
				analyzer->arena->nodes[when_true.index].constant,
				node.as.conditional.resolution.when_true_conversion);
			false_constant = converted_constant(analyzer,
				analyzer->arena->nodes[when_false.index].constant,
				node.as.conditional.resolution.when_false_conversion);
			node.constant = condition_constant.index == PIGEN_INVALID_ID ||
				true_constant.index == PIGEN_INVALID_ID ||
				false_constant.index == PIGEN_INVALID_ID ?
				INVALID_ID(pigen_const_expr_id) :
				pigen_const_expr_intern_conditional(analyzer->model,
					node.as.conditional.resolution.operation, condition_constant,
					true_constant, false_constant);
			return append_node(analyzer, node);
		}
		case PIGEN_SYNTAX_EXPR_CAST:
		{
			pigen_data_type_id target = pigen_resolve_type(analyzer->syntax,
				analyzer->model, analyzer->scope, syntax->as.cast.type,
				analyzer->error);
			if (target.index == PIGEN_INVALID_ID)
				return INVALID_ID(pigen_analyzed_expr_id);
			operand = analyze(analyzer, syntax->as.cast.value);
			if (operand.index == PIGEN_INVALID_ID) return operand;
			if (!pigen_data_type_resolve_explicit_conversion(analyzer->model,
				analyzer->arena->nodes[operand.index].data_type, target,
				&node.as.conversion.conversion))
				return fail(analyzer, syntax->location, "invalid explicit cast");
			node.as.conversion.operand = operand;
			node.data_type = target;
			node.shape = analyzer->arena->nodes[operand.index].shape;
			node.constant = converted_constant(analyzer,
				analyzer->arena->nodes[operand.index].constant,
				node.as.conversion.conversion);
			return append_node(analyzer, node);
		}
		case PIGEN_SYNTAX_EXPR_INDEX:
		{
			const pigen_semantic_shape *base_shape;
			const pigen_semantic_shape *index_shape;

			left = analyze(analyzer, syntax->as.index.base);
			right = analyze(analyzer, syntax->as.index.index);
			if (left.index == PIGEN_INVALID_ID || right.index == PIGEN_INVALID_ID)
				return INVALID_ID(pigen_analyzed_expr_id);
			base_shape = pigen_shape_get(analyzer->model,
				analyzer->arena->nodes[left.index].shape);
			index_shape = pigen_shape_get(analyzer->model,
				analyzer->arena->nodes[right.index].shape);
			if (!base_shape || !index_shape || index_shape->dimension_count)
				return fail(analyzer, syntax->location, "invalid index operands");
			node.as.index.base = left;
			node.as.index.index = right;
			if (base_shape->dimension_count)
			{
				node.data_type = analyzer->arena->nodes[left.index].data_type;
				node.shape = pigen_shape_element(analyzer->model,
					analyzer->arena->nodes[left.index].shape);
				node.constant = INVALID_ID(pigen_const_expr_id);
			}
			else
			{
				node.data_type = pigen_data_type_packed_element(analyzer->model,
					analyzer->arena->nodes[left.index].data_type);
				node.shape = analyzer->arena->nodes[left.index].shape;
				node.constant = analyzer->arena->nodes[left.index].constant.index ==
					PIGEN_INVALID_ID ||
					analyzer->arena->nodes[right.index].constant.index ==
					PIGEN_INVALID_ID ? INVALID_ID(pigen_const_expr_id) :
					pigen_const_expr_intern_index(analyzer->model,
						analyzer->arena->nodes[left.index].constant,
						analyzer->arena->nodes[right.index].constant,
						node.data_type);
			}
			if (node.data_type.index == PIGEN_INVALID_ID)
				return fail(analyzer, syntax->location, "invalid indexed value");
			return append_node(analyzer, node);
		}
		case PIGEN_SYNTAX_EXPR_SELECT:
		{
			const pigen_semantic_shape *base_shape;
			const pigen_semantic_shape *left_shape;
			const pigen_semantic_shape *right_shape;
			pigen_const_expr_id type_left;

			operand = analyze(analyzer, syntax->as.select.base);
			left = analyze(analyzer, syntax->as.select.left);
			right = analyze(analyzer, syntax->as.select.right);
			if (operand.index == PIGEN_INVALID_ID ||
				left.index == PIGEN_INVALID_ID || right.index == PIGEN_INVALID_ID)
				return INVALID_ID(pigen_analyzed_expr_id);
			base_shape = pigen_shape_get(analyzer->model,
				analyzer->arena->nodes[operand.index].shape);
			left_shape = pigen_shape_get(analyzer->model,
				analyzer->arena->nodes[left.index].shape);
			right_shape = pigen_shape_get(analyzer->model,
				analyzer->arena->nodes[right.index].shape);
			if (!base_shape || !left_shape || !right_shape ||
				base_shape->dimension_count || left_shape->dimension_count ||
				right_shape->dimension_count ||
				analyzer->arena->nodes[right.index].constant.index ==
					PIGEN_INVALID_ID ||
				(syntax->as.select.kind == PIGEN_SELECT_RANGE &&
				analyzer->arena->nodes[left.index].constant.index ==
					PIGEN_INVALID_ID))
				return fail(analyzer, syntax->location, "invalid packed selection");
			node.as.select.base = operand;
			node.as.select.left = left;
			node.as.select.right = right;
			node.as.select.kind = select_kind(syntax->as.select.kind);
			type_left = node.as.select.kind == PIGEN_SEMANTIC_SELECT_RANGE ?
				analyzer->arena->nodes[left.index].constant :
				INVALID_ID(pigen_const_expr_id);
			node.data_type = pigen_data_type_packed_select(analyzer->model,
				analyzer->arena->nodes[operand.index].data_type, type_left,
				analyzer->arena->nodes[right.index].constant,
				node.as.select.kind);
			node.shape = analyzer->arena->nodes[operand.index].shape;
			if (node.data_type.index == PIGEN_INVALID_ID)
				return fail(analyzer, syntax->location, "invalid packed selection");
			node.constant =
				analyzer->arena->nodes[operand.index].constant.index ==
					PIGEN_INVALID_ID ||
				analyzer->arena->nodes[left.index].constant.index ==
					PIGEN_INVALID_ID ? INVALID_ID(pigen_const_expr_id) :
				pigen_const_expr_intern_select(analyzer->model,
					analyzer->arena->nodes[operand.index].constant,
					analyzer->arena->nodes[left.index].constant,
					analyzer->arena->nodes[right.index].constant,
					node.as.select.kind, node.data_type);
			return append_node(analyzer, node);
		}
		case PIGEN_SYNTAX_EXPR_CONCATENATION:
		{
			const pigen_syntax_expr_id *syntax_children =
				pigen_syntax_expr_children(&analyzer->syntax->expressions,
					syntax->as.sequence.first_child,
					syntax->as.sequence.child_count);
			pigen_analyzed_expr_id *children;
			pigen_data_type_id *types;
			pigen_const_expr_id *constants;
			size_t i;
			int all_constant = 1;

			if (!syntax_children || !syntax->as.sequence.child_count)
				return fail(analyzer, syntax->location, "invalid concatenation");
			children = pigen_resize(NULL,
				syntax->as.sequence.child_count * sizeof(*children));
			types = pigen_resize(NULL,
				syntax->as.sequence.child_count * sizeof(*types));
			constants = pigen_resize(NULL,
				syntax->as.sequence.child_count * sizeof(*constants));
			for (i = 0; i < syntax->as.sequence.child_count; i++)
			{
				const pigen_semantic_shape *shape;
				children[i] = analyze(analyzer, syntax_children[i]);
				if (children[i].index == PIGEN_INVALID_ID ||
					!(shape = pigen_shape_get(analyzer->model,
						analyzer->arena->nodes[children[i].index].shape)) ||
					shape->dimension_count)
				{
					free(children);
					free(types);
					free(constants);
					return fail(analyzer, syntax->location,
						"invalid concatenation element");
				}
				types[i] = analyzer->arena->nodes[children[i].index].data_type;
				constants[i] = analyzer->arena->nodes[children[i].index].constant;
				if (constants[i].index == PIGEN_INVALID_ID) all_constant = 0;
			}
			node.as.sequence.first_child = append_children(analyzer, children,
				syntax->as.sequence.child_count);
			node.as.sequence.child_count = syntax->as.sequence.child_count;
			node.data_type = pigen_data_type_concatenation(analyzer->model,
				types, syntax->as.sequence.child_count);
			node.shape = pigen_semantic_scalar_shape(analyzer->model);
			node.constant = all_constant ?
				pigen_const_expr_intern_concatenation(analyzer->model, constants,
					syntax->as.sequence.child_count, node.data_type) :
				INVALID_ID(pigen_const_expr_id);
			free(children);
			free(types);
			free(constants);
			if (node.as.sequence.first_child == SIZE_MAX ||
				node.data_type.index == PIGEN_INVALID_ID ||
				(all_constant && node.constant.index == PIGEN_INVALID_ID))
				return fail(analyzer, syntax->location, "invalid concatenation");
			return append_node(analyzer, node);
		}
		default:
			return fail(analyzer, syntax->location,
				"unsupported expression form");
	}
}

int pigen_analyze_expression(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model, pigen_scope_id scope,
	pigen_syntax_expr_id expression, int constant_only,
	pigen_analyzed_expr_arena *arena, pigen_analyzed_expr_id *result,
	pigen_semantic_error *error)
{
	expression_analyzer analyzer;

	if (result) *result = INVALID_ID(pigen_analyzed_expr_id);
	if (!syntax || !syntax->expanded || !model || !arena || !result ||
		!pigen_scope_get(model, scope))
		return 0;
	analyzer.syntax = syntax;
	analyzer.model = model;
	analyzer.scope = scope;
	analyzer.constant_only = constant_only;
	analyzer.arena = arena;
	analyzer.error = error;
	*result = analyze(&analyzer, expression);
	return result->index != PIGEN_INVALID_ID &&
		(!constant_only || arena->nodes[result->index].constant.index !=
			PIGEN_INVALID_ID);
}

const pigen_analyzed_expr *pigen_analyzed_expr_get(
	const pigen_analyzed_expr_arena *arena, pigen_analyzed_expr_id expression)
{
	if (!arena || expression.index == PIGEN_INVALID_ID ||
		expression.index >= arena->node_count)
		return NULL;
	return &arena->nodes[expression.index];
}

void pigen_free_analyzed_expr_arena(pigen_analyzed_expr_arena *arena)
{
	if (!arena) return;
	free(arena->nodes);
	free(arena->children);
	free(arena->literal_states);
	memset(arena, 0, sizeof(*arena));
}
