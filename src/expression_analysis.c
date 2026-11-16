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
	pigen_literal_domain literal_domain;
	const pigen_resolve_policy *policy;
	pigen_analyzed_expr_arena *arena;
	pigen_semantic_error *error;
} expression_analyzer;

static pigen_const_expr_id normalize_analyzed_constant(expression_analyzer *analyzer,pigen_analyzed_expr_id id);

int pigen_expression_contains_unbased_fill(const pigen_syntax_tree *syntax,pigen_syntax_expr_id expression)
{
	if(!syntax || !syntax->expanded) return 0;
	const pigen_syntax_expr *node=pigen_syntax_expr_get(&syntax->expressions,expression);
	if(!node) return 0;
	if(node->kind==PIGEN_SYNTAX_EXPR_LITERAL)
	{
		const pigen_expanded_token *token=pigen_expanded_token_get(syntax->expanded,node->as.atom.token);
		size_t length=0;const char *text=pigen_expanded_token_text(syntax->expanded,token,&length);
		return text && length==2 && text[0]=='\'' && strchr("01xXzZ",text[1])!=NULL;
	}
	if(node->kind==PIGEN_SYNTAX_EXPR_GROUP) return pigen_expression_contains_unbased_fill(syntax,node->as.group.operand);
	if(node->kind==PIGEN_SYNTAX_EXPR_UNARY) return pigen_expression_contains_unbased_fill(syntax,node->as.unary.operand);
	if(node->kind==PIGEN_SYNTAX_EXPR_BINARY)
		return pigen_expression_contains_unbased_fill(syntax,node->as.binary.left) || pigen_expression_contains_unbased_fill(syntax,node->as.binary.right);
	if(node->kind==PIGEN_SYNTAX_EXPR_CAST) return pigen_expression_contains_unbased_fill(syntax,node->as.cast.value);
	if(node->kind==PIGEN_SYNTAX_EXPR_INDEX)
		return pigen_expression_contains_unbased_fill(syntax,node->as.index.base) || pigen_expression_contains_unbased_fill(syntax,node->as.index.index);
	if(node->kind==PIGEN_SYNTAX_EXPR_SELECT)
		return pigen_expression_contains_unbased_fill(syntax,node->as.select.base) || pigen_expression_contains_unbased_fill(syntax,node->as.select.left) || pigen_expression_contains_unbased_fill(syntax,node->as.select.right);
	if(node->kind==PIGEN_SYNTAX_EXPR_CALL)
	{
		const pigen_syntax_expr_id *arguments=pigen_syntax_expr_children(&syntax->expressions,node->as.call.first_argument,node->as.call.argument_count);
		for(size_t i=0;arguments && i<node->as.call.argument_count;i++) if(pigen_expression_contains_unbased_fill(syntax,arguments[i])) return 1;
		return 0;
	}
	if(node->kind==PIGEN_SYNTAX_EXPR_CONCATENATION || node->kind==PIGEN_SYNTAX_EXPR_REPLICATION)
	{
		size_t first=node->kind==PIGEN_SYNTAX_EXPR_CONCATENATION ? node->as.sequence.first_child : node->as.replication.first_child;
		size_t count=node->kind==PIGEN_SYNTAX_EXPR_CONCATENATION ? node->as.sequence.child_count : node->as.replication.child_count;
		const pigen_syntax_expr_id *children=pigen_syntax_expr_children(&syntax->expressions,first,count);
		for(size_t i=0;children && i<count;i++) if(pigen_expression_contains_unbased_fill(syntax,children[i])) return 1;
		return 0;
	}
	return node->kind==PIGEN_SYNTAX_EXPR_CONDITIONAL &&
		(pigen_expression_contains_unbased_fill(syntax,node->as.conditional.condition) || pigen_expression_contains_unbased_fill(syntax,node->as.conditional.when_true) ||
		 pigen_expression_contains_unbased_fill(syntax,node->as.conditional.when_false));
}

pigen_literal_domain pigen_case_cast_literal_domain(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,pigen_scope_id scope,const pigen_syntax_expr *expression)
{
	if (!expression || expression->kind!=PIGEN_SYNTAX_EXPR_CAST || expression->as.cast.is_size) return PIGEN_LITERAL_DOMAIN_CASE;
	pigen_semantic_error ignored={0};
	pigen_data_type_id type=pigen_resolve_type(syntax,model,scope,expression->as.cast.type,&ignored);
	pigen_numerical_interpretation family=pigen_data_type_numerical_interpretation(model,type);
	return family==PIGEN_NUMERICAL_SIGNED_INTEGER || family==PIGEN_NUMERICAL_UNSIGNED_INTEGER ? PIGEN_LITERAL_DOMAIN_PIGEN : PIGEN_LITERAL_DOMAIN_CASE;
}

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
	if(analyzer->constant_only && analyzer->literal_domain==PIGEN_LITERAL_DOMAIN_CASE && node.constant.index!=PIGEN_INVALID_ID)
		analyzer->arena->nodes[result.index].constant=normalize_analyzed_constant(analyzer,result);
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

static int append_constraint(expression_analyzer *analyzer,
	pigen_width_constraint constraint)
{
	if (analyzer->arena->constraint_count ==
		analyzer->arena->constraint_capacity)
	{
		analyzer->arena->constraint_capacity =
			analyzer->arena->constraint_capacity ?
			analyzer->arena->constraint_capacity * 2 : 8;
		analyzer->arena->constraints = pigen_resize(
			analyzer->arena->constraints,
			analyzer->arena->constraint_capacity *
				sizeof(*analyzer->arena->constraints));
	}
	analyzer->arena->constraints[analyzer->arena->constraint_count++] =
		constraint;
	return 1;
}

static int generated_width_operator(pigen_binary_operator operator)
{
	return operator == PIGEN_BINARY_POWER ||
		operator == PIGEN_BINARY_SHIFT_LEFT ||
		operator == PIGEN_BINARY_ARITH_SHIFT_LEFT;
}

static int check_generated_width(expression_analyzer *analyzer,
	pigen_data_type_id data_type, pigen_binary_operator operator,
	pigen_syntax_location location)
{
	pigen_const_expr_id width;
	uint64_t value;

	if (!analyzer->policy || !generated_width_operator(operator)) return 1;
	width = pigen_data_type_packed_width(analyzer->model, data_type);
	if (width.index == PIGEN_INVALID_ID)
	{
		fail(analyzer, location, "generated result has no bounded width");
		return 0;
	}
	if (pigen_const_expr_evaluate_u64(analyzer->model, width, &value))
	{
		if (value <= analyzer->policy->maximum_generated_bits) return 1;
		fail(analyzer, location, "generated result exceeds width limit");
		return 0;
	}
	if (!pigen_const_expr_is_symbolic(analyzer->model, width))
	{
		fail(analyzer, location, "generated result width overflows");
		return 0;
	}
	return append_constraint(analyzer, (pigen_width_constraint){width,
		analyzer->policy->maximum_generated_bits, location.source_span});
}

static int reject_excessive_exact_amount(expression_analyzer *analyzer,
	pigen_data_type_id base, pigen_data_type_id amount,
	pigen_binary_operator operator, pigen_syntax_location location)
{
	pigen_integer_id value;
	pigen_integer_id limit;
	uint64_t base_width;

	if (!analyzer->policy || !generated_width_operator(operator) ||
		pigen_data_type_numerical_interpretation(analyzer->model, amount) !=
			PIGEN_NUMERICAL_EXACT_INTEGER)
		return 0;
	value = pigen_data_type_exact_value(analyzer->model, amount);
	if (value.index == PIGEN_INVALID_ID ||
		pigen_integer_is_negative(analyzer->model, value))
		return 0;
	limit = pigen_integer_intern_u64(analyzer->model,
		analyzer->policy->maximum_generated_bits);
	if (limit.index == PIGEN_INVALID_ID ||
		pigen_integer_compare(analyzer->model, value, limit) <= 0)
		return 0;
	if (operator == PIGEN_BINARY_POWER &&
		(!pigen_const_expr_evaluate_u64(analyzer->model,
			pigen_data_type_packed_width(analyzer->model, base), &base_width) ||
		base_width <= 1))
		return 0;
	fail(analyzer, location, "generated result exceeds width limit");
	return 1;
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
	else if (character == 'z' || character == 'Z' || character == '?')
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
	if (analyzer->literal_domain==PIGEN_LITERAL_DOMAIN_CASE && length==2 && text[0]=='\'')
	{
		unsigned ignored=0;pigen_bit_state state;
		if (!based_digit(text[1],2,&ignored,&state)) return 0;
		if (state!=PIGEN_BIT_X && state!=PIGEN_BIT_Z) state=ignored ? PIGEN_BIT_ONE : PIGEN_BIT_ZERO;
		*states=pigen_resize(NULL,sizeof(**states));(*states)[0]=state;*state_count=1;
		*type=pigen_data_type_sized_logic(analyzer->model,1,PIGEN_SIGN_UNSIGNED);return type->index!=PIGEN_INVALID_ID;
	}
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

/* Named generate paths select existing lexical scopes. Indices belong to
 * scope selection, never to payload coordinates or printed RTL names. */
static pigen_scope_id generate_scope_reference(expression_analyzer *analyzer,
	const pigen_syntax_expr *syntax, size_t depth)
{
	pigen_scope_id invalid={PIGEN_INVALID_ID};
	if(!syntax || depth>128) return invalid;
	if(syntax->kind==PIGEN_SYNTAX_EXPR_GROUP)
		return generate_scope_reference(analyzer,pigen_syntax_expr_get(&analyzer->syntax->expressions,syntax->as.group.operand),depth+1);
	int indexed=syntax->kind==PIGEN_SYNTAX_EXPR_INDEX;
	int64_t index=0;
	const pigen_syntax_expr *name=syntax;
	if(indexed)
	{
		pigen_analyzed_expr_arena arena={0};pigen_analyzed_expr_id result;
		int ok=pigen_analyze_expression(analyzer->syntax,analyzer->model,analyzer->scope,syntax->as.index.index,
			1,PIGEN_LITERAL_DOMAIN_SYSTEMVERILOG,analyzer->policy,&arena,&result,NULL);
		if(ok) ok=pigen_const_expr_evaluate_i64(analyzer->model,arena.nodes[result.index].constant,&index);
		pigen_free_analyzed_expr_arena(&arena);
		if(!ok) return invalid;
		name=pigen_syntax_expr_get(&analyzer->syntax->expressions,syntax->as.index.base);
	}
	pigen_scope_id parent=analyzer->scope;pigen_token_id token;
	int lexical=0;
	if(name && name->kind==PIGEN_SYNTAX_EXPR_NAME) {token=name->as.atom.token;lexical=1;}
	else if(name && name->kind==PIGEN_SYNTAX_EXPR_MEMBER && !name->as.member.scoped)
	{
		parent=generate_scope_reference(analyzer,pigen_syntax_expr_get(&analyzer->syntax->expressions,name->as.member.base),depth+1);
		token=name->as.member.member;
	}
	else return invalid;
	pigen_source_span spelling=token_spelling(analyzer,token);size_t length=0;
	const char *text=pigen_source_span_text(analyzer->model->sources,spelling,&length);
	while(pigen_scope_get(analyzer->model,parent))
	{
		pigen_symbol_id shadow=pigen_symbol_lookup_local(analyzer->model,parent,spelling);
		const pigen_symbol *symbol=pigen_symbol_get(analyzer->model,shadow);
		if (symbol && symbol->kind==PIGEN_SYMBOL_PROCEDURAL_SCOPE && !indexed)
			return symbol->object.scope;
		if(symbol) return invalid;
		int found=0;
		for(size_t i=0;i<analyzer->model->generate_scope_count;i++)
		{
			const pigen_semantic_generate_scope *scope=&analyzer->model->generate_scopes[i];
			if(analyzer->model->scopes[scope->scope.index].parent.index!=parent.index) continue;
			size_t n=0;const char *candidate=pigen_source_span_text(analyzer->model->sources,scope->name,&n);
			if(!text || !candidate || length!=n || memcmp(text,candidate,n)) continue;
			found=1;
			if(indexed==scope->is_iteration && (!indexed || index==scope->index)) return scope->scope;
		}
		if(found || !lexical) return invalid;
		parent=analyzer->model->scopes[parent.index].parent;
	}
	return invalid;
}

static pigen_symbol_id scoped_symbol_reference(expression_analyzer *analyzer,
	const pigen_syntax_expr *syntax)
{
	pigen_symbol_id invalid={PIGEN_INVALID_ID};
	if(!syntax) return invalid;
	if(syntax->kind==PIGEN_SYNTAX_EXPR_GROUP)
		return scoped_symbol_reference(analyzer,pigen_syntax_expr_get(&analyzer->syntax->expressions,syntax->as.group.operand));
	if(syntax->kind==PIGEN_SYNTAX_EXPR_NAME)
		return pigen_symbol_resolve_at(analyzer->model,analyzer->scope,token_spelling(analyzer,syntax->as.atom.token),syntax->location.extent.first.index);
	if(syntax->kind!=PIGEN_SYNTAX_EXPR_MEMBER) return invalid;
	if(syntax->as.member.scoped)
	{
		const pigen_syntax_expr *base=pigen_syntax_expr_get(&analyzer->syntax->expressions,syntax->as.member.base);
		return base && base->kind==PIGEN_SYNTAX_EXPR_NAME ? pigen_symbol_lookup_package_member(analyzer->model,
		 analyzer->scope,token_spelling(analyzer,base->as.atom.token),token_spelling(analyzer,syntax->as.member.member)) : invalid;
	}
	pigen_scope_id scope=generate_scope_reference(analyzer,
		pigen_syntax_expr_get(&analyzer->syntax->expressions,syntax->as.member.base),0);
	if(scope.index==PIGEN_INVALID_ID) return invalid;
	return pigen_symbol_lookup_local(analyzer->model,scope,token_spelling(analyzer,syntax->as.member.member));
}

/* Callable namespace inspection is pure: builtin names must not activate an
 * unrelated wildcard candidate merely because a call is being classified. */
static pigen_symbol_id retained_subroutine_reference(expression_analyzer *analyzer,const pigen_syntax_expr *syntax)
{
 if(!syntax) return INVALID_ID(pigen_symbol_id);
 if(syntax->kind==PIGEN_SYNTAX_EXPR_GROUP)
  return retained_subroutine_reference(analyzer,pigen_syntax_expr_get(&analyzer->syntax->expressions,syntax->as.group.operand));
 if(syntax->kind==PIGEN_SYNTAX_EXPR_NAME)
 {
  size_t length=0;const char *text=token_text(analyzer,syntax,&length);
  return pigen_symbol_lookup_text_at(analyzer->model,analyzer->scope,text,length,syntax->location.extent.first.index);
 }
 if(syntax->kind==PIGEN_SYNTAX_EXPR_MEMBER && syntax->as.member.scoped)
 {
  const pigen_syntax_expr *base=pigen_syntax_expr_get(&analyzer->syntax->expressions,syntax->as.member.base);
  if(base && base->kind==PIGEN_SYNTAX_EXPR_NAME)
  {
   size_t length=0;const char *text=token_text(analyzer,base,&length);
   const pigen_symbol *package=pigen_symbol_get(analyzer->model,pigen_symbol_lookup_text_at(analyzer->model,
    analyzer->scope,text,length,base->location.extent.first.index));
   if(package && package->kind==PIGEN_SYMBOL_PACKAGE)
    return pigen_symbol_lookup_local(analyzer->model,package->object.scope,token_spelling(analyzer,syntax->as.member.member));
  }
 }
 return INVALID_ID(pigen_symbol_id);
}

pigen_symbol_id pigen_lookup_callee_symbol(const pigen_syntax_tree *syntax,
 pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression)
{
 expression_analyzer analyzer={0};
 analyzer.syntax=syntax;analyzer.model=model;analyzer.scope=scope;
 return retained_subroutine_reference(&analyzer,pigen_syntax_expr_get(&syntax->expressions,expression));
}

/* A child array uses its declaration's collection identity; scalar instance
 * views are selected only after the lane coordinate has been resolved. */
static pigen_protocol_array_id protocol_array_reference(expression_analyzer *analyzer,
	const pigen_syntax_expr *syntax, pigen_instance_id *instance_id)
{
	*instance_id = INVALID_ID(pigen_instance_id);
	if (!syntax || analyzer->constant_only) return INVALID_ID(pigen_protocol_array_id);
	if (syntax->kind == PIGEN_SYNTAX_EXPR_GROUP)
		return protocol_array_reference(analyzer, pigen_syntax_expr_get(&analyzer->syntax->expressions,
			syntax->as.group.operand), instance_id);
	pigen_protocol_array_id direct=pigen_symbol_protocol_array(analyzer->model,scoped_symbol_reference(analyzer,syntax));
	if(direct.index!=PIGEN_INVALID_ID) return direct;
	if (syntax->kind != PIGEN_SYNTAX_EXPR_MEMBER || syntax->as.member.scoped)
		return INVALID_ID(pigen_protocol_array_id);
	const pigen_syntax_expr *base = pigen_syntax_expr_get(&analyzer->syntax->expressions, syntax->as.member.base);
	const pigen_symbol *symbol=pigen_symbol_get(analyzer->model,scoped_symbol_reference(analyzer,base));
	if (!symbol || symbol->kind != PIGEN_SYMBOL_INSTANCE) return INVALID_ID(pigen_protocol_array_id);
	const pigen_semantic_instance *instance = pigen_instance_get(analyzer->model, symbol->object.instance);
	const pigen_semantic_module *child = instance ? pigen_module_get(analyzer->model, instance->child_module) : NULL;
	if (!child) return INVALID_ID(pigen_protocol_array_id);
	*instance_id = symbol->object.instance;
	return pigen_symbol_protocol_array(analyzer->model, pigen_symbol_lookup(analyzer->model,
		child->scope, token_spelling(analyzer, syntax->as.member.member)));
}

pigen_protocol_array_id pigen_resolve_protocol_array_reference(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression, pigen_instance_id *instance)
{
	if (!syntax || !model || !instance) return INVALID_ID(pigen_protocol_array_id);
	expression_analyzer analyzer = {.syntax = syntax, .model = model, .scope = scope};
	return protocol_array_reference(&analyzer,
		pigen_syntax_expr_get(&syntax->expressions, expression), instance);
}

void pigen_free_protocol_array_view(pigen_protocol_array_view *view)
{
	if(!view) return;
	free(view->dimensions);free(view->elements);
	*view=(pigen_protocol_array_view){.array={PIGEN_INVALID_ID}};
}

static pigen_const_expr_id constant_context(pigen_semantic_model *model,
	const pigen_analyzed_expr_arena *arena,pigen_analyzed_expr_id id,size_t width,pigen_signedness sign,size_t depth);

static pigen_const_expr_id constant_convert(pigen_semantic_model *model,pigen_const_expr_id value,pigen_data_type_id type)
{
	const pigen_const_expr *known=pigen_const_expr_get(model,value);pigen_conversion conversion;
	if(!known || !pigen_data_type_resolve_explicit_conversion(model,known->data_type,type,&conversion)) return INVALID_ID(pigen_const_expr_id);
	return conversion.kind==PIGEN_CONVERSION_IDENTITY ? value : pigen_const_expr_intern_conversion(model,conversion,value);
}

static pigen_const_expr_id constant_coerce(pigen_semantic_model *model,pigen_const_expr_id value,size_t width,pigen_signedness sign)
{
	const pigen_const_expr *known=pigen_const_expr_get(model,value);uint64_t original;
	if(!known) return INVALID_ID(pigen_const_expr_id);
	/* Width construction can intern constants and relocate their arena. */
	pigen_data_type_id type=known->data_type;
	if(!pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,type),&original) || !original || original>SIZE_MAX) return INVALID_ID(pigen_const_expr_id);
	pigen_state_domain state=pigen_data_type_state_domain(model,type);
	value=constant_convert(model,value,pigen_data_type_sized_integral(model,(size_t)original,sign,state));
	return constant_convert(model,value,pigen_data_type_sized_integral(model,width,sign,state));
}

