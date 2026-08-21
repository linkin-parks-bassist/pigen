/* Shared typed resolution for SystemVerilog expressions. */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "pigen/expression_resolve.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

typedef struct {
	const pigen_syntax_tree *syntax;
	pigen_semantic_model *model;
	pigen_type_id integer_type;
	pigen_type_id boolean_type;
	int constant_only;
} expression_resolver;

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

static pigen_type_id sized_literal_type(expression_resolver *resolver,
	size_t width, int is_signed)
{
	pigen_packed_dimension dimension;
	pigen_const_expr_id left;
	pigen_const_expr_id right;

	if (width == 1)
		return pigen_type_intern(resolver->model, PIGEN_TYPE_LOGIC,
			is_signed ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED,
			INVALID_ID(pigen_symbol_id), NULL, 0);
	left = pigen_const_expr_intern_integer(resolver->model,
		(uint64_t)(width - 1), resolver->integer_type);
	right = pigen_const_expr_intern_integer(resolver->model, 0,
		resolver->integer_type);
	if (left.index == PIGEN_INVALID_ID || right.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_type_id);
	dimension = (pigen_packed_dimension){left, right};
	return pigen_type_intern(resolver->model, PIGEN_TYPE_LOGIC,
		is_signed ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED,
		INVALID_ID(pigen_symbol_id), &dimension, 1);
}

static int based_literal(expression_resolver *resolver,
	const pigen_syntax_expr *expression, pigen_bit_state **states,
	size_t *state_count, pigen_type_id *type)
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
	*type = INVALID_ID(pigen_type_id);
	if (expression->location.extent.after.index !=
		expression->location.extent.first.index + 1)
		return 0;
	token = pigen_expanded_token_get(resolver->syntax->expanded,
		expression->as.atom.token);
	text = pigen_expanded_token_text(resolver->syntax->expanded, token,
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
	if (at == length) return 0;
	if (width > SIZE_MAX / sizeof(**states)) return 0;
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
	*type = sized_literal_type(resolver, width, is_signed);
	if (type->index == PIGEN_INVALID_ID)
	{
		free(*states);
		*states = NULL;
		return 0;
	}
	*state_count = width;
	return 1;
}

static pigen_source_span token_spelling(const expression_resolver *resolver,
	pigen_token_id token)
{
	const pigen_expanded_token *known = pigen_expanded_token_get(
		resolver->syntax->expanded, token);
	return known ? pigen_origin_spelling_span(resolver->syntax->expanded,
		known->origin) :
		(pigen_source_span){INVALID_ID(pigen_source_id), 0, 0};
}

static int literal_value(const expression_resolver *resolver,
	const pigen_syntax_expr *expression, uint64_t *value)
{
	const pigen_expanded_token *token;
	const char *text;
	size_t length;
	size_t i;
	int any = 0;

	token = pigen_expanded_token_get(resolver->syntax->expanded,
		expression->as.atom.token);
	text = pigen_expanded_token_text(resolver->syntax->expanded, token,
		&length);
	if (!text || !length || expression->location.extent.after.index !=
		expression->location.extent.first.index + 1 ||
		memchr(text, '\'', length))
		return 0;
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
		case PIGEN_SYNTAX_OP_POSITIVE:
			*semantic = PIGEN_UNARY_POSITIVE;
			break;
		case PIGEN_SYNTAX_OP_NEGATE:
			*semantic = PIGEN_UNARY_NEGATE;
			break;
		case PIGEN_SYNTAX_OP_LOGICAL_NOT:
			*semantic = PIGEN_UNARY_LOGICAL_NOT;
			break;
		case PIGEN_SYNTAX_OP_BITWISE_NOT:
			*semantic = PIGEN_UNARY_BITWISE_NOT;
			break;
		case PIGEN_SYNTAX_OP_REDUCTION_AND:
			*semantic = PIGEN_UNARY_REDUCTION_AND;
			break;
		case PIGEN_SYNTAX_OP_REDUCTION_NAND:
			*semantic = PIGEN_UNARY_REDUCTION_NAND;
			break;
		case PIGEN_SYNTAX_OP_REDUCTION_OR:
			*semantic = PIGEN_UNARY_REDUCTION_OR;
			break;
		case PIGEN_SYNTAX_OP_REDUCTION_NOR:
			*semantic = PIGEN_UNARY_REDUCTION_NOR;
			break;
		case PIGEN_SYNTAX_OP_REDUCTION_XOR:
			*semantic = PIGEN_UNARY_REDUCTION_XOR;
			break;
		case PIGEN_SYNTAX_OP_REDUCTION_XNOR:
			*semantic = PIGEN_UNARY_REDUCTION_XNOR;
			break;
		default:
			return 0;
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
		MAP(PIGEN_SYNTAX_OP_ARITH_SHIFT_LEFT,
			PIGEN_BINARY_ARITH_SHIFT_LEFT);
		MAP(PIGEN_SYNTAX_OP_ARITH_SHIFT_RIGHT,
			PIGEN_BINARY_ARITH_SHIFT_RIGHT);
		MAP(PIGEN_SYNTAX_OP_LESS, PIGEN_BINARY_LESS);
		MAP(PIGEN_SYNTAX_OP_LESS_EQUAL, PIGEN_BINARY_LESS_EQUAL);
		MAP(PIGEN_SYNTAX_OP_GREATER, PIGEN_BINARY_GREATER);
		MAP(PIGEN_SYNTAX_OP_GREATER_EQUAL, PIGEN_BINARY_GREATER_EQUAL);
		MAP(PIGEN_SYNTAX_OP_EQUAL, PIGEN_BINARY_EQUAL);
		MAP(PIGEN_SYNTAX_OP_NOT_EQUAL, PIGEN_BINARY_NOT_EQUAL);
		MAP(PIGEN_SYNTAX_OP_CASE_EQUAL, PIGEN_BINARY_CASE_EQUAL);
		MAP(PIGEN_SYNTAX_OP_CASE_NOT_EQUAL,
			PIGEN_BINARY_CASE_NOT_EQUAL);
		MAP(PIGEN_SYNTAX_OP_WILDCARD_EQUAL,
			PIGEN_BINARY_WILDCARD_EQUAL);
		MAP(PIGEN_SYNTAX_OP_WILDCARD_NOT_EQUAL,
			PIGEN_BINARY_WILDCARD_NOT_EQUAL);
		MAP(PIGEN_SYNTAX_OP_BITWISE_AND, PIGEN_BINARY_BITWISE_AND);
		MAP(PIGEN_SYNTAX_OP_BITWISE_XOR, PIGEN_BINARY_BITWISE_XOR);
		MAP(PIGEN_SYNTAX_OP_BITWISE_XNOR, PIGEN_BINARY_BITWISE_XNOR);
		MAP(PIGEN_SYNTAX_OP_BITWISE_OR, PIGEN_BINARY_BITWISE_OR);
		MAP(PIGEN_SYNTAX_OP_LOGICAL_AND, PIGEN_BINARY_LOGICAL_AND);
		MAP(PIGEN_SYNTAX_OP_LOGICAL_OR, PIGEN_BINARY_LOGICAL_OR);
		default:
			return 0;
	}
#undef MAP
	return 1;
}

static int unary_boolean_result(pigen_syntax_operator operator)
{
	return operator == PIGEN_SYNTAX_OP_LOGICAL_NOT ||
		(operator >= PIGEN_SYNTAX_OP_REDUCTION_AND &&
		operator <= PIGEN_SYNTAX_OP_REDUCTION_XNOR);
}

static int binary_boolean_result(pigen_syntax_operator operator)
{
	return (operator >= PIGEN_SYNTAX_OP_LESS &&
		operator <= PIGEN_SYNTAX_OP_WILDCARD_NOT_EQUAL) ||
		operator == PIGEN_SYNTAX_OP_LOGICAL_AND ||
		operator == PIGEN_SYNTAX_OP_LOGICAL_OR;
}

static pigen_select_kind select_kind(pigen_syntax_select_kind kind)
{
	switch (kind)
	{
		case PIGEN_SELECT_RANGE:
			return PIGEN_SEMANTIC_SELECT_RANGE;
		case PIGEN_SELECT_INDEXED_UP:
			return PIGEN_SEMANTIC_SELECT_INDEXED_UP;
		case PIGEN_SELECT_INDEXED_DOWN:
			return PIGEN_SEMANTIC_SELECT_INDEXED_DOWN;
	}
	pigen_fail("invalid syntax select kind");
	return PIGEN_SEMANTIC_SELECT_RANGE;
}

static int supported_integral_type(const expression_resolver *resolver,
	pigen_type_id type)
{
	const pigen_semantic_type *known = pigen_type_get(resolver->model, type);
	return known && (known->kind == PIGEN_TYPE_INTEGER ||
		known->kind == PIGEN_TYPE_LOGIC || known->kind == PIGEN_TYPE_BIT);
}