static pigen_const_expr_id constant_self(pigen_semantic_model *model,const pigen_analyzed_expr_arena *arena,
	pigen_analyzed_expr_id id,size_t depth)
{
	const pigen_analyzed_expr *node=pigen_analyzed_expr_get(arena,id);uint64_t width;
	if(node && pigen_data_type_numerical_interpretation(model,node->data_type)==PIGEN_NUMERICAL_EXACT_INTEGER) return node->constant;
	if(!node || !pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,node->data_type),&width) || !width || width>SIZE_MAX) return INVALID_ID(pigen_const_expr_id);
	return constant_context(model,arena,id,(size_t)width,
		pigen_data_type_signedness(model,node->data_type)==PIGEN_SIGN_SIGNED ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED,depth);
}

/* Constant-only counterpart of the native context walk. It interns constant
 * recipes directly and never publishes semantic expressions or lvalues. */
static pigen_const_expr_id constant_context(pigen_semantic_model *model,
	const pigen_analyzed_expr_arena *arena,pigen_analyzed_expr_id id,size_t width,pigen_signedness sign,size_t depth)
{
	const pigen_analyzed_expr *found=pigen_analyzed_expr_get(arena,id);
	if(!found || depth>128 || !width || width>SIZE_MAX/sizeof(pigen_bit_state)) return INVALID_ID(pigen_const_expr_id);
	pigen_analyzed_expr node=*found;
	if(node.form!=PIGEN_ANALYZED_INTRINSIC) return INVALID_ID(pigen_const_expr_id);
 if(node.kind==PIGEN_SYNTAX_EXPR_CALL && node.as.call.kind==PIGEN_EXPR_PEEK)
  return constant_convert(model,node.constant,pigen_data_type_sized_integral(model,width,sign,pigen_data_type_state_domain(model,node.data_type)));
	if(node.unbased_fill)
	{
		pigen_bit_state *states=pigen_resize(NULL,width*sizeof(*states));
		for(size_t i=0;i<width;i++) states[i]=(pigen_bit_state)(node.unbased_fill-1);
		pigen_data_type_id type=pigen_data_type_sized_integral(model,width,sign,PIGEN_DATA_TYPE_STATE_FOUR);
		pigen_const_expr_id value=pigen_const_expr_intern_bits(model,states,width,type);free(states);return value;
	}
	if(node.kind==PIGEN_SYNTAX_EXPR_GROUP) return constant_context(model,arena,node.as.group.operand,width,sign,depth+1);
	if(node.kind!=PIGEN_SYNTAX_EXPR_CAST && pigen_data_type_numerical_interpretation(model,node.data_type)!=PIGEN_NUMERICAL_SYSTEMVERILOG) return node.constant;
	pigen_const_expr_id value=node.constant;
	pigen_data_type_id result=pigen_data_type_sized_integral(model,width,sign,pigen_data_type_state_domain(model,node.data_type));
	if(node.kind==PIGEN_SYNTAX_EXPR_CAST)
	{
		uint64_t target,source;
		const pigen_analyzed_expr *operand=pigen_analyzed_expr_get(arena,node.as.conversion.operand);
		if(!operand || !pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,node.data_type),&target) || !target || target>SIZE_MAX) return INVALID_ID(pigen_const_expr_id);
		if(pigen_data_type_numerical_interpretation(model,operand->data_type)==PIGEN_NUMERICAL_EXACT_INTEGER) value=operand->constant;
		else
		{
			if(!pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,operand->data_type),&source) || !source || source>SIZE_MAX) return INVALID_ID(pigen_const_expr_id);
			value=constant_context(model,arena,node.as.conversion.operand,(size_t)(target>source ? target : source),
				pigen_data_type_signedness(model,operand->data_type)==PIGEN_SIGN_SIGNED ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED,depth+1);
		}
		value=constant_convert(model,value,node.data_type);
	}
	else if(node.kind==PIGEN_SYNTAX_EXPR_UNARY)
	{
		pigen_unary_operator op=node.as.unary.resolution.operation.operator;
		int contextual=op==PIGEN_UNARY_POSITIVE || op==PIGEN_UNARY_NEGATE || op==PIGEN_UNARY_BITWISE_NOT;
		pigen_const_expr_id operand=contextual ? constant_context(model,arena,node.as.unary.operand,width,sign,depth+1) : constant_self(model,arena,node.as.unary.operand,depth+1);
		const pigen_const_expr *child=pigen_const_expr_get(model,operand);
		if(!child) return INVALID_ID(pigen_const_expr_id);
		value=pigen_const_expr_intern_unary(model,(pigen_unary_operation){op,child->data_type,contextual ? result : node.data_type},operand);
	}
	else if(node.kind==PIGEN_SYNTAX_EXPR_BINARY)
	{
		pigen_binary_operator op=node.as.binary.resolution.operation.operator;
		const pigen_analyzed_expr *original_left=pigen_analyzed_expr_get(arena,node.as.binary.left),*original_right=pigen_analyzed_expr_get(arena,node.as.binary.right);
		if(!original_left || !original_right) return INVALID_ID(pigen_const_expr_id);
		if(pigen_data_type_numerical_interpretation(model,original_left->data_type)!=PIGEN_NUMERICAL_SYSTEMVERILOG ||
			pigen_data_type_numerical_interpretation(model,original_right->data_type)!=PIGEN_NUMERICAL_SYSTEMVERILOG)
			return constant_coerce(model,node.constant,width,sign);
		int contextual=op<=PIGEN_BINARY_ARITH_SHIFT_RIGHT || (op>=PIGEN_BINARY_BITWISE_AND && op<=PIGEN_BINARY_BITWISE_OR);
		int count=op==PIGEN_BINARY_POWER || (op>=PIGEN_BINARY_SHIFT_LEFT && op<=PIGEN_BINARY_ARITH_SHIFT_RIGHT);
		int logical=op==PIGEN_BINARY_LOGICAL_AND || op==PIGEN_BINARY_LOGICAL_OR;
		size_t working=width;pigen_signedness common=sign;
		if(!contextual && !logical)
		{
			const pigen_analyzed_expr *left=pigen_analyzed_expr_get(arena,node.as.binary.left),*right=pigen_analyzed_expr_get(arena,node.as.binary.right);
			uint64_t a,b;
			if(!left || !right || !pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,left->data_type),&a) ||
				!pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,right->data_type),&b) || !a || !b || a>SIZE_MAX || b>SIZE_MAX) return INVALID_ID(pigen_const_expr_id);
			working=(size_t)(a>b ? a : b);
			common=pigen_data_type_signedness(model,left->data_type)==PIGEN_SIGN_SIGNED && pigen_data_type_signedness(model,right->data_type)==PIGEN_SIGN_SIGNED ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED;
		}
		pigen_const_expr_id left=logical ? constant_self(model,arena,node.as.binary.left,depth+1) : constant_context(model,arena,node.as.binary.left,working,common,depth+1);
		pigen_const_expr_id right=count || logical ? constant_self(model,arena,node.as.binary.right,depth+1) : constant_context(model,arena,node.as.binary.right,working,common,depth+1);
		const pigen_const_expr *a=pigen_const_expr_get(model,left),*b=pigen_const_expr_get(model,right);
		if(!a || !b) return INVALID_ID(pigen_const_expr_id);
		value=pigen_const_expr_intern_binary(model,(pigen_binary_operation){op,a->data_type,b->data_type,contextual ? result : node.data_type},left,right);
	}
	else if(node.kind==PIGEN_SYNTAX_EXPR_CONDITIONAL)
	{
		pigen_const_expr_id c=constant_self(model,arena,node.as.conditional.condition,depth+1);
		pigen_const_expr_id a=constant_context(model,arena,node.as.conditional.when_true,width,sign,depth+1);
		pigen_const_expr_id b=constant_context(model,arena,node.as.conditional.when_false,width,sign,depth+1);
		const pigen_const_expr *condition=pigen_const_expr_get(model,c),*left=pigen_const_expr_get(model,a),*right=pigen_const_expr_get(model,b);
		if(!condition || !left || !right) return INVALID_ID(pigen_const_expr_id);
		value=pigen_const_expr_intern_conditional(model,(pigen_conditional_operation){condition->data_type,left->data_type,right->data_type,result},c,a,b);
	}
	else if(node.kind==PIGEN_SYNTAX_EXPR_CONCATENATION)
	{
		size_t count=node.as.sequence.child_count;
		pigen_const_expr_id *children=pigen_resize(NULL,count*sizeof(*children));
		for(size_t i=0;i<count;i++)
		{
			pigen_analyzed_expr_id child=arena->children[node.as.sequence.first_child+i];
			children[i]=constant_convert(model,constant_self(model,arena,child,depth+1),arena->nodes[child.index].data_type);
		}
		value=pigen_const_expr_intern_concatenation(model,children,count,node.data_type);free(children);
	}
	return constant_coerce(model,value,width,sign);
}

static pigen_const_expr_id normalize_analyzed_constant(expression_analyzer *analyzer,pigen_analyzed_expr_id id)
{
	pigen_data_type_id type=analyzer->arena->nodes[id.index].data_type;
	return constant_convert(analyzer->model,constant_self(analyzer->model,analyzer->arena,id,0),type);
}

static int array_selection_constant(const pigen_syntax_tree *syntax,pigen_semantic_model *model,
	pigen_scope_id scope,pigen_syntax_expr_id expression,const pigen_resolve_policy *policy,int64_t *value)
{
	pigen_analyzed_expr_arena arena={0};pigen_analyzed_expr_id result;
	int ok=pigen_analyze_expression(syntax,model,scope,expression,1,PIGEN_LITERAL_DOMAIN_CASE,policy,&arena,&result,NULL);
	if(ok) ok=pigen_const_expr_evaluate_i64(model,arena.nodes[result.index].constant,value);
	pigen_free_analyzed_expr_arena(&arena);return ok;
}

int pigen_resolve_protocol_array_view(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression,
	const pigen_resolve_policy *policy,pigen_protocol_array_view *view)
{
	if(!view) return -1;
	*view=(pigen_protocol_array_view){.array={PIGEN_INVALID_ID}};
	if(!syntax || !model || !policy) return -1;
	const pigen_syntax_expr *selectors[128],*base=pigen_syntax_expr_get(&syntax->expressions,expression);
	size_t selected=0;
	while(base)
	{
		if(base->kind==PIGEN_SYNTAX_EXPR_GROUP)
		{base=pigen_syntax_expr_get(&syntax->expressions,base->as.group.operand);continue;}
		if(base->kind!=PIGEN_SYNTAX_EXPR_INDEX && base->kind!=PIGEN_SYNTAX_EXPR_SELECT) break;
		if(selected==128) return -1;
		selectors[selected++]=base;
		base=pigen_syntax_expr_get(&syntax->expressions,base->kind==PIGEN_SYNTAX_EXPR_INDEX ? base->as.index.base : base->as.select.base);
	}
	expression_analyzer analyzer={.syntax=syntax,.model=model,.scope=scope,.policy=policy};
	pigen_instance_id instance;
	view->array=protocol_array_reference(&analyzer,base,&instance);
	const pigen_semantic_protocol_array *array=pigen_protocol_array_get(model,view->array);
	if(!array) return 0;
	if(selected>array->dimension_count) return -1;
	size_t dimensions=array->dimension_count;
	size_t *starts=pigen_resize(NULL,dimensions*sizeof(*starts));
	size_t *counts=pigen_resize(NULL,dimensions*sizeof(*counts));
	size_t *strides=pigen_resize(NULL,dimensions*sizeof(*strides));
	view->dimensions=pigen_resize(NULL,dimensions*sizeof(*view->dimensions));
	int ok=1,slices=0;size_t total=1,stride=1;
	for(size_t d=dimensions;d--;)
	{strides[d]=stride;stride*=model->protocol_array_dimensions[array->first_dimension+d].count;}
	for(size_t d=0;d<dimensions;d++)
	{
		pigen_protocol_array_dimension original=model->protocol_array_dimensions[array->first_dimension+d];
		int64_t left=original.left,right=original.right;int indexed=0;
		if(d<selected)
		{
			const pigen_syntax_expr *at=selectors[selected-1-d];
			indexed=at->kind==PIGEN_SYNTAX_EXPR_INDEX;
			if(!array_selection_constant(syntax,model,scope,indexed ? at->as.index.index : at->as.select.left,policy,&left)) {ok=0;break;}
			right=left;
			if(!indexed)
			{
				int64_t operand;
				if(++slices>1 || !array_selection_constant(syntax,model,scope,at->as.select.right,policy,&operand)) {ok=0;break;}
				if(at->as.select.kind==PIGEN_SELECT_RANGE) right=operand;
				else
				{
					if(operand<=0) {ok=0;break;}
					int64_t delta=operand-1;
					if(at->as.select.kind==PIGEN_SELECT_INDEXED_UP)
					{if(left>INT64_MAX-delta) {ok=0;break;}right=left+delta;}
					else {if(left<INT64_MIN+delta) {ok=0;break;}right=left-delta;}
					int64_t low=left<right ? left : right,high=left>right ? left : right;
					left=original.left>original.right ? high : low;right=original.left>original.right ? low : high;
				}
				if(left!=right && (left>right)!=(original.left>original.right)) {ok=0;break;}
			}
		}
		int64_t low=original.left<original.right ? original.left : original.right;
		int64_t high=original.left>original.right ? original.left : original.right;
		if(left<low || left>high || right<low || right>high) {ok=0;break;}
		starts[d]=(size_t)(original.left<=original.right ? (uint64_t)left-(uint64_t)original.left : (uint64_t)original.left-(uint64_t)left);
		counts[d]=(size_t)(left<=right ? (uint64_t)right-(uint64_t)left : (uint64_t)left-(uint64_t)right)+1;
		if(counts[d]>policy->maximum_generated_bits/total) {ok=0;break;}
		total*=counts[d];
		if(!indexed) view->dimensions[view->dimension_count++]=(pigen_protocol_array_dimension){left,right,counts[d]};
	}
	if(ok)
	{
		view->elements=pigen_resize(NULL,total*sizeof(*view->elements));view->element_count=total;
		for(size_t lane=0;lane<total;lane++)
		{
			size_t remaining=lane,ordinal=0;
			for(size_t d=dimensions;d--;)
			{ordinal+=(starts[d]+remaining%counts[d])*strides[d];remaining/=counts[d];}
			pigen_protocol_endpoint_id endpoint=model->protocol_array_elements[array->first_element+ordinal];
			if(instance.index!=PIGEN_INVALID_ID) endpoint=pigen_instance_protocol_endpoint(model,instance,endpoint);
			if(endpoint.index==PIGEN_INVALID_ID) {ok=0;break;}
			view->elements[lane]=endpoint;
		}
	}
	free(starts);free(counts);free(strides);
	if(!ok) {pigen_free_protocol_array_view(view);return -1;}
	return 1;
}

/* Native keys compare the protocol plus the semantic DATA type, never just
 * the packed record width. DATA-free streams have no payload type to compare. */
static int protocol_type_key_matches(pigen_semantic_model *model,const pigen_symbol *key,
 pigen_protocol_endpoint_id target)
{
 if(key->kind==PIGEN_SYMBOL_TYPEDEF) return 0;
 const pigen_semantic_protocol_endpoint *endpoint=pigen_protocol_endpoint_get(model,target);
 if(!endpoint) return -1;
 pigen_protocol_type_id type=pigen_symbol_protocol_type(model,(pigen_symbol_id){(uint32_t)(key-model->symbols)});
 if(type.index!=endpoint->type.index) return 0;
 const pigen_protocol *protocol=pigen_protocol_type_get(model,type);
 if(!protocol) return -1;
 if(!protocol->data_bits) return 1;
 pigen_protocol_channel channel=protocol->family==PIGEN_PROTOCOL_AXI4_STREAM ? PIGEN_CHANNEL_BEAT : PIGEN_CHANNEL_W;
 const pigen_semantic_signal *signal=pigen_signal_get(model,endpoint->channels[channel]);
 if(!signal) return -1;
 pigen_data_type_id record=signal->data_type;
 for(size_t depth=0;depth<=model->data_type_count;depth++)
 {
  pigen_symbol_id owner=pigen_data_type_alias_symbol(model,record);
  if(owner.index==PIGEN_INVALID_ID) break;
  for(size_t m=0;m<model->struct_member_count;m++)
  {
   const pigen_semantic_struct_member *member=&model->struct_members[m];
   if(member->owner.index!=owner.index) continue;
   size_t n=0;const char *name=pigen_source_span_text(model->sources,member->name,&n);
   if(name && n==4 && !memcmp(name,"data",4))
    return pigen_data_type_matches(model,key->data_type,member->data_type);
  }
  record=pigen_data_type_alias_target(model,record);
 }
 return -1;
}

static const pigen_symbol *protocol_pattern_key_symbol(const pigen_syntax_tree *syntax,
 pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id key,int *is_type)
{
 *is_type=0;
 const pigen_syntax_expr *node=pigen_syntax_expr_get(&syntax->expressions,key);
 if(!node) return NULL;
 if(node->kind==PIGEN_SYNTAX_EXPR_MEMBER && node->as.member.scoped)
 {
  expression_analyzer analyzer={.syntax=syntax,.model=model,.scope=scope};
  const pigen_symbol *symbol=pigen_symbol_get(model,scoped_symbol_reference(&analyzer,node));
  if(symbol) *is_type=symbol->kind==PIGEN_SYMBOL_PROTOCOL_TYPE || symbol->kind==PIGEN_SYMBOL_TYPEDEF;
  return symbol;
 }
 if(node->kind!=PIGEN_SYNTAX_EXPR_NAME) return NULL;
 const pigen_expanded_token *token=pigen_expanded_token_get(syntax->expanded,node->as.atom.token);
 size_t n=0;const char *name=pigen_expanded_token_text(syntax->expanded,token,&n);
 const pigen_symbol *symbol=name ? pigen_symbol_get(model,pigen_symbol_resolve_text_at(model,scope,name,n,node->location.extent.first.index)) : NULL;
 if(symbol) *is_type=symbol->kind==PIGEN_SYMBOL_PROTOCOL_TYPE || symbol->kind==PIGEN_SYMBOL_TYPEDEF;
 else if(token) *is_type=pigen_data_type_spelling_domain(model,pigen_origin_spelling_span(syntax->expanded,token->origin))!=PIGEN_TYPE_SPELLING_UNKNOWN;
 return symbol;
}

typedef struct {
 pigen_syntax_expr_id value;
 int is_default;
 int is_type;
} protocol_pattern_entry;

static protocol_pattern_entry *protocol_pattern_order(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,pigen_scope_id scope,const pigen_syntax_expr *node,
	const pigen_protocol_array_dimension *dimension,pigen_protocol_endpoint_id target,const pigen_resolve_policy *policy)
{
	if(!dimension || !dimension->count || dimension->count>policy->maximum_generated_bits ||
		dimension->count>SIZE_MAX/sizeof(protocol_pattern_entry)) return NULL;
	protocol_pattern_entry *ordered=pigen_resize(NULL,dimension->count*sizeof(*ordered));
	for(size_t i=0;i<dimension->count;i++) ordered[i]=(protocol_pattern_entry){INVALID_ID(pigen_syntax_expr_id),0,0};
	pigen_syntax_expr_id fallback=INVALID_ID(pigen_syntax_expr_id),type_fallback=INVALID_ID(pigen_syntax_expr_id);
	const pigen_syntax_expr_id *entries=pigen_syntax_expr_children(&syntax->expressions,
		node->as.sequence.first_child,node->as.sequence.child_count);
	int ok=entries!=NULL;
	for(size_t i=0;ok && i<node->as.sequence.child_count;i++)
	{
		const pigen_syntax_expr *entry=pigen_syntax_expr_get(&syntax->expressions,entries[i]);
		if(!entry || entry->kind!=PIGEN_SYNTAX_EXPR_RECORD_FIELD) {ok=0;break;}
		const pigen_expanded_token *token=pigen_expanded_token_get(syntax->expanded,entry->as.member.member);
		size_t length=0;const char *text=pigen_expanded_token_text(syntax->expanded,token,&length);
		if(text && length==7 && !memcmp(text,"default",7))
		{
			if(fallback.index!=PIGEN_INVALID_ID) {ok=0;break;}
			fallback=entry->as.member.base;continue;
		}
		int is_type=0;
		const pigen_symbol *symbol=protocol_pattern_key_symbol(syntax,model,scope,entry->as.member.pattern_key,&is_type);
		if(is_type)
		{
			int match=symbol ? protocol_type_key_matches(model,symbol,target) : 0;
			if(match<0) {ok=0;break;}
			if(match) type_fallback=entry->as.member.base;
			continue;
		}
		int64_t key;
		if(!array_selection_constant(syntax,model,scope,entry->as.member.pattern_key,policy,&key)) {ok=0;break;}
		int64_t low=dimension->left<dimension->right ? dimension->left : dimension->right;
		int64_t high=dimension->left>dimension->right ? dimension->left : dimension->right;
		if(key<low || key>high) {ok=0;break;}
		size_t ordinal=(size_t)(dimension->left<=dimension->right ?
			(uint64_t)key-(uint64_t)dimension->left : (uint64_t)dimension->left-(uint64_t)key);
		if(ordinal>=dimension->count || ordered[ordinal].value.index!=PIGEN_INVALID_ID) {ok=0;break;}
		ordered[ordinal].value=entry->as.member.base;
	}
	for(size_t i=0;ok && i<dimension->count;i++)
	{
		if(ordered[i].value.index==PIGEN_INVALID_ID) ordered[i]=type_fallback.index!=PIGEN_INVALID_ID ?
			(protocol_pattern_entry){type_fallback,0,1} : (protocol_pattern_entry){fallback,1,0};
		if(ordered[i].value.index==PIGEN_INVALID_ID) ok=0;
	}
	if(!ok) {free(ordered);return NULL;}
	return ordered;
}

/* A default may match a deeper element rather than the immediate row.
 * Repeated IDs are deliberate: normal endpoint ownership must reject any
 * resulting multiple consuming bindings instead of cloning protocol state. */
static int protocol_default_descent(pigen_protocol_array_view *item,
	const pigen_protocol_array_dimension *dimensions,size_t rank,
	const pigen_resolve_policy *policy)
{
	if(!dimensions || item->dimension_count>=rank || !item->element_count) return 0;
	size_t prefix=rank-item->dimension_count,total=item->element_count;
	for(size_t d=0;d<item->dimension_count;d++)
		if(item->dimensions[d].count!=dimensions[prefix+d].count) return 0;
	for(size_t d=0;d<prefix;d++)
	{
		if(!dimensions[d].count || total>policy->maximum_generated_bits/dimensions[d].count) return 0;
		total*=dimensions[d].count;
	}
	if(total>SIZE_MAX/sizeof(*item->elements) || rank>SIZE_MAX/sizeof(*item->dimensions)) return 0;
	size_t original=item->element_count;
	item->elements=pigen_resize(item->elements,total*sizeof(*item->elements));
	for(size_t i=original;i<total;i++) item->elements[i]=item->elements[i%original];
	item->element_count=total;
	item->dimensions=pigen_resize(item->dimensions,rank*sizeof(*item->dimensions));
	memcpy(item->dimensions,dimensions,rank*sizeof(*dimensions));item->dimension_count=rank;
	return 1;
}