static pigen_expr_id resolve_expression(expression_resolver *resolver,
	pigen_scope_id scope, pigen_syntax_expr_id syntax_id)
{
	const pigen_syntax_expr *syntax = pigen_syntax_expr_get(
		&resolver->syntax->expressions, syntax_id);
	pigen_expr_id operand;
	pigen_expr_id left;
	pigen_expr_id right;
	pigen_expr_id condition;
	pigen_expr_id when_true;
	pigen_expr_id when_false;
	uint64_t value;

	if (!syntax) return INVALID_ID(pigen_expr_id);
	switch (syntax->kind)
	{
		case PIGEN_SYNTAX_EXPR_LITERAL:
			if (literal_value(resolver, syntax, &value))
				return pigen_expr_add_integer(resolver->model, value,
					resolver->integer_type, syntax->location.source_span);
			else
			{
				pigen_bit_state *states;
				size_t state_count;
				pigen_type_id type;
				pigen_expr_id result;

				if (!based_literal(resolver, syntax, &states, &state_count,
					&type))
					return INVALID_ID(pigen_expr_id);
				result = pigen_expr_add_bits(resolver->model, states,
					state_count, type, syntax->location.source_span);
				free(states);
				return result;
			}
		case PIGEN_SYNTAX_EXPR_NAME:
		{
			pigen_symbol_id symbol = pigen_symbol_lookup(resolver->model,
				scope, token_spelling(resolver, syntax->as.atom.token));
			const pigen_symbol *known = pigen_symbol_get(resolver->model,
				symbol);
			if (!known || (resolver->constant_only &&
				known->kind != PIGEN_SYMBOL_PARAMETER) ||
				(!resolver->constant_only &&
				known->kind != PIGEN_SYMBOL_PARAMETER &&
				known->kind != PIGEN_SYMBOL_SIGNAL))
				return INVALID_ID(pigen_expr_id);
			return pigen_expr_add_symbol(resolver->model, symbol, known->type,
				syntax->location.source_span);
		}
		case PIGEN_SYNTAX_EXPR_GROUP:
			operand = resolve_expression(resolver, scope,
				syntax->as.group.operand);
			return pigen_expr_add_group(resolver->model, operand,
				syntax->location.source_span);
		case PIGEN_SYNTAX_EXPR_UNARY:
		{
			pigen_unary_operator operator;
			const pigen_semantic_expr *known;
			pigen_type_id result_type;

			if (!unary_operator(syntax->as.unary.operator, &operator))
				return INVALID_ID(pigen_expr_id);
			operand = resolve_expression(resolver, scope,
				syntax->as.unary.operand);
			known = pigen_expr_get(resolver->model, operand);
			if (!known || !supported_integral_type(resolver, known->type))
				return INVALID_ID(pigen_expr_id);
			result_type = unary_boolean_result(syntax->as.unary.operator) ?
				resolver->boolean_type : known->type;
			return pigen_expr_add_unary(resolver->model, operator, operand,
				result_type, syntax->location.source_span);
		}
		case PIGEN_SYNTAX_EXPR_BINARY:
		{
			pigen_binary_operator operator;
			const pigen_semantic_expr *left_known;
			const pigen_semantic_expr *right_known;
			pigen_type_id result_type;

			if (!binary_operator(syntax->as.binary.operator, &operator))
				return INVALID_ID(pigen_expr_id);
			left = resolve_expression(resolver, scope,
				syntax->as.binary.left);
			right = resolve_expression(resolver, scope,
				syntax->as.binary.right);
			left_known = pigen_expr_get(resolver->model, left);
			right_known = pigen_expr_get(resolver->model, right);
			if (!left_known || !right_known ||
				!supported_integral_type(resolver, left_known->type) ||
				!supported_integral_type(resolver, right_known->type))
				return INVALID_ID(pigen_expr_id);
			if (!binary_boolean_result(syntax->as.binary.operator) &&
				left_known->type.index != right_known->type.index)
				return INVALID_ID(pigen_expr_id);
			result_type = binary_boolean_result(syntax->as.binary.operator) ?
				resolver->boolean_type : left_known->type;
			return pigen_expr_add_binary(resolver->model, operator, left,
				right, result_type, syntax->location.source_span);
		}
		case PIGEN_SYNTAX_EXPR_CONDITIONAL:
		{
			const pigen_semantic_expr *condition_known;
			const pigen_semantic_expr *true_known;
			const pigen_semantic_expr *false_known;

			condition = resolve_expression(resolver, scope,
				syntax->as.conditional.condition);
			when_true = resolve_expression(resolver, scope,
				syntax->as.conditional.when_true);
			when_false = resolve_expression(resolver, scope,
				syntax->as.conditional.when_false);
			condition_known = pigen_expr_get(resolver->model, condition);
			true_known = pigen_expr_get(resolver->model, when_true);
			false_known = pigen_expr_get(resolver->model, when_false);
			if (!condition_known || !true_known || !false_known ||
				!supported_integral_type(resolver, condition_known->type) ||
				true_known->type.index != false_known->type.index)
				return INVALID_ID(pigen_expr_id);
			return pigen_expr_add_conditional(resolver->model, condition,
				when_true, when_false, true_known->type,
				syntax->location.source_span);
		}
		case PIGEN_SYNTAX_EXPR_INDEX:
			left = resolve_expression(resolver, scope, syntax->as.index.base);
			right = resolve_expression(resolver, scope, syntax->as.index.index);
			if (!pigen_expr_get(resolver->model, left) ||
				!pigen_expr_get(resolver->model, right))
				return INVALID_ID(pigen_expr_id);
			return pigen_expr_add_index(resolver->model, left, right,
				syntax->location.source_span);
		case PIGEN_SYNTAX_EXPR_SELECT:
		{
			pigen_expr_id base = resolve_expression(resolver, scope,
				syntax->as.select.base);
			left = resolve_expression(resolver, scope,
				syntax->as.select.left);
			right = resolve_expression(resolver, scope,
				syntax->as.select.right);
			if (!pigen_expr_get(resolver->model, base) ||
				!pigen_expr_get(resolver->model, left) ||
				!pigen_expr_get(resolver->model, right))
				return INVALID_ID(pigen_expr_id);
			return pigen_expr_add_select(resolver->model, base, left, right,
				select_kind(syntax->as.select.kind),
				syntax->location.source_span);
		}
		case PIGEN_SYNTAX_EXPR_CONCATENATION:
		{
			const pigen_syntax_expr_id *syntax_children =
				pigen_syntax_expr_children(&resolver->syntax->expressions,
					syntax->as.sequence.first_child,
					syntax->as.sequence.child_count);
			pigen_expr_id *children;
			pigen_expr_id result;
			size_t i;

			if (!syntax_children || !syntax->as.sequence.child_count)
				return INVALID_ID(pigen_expr_id);
			children = pigen_resize(NULL,
				syntax->as.sequence.child_count * sizeof(*children));
			for (i = 0; i < syntax->as.sequence.child_count; i++)
			{
				children[i] = resolve_expression(resolver, scope,
					syntax_children[i]);
				if (children[i].index == PIGEN_INVALID_ID)
				{
					free(children);
					return INVALID_ID(pigen_expr_id);
				}
			}
			result = pigen_expr_add_concatenation(resolver->model, children,
				syntax->as.sequence.child_count,
				syntax->location.source_span);
			free(children);
			return result;
		}
		default:
			return INVALID_ID(pigen_expr_id);
	}
}

static pigen_expr_id resolve_with_policy(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression, int constant_only)
{
	expression_resolver resolver;

	if (!syntax || !syntax->expanded || !model ||
		!pigen_scope_get(model, scope))
		return INVALID_ID(pigen_expr_id);
	resolver.syntax = syntax;
	resolver.model = model;
	resolver.constant_only = constant_only;
	resolver.integer_type = pigen_semantic_integer_type(model);
	resolver.boolean_type = pigen_semantic_boolean_result_type(model);
	if (resolver.integer_type.index == PIGEN_INVALID_ID ||
		resolver.boolean_type.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_expr_id);
	return resolve_expression(&resolver, scope, expression);
}

pigen_expr_id pigen_resolve_expression(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression)
{
	return resolve_with_policy(syntax, model, scope, expression, 0);
}

pigen_expr_id pigen_resolve_constant_expression(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression)
{
	pigen_expr_id result = resolve_with_policy(syntax, model, scope,
		expression, 1);
	return pigen_expr_constant(model, result).index == PIGEN_INVALID_ID ?
		INVALID_ID(pigen_expr_id) : result;
}