static int protocol_array_binding(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression,
	size_t rank,const pigen_protocol_array_dimension *dimensions,
	const pigen_resolve_policy *policy,pigen_protocol_array_view *view,size_t depth,pigen_protocol_endpoint_id target)
{
	if(!view) return -1;
	*view=(pigen_protocol_array_view){.array={PIGEN_INVALID_ID}};
	if(!syntax || !model || !policy || depth>128) return -1;
	const pigen_syntax_expr *node=pigen_syntax_expr_get(&syntax->expressions,expression);
	while(node && node->kind==PIGEN_SYNTAX_EXPR_GROUP)
		node=pigen_syntax_expr_get(&syntax->expressions,node->as.group.operand);
	int keyed=node && node->kind==PIGEN_SYNTAX_EXPR_RECORD;
	int replicated=node && node->kind==PIGEN_SYNTAX_EXPR_ARRAY_PATTERN_REPLICATION;
	int pattern=keyed || replicated || (node && node->kind==PIGEN_SYNTAX_EXPR_ARRAY_PATTERN);
	if(!node || (!pattern && node->kind!=PIGEN_SYNTAX_EXPR_CONCATENATION))
	{
		int result=pigen_resolve_protocol_array_view(syntax,model,scope,expression,policy,view);
		if(!result && !rank)
		{
			pigen_protocol_endpoint_id endpoint=pigen_resolve_protocol_reference(syntax,model,scope,expression, policy);
			if(endpoint.index!=PIGEN_INVALID_ID)
			{
				view->elements=pigen_resize(NULL,sizeof(*view->elements));
				view->elements[0]=endpoint;view->element_count=1;return 1;
			}
		}
		return result;
	}
	/* IEEE 1800-2017 10.9.1 patterns provide one contextually typed element
	 * per entry. Section 10.10 concatenations require self-determined items
	 * and can also contribute a leading array dimension's elements. */
	size_t body_count=replicated ? node->as.replication.child_count : node->as.sequence.child_count;
	size_t repetitions=1;
	if(replicated)
	{
		int64_t count;
		if(!array_selection_constant(syntax,model,scope,node->as.replication.count,policy,&count) || count<=0 ||
			(uint64_t)count>SIZE_MAX) return -1;
		repetitions=(size_t)count;
	}
	if(!rank || !body_count || repetitions>policy->maximum_generated_bits/body_count) return -1;
	const pigen_syntax_expr_id *children=pigen_syntax_expr_children(&syntax->expressions,
		replicated ? node->as.replication.first_child : node->as.sequence.first_child,body_count);
	if(!children) return -1;
	protocol_pattern_entry *ordered=keyed ? protocol_pattern_order(syntax,model,scope,node,dimensions,target,policy) : NULL;
	if(keyed && !ordered) return -1;
	size_t child_count=keyed ? dimensions[0].count : body_count*repetitions;
	if(replicated && dimensions && child_count!=dimensions[0].count) return -1;
	view->dimensions=pigen_resize(NULL,rank*sizeof(*view->dimensions));
	view->dimension_count=rank;
	size_t leading=0;
	for(size_t i=0;i<child_count;i++)
	{
		pigen_protocol_array_view item={0};
		pigen_syntax_expr_id child=keyed ? ordered[i].value : children[i%body_count];
		int type_entry=keyed && ordered[i].is_type;
		int result=pattern ? protocol_array_binding(syntax,model,scope,child,type_entry ? 0 : rank-1,type_entry ? NULL : dimensions ? dimensions+1 : NULL,policy,&item,depth+1,target) :
			pigen_resolve_protocol_array_view(syntax,model,scope,child,policy,&item);
		if(!result)
		{
			pigen_protocol_endpoint_id endpoint=pigen_resolve_protocol_reference(syntax,model,scope,child, policy);
			if(endpoint.index!=PIGEN_INVALID_ID)
			{
				item.elements=pigen_resize(NULL,sizeof(*item.elements));
				item.elements[0]=endpoint;item.element_count=1;result=1;
			}
		}
		if(result==1 && type_entry)
		{
			if(item.dimension_count) result=-1;
			else if(rank>1 && !protocol_default_descent(&item,dimensions ? dimensions+1 : NULL,rank-1,policy)) result=-1;
		}
		if(result==1 && keyed && ordered[i].is_default && item.dimension_count<rank-1)
		{
			if(!protocol_default_descent(&item,dimensions ? dimensions+1 : NULL,rank-1,policy)) result=-1;
		}
		int compatible=result==1 && (item.dimension_count==rank-1 || (!pattern && item.dimension_count==rank));
		size_t offset=item.dimension_count==rank ? 1 : 0;
		for(size_t d=1;compatible && d<rank;d++)
		{
			pigen_protocol_array_dimension dimension=item.dimensions[d-1+offset];
			if(!i) view->dimensions[d]=dimension;
			else if(view->dimensions[d].count!=dimension.count) compatible=0;
		}
		if(!compatible || !item.element_count || item.element_count>policy->maximum_generated_bits ||
			view->element_count>policy->maximum_generated_bits-item.element_count ||
			item.element_count>SIZE_MAX/sizeof(*view->elements) ||
			view->element_count>SIZE_MAX/sizeof(*view->elements)-item.element_count)
		{
			pigen_free_protocol_array_view(&item);pigen_free_protocol_array_view(view);free(ordered);return -1;
		}
		leading+=offset ? item.dimensions[0].count : 1;
		view->elements=pigen_resize(view->elements,(view->element_count+item.element_count)*sizeof(*view->elements));
		memcpy(view->elements+view->element_count,item.elements,item.element_count*sizeof(*view->elements));
		view->element_count+=item.element_count;
		pigen_free_protocol_array_view(&item);
	}
	free(ordered);
	if(leading>INT64_MAX) {pigen_free_protocol_array_view(view);return -1;}
	view->dimensions[0]=keyed ? dimensions[0] : (pigen_protocol_array_dimension){(int64_t)leading-1,0,leading};
	return 1;
}

int pigen_resolve_protocol_array_binding(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression,
	size_t rank,const pigen_resolve_policy *policy,pigen_protocol_array_view *view)
{
	return protocol_array_binding(syntax,model,scope,expression,rank,NULL,policy,view,0,INVALID_ID(pigen_protocol_endpoint_id));
}

int pigen_resolve_protocol_array_binding_to_shape(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression,
	const pigen_protocol_array_dimension *dimensions,size_t rank,
	const pigen_resolve_policy *policy,pigen_protocol_array_view *view)
{
	return protocol_array_binding(syntax,model,scope,expression,rank,dimensions,policy,view,0,INVALID_ID(pigen_protocol_endpoint_id));
}

int pigen_resolve_protocol_array_binding_to_array(const pigen_syntax_tree *syntax,
 pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression,
 pigen_protocol_array_id target,const pigen_resolve_policy *policy,pigen_protocol_array_view *view)
{
 const pigen_semantic_protocol_array *array=pigen_protocol_array_get(model,target);
 if(!array || !array->element_count)
 {if(view) *view=(pigen_protocol_array_view){.array={PIGEN_INVALID_ID}};return -1;}
 pigen_protocol_endpoint_id element=model->protocol_array_elements[array->first_element];
 return protocol_array_binding(syntax,model,scope,expression,array->dimension_count,
  model->protocol_array_dimensions+array->first_dimension,policy,view,0,element);
}

static pigen_protocol_endpoint_id protocol_index_reference(expression_analyzer *analyzer,
	const pigen_syntax_expr *syntax, int *is_array)
{
	const pigen_syntax_expr *base = syntax;
	size_t rank = 0;
	while (base && (base->kind == PIGEN_SYNTAX_EXPR_INDEX || base->kind == PIGEN_SYNTAX_EXPR_GROUP))
	{
		if (base->kind == PIGEN_SYNTAX_EXPR_GROUP)
			base = pigen_syntax_expr_get(&analyzer->syntax->expressions, base->as.group.operand);
		else { rank++; base = pigen_syntax_expr_get(&analyzer->syntax->expressions, base->as.index.base); }
	}
	pigen_instance_id instance;
	pigen_protocol_array_id array_id = protocol_array_reference(analyzer, base, &instance);
	const pigen_semantic_protocol_array *array = pigen_protocol_array_get(analyzer->model, array_id);
	*is_array = array != NULL;
	if (!array) return INVALID_ID(pigen_protocol_endpoint_id);
	if (rank != array->dimension_count)
	{
		fail(analyzer, syntax->location, "AXI endpoint array requires one constant index per dimension");
		return INVALID_ID(pigen_protocol_endpoint_id);
	}
	int64_t *coordinates = pigen_resize(NULL, rank * sizeof(*coordinates));
	base = syntax;
	size_t at = rank;
	int ok = 1;
	while (base && at)
	{
		if (base->kind == PIGEN_SYNTAX_EXPR_GROUP)
		{ base = pigen_syntax_expr_get(&analyzer->syntax->expressions, base->as.group.operand); continue; }
		ok = array_selection_constant(analyzer->syntax,analyzer->model,analyzer->scope,
			base->as.index.index,analyzer->policy,&coordinates[--at]);
		if (!ok) break;
		base = pigen_syntax_expr_get(&analyzer->syntax->expressions, base->as.index.base);
	}
	pigen_protocol_endpoint_id endpoint = ok ? pigen_protocol_array_element(analyzer->model, array_id, coordinates, rank) :
		INVALID_ID(pigen_protocol_endpoint_id);
	free(coordinates);
	if (endpoint.index == PIGEN_INVALID_ID)
		fail(analyzer, syntax->location, "AXI endpoint array index must be constant and in bounds");
	return instance.index == PIGEN_INVALID_ID ? endpoint :
		pigen_instance_protocol_endpoint(analyzer->model, instance, endpoint);
}

static pigen_protocol_endpoint_id protocol_reference(expression_analyzer *analyzer,
	const pigen_syntax_expr *syntax)
{
	if (!syntax || analyzer->constant_only) return INVALID_ID(pigen_protocol_endpoint_id);
	if (syntax->kind == PIGEN_SYNTAX_EXPR_GROUP)
		return protocol_reference(analyzer, pigen_syntax_expr_get(&analyzer->syntax->expressions, syntax->as.group.operand));
	if (syntax->kind == PIGEN_SYNTAX_EXPR_INDEX)
	{
		int is_array;
		return protocol_index_reference(analyzer, syntax, &is_array);
	}
	pigen_protocol_endpoint_id direct=pigen_symbol_protocol_endpoint(analyzer->model,scoped_symbol_reference(analyzer,syntax));
	if(direct.index!=PIGEN_INVALID_ID) return direct;
	if (syntax->kind != PIGEN_SYNTAX_EXPR_MEMBER || syntax->as.member.scoped)
		return INVALID_ID(pigen_protocol_endpoint_id);
	const pigen_syntax_expr *base = pigen_syntax_expr_get(
		&analyzer->syntax->expressions, syntax->as.member.base);
	const pigen_symbol *owner=pigen_symbol_get(analyzer->model,scoped_symbol_reference(analyzer,base));
	if (!owner || owner->kind != PIGEN_SYMBOL_INSTANCE) return INVALID_ID(pigen_protocol_endpoint_id);
	const pigen_semantic_instance *instance = pigen_instance_get(analyzer->model, owner->object.instance);
	const pigen_semantic_module *child = pigen_module_get(analyzer->model, instance->child_module);
	pigen_protocol_endpoint_id port = pigen_symbol_protocol_endpoint(analyzer->model,
		pigen_symbol_lookup(analyzer->model, child->scope,
			token_spelling(analyzer, syntax->as.member.member)));
	return pigen_instance_protocol_endpoint(analyzer->model, owner->object.instance, port);
}

pigen_protocol_endpoint_id pigen_resolve_protocol_reference(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression, const pigen_resolve_policy *policy)
{
	if (!syntax || !model || !policy) return INVALID_ID(pigen_protocol_endpoint_id);
	const pigen_syntax_expr *written = pigen_syntax_expr_get(&syntax->expressions, expression);
	while (written && written->kind == PIGEN_SYNTAX_EXPR_GROUP)
		written = pigen_syntax_expr_get(&syntax->expressions, written->as.group.operand);
	expression_analyzer analyzer = {.syntax = syntax, .model = model, .scope = scope, .policy = policy};
	return protocol_reference(&analyzer, written);
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

int pigen_binary_operator_from_syntax(pigen_syntax_operator syntax,
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

static pigen_analyzed_expr_id append_integer_literal(expression_analyzer *analyzer,
	uint64_t value, pigen_source_span span)
{
	pigen_analyzed_expr node = {0};

	node.kind = PIGEN_SYNTAX_EXPR_LITERAL;
	node.literal_kind = PIGEN_ANALYZED_LITERAL_INTEGER;
	node.span = span;
	node.data_type = pigen_data_type_unsized_integer(analyzer->model);
	node.shape = pigen_semantic_scalar_shape(analyzer->model);
	node.as.integer = value;
	node.constant = pigen_const_expr_intern_integer(analyzer->model, value,
		node.data_type);
	return append_node(analyzer, node);
}

static pigen_analyzed_expr_id analyze(expression_analyzer *analyzer,
	pigen_syntax_expr_id expression);

static int analyzed_lvalue(expression_analyzer *analyzer,pigen_analyzed_expr_id id,size_t depth)
{
 const pigen_analyzed_expr *node=pigen_analyzed_expr_get(analyzer->arena,id);
 if(!node || node->form!=PIGEN_ANALYZED_INTRINSIC || depth>128) return 0;
 if(node->kind==PIGEN_SYNTAX_EXPR_GROUP) return analyzed_lvalue(analyzer,node->as.group.operand,depth+1);
 if(node->kind==PIGEN_SYNTAX_EXPR_INDEX || node->kind==PIGEN_SYNTAX_EXPR_SELECT)
 {
  for(size_t i=depth;i<128;i++)
  {
   if(node->kind==PIGEN_SYNTAX_EXPR_GROUP) id=node->as.group.operand;
   else if(node->kind==PIGEN_SYNTAX_EXPR_INDEX) id=node->as.index.base;
   else if(node->kind==PIGEN_SYNTAX_EXPR_SELECT) id=node->as.select.base;
   else return node->form==PIGEN_ANALYZED_INTRINSIC && node->kind==PIGEN_SYNTAX_EXPR_NAME && pigen_symbol_signal(analyzer->model,node->as.symbol).index!=PIGEN_INVALID_ID;
   node=pigen_analyzed_expr_get(analyzer->arena,id);
   if(!node || node->form!=PIGEN_ANALYZED_INTRINSIC) return 0;
  }
  return 0;
 }
 if(node->kind==PIGEN_SYNTAX_EXPR_CONCATENATION)
 {
  for(size_t i=0;i<node->as.sequence.child_count;i++)
   if(!analyzed_lvalue(analyzer,analyzer->arena->children[node->as.sequence.first_child+i],depth+1)) return 0;
  return node->as.sequence.child_count!=0;
 }
 return node->kind==PIGEN_SYNTAX_EXPR_NAME && pigen_symbol_signal(analyzer->model,node->as.symbol).index!=PIGEN_INVALID_ID;
}

static pigen_analyzed_expr_id analyze_handshake_call(expression_analyzer *analyzer,
 pigen_syntax_expr_id expression,const pigen_syntax_expr *syntax,pigen_semantic_expr_kind kind)
{
 size_t count=kind==PIGEN_EXPR_ACCEPTS ? 2 : 1;
 const pigen_syntax_expr_id *arguments=pigen_syntax_expr_children(&analyzer->syntax->expressions,
  syntax->as.call.first_argument,syntax->as.call.argument_count);
 if(!arguments || syntax->as.call.argument_count!=count)
  return fail(analyzer,syntax->location,"invalid handshake call argument count");
 pigen_analyzed_expr node={0};node.kind=PIGEN_SYNTAX_EXPR_CALL;node.syntax=expression;
 node.span=syntax->location.source_span;node.constant=INVALID_ID(pigen_const_expr_id);
 node.as.call.kind=kind;node.as.call.second=INVALID_ID(pigen_analyzed_expr_id);node.as.call.signal=INVALID_ID(pigen_signal_id);
 expression_analyzer local=*analyzer;
 int filled=pigen_expression_contains_unbased_fill(analyzer->syntax,arguments[0]);
 if(kind==PIGEN_EXPR_PEEK && filled) local.literal_domain=PIGEN_LITERAL_DOMAIN_CASE;
 pigen_analyzed_expr_id first=analyze(&local,arguments[0]);
 const pigen_analyzed_expr *known=pigen_analyzed_expr_get(analyzer->arena,first);
 if(!known) return INVALID_ID(pigen_analyzed_expr_id);
 node.as.call.first=first;
 if(kind==PIGEN_EXPR_PEEK)
 {
  node.data_type=known->data_type;node.shape=known->shape;node.constant=known->constant;
  if(filled && node.constant.index!=PIGEN_INVALID_ID) node.constant=normalize_analyzed_constant(&local,first);
  pigen_analyzed_expr boundary={0};boundary.form=PIGEN_ANALYZED_CASE_VALUE;boundary.kind=PIGEN_SYNTAX_EXPR_GROUP;
  boundary.syntax=known->syntax;boundary.span=known->span;boundary.data_type=known->data_type;boundary.shape=known->shape;
  boundary.constant=INVALID_ID(pigen_const_expr_id);boundary.as.group.operand=first;
  node.as.call.first=append_node(analyzer,boundary);
 }
 else
 {
  node.data_type=pigen_data_type_boolean(analyzer->model);node.shape=pigen_semantic_scalar_shape(analyzer->model);
  if(kind==PIGEN_EXPR_ACCEPTS)
  {
   if(!analyzed_lvalue(analyzer,first,0)) return fail(analyzer,syntax->location,"accepts requires a signal destination");
   node.as.call.second=analyze(analyzer,arguments[1]);
   known=pigen_analyzed_expr_get(analyzer->arena,node.as.call.second);
  }
  node.as.call.signal=known && known->form==PIGEN_ANALYZED_INTRINSIC && known->kind==PIGEN_SYNTAX_EXPR_NAME ?
   pigen_symbol_signal(analyzer->model,known->as.symbol) : INVALID_ID(pigen_signal_id);
  if(node.as.call.signal.index==PIGEN_INVALID_ID)
   return fail(analyzer,syntax->location,"handshake control requires a direct signal reference");
 }
 return append_node(analyzer,node);
}

/* Inquiries inspect types without publishing operand reads. */
static int inquiry_type(expression_analyzer *analyzer,const pigen_syntax_expr *syntax,
 pigen_data_type_id *result_type,pigen_shape_id *result_shape)
{
 pigen_data_type_id type=INVALID_ID(pigen_data_type_id);
 pigen_shape_id shape=pigen_semantic_scalar_shape(analyzer->model);
 const pigen_syntax_expr_id *arguments=pigen_syntax_expr_children(
  &analyzer->syntax->expressions,syntax->as.call.first_argument,syntax->as.call.argument_count);
 int use_type=syntax->as.call.type_argument_kind==1;
 if(!use_type)
 {
  const pigen_syntax_expr *argument=arguments ? pigen_syntax_expr_get(&analyzer->syntax->expressions,arguments[0]) : NULL;
  pigen_symbol_id symbol_id=INVALID_ID(pigen_symbol_id);
  if(syntax->as.call.type_argument_kind==2)
   symbol_id=pigen_resolve_type_symbol(analyzer->syntax,analyzer->model,analyzer->scope,syntax->as.call.type_argument);
  else if(argument) symbol_id=scoped_symbol_reference(analyzer,argument);
  const pigen_symbol *symbol=pigen_symbol_get(analyzer->model,symbol_id);
  if(symbol && symbol->kind==PIGEN_SYMBOL_PROTOCOL_TYPE)
  {
   fail(analyzer,syntax->location,"native AXI endpoint type is not a packed data type");return 0;
  }
  if(symbol && symbol->kind==PIGEN_SYMBOL_TYPEDEF)
  {
   if(syntax->as.call.type_argument_kind==2) use_type=1;
   else type=symbol->data_type;
  }
 }
 if(use_type)
  type=pigen_resolve_type(analyzer->syntax,analyzer->model,analyzer->scope,syntax->as.call.type_argument,analyzer->error);
 else if(type.index==PIGEN_INVALID_ID)
 {
  expression_analyzer query=*analyzer;
  query.constant_only=0;query.literal_domain=PIGEN_LITERAL_DOMAIN_CASE;
  pigen_analyzed_expr_id operand=arguments ? analyze(&query,arguments[0]) : INVALID_ID(pigen_analyzed_expr_id);
  const pigen_analyzed_expr *known=pigen_analyzed_expr_get(analyzer->arena,operand);
  if(!known) return 0;
  type=known->data_type;shape=known->shape;
 }
 *result_type=type;*result_shape=shape;return type.index!=PIGEN_INVALID_ID;
}

static pigen_analyzed_expr_id append_query_integer(expression_analyzer *analyzer,
 pigen_syntax_expr_id expression,const pigen_syntax_expr *syntax,uint64_t value,int unknown)
{
 pigen_bit_state states[32];
 for(size_t i=0;i<32;i++) states[i]=unknown ? PIGEN_BIT_X : (pigen_bit_state)((value>>i)&1);
 pigen_analyzed_expr node={0};
 node.kind=PIGEN_SYNTAX_EXPR_LITERAL;node.syntax=expression;
 node.span=syntax->location.source_span;node.literal_kind=PIGEN_ANALYZED_LITERAL_BITS;
 node.data_type=pigen_data_type_sv_integer(analyzer->model);
 node.shape=pigen_semantic_scalar_shape(analyzer->model);
 node.as.bits.first_state=append_states(analyzer,states,32);node.as.bits.state_count=32;
 node.constant=pigen_const_expr_intern_bits(analyzer->model,states,32,node.data_type);
 return append_node(analyzer,node);
}

static pigen_analyzed_expr_id analyze_bits(expression_analyzer *analyzer,
 pigen_syntax_expr_id expression,const pigen_syntax_expr *syntax)
{
 pigen_data_type_id type;pigen_shape_id shape;
 if(syntax->as.call.argument_count!=(syntax->as.call.type_argument_kind==1 ? 0u : 1u))
  return fail(analyzer,syntax->location,"$bits requires one expression or fixed-size type");
 if(!inquiry_type(analyzer,syntax,&type,&shape)) return INVALID_ID(pigen_analyzed_expr_id);
 uint64_t bits=0;
 if(type.index==PIGEN_INVALID_ID || !pigen_const_expr_evaluate_u64(analyzer->model,
  pigen_data_type_packed_width(analyzer->model,type),&bits) || !bits)
  return fail(analyzer,syntax->location,"$bits requires a concrete fixed-size type");
 const pigen_semantic_shape *known_shape=pigen_shape_get(analyzer->model,shape);
 const pigen_shape_dimension *dimensions=pigen_shape_dimensions(analyzer->model,shape);
 if(!known_shape) return fail(analyzer,syntax->location,"invalid $bits operand shape");
 for(size_t i=0;i<known_shape->dimension_count;i++)
 {
  uint64_t count=0;
  if(dimensions[i].form==PIGEN_SHAPE_DIMENSION_COUNT)
  {
   if(!pigen_const_expr_evaluate_u64(analyzer->model,dimensions[i].as.count,&count)) count=0;
  }
  else
  {
   int64_t left,right;
   if(!pigen_const_expr_evaluate_i64(analyzer->model,dimensions[i].as.range.left,&left) ||
      !pigen_const_expr_evaluate_i64(analyzer->model,dimensions[i].as.range.right,&right))
    return fail(analyzer,syntax->location,"$bits requires fixed array bounds");
   uint64_t distance=left>=right ? (uint64_t)left-(uint64_t)right : (uint64_t)right-(uint64_t)left;
   if(distance!=UINT64_MAX) count=distance+1;
  }
  if(!count || bits>UINT64_MAX/count)
   return fail(analyzer,syntax->location,"$bits operand size is unresolved or too large");
  bits*=count;
 }
 return append_query_integer(analyzer,expression,syntax,bits,0);
}

typedef struct {int64_t left,right;} inquiry_dimension;

static uint64_t inquiry_value(inquiry_dimension dimension,const char *name,size_t length)
{
 int64_t left=dimension.left,right=dimension.right;
 if(length==5 && !memcmp(name,"$left",5)) return (uint64_t)left;
 if(length==6 && !memcmp(name,"$right",6)) return (uint64_t)right;
 if(length==4 && !memcmp(name,"$low",4)) return (uint64_t)(left<right ? left : right);
 if(length==5 && !memcmp(name,"$high",5)) return (uint64_t)(left>right ? left : right);
 if(length==10 && !memcmp(name,"$increment",10)) return left>=right ? 1 : UINT64_MAX;
 return (left>=right ? (uint64_t)left-(uint64_t)right : (uint64_t)right-(uint64_t)left)+1;
}

static pigen_analyzed_expr_id runtime_array_query(expression_analyzer *analyzer,
 pigen_syntax_expr_id expression,const pigen_syntax_expr *syntax,const char *name,size_t length,
 const inquiry_dimension *dimensions,size_t count,pigen_analyzed_expr_id index)
{
 pigen_analyzed_expr boundary={0};
 const pigen_analyzed_expr *dimension=pigen_analyzed_expr_get(analyzer->arena,index);
 const pigen_semantic_shape *shape=dimension ? pigen_shape_get(analyzer->model,dimension->shape) : NULL;
 if(!shape || shape->dimension_count || !pigen_data_type_is_integral(analyzer->model,dimension->data_type))
  return fail(analyzer,syntax->location,"array inquiry dimension requires a scalar integral value");
 boundary.form=PIGEN_ANALYZED_CASE_VALUE;boundary.kind=PIGEN_SYNTAX_EXPR_GROUP;
 boundary.syntax=dimension->syntax;boundary.span=dimension->span;boundary.data_type=dimension->data_type;
 boundary.shape=dimension->shape;boundary.constant=INVALID_ID(pigen_const_expr_id);boundary.as.group.operand=index;
 index=append_node(analyzer,boundary);
 pigen_analyzed_expr_id result=append_query_integer(analyzer,expression,syntax,0,1);
 if(!count)
 {
  pigen_analyzed_expr node={0};node.form=PIGEN_ANALYZED_EVALUATED_VALUE;
  node.kind=PIGEN_SYNTAX_EXPR_CALL;node.syntax=expression;node.span=syntax->location.source_span;
  node.data_type=pigen_data_type_sv_integer(analyzer->model);node.shape=pigen_semantic_scalar_shape(analyzer->model);
  node.constant=INVALID_ID(pigen_const_expr_id);node.as.evaluation.value=result;
  node.as.evaluation.evaluated=index;node.as.evaluation.before=1;return append_node(analyzer,node);
 }
 for(size_t i=count;i--;)
 {
  pigen_analyzed_expr_id ordinal=append_query_integer(analyzer,expression,syntax,i+1,0);
  pigen_analyzed_expr comparison={0};comparison.kind=PIGEN_SYNTAX_EXPR_BINARY;
  comparison.syntax=expression;comparison.span=syntax->location.source_span;
  comparison.constant=INVALID_ID(pigen_const_expr_id);comparison.shape=pigen_semantic_scalar_shape(analyzer->model);
  if(!pigen_data_type_resolve_case_binary_operation(analyzer->model,PIGEN_BINARY_CASE_EQUAL,
   analyzer->arena->nodes[index.index].data_type,analyzer->arena->nodes[ordinal.index].data_type,&comparison.as.binary.resolution))
   return fail(analyzer,syntax->location,"invalid array inquiry dimension type");
  comparison.data_type=comparison.as.binary.resolution.operation.result_data_type;
  comparison.as.binary.left=index;comparison.as.binary.right=ordinal;
  pigen_analyzed_expr_id condition=append_node(analyzer,comparison);
  pigen_analyzed_expr_id value=append_query_integer(analyzer,expression,syntax,inquiry_value(dimensions[i],name,length),0);
  pigen_analyzed_expr node={0};node.kind=PIGEN_SYNTAX_EXPR_CONDITIONAL;
  node.syntax=expression;node.span=syntax->location.source_span;node.constant=INVALID_ID(pigen_const_expr_id);
  node.shape=pigen_semantic_scalar_shape(analyzer->model);
  if(!pigen_data_type_resolve_conditional_operation(analyzer->model,comparison.data_type,
   analyzer->arena->nodes[value.index].data_type,analyzer->arena->nodes[result.index].data_type,&node.as.conditional.resolution))
   return fail(analyzer,syntax->location,"invalid array inquiry result type");
  node.data_type=node.as.conditional.resolution.operation.result_data_type;
  node.as.conditional.condition=condition;node.as.conditional.when_true=value;node.as.conditional.when_false=result;
  result=append_node(analyzer,node);
 }
 return result;
}

static pigen_analyzed_expr_id analyze_array_query(expression_analyzer *analyzer,
 pigen_syntax_expr_id expression,const pigen_syntax_expr *syntax,const char *name,size_t length)
{
 int rank_query=(length==11 && !memcmp(name,"$dimensions",11)) ||
  (length==20 && !memcmp(name,"$unpacked_dimensions",20));
 size_t first=syntax->as.call.type_argument_kind==1 ? 0 : 1;
 size_t arguments_count=syntax->as.call.argument_count;
 if(arguments_count<first || arguments_count>first+(rank_query ? 0u : 1u))
  return fail(analyzer,syntax->location,"array inquiry requires one operand and an optional dimension");
 const pigen_syntax_expr_id *arguments=pigen_syntax_expr_children(&analyzer->syntax->expressions,
  syntax->as.call.first_argument,arguments_count);
 inquiry_dimension *dimensions=NULL;size_t count=0,unpacked=0;
 pigen_protocol_array_view view={0};
 int native=first && analyzer->policy ? pigen_resolve_protocol_array_view(analyzer->syntax,
  analyzer->model,analyzer->scope,arguments[0],analyzer->policy,&view) : 0;
 if(native<0) return fail(analyzer,syntax->location,"invalid native collection inquiry");
 if(!native && first && analyzer->policy && pigen_resolve_protocol_reference(analyzer->syntax,
    analyzer->model,analyzer->scope,arguments[0],analyzer->policy).index!=PIGEN_INVALID_ID)
  native=1; /* A scalar protocol endpoint has no structural dimensions. */
 if(native)
 {
  unpacked=count=view.dimension_count;
  if(count) dimensions=pigen_resize(NULL,count*sizeof(*dimensions));
  for(size_t i=0;i<count;i++) dimensions[i]=(inquiry_dimension){view.dimensions[i].left,view.dimensions[i].right};
  pigen_free_protocol_array_view(&view);
 }
 else
 {
  pigen_data_type_id type;pigen_shape_id shape;
  if(!inquiry_type(analyzer,syntax,&type,&shape)) return INVALID_ID(pigen_analyzed_expr_id);
  const pigen_semantic_shape *known=pigen_shape_get(analyzer->model,shape);
  const pigen_shape_dimension *shape_dimensions=pigen_shape_dimensions(analyzer->model,shape);
  pigen_packed_dimension *packed=NULL;size_t packed_count=0;
  if(!known || !pigen_data_type_query_dimensions(analyzer->model,type,&packed,&packed_count))
   return fail(analyzer,syntax->location,"array inquiry requires a fixed integral or native collection type");
  if(!packed_count && syntax->as.call.type_argument_kind==1 &&
     analyzer->syntax->types.nodes[syntax->as.call.type_argument.index].is_packed_struct)
  {
   pigen_const_expr_id zero=pigen_const_expr_intern_integer(analyzer->model,0,pigen_data_type_unsized_integer(analyzer->model));
   packed=pigen_resize(packed,sizeof(*packed));packed[0]=(pigen_packed_dimension){zero,zero};packed_count=1;
  }
  unpacked=known->dimension_count;
  if(packed_count>SIZE_MAX/sizeof(*dimensions)-unpacked) {free(packed);goto invalid_bounds;}
  count=unpacked+packed_count;
  if(count) dimensions=pigen_resize(NULL,count*sizeof(*dimensions));
  int valid=1;
  for(size_t i=0;i<count;i++)
  {
   pigen_const_expr_id left,right;
   if(i<unpacked && shape_dimensions[i].form==PIGEN_SHAPE_DIMENSION_COUNT)
   {
    uint64_t n=0;
    if(!pigen_const_expr_evaluate_u64(analyzer->model,shape_dimensions[i].as.count,&n) || !n || n-1>INT64_MAX)
    {valid=0;break;}
    dimensions[i]=(inquiry_dimension){0,(int64_t)(n-1)};continue;
   }
   if(i<unpacked) {left=shape_dimensions[i].as.range.left;right=shape_dimensions[i].as.range.right;}
   else {left=packed[i-unpacked].left;right=packed[i-unpacked].right;}
   if(!pigen_const_expr_evaluate_i64(analyzer->model,left,&dimensions[i].left) ||
      !pigen_const_expr_evaluate_i64(analyzer->model,right,&dimensions[i].right)) {valid=0;break;}
  }
  free(packed);
  if(!valid) goto invalid_bounds;
 }
 if(rank_query)
 {
  free(dimensions);
  return append_query_integer(analyzer,expression,syntax,length==20 ? unpacked : count,0);
 }
 int64_t selected=1;
 if(arguments_count>first)
 {
  expression_analyzer counter=*analyzer;counter.constant_only=analyzer->constant_only;counter.literal_domain=PIGEN_LITERAL_DOMAIN_CASE;
  pigen_analyzed_expr_id index=analyze(&counter,arguments[first]);
  const pigen_analyzed_expr *known=pigen_analyzed_expr_get(analyzer->arena,index);
  if(!known) {free(dimensions);return INVALID_ID(pigen_analyzed_expr_id);}
  if(known->constant.index==PIGEN_INVALID_ID)
  {
   pigen_analyzed_expr_id result=runtime_array_query(analyzer,expression,syntax,name,length,dimensions,count,index);
   free(dimensions);return result;
  }
  if(!pigen_const_expr_evaluate_i64(analyzer->model,known->constant,&selected)) selected=0;
 }
 if(selected<=0 || (uint64_t)selected>count)
 {free(dimensions);return append_query_integer(analyzer,expression,syntax,0,1);}
 inquiry_dimension dimension=dimensions[(size_t)selected-1];free(dimensions);
 return append_query_integer(analyzer,expression,syntax,inquiry_value(dimension,name,length),0);
invalid_bounds:
 free(dimensions);return fail(analyzer,syntax->location,"array inquiry requires concrete fixed bounds");
}

/* Only constant analysis can erase an empty field: runtime evaluation and
	* transfer dependencies must survive even when no payload bits contribute. */
static int constant_empty_replication(expression_analyzer *analyzer,
	pigen_syntax_expr_id expression)
{
	const pigen_syntax_expr *node=pigen_syntax_expr_get(&analyzer->syntax->expressions,expression);
	while(node && node->kind==PIGEN_SYNTAX_EXPR_GROUP) node=pigen_syntax_expr_get(&analyzer->syntax->expressions,node->as.group.operand);
	if (!analyzer->constant_only || !node || node->kind!=PIGEN_SYNTAX_EXPR_REPLICATION) return 0;
	expression_analyzer counter=*analyzer;
	counter.literal_domain=PIGEN_LITERAL_DOMAIN_PIGEN;
	pigen_analyzed_expr_id count=analyze(&counter,node->as.replication.count);
	uint64_t repeat;
	if (count.index==PIGEN_INVALID_ID || !pigen_const_expr_evaluate_u64(analyzer->model,
		analyzer->arena->nodes[count.index].constant,&repeat)) return -1;
	if (repeat) return 0;
	size_t n=node->as.replication.child_count;
	const pigen_syntax_expr_id *body=pigen_syntax_expr_children(&analyzer->syntax->expressions,node->as.replication.first_child,n);
	if (!body || !n || !analyzer->policy || !analyzer->policy->maximum_generated_bits ||
		n>analyzer->policy->maximum_generated_bits || n>SIZE_MAX/sizeof(pigen_data_type_id)) return -1;
	pigen_data_type_id *types=pigen_resize(NULL,n*sizeof(*types));
	int valid=1;size_t kept=0;
	for(size_t i=0;i<n;i++)
	{
		int empty=constant_empty_replication(analyzer,body[i]);
		if (empty<0) {valid=0;break;}
		if (empty) continue;
		pigen_analyzed_expr_id value=analyze(analyzer,body[i]);
		const pigen_analyzed_expr *known=pigen_analyzed_expr_get(analyzer->arena,value);
		const pigen_semantic_shape *shape=known ? pigen_shape_get(analyzer->model,known->shape) : NULL;
		if (!known || known->constant.index==PIGEN_INVALID_ID || !shape || shape->dimension_count) {valid=0;break;}
		types[kept++]=known->data_type;
	}
	if (valid) valid=kept && pigen_data_type_concatenation(analyzer->model,types,kept).index!=PIGEN_INVALID_ID;
	free(types);
	return valid ? 1 : -1;
}

static pigen_analyzed_expr_id analyzed_sequence(expression_analyzer *analyzer,
	pigen_syntax_expr_id syntax_id,const pigen_analyzed_expr_id *children,size_t count)
{
	const pigen_syntax_expr *syntax=pigen_syntax_expr_get(&analyzer->syntax->expressions,syntax_id);
	if(!syntax || !count || count>SIZE_MAX/sizeof(pigen_data_type_id) || count>SIZE_MAX/sizeof(pigen_const_expr_id)) return INVALID_ID(pigen_analyzed_expr_id);
	pigen_data_type_id *types=pigen_resize(NULL,count*sizeof(*types));
	pigen_const_expr_id *constants=pigen_resize(NULL,count*sizeof(*constants));
	pigen_analyzed_expr node={0};node.syntax=syntax_id;node.span=syntax->location.source_span;
	node.kind=PIGEN_SYNTAX_EXPR_CONCATENATION;node.constant=INVALID_ID(pigen_const_expr_id);
	int valid=1,all_constant=1;
	for(size_t i=0;i<count;i++)
	{
		const pigen_analyzed_expr *child=pigen_analyzed_expr_get(analyzer->arena,children[i]);
		const pigen_semantic_shape *shape=child ? pigen_shape_get(analyzer->model,child->shape) : NULL;
		if(!child || !shape || shape->dimension_count) {valid=0;break;}
		types[i]=child->data_type;constants[i]=child->constant;
		if(constants[i].index==PIGEN_INVALID_ID) all_constant=0;
	}
	if(valid)
	{
		node.data_type=pigen_data_type_concatenation(analyzer->model,types,count);
		node.shape=pigen_semantic_scalar_shape(analyzer->model);
		valid=node.data_type.index!=PIGEN_INVALID_ID;
	}
	if(valid && all_constant)
	{
		node.constant=pigen_const_expr_intern_concatenation(analyzer->model,constants,count,node.data_type);
		valid=node.constant.index!=PIGEN_INVALID_ID;
	}
	free(types);free(constants);
	if(!valid) return fail(analyzer,syntax->location,"invalid concatenation element");
	node.as.sequence.first_child=append_children(analyzer,children,count);node.as.sequence.child_count=count;
	return node.as.sequence.first_child==SIZE_MAX ? INVALID_ID(pigen_analyzed_expr_id) : append_node(analyzer,node);
}

static pigen_analyzed_expr_id analyzed_evaluation(expression_analyzer *analyzer,
	pigen_syntax_expr_id syntax_id,pigen_analyzed_expr_id value,pigen_analyzed_expr_id evaluated,int before)
{
	const pigen_analyzed_expr *v=pigen_analyzed_expr_get(analyzer->arena,value),*e=pigen_analyzed_expr_get(analyzer->arena,evaluated);
	const pigen_syntax_expr *syntax=pigen_syntax_expr_get(&analyzer->syntax->expressions,syntax_id);
	if(!v || !e || !syntax || (before!=0 && before!=1)) return INVALID_ID(pigen_analyzed_expr_id);
	const pigen_semantic_shape *vs=pigen_shape_get(analyzer->model,v->shape),*es=pigen_shape_get(analyzer->model,e->shape);
	if(!vs || !es || vs->dimension_count || es->dimension_count ||
		!pigen_data_type_is_integral(analyzer->model,v->data_type) || !pigen_data_type_is_integral(analyzer->model,e->data_type))
		return fail(analyzer,syntax->location,"invalid empty-field evaluation");
	pigen_analyzed_expr node={0};node.syntax=syntax_id;node.kind=syntax->kind;node.span=syntax->location.source_span;
	node.form=PIGEN_ANALYZED_EVALUATED_VALUE;node.data_type=v->data_type;node.shape=v->shape;
	node.constant=INVALID_ID(pigen_const_expr_id);node.as.evaluation.value=value;
	node.as.evaluation.evaluated=evaluated;node.as.evaluation.before=before;
	return append_node(analyzer,node);
}

static pigen_analyzed_expr_id analyze_runtime_sequence(expression_analyzer *analyzer,
	pigen_syntax_expr_id syntax_id,int allow_empty,int *empty)
{
	const pigen_syntax_expr *syntax=pigen_syntax_expr_get(&analyzer->syntax->expressions,syntax_id);
	if(!syntax) return INVALID_ID(pigen_analyzed_expr_id);
	int replicated=syntax->kind==PIGEN_SYNTAX_EXPR_REPLICATION;*empty=0;
	if(replicated && (!analyzer->policy || !analyzer->policy->maximum_generated_bits))
		return fail(analyzer,syntax->location,"replication requires a generated-size policy");
	size_t count=replicated ? syntax->as.replication.child_count : syntax->as.sequence.child_count;
	size_t first=replicated ? syntax->as.replication.first_child : syntax->as.sequence.first_child;
	uint64_t repeat=1;
	if(replicated)
	{
		expression_analyzer counter=*analyzer;counter.constant_only=1;counter.literal_domain=PIGEN_LITERAL_DOMAIN_PIGEN;
		pigen_analyzed_expr_id amount=analyze(&counter,syntax->as.replication.count);
		if(amount.index==PIGEN_INVALID_ID || !pigen_const_expr_evaluate_u64(analyzer->model,analyzer->arena->nodes[amount.index].constant,&repeat) || repeat>analyzer->policy->maximum_generated_bits)
			return fail(analyzer,syntax->location,"replication requires a nonnegative bounded constant count");
		if(!repeat && !allow_empty) return fail(analyzer,syntax->location,"zero-count replication requires a positive-sized concatenation peer");
	}
	if(!count || count>SIZE_MAX/sizeof(pigen_analyzed_expr_id) ||
		(replicated && (count>analyzer->policy->maximum_generated_bits || (repeat && count>analyzer->policy->maximum_generated_bits/repeat))))
		return fail(analyzer,syntax->location,"invalid concatenation size");
	const pigen_syntax_expr_id *written=pigen_syntax_expr_children(&analyzer->syntax->expressions,first,count);
	if(!written) return INVALID_ID(pigen_analyzed_expr_id);
	pigen_analyzed_expr_id *values=pigen_resize(NULL,count*sizeof(*values));
	unsigned char *ignored=pigen_resize(NULL,count);size_t positive=0;
	pigen_analyzed_expr_id result=INVALID_ID(pigen_analyzed_expr_id);
	for(size_t i=0;i<count;i++)
	{
		pigen_syntax_expr_id child_id=written[i];const pigen_syntax_expr *child=pigen_syntax_expr_get(&analyzer->syntax->expressions,child_id);int zero=0;
		while(child && child->kind==PIGEN_SYNTAX_EXPR_GROUP) {child_id=child->as.group.operand;child=pigen_syntax_expr_get(&analyzer->syntax->expressions,child_id);}
		if(child && (child->kind==PIGEN_SYNTAX_EXPR_REPLICATION || child->kind==PIGEN_SYNTAX_EXPR_CONCATENATION))
			values[i]=analyze_runtime_sequence(analyzer,child_id,1,&zero);
		else values[i]=analyze(analyzer,written[i]);
		if(values[i].index==PIGEN_INVALID_ID) goto done;
		ignored[i]=(unsigned char)zero;if(!zero) positive++;
	}
	if(!positive) {result=fail(analyzer,syntax->location,"concatenation requires a positive-sized operand");goto done;}
	size_t first_value=0;while(ignored[first_value]) first_value++;
	size_t kept=0;
	for(size_t i=first_value;i<count;)
	{
		pigen_analyzed_expr_id value=values[i];size_t next=i+1;while(next<count && ignored[next]) next++;
		if(i==first_value && first_value)
		{
			pigen_analyzed_expr_id evaluated=analyzed_sequence(analyzer,syntax_id,values,first_value);
			value=analyzed_evaluation(analyzer,syntax_id,value,evaluated,1);
		}
		if(next>i+1)
		{
			pigen_analyzed_expr_id evaluated=analyzed_sequence(analyzer,syntax_id,values+i+1,next-i-1);
			value=analyzed_evaluation(analyzer,syntax_id,value,evaluated,0);
		}
		if(value.index==PIGEN_INVALID_ID) goto done;
		values[kept++]=value;i=next;
	}
	if(!repeat) {result=analyzed_sequence(analyzer,syntax_id,values,kept);*empty=1;goto done;}
	if(repeat>SIZE_MAX/kept || kept*(size_t)repeat>SIZE_MAX/sizeof(*values)) goto done;
	size_t total=kept*(size_t)repeat;values=pigen_resize(values,total*sizeof(*values));
	for(size_t i=kept;i<total;i++) values[i]=values[i%kept];
	result=analyzed_sequence(analyzer,syntax_id,values,total);
done:
	free(ignored);free(values);return result;
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
				if (analyzer->literal_domain != PIGEN_LITERAL_DOMAIN_PIGEN &&
					literal_u64(text, length, &value))
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
			if (length==2 && text[0]=='\'' && state_count==1) node.unbased_fill=1+(int)states[0];
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
			const pigen_expanded_token *name_token =
				pigen_expanded_token_get(analyzer->syntax->expanded,
					syntax->as.atom.token);
			pigen_source_span spelling = token_spelling(analyzer,
				syntax->as.atom.token);
			pigen_source_span origin_spelling = name_token ?
				pigen_origin_spelling_span(analyzer->syntax->expanded,
					name_token->origin) : spelling;
			size_t name_length = 0;
			const char *name_text = name_token ?
				pigen_expanded_token_text(analyzer->syntax->expanded,
					name_token, &name_length) : NULL;
			size_t origin_length = 0;
			const char *origin_text = pigen_source_span_text(
				analyzer->model->sources, origin_spelling, &origin_length);
			pigen_symbol_id symbol = name_text && origin_text &&
				(name_length != origin_length ||
				 memcmp(name_text, origin_text, name_length)) ?
				pigen_symbol_resolve_text_at(analyzer->model, analyzer->scope,
					name_text, name_length,syntax->location.extent.first.index) :
				pigen_symbol_resolve_at(analyzer->model, analyzer->scope,
					spelling,syntax->location.extent.first.index);
			const pigen_symbol *known = pigen_symbol_get(analyzer->model, symbol);
			const pigen_semantic_signal *signal;
			if (known && known->kind == PIGEN_SYMBOL_PROTOCOL_ENDPOINT && !analyzer->constant_only)
			{
				const pigen_semantic_protocol_endpoint *endpoint = pigen_protocol_endpoint_get(
					analyzer->model, pigen_symbol_protocol_endpoint(analyzer->model, symbol));
				const pigen_protocol *protocol = endpoint ?
					pigen_protocol_type_get(analyzer->model, endpoint->type) : NULL;
				if (!protocol || protocol->family != PIGEN_PROTOCOL_AXI4_STREAM)
					return fail(analyzer, syntax->location, "AXI memory endpoint requires a channel selection");
				signal = pigen_signal_get(analyzer->model, endpoint->channels[PIGEN_CHANNEL_BEAT]);
				if (!signal) return fail(analyzer, syntax->location, "invalid AXI stream channel");
				symbol = signal->symbol;
				known = pigen_symbol_get(analyzer->model, symbol);
			}

			if (!known || (analyzer->constant_only &&
				known->kind != PIGEN_SYMBOL_PARAMETER) ||
				(!analyzer->constant_only &&
				known->kind != PIGEN_SYMBOL_PARAMETER &&
				known->kind != PIGEN_SYMBOL_SIGNAL))
				return fail(analyzer, syntax->location, pigen_symbol_import_ambiguous(analyzer->model,analyzer->scope,
     spelling,syntax->location.extent.first.index) ? "ambiguous wildcard package import" : "unknown value name");
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
		case PIGEN_SYNTAX_EXPR_MEMBER:
		{
			const pigen_syntax_expr *base_syntax = pigen_syntax_expr_get(
				&analyzer->syntax->expressions, syntax->as.member.base);
			pigen_symbol_id selected=scoped_symbol_reference(analyzer,syntax);
			const pigen_symbol *selected_symbol=pigen_symbol_get(analyzer->model,selected);
			if(selected_symbol && (selected_symbol->kind==PIGEN_SYMBOL_PARAMETER ||
				(!analyzer->constant_only && selected_symbol->kind==PIGEN_SYMBOL_SIGNAL)))
			{
				const pigen_semantic_signal *signal=selected_symbol->kind==PIGEN_SYMBOL_SIGNAL ?
					pigen_signal_get(analyzer->model,pigen_symbol_signal(analyzer->model,selected)) : NULL;
				if(signal && signal->is_automatic) return fail(analyzer,syntax->location,"automatic variables cannot be accessed by hierarchical names");
				node.kind=PIGEN_SYNTAX_EXPR_NAME;node.as.symbol=selected;node.data_type=selected_symbol->data_type;
				node.shape=signal ? signal->shape : pigen_semantic_scalar_shape(analyzer->model);
				node.constant=signal ? INVALID_ID(pigen_const_expr_id) :
					pigen_const_expr_intern_symbol(analyzer->model,selected,selected_symbol->data_type);
				return append_node(analyzer,node);
			}
			pigen_protocol_endpoint_id aggregate = protocol_reference(analyzer, base_syntax);
			const pigen_semantic_protocol_endpoint *native = pigen_protocol_endpoint_get(analyzer->model, aggregate);
			if (native && !syntax->as.member.scoped)
			{
				size_t length = 0;
				const char *member = pigen_source_span_text(analyzer->model->sources,
					token_spelling(analyzer, syntax->as.member.member), &length);
				for (pigen_protocol_channel channel = 0; channel < PIGEN_CHANNEL_COUNT; channel++)
				{
					const char *name = pigen_protocol_channel_name(channel);
					if (!member || strlen(name) != length || memcmp(member, name, length)) continue;
					const pigen_semantic_signal *signal = pigen_signal_get(analyzer->model, native->channels[channel]);
					if (!signal) return fail(analyzer, syntax->location, "channel is not present in this AXI protocol");
					node.kind = PIGEN_SYNTAX_EXPR_NAME;
					node.as.symbol = signal->symbol;
					node.data_type = signal->data_type;
					node.shape = signal->shape;
					return append_node(analyzer, node);
				}
				const pigen_protocol *protocol = pigen_protocol_type_get(analyzer->model, native->type);
				if (protocol->family != PIGEN_PROTOCOL_AXI4_STREAM)
					return fail(analyzer, syntax->location, "unknown AXI channel");
			}
			pigen_protocol_endpoint_id self = protocol_reference(analyzer, syntax);
			const pigen_semantic_protocol_endpoint *stream = pigen_protocol_endpoint_get(analyzer->model, self);
			if (stream)
			{
				const pigen_protocol *protocol = pigen_protocol_type_get(analyzer->model, stream->type);
				if (protocol->family != PIGEN_PROTOCOL_AXI4_STREAM)
					return fail(analyzer, syntax->location, "AXI memory endpoint requires a channel selection");
				const pigen_semantic_signal *signal = pigen_signal_get(analyzer->model, stream->channels[PIGEN_CHANNEL_BEAT]);
				node.kind = PIGEN_SYNTAX_EXPR_NAME;
				node.as.symbol = signal->symbol;
				node.data_type = signal->data_type;
				node.shape = signal->shape;
				return append_node(analyzer, node);
			}
			if (base_syntax && !syntax->as.member.scoped)
			{
				pigen_symbol_id owner_id=scoped_symbol_reference(analyzer,base_syntax);
				const pigen_symbol *owner = pigen_symbol_get(analyzer->model, owner_id);
				if (owner && owner->kind == PIGEN_SYMBOL_INSTANCE)
				{
					const pigen_semantic_instance *instance = pigen_instance_get(analyzer->model, owner->object.instance);
					const pigen_semantic_module *child = pigen_module_get(analyzer->model, instance->child_module);
					pigen_symbol_id port = pigen_symbol_lookup(analyzer->model, child->scope,
						token_spelling(analyzer, syntax->as.member.member));
					for (size_t c = 0; c < instance->connection_count; c++)
					{
						const pigen_semantic_instance_connection *connection = &analyzer->model->instance_connections[instance->first_connection+c];
						const pigen_semantic_expr *value;
						const pigen_semantic_signal *signal;
						if (connection->port_symbol.index != port.index) continue;
						/* Controls share the payload's port identity, but are not
						 * the value denoted by instance.payload. */
						size_t n = 0, m = 0;
						const char *connection_name = pigen_source_span_text(analyzer->model->sources,
							connection->port_name, &n);
						const pigen_symbol *port_record = pigen_symbol_get(analyzer->model, port);
						const char *port_name = port_record ? pigen_source_span_text(analyzer->model->sources,
							port_record->name, &m) : NULL;
						if (!connection_name || !port_name || n != m || memcmp(connection_name, port_name, n)) continue;
						if (connection->endpoint_view.index != PIGEN_INVALID_ID)
						{
							signal = pigen_signal_get(analyzer->model, connection->endpoint_view);
							node.kind = PIGEN_SYNTAX_EXPR_NAME;
							node.as.symbol = signal->symbol;
							node.data_type = signal->data_type;
							node.shape = signal->shape;
							node.constant = INVALID_ID(pigen_const_expr_id);
							return append_node(analyzer, node);
						}
						value = pigen_expr_get(analyzer->model, connection->value);
						while (value && value->kind == PIGEN_EXPR_CONVERSION)
							value = pigen_expr_get(analyzer->model, value->as.conversion.operand);
						if (!value || value->kind != PIGEN_EXPR_SYMBOL) break;
						signal = pigen_signal_get(analyzer->model, pigen_symbol_signal(analyzer->model,value->as.symbol));
						if (!signal) break;
						node.kind = PIGEN_SYNTAX_EXPR_NAME;
						node.as.symbol = value->as.symbol;
						node.data_type = signal->data_type;
						node.shape = signal->shape;
						node.constant = INVALID_ID(pigen_const_expr_id);
						return append_node(analyzer,node);
					}
					return fail(analyzer, syntax->location, "child port has no addressable parent endpoint");
				}
			}
			pigen_analyzed_expr_id base = analyze(analyzer,
				syntax->as.member.base);
			const pigen_analyzed_expr *base_known = pigen_analyzed_expr_get(
				analyzer->arena, base);
			pigen_symbol_id alias;
			const pigen_semantic_struct_member *member;
			uint64_t high;
			uint64_t low;
			pigen_analyzed_expr_id left;
			pigen_analyzed_expr_id right;

			if (base.index == PIGEN_INVALID_ID || !base_known)
				return INVALID_ID(pigen_analyzed_expr_id);
			const pigen_semantic_shape *shape = pigen_shape_get(analyzer->model,base_known->shape);
			if (shape && shape->dimension_count)
				return fail(analyzer,syntax->location,"record field access requires a scalar record; index the array first");
			alias = pigen_data_type_alias_symbol(analyzer->model,
				base_known->data_type);
			if (alias.index == PIGEN_INVALID_ID)
				return fail(analyzer, syntax->location,
					"member access requires a struct value");
			member = pigen_struct_member_for_type(analyzer->model, base_known->data_type,
				token_spelling(analyzer, syntax->as.member.member));
			if (!member)
				return fail(analyzer, syntax->location,
					"unknown struct member");
			high = member->offset + (uint64_t)0;
			{
				pigen_const_expr_id width = pigen_data_type_packed_width(
					analyzer->model, member->data_type);
				uint64_t bits = 0;

				if (!pigen_const_expr_evaluate_u64(analyzer->model, width,
					&bits) || bits == 0)
					return fail(analyzer, syntax->location,
						"invalid struct member width");
				high = member->offset + bits - 1;
				low = member->offset;
			}
			pigen_shape_id member_shape = base_known->shape;
			left = append_integer_literal(analyzer, high,
				syntax->location.source_span);
			right = append_integer_literal(analyzer, low,
				syntax->location.source_span);
			if (left.index == PIGEN_INVALID_ID ||
				right.index == PIGEN_INVALID_ID)
				return INVALID_ID(pigen_analyzed_expr_id);
			node.kind = PIGEN_SYNTAX_EXPR_SELECT;
			node.data_type = member->data_type;
			node.shape = member_shape;
			node.as.select.base = base;
			node.as.select.left = left;
			node.as.select.right = right;
			node.as.select.kind = PIGEN_SEMANTIC_SELECT_RANGE;
			node.as.select.is_member = 1;
			node.as.select.member_name = member->name;
			return append_node(analyzer, node);
		}
		case PIGEN_SYNTAX_EXPR_CALL:
		{
			const pigen_syntax_expr *callee = pigen_syntax_expr_get(
				&analyzer->syntax->expressions, syntax->as.call.callee);
			const pigen_syntax_expr_id *arguments;
			size_t name_length = 0;
			const char *name;

            const pigen_symbol *routine=pigen_symbol_get(analyzer->model,pigen_lookup_callee_symbol(analyzer->syntax,analyzer->model,analyzer->scope,syntax->as.call.callee));
            if(routine && routine->kind==PIGEN_SYMBOL_RETAINED_SUBROUTINE)
                return fail(analyzer,syntax->location,"retained package subroutine calls require ordinary SystemVerilog context");
            if(!callee || callee->kind!=PIGEN_SYNTAX_EXPR_NAME) return INVALID_ID(pigen_analyzed_expr_id);
			name = token_text(analyzer, callee, &name_length);
   if(name && name_length==4 && !memcmp(name,"peek",4)) return analyze_handshake_call(analyzer,syntax_id,syntax,PIGEN_EXPR_PEEK);
   if(name && name_length==5 && !memcmp(name,"valid",5)) return analyze_handshake_call(analyzer,syntax_id,syntax,PIGEN_EXPR_VALID);
   if(name && name_length==5 && !memcmp(name,"ready",5)) return analyze_handshake_call(analyzer,syntax_id,syntax,PIGEN_EXPR_READY);
   if(name && name_length==7 && !memcmp(name,"accepts",7)) return analyze_handshake_call(analyzer,syntax_id,syntax,PIGEN_EXPR_ACCEPTS);
			if(name && name_length==5 && !memcmp(name,"$bits",5)) return analyze_bits(analyzer,syntax_id,syntax);
   const char *array_queries[]={"$left","$right","$low","$high","$increment","$size","$dimensions","$unpacked_dimensions"};
   for(size_t i=0;name && i<sizeof array_queries/sizeof *array_queries;i++)
    if(strlen(array_queries[i])==name_length && !memcmp(name,array_queries[i],name_length))
     return analyze_array_query(analyzer,syntax_id,syntax,name,name_length);
			int signed_call=name && name_length==7 && !memcmp(name,"$signed",7);
			int unsigned_call=name && name_length==9 && !memcmp(name,"$unsigned",9);
            if(!name || (!signed_call && !unsigned_call && (name_length!=6 || memcmp(name,"$clog2",6))))
                return INVALID_ID(pigen_analyzed_expr_id);
			arguments = pigen_syntax_expr_children(
				&analyzer->syntax->expressions,
				syntax->as.call.first_argument,
				syntax->as.call.argument_count);
			if (!arguments || syntax->as.call.argument_count != 1)
				return INVALID_ID(pigen_analyzed_expr_id);
			{
				pigen_analyzed_expr_id argument = analyze(analyzer,
					arguments[0]);
				const pigen_analyzed_expr *known = pigen_analyzed_expr_get(
					analyzer->arena, argument);
				uint64_t value = 0;
				uint64_t bits = 0;
				if(known && (signed_call || unsigned_call))
				{
					if(!pigen_const_expr_evaluate_u64(analyzer->model,pigen_data_type_packed_width(analyzer->model,known->data_type),&bits) || !bits || bits>SIZE_MAX)
						return fail(analyzer,syntax->location,"signedness call requires a concrete integral width");
					pigen_data_type_id source=known->data_type;node.shape=known->shape;
					node.kind=PIGEN_SYNTAX_EXPR_CAST;
					node.data_type=pigen_data_type_sized_integral(analyzer->model,(size_t)bits,signed_call ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED,pigen_data_type_state_domain(analyzer->model,source));
					if(!pigen_data_type_resolve_explicit_conversion(analyzer->model,source,node.data_type,&node.as.conversion.conversion))
						return fail(analyzer,syntax->location,"invalid signedness call");
					node.as.conversion.operand=argument;
					node.constant=converted_constant(analyzer,analyzer->arena->nodes[argument.index].constant,node.as.conversion.conversion);
					return append_node(analyzer,node);
				}

				if (!known || known->constant.index == PIGEN_INVALID_ID ||
					!pigen_const_expr_evaluate_u64(analyzer->model,
						known->constant, &value))
					return INVALID_ID(pigen_analyzed_expr_id);
				value = value > 0 ? value - 1 : 0;
				while (value && (bits < 64))
				{
					value >>= 1;
					bits++;
				}
				return append_integer_literal(analyzer, bits,
					syntax->location.source_span);
			}
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
			if (!(analyzer->literal_domain==PIGEN_LITERAL_DOMAIN_CASE ? pigen_data_type_resolve_case_unary_operation : pigen_data_type_resolve_unary_operation)(analyzer->model,
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

			if (!pigen_binary_operator_from_syntax(syntax->as.binary.operator, &operator))
				return fail(analyzer, syntax->as.binary.operator_location,
					"unsupported binary operator");
			left = analyze(analyzer, syntax->as.binary.left);
			right = analyze(analyzer, syntax->as.binary.right);
			if (left.index == PIGEN_INVALID_ID || right.index == PIGEN_INVALID_ID)
				return INVALID_ID(pigen_analyzed_expr_id);
			if (analyzer->literal_domain==PIGEN_LITERAL_DOMAIN_CASE)
			{
				pigen_numerical_interpretation li=pigen_data_type_numerical_interpretation(analyzer->model,analyzer->arena->nodes[left.index].data_type);
				pigen_numerical_interpretation ri=pigen_data_type_numerical_interpretation(analyzer->model,analyzer->arena->nodes[right.index].data_type);
				analyzer->literal_domain=PIGEN_LITERAL_DOMAIN_PIGEN;
				if ((li==PIGEN_NUMERICAL_SIGNED_INTEGER || li==PIGEN_NUMERICAL_UNSIGNED_INTEGER) &&
					pigen_syntax_expr_is_decimal_constant(analyzer->syntax->expanded,&analyzer->syntax->expressions,syntax->as.binary.right)) right=analyze(analyzer,syntax->as.binary.right);
				if ((ri==PIGEN_NUMERICAL_SIGNED_INTEGER || ri==PIGEN_NUMERICAL_UNSIGNED_INTEGER) &&
					pigen_syntax_expr_is_decimal_constant(analyzer->syntax->expanded,&analyzer->syntax->expressions,syntax->as.binary.left)) left=analyze(analyzer,syntax->as.binary.left);
				analyzer->literal_domain=PIGEN_LITERAL_DOMAIN_CASE;
				if (left.index==PIGEN_INVALID_ID || right.index==PIGEN_INVALID_ID) return INVALID_ID(pigen_analyzed_expr_id);
			}
			if (analyzer->arena->nodes[left.index].shape.index !=
				analyzer->arena->nodes[right.index].shape.index)
				return fail(analyzer, syntax->as.binary.operator_location,
					"binary operands have different shapes");
			if (reject_excessive_exact_amount(analyzer,
				analyzer->arena->nodes[left.index].data_type,
				analyzer->arena->nodes[right.index].data_type, operator,
				syntax->as.binary.operator_location))
				return INVALID_ID(pigen_analyzed_expr_id);
			if (!(analyzer->literal_domain==PIGEN_LITERAL_DOMAIN_CASE ? pigen_data_type_resolve_case_binary_operation : pigen_data_type_resolve_binary_operation)(analyzer->model,
				operator, analyzer->arena->nodes[left.index].data_type,
				analyzer->arena->nodes[right.index].data_type,
				&node.as.binary.resolution))
				return fail(analyzer, syntax->as.binary.operator_location,
					"invalid operands for binary operator");
			node.as.binary.left = left;
			node.as.binary.right = right;
			node.data_type = node.as.binary.resolution.operation.result_data_type;
			node.shape = analyzer->arena->nodes[left.index].shape;
			if (!check_generated_width(analyzer, node.data_type, operator,
				syntax->as.binary.operator_location))
				return INVALID_ID(pigen_analyzed_expr_id);
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
			pigen_literal_domain saved=analyzer->literal_domain;
			if (saved==PIGEN_LITERAL_DOMAIN_CASE) analyzer->literal_domain=pigen_case_cast_literal_domain(analyzer->syntax,analyzer->model,analyzer->scope,syntax);
			if(saved==PIGEN_LITERAL_DOMAIN_CASE && pigen_expression_contains_unbased_fill(analyzer->syntax,syntax->as.cast.value)) analyzer->literal_domain=PIGEN_LITERAL_DOMAIN_CASE;
			operand = analyze(analyzer, syntax->as.cast.value);
			analyzer->literal_domain=saved;
			if (operand.index == PIGEN_INVALID_ID) return operand;
			pigen_data_type_id target = pigen_resolve_cast_type(analyzer->syntax,
				analyzer->model, analyzer->scope, syntax,
				analyzer->arena->nodes[operand.index].data_type, analyzer->policy, analyzer->error);
			if (target.index == PIGEN_INVALID_ID)
				return INVALID_ID(pigen_analyzed_expr_id);
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
			int is_array = 0;
			pigen_protocol_endpoint_id selected = protocol_index_reference(analyzer, syntax, &is_array);
			if (is_array)
			{
				const pigen_semantic_protocol_endpoint *endpoint = pigen_protocol_endpoint_get(analyzer->model, selected);
				if (!endpoint) return INVALID_ID(pigen_analyzed_expr_id);
				const pigen_protocol *protocol = pigen_protocol_type_get(analyzer->model, endpoint->type);
				if (!protocol || protocol->family != PIGEN_PROTOCOL_AXI4_STREAM)
					return fail(analyzer, syntax->location, "AXI memory endpoint requires a channel selection");
				const pigen_semantic_signal *signal = pigen_signal_get(analyzer->model, endpoint->channels[PIGEN_CHANNEL_BEAT]);
				if (!signal) return fail(analyzer, syntax->location, "invalid AXI stream lane");
				node.kind = PIGEN_SYNTAX_EXPR_NAME; node.as.symbol = signal->symbol;
				node.data_type = signal->data_type; node.shape = signal->shape;
				return append_node(analyzer, node);
			}
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
		case PIGEN_SYNTAX_EXPR_REPLICATION:
		case PIGEN_SYNTAX_EXPR_CONCATENATION:
		{
			if(!analyzer->constant_only) {int empty=0;return analyze_runtime_sequence(analyzer,syntax_id,0,&empty);}
			int replicated = syntax->kind == PIGEN_SYNTAX_EXPR_REPLICATION;
			size_t body_count = replicated ? syntax->as.replication.child_count : syntax->as.sequence.child_count;
			size_t first = replicated ? syntax->as.replication.first_child : syntax->as.sequence.first_child;
			uint64_t repeat = 1;
			if (replicated)
			{
				if (!analyzer->policy || !analyzer->policy->maximum_generated_bits)
					return fail(analyzer, syntax->location, "replication requires a generated-size policy");
				/* Counts retain the ordinary replication count domain. The
				 * temporary analyzed count never publishes expressions. */
				expression_analyzer counter = *analyzer;
				counter.constant_only = 1;
				counter.literal_domain = PIGEN_LITERAL_DOMAIN_PIGEN;
				pigen_analyzed_expr_id count = analyze(&counter, syntax->as.replication.count);
				if (count.index == PIGEN_INVALID_ID ||
					!pigen_const_expr_evaluate_u64(analyzer->model, analyzer->arena->nodes[count.index].constant, &repeat) ||
					!repeat || repeat > analyzer->policy->maximum_generated_bits)
					return fail(analyzer, syntax->location, "replication requires a positive bounded constant count");
			}
			if (!body_count || (replicated && body_count > analyzer->policy->maximum_generated_bits / repeat) ||
				repeat > SIZE_MAX / body_count || body_count * (size_t)repeat > SIZE_MAX / sizeof(pigen_analyzed_expr_id) ||
				body_count * (size_t)repeat > SIZE_MAX / sizeof(pigen_data_type_id) ||
				body_count * (size_t)repeat > SIZE_MAX / sizeof(pigen_const_expr_id))
				return fail(analyzer, syntax->location, "invalid concatenation size");
			size_t child_count = body_count * (size_t)repeat;
			const pigen_syntax_expr_id *syntax_children =
				pigen_syntax_expr_children(&analyzer->syntax->expressions, first, body_count);
			pigen_analyzed_expr_id *children;
			pigen_data_type_id *types;
			pigen_const_expr_id *constants;
			size_t i;
			int all_constant = 1;

			if (!syntax_children || !body_count)
				return fail(analyzer, syntax->location, "invalid concatenation");
			children = pigen_resize(NULL,
				child_count * sizeof(*children));
			types = pigen_resize(NULL,
				child_count * sizeof(*types));
			constants = pigen_resize(NULL,
				child_count * sizeof(*constants));
			size_t kept=0;
			for (i = 0; i < body_count; i++)
			{
				int empty=constant_empty_replication(analyzer,syntax_children[i]);
				if (empty<0)
				{free(children);free(types);free(constants);return fail(analyzer,syntax->location,"invalid zero-count replication operand");}
				if (empty) continue;
				const pigen_semantic_shape *shape;
				children[kept] = analyze(analyzer, syntax_children[i]);
				if (children[kept].index == PIGEN_INVALID_ID ||
					!(shape = pigen_shape_get(analyzer->model,
						analyzer->arena->nodes[children[kept].index].shape)) ||
					shape->dimension_count)
				{
					free(children);
					free(types);
					free(constants);
					return fail(analyzer, syntax->location,
						"invalid concatenation element");
				}
				types[kept] = analyzer->arena->nodes[children[kept].index].data_type;
				constants[kept] = analyzer->arena->nodes[children[kept].index].constant;
				if (constants[kept].index == PIGEN_INVALID_ID) all_constant = 0;
				kept++;
			}
			if (!kept)
			{free(children);free(types);free(constants);return fail(analyzer,syntax->location,"concatenation requires a positive-sized operand");}
			body_count=kept;child_count=body_count*(size_t)repeat;
			for (i = body_count; i < child_count; i++)
			{
				children[i] = children[i % body_count];
				types[i] = types[i % body_count];
				constants[i] = constants[i % body_count];
			}
			/* Repetition expands references in the temporary arena; the
			 * normal concatenation owners retain typing/materialization. */
			node.kind = PIGEN_SYNTAX_EXPR_CONCATENATION;
			node.as.sequence.first_child = append_children(analyzer, children,
				child_count);
			node.as.sequence.child_count = child_count;
			node.data_type = pigen_data_type_concatenation(analyzer->model,
				types, child_count);
			node.shape = pigen_semantic_scalar_shape(analyzer->model);
			node.constant = all_constant ?
				pigen_const_expr_intern_concatenation(analyzer->model, constants,
					child_count, node.data_type) :
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
	pigen_literal_domain literal_domain,
	const pigen_resolve_policy *policy,
	pigen_analyzed_expr_arena *arena, pigen_analyzed_expr_id *result,
	pigen_semantic_error *error)
{
	expression_analyzer analyzer;

	if (result) *result = INVALID_ID(pigen_analyzed_expr_id);
	if (!syntax || !syntax->expanded || !model || !arena || !result ||
		!pigen_scope_get(model, scope) ||
		(literal_domain != PIGEN_LITERAL_DOMAIN_PIGEN &&
		literal_domain != PIGEN_LITERAL_DOMAIN_SYSTEMVERILOG &&
		 literal_domain != PIGEN_LITERAL_DOMAIN_CASE))
		return 0;
	analyzer.syntax = syntax;
	analyzer.model = model;
	analyzer.scope = scope;
	analyzer.constant_only = constant_only;
	analyzer.literal_domain = literal_domain;
	analyzer.policy = policy;
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
	free(arena->constraints);
	memset(arena, 0, sizeof(*arena));
}
