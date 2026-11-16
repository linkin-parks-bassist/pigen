/* Public expression-resolution orchestration and semantic materialization. */
#include <stdlib.h>
#include <string.h>

#include "pigen/integer.h"
#include "pigen/expression_analysis.h"
#include "pigen/expression_resolve.h"
#include "pigen/type_resolve.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

typedef struct {
	pigen_semantic_model *model;
	const pigen_analyzed_expr_arena *arena;
	int case_mode;
	const pigen_syntax_tree *syntax;
} expression_materializer;

static pigen_expr_id record_failure(pigen_semantic_error *error,
	pigen_syntax_location location,const char *message);

static int assignment_fill(const pigen_syntax_tree *syntax,const pigen_syntax_expr *node)
{
	if(!node || node->kind!=PIGEN_SYNTAX_EXPR_LITERAL) return -1;
	const pigen_expanded_token *token=pigen_expanded_token_get(syntax->expanded,node->as.atom.token);
	size_t length=0;const char *text=pigen_expanded_token_text(syntax->expanded,token,&length);
	if(!text || length!=2 || text[0]!='\'') return -1;
	switch(text[1])
	{
		case '0': return PIGEN_BIT_ZERO;
		case '1': return PIGEN_BIT_ONE;
		case 'x': case 'X': return PIGEN_BIT_X;
		case 'z': case 'Z': return PIGEN_BIT_Z;
		default: return -1;
	}
}

static pigen_expr_id resolve_assignment_fill(pigen_semantic_model *model,
	const pigen_syntax_expr *node,pigen_data_type_id target,int fill,
	const pigen_resolve_policy *policy,pigen_semantic_error *error)
{
	uint64_t width;
	if(!pigen_data_type_is_integral(model,target) ||
		!pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,target),&width) || !width ||
		width>SIZE_MAX/sizeof(pigen_bit_state) || (policy && width>policy->maximum_generated_bits))
		return record_failure(error,node->location,"assignment fill requires a bounded integral destination");
	if(pigen_data_type_state_domain(model,target)==PIGEN_DATA_TYPE_STATE_TWO &&
		(fill==PIGEN_BIT_X || fill==PIGEN_BIT_Z)) fill=PIGEN_BIT_ZERO;
	pigen_bit_state *states=pigen_resize(NULL,(size_t)width*sizeof(*states));
	for(size_t i=0;i<(size_t)width;i++) states[i]=(pigen_bit_state)fill;
	pigen_expr_id result=pigen_expr_add_bits(model,states,(size_t)width,target,node->location.source_span);
	free(states);return result;
}

static int contains_contextual_record(const pigen_syntax_tree *syntax,
	pigen_syntax_expr_id expression);
static int contains_assignment_fill(const pigen_syntax_tree *syntax,
	pigen_syntax_expr_id expression);
static pigen_expr_id selection_fill_context(pigen_semantic_model *model,pigen_expr_id value);

static pigen_expr_id materialize(expression_materializer *materializer,
	pigen_analyzed_expr_id expression);

static pigen_expr_id materialize_conversion(
	expression_materializer *materializer, pigen_expr_id operand,
	pigen_conversion conversion, pigen_source_span span)
{
	if (operand.index == PIGEN_INVALID_ID) return operand;
	if (conversion.kind == PIGEN_CONVERSION_IDENTITY) return operand;
	return pigen_expr_add_conversion(materializer->model, conversion, operand,
		span);
}

static pigen_expr_id materialize_sequence(
	expression_materializer *materializer, const pigen_analyzed_expr *known)
{
	pigen_expr_id *children;
	pigen_expr_id result;
	size_t i;

	if (known->as.sequence.first_child > materializer->arena->child_count ||
		known->as.sequence.child_count > materializer->arena->child_count -
			known->as.sequence.first_child)
		return INVALID_ID(pigen_expr_id);
	children = pigen_resize(NULL,
		known->as.sequence.child_count * sizeof(*children));
	for (i = 0; i < known->as.sequence.child_count; i++)
	{
		children[i] = materialize(materializer,
			materializer->arena->children[
				known->as.sequence.first_child + i]);
		if (children[i].index == PIGEN_INVALID_ID)
		{
			free(children);
			return INVALID_ID(pigen_expr_id);
		}
	}
	result = pigen_expr_add_concatenation(materializer->model, children,
		known->as.sequence.child_count, known->span);
	free(children);
	return result;
}

static pigen_expr_id materialize(expression_materializer *materializer,
	pigen_analyzed_expr_id expression)
{
	const pigen_analyzed_expr *known = pigen_analyzed_expr_get(
		materializer->arena, expression);
	pigen_expr_id operand;
	pigen_expr_id left;
	pigen_expr_id right;
	pigen_expr_id condition;
	pigen_expr_id when_true;
	pigen_expr_id when_false;

	if (!known) return INVALID_ID(pigen_expr_id);
	if(known->form==PIGEN_ANALYZED_CASE_VALUE)
	{
		expression_materializer local=*materializer;local.case_mode=1;
		pigen_expr_id value=materialize(&local,known->as.group.operand);
		if(value.index!=PIGEN_INVALID_ID && pigen_expression_contains_unbased_fill(materializer->syntax,known->syntax))
			value=selection_fill_context(materializer->model,value);
		return value;
	}
	if(known->form==PIGEN_ANALYZED_EVALUATED_VALUE)
	{
		pigen_expr_id value,evaluated;
		if(known->as.evaluation.before)
		{evaluated=materialize(materializer,known->as.evaluation.evaluated);value=materialize(materializer,known->as.evaluation.value);}
		else
		{value=materialize(materializer,known->as.evaluation.value);evaluated=materialize(materializer,known->as.evaluation.evaluated);}
		return pigen_expr_add_evaluated_value(materializer->model,value,evaluated,known->as.evaluation.before,known->span);
	}
	if(known->form!=PIGEN_ANALYZED_INTRINSIC) return INVALID_ID(pigen_expr_id);
	switch (known->kind)
	{
  case PIGEN_SYNTAX_EXPR_CALL:
   if(known->as.call.kind==PIGEN_EXPR_VALID || known->as.call.kind==PIGEN_EXPR_READY)
    return pigen_expr_add_control(materializer->model,known->as.call.kind,known->as.call.signal,known->span);
   if(known->as.call.kind==PIGEN_EXPR_PEEK)
    return pigen_expr_add_peek(materializer->model,materialize(materializer,known->as.call.first),known->span);
   if(known->as.call.kind==PIGEN_EXPR_ACCEPTS)
   {
    pigen_expr_id destination=materialize(materializer,known->as.call.first);
    pigen_expr_id source=materialize(materializer,known->as.call.second);
    return pigen_expr_add_accepts(materializer->model,pigen_lvalue_resolve(materializer->model,destination),source,known->as.call.signal,known->span);
   }
   return INVALID_ID(pigen_expr_id);
		case PIGEN_SYNTAX_EXPR_LITERAL:
			if (known->literal_kind == PIGEN_ANALYZED_LITERAL_BITS)
			{
				pigen_expr_id value=pigen_expr_add_bits(materializer->model,
					materializer->arena->literal_states+known->as.bits.first_state,
					known->as.bits.state_count,known->data_type,known->span);
				if (value.index!=PIGEN_INVALID_ID && materializer->case_mode)
					materializer->model->expressions[value.index].unbased_fill=known->unbased_fill;
				return value;
			}
			if (known->literal_kind == PIGEN_ANALYZED_LITERAL_EXACT_INTEGER)
				return pigen_expr_add_exact_integer(materializer->model,
					known->as.exact_integer, known->span);
			if (known->literal_kind == PIGEN_ANALYZED_LITERAL_INTEGER)
				return pigen_expr_add_integer(materializer->model,
					known->as.integer, known->data_type, known->span);
			return INVALID_ID(pigen_expr_id);
		case PIGEN_SYNTAX_EXPR_NAME:
			return pigen_expr_add_symbol(materializer->model, known->as.symbol,
				known->data_type, known->span);
		case PIGEN_SYNTAX_EXPR_GROUP:
			operand = materialize(materializer, known->as.group.operand);
			return pigen_expr_add_group(materializer->model, operand, known->span);
		case PIGEN_SYNTAX_EXPR_UNARY:
			operand = materialize(materializer, known->as.unary.operand);
			operand = materialize_conversion(materializer, operand,
				known->as.unary.resolution.operand_conversion, known->span);
			return pigen_expr_add_unary(materializer->model,
				known->as.unary.resolution.operation, operand, known->span);
		case PIGEN_SYNTAX_EXPR_BINARY:
			left = materialize(materializer, known->as.binary.left);
			right = materialize(materializer, known->as.binary.right);
			left = materialize_conversion(materializer, left,
				known->as.binary.resolution.left_conversion, known->span);
			right = materialize_conversion(materializer, right,
				known->as.binary.resolution.right_conversion, known->span);
			return pigen_expr_add_binary(materializer->model,
				known->as.binary.resolution.operation, left, right, known->span);
		case PIGEN_SYNTAX_EXPR_CONDITIONAL:
			condition = materialize(materializer,
				known->as.conditional.condition);
			when_true = materialize(materializer,
				known->as.conditional.when_true);
			when_false = materialize(materializer,
				known->as.conditional.when_false);
			condition = materialize_conversion(materializer, condition,
				known->as.conditional.resolution.condition_conversion, known->span);
			when_true = materialize_conversion(materializer, when_true,
				known->as.conditional.resolution.when_true_conversion, known->span);
			when_false = materialize_conversion(materializer, when_false,
				known->as.conditional.resolution.when_false_conversion, known->span);
			return pigen_expr_add_conditional(materializer->model,
				known->as.conditional.resolution.operation, condition, when_true,
				when_false, known->span);
		case PIGEN_SYNTAX_EXPR_CAST:
		{
			operand = materialize(materializer, known->as.conversion.operand);
			pigen_expr_id value=materialize_conversion(materializer,operand,known->as.conversion.conversion,known->span);
			if (value.index!=PIGEN_INVALID_ID && materializer->case_mode)
			{
				if (value.index==operand.index) value=pigen_expr_add_group(materializer->model,value,known->span);
				if (value.index!=PIGEN_INVALID_ID) materializer->model->expressions[value.index].is_context_cast=1;
			}
			return value;
		}
		case PIGEN_SYNTAX_EXPR_INDEX:
			left = materialize(materializer, known->as.index.base);
			right = materialize(materializer, known->as.index.index);
			if(materializer->case_mode && pigen_expression_contains_unbased_fill(materializer->syntax,materializer->arena->nodes[known->as.index.base.index].syntax))
				left=selection_fill_context(materializer->model,left);
			if(materializer->case_mode && pigen_expression_contains_unbased_fill(materializer->syntax,materializer->arena->nodes[known->as.index.index.index].syntax))
			{
				right=selection_fill_context(materializer->model,right);
			}
			return pigen_expr_add_index(materializer->model, left, right,
				known->span);
		case PIGEN_SYNTAX_EXPR_SELECT:
			operand = materialize(materializer, known->as.select.base);
			left = materialize(materializer, known->as.select.left);
			right = materialize(materializer, known->as.select.right);
			if(materializer->case_mode && pigen_expression_contains_unbased_fill(materializer->syntax,materializer->arena->nodes[known->as.select.base.index].syntax))
				operand=selection_fill_context(materializer->model,operand);
			if(materializer->case_mode && pigen_expression_contains_unbased_fill(materializer->syntax,materializer->arena->nodes[known->as.select.left.index].syntax))
				left=selection_fill_context(materializer->model,left);
			if(materializer->case_mode && pigen_expression_contains_unbased_fill(materializer->syntax,materializer->arena->nodes[known->as.select.right.index].syntax))
				right=selection_fill_context(materializer->model,right);
			if (known->as.select.is_member)
				return pigen_expr_add_member_select(materializer->model,operand,left,right,
					known->data_type,known->as.select.member_name,known->span);
			return pigen_expr_add_typed_select(materializer->model, operand, left,
				right, known->as.select.kind, known->data_type, known->span);
		case PIGEN_SYNTAX_EXPR_CONCATENATION:
			return materialize_sequence(materializer, known);
		default:
			return INVALID_ID(pigen_expr_id);
	}
}

static pigen_expr_id resolve_with_policy(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression, int constant_only,
	pigen_literal_domain literal_domain,
	const pigen_resolve_policy *policy, pigen_semantic_error *error)
{
	pigen_analyzed_expr_arena arena = {0};
	pigen_analyzed_expr_id analyzed;
	expression_materializer materializer;
	pigen_expr_id result;
	size_t expression_count;
	size_t child_count;
	size_t constraint_count;
	size_t i;

	if (!policy || !policy->maximum_generated_bits ||
		!pigen_analyze_expression(syntax, model, scope, expression,
			constant_only, literal_domain, policy, &arena,
			&analyzed, error))
	{
		pigen_free_analyzed_expr_arena(&arena);
		return INVALID_ID(pigen_expr_id);
	}
	expression_count = model->expression_count;
	child_count = model->expression_child_count;
	constraint_count = model->width_constraint_count;
	materializer.model = model;
	materializer.arena = &arena;
	materializer.case_mode = literal_domain==PIGEN_LITERAL_DOMAIN_CASE;
	materializer.syntax = syntax;
	result = materialize(&materializer, analyzed);
	if(result.index!=PIGEN_INVALID_ID && constant_only && literal_domain==PIGEN_LITERAL_DOMAIN_CASE)
		result=selection_fill_context(model,result);
	for (i = 0; result.index != PIGEN_INVALID_ID &&
		i < arena.constraint_count; i++)
		if (!pigen_width_constraint_add(model, arena.constraints[i].width,
			arena.constraints[i].maximum_bits, arena.constraints[i].span))
			result = INVALID_ID(pigen_expr_id);
	if (result.index == PIGEN_INVALID_ID)
	{
		model->expression_count = expression_count;
		model->expression_child_count = child_count;
		model->width_constraint_count = constraint_count;
		if (error && !error->message)
		{
			error->span = arena.nodes[analyzed.index].span;
			error->message = "internal expression materialization failure";
		}
	}
	pigen_free_analyzed_expr_arena(&arena);
	return result;
}

static int call_is_intrinsic(const pigen_syntax_tree *syntax,
	pigen_syntax_expr_id expression)
{
	const pigen_syntax_expr *call = pigen_syntax_expr_get(
		&syntax->expressions, expression);
	const pigen_syntax_expr *callee;
	const pigen_expanded_token *token;
	const char *name;
	size_t length = 0;

	if (!call || call->kind != PIGEN_SYNTAX_EXPR_CALL) return 0;
	callee = pigen_syntax_expr_get(&syntax->expressions, call->as.call.callee);
	if (!callee || callee->kind != PIGEN_SYNTAX_EXPR_NAME) return 0;
	token = pigen_expanded_token_get(syntax->expanded, callee->as.atom.token);
	name = token ? pigen_expanded_token_text(syntax->expanded, token, &length) :
		NULL;
	return name && ((length == 7 && !memcmp(name, "accepts", 7)) ||
		(length == 5 && !memcmp(name, "valid", 5)) ||
		(length == 5 && !memcmp(name, "ready", 5)) ||
		(length == 4 && !memcmp(name, "peek", 4)) ||
		(length == 7 && !memcmp(name, "$signed", 7)) ||
		(length == 9 && !memcmp(name, "$unsigned", 9)));
}

static int requires_direct_resolution(const pigen_syntax_tree *syntax,
	pigen_syntax_expr_id expression)
{
	const pigen_syntax_expr *known = pigen_syntax_expr_get(
		&syntax->expressions, expression);

	if (!known) return 0;
	if (known->kind == PIGEN_SYNTAX_EXPR_CALL)
		return call_is_intrinsic(syntax, expression) || contains_assignment_fill(syntax,expression);
	if (known->kind == PIGEN_SYNTAX_EXPR_GROUP)
		return requires_direct_resolution(syntax, known->as.group.operand);
	if (known->kind == PIGEN_SYNTAX_EXPR_UNARY)
		return requires_direct_resolution(syntax, known->as.unary.operand);
	if (known->kind == PIGEN_SYNTAX_EXPR_BINARY)
		return requires_direct_resolution(syntax, known->as.binary.left) ||
			requires_direct_resolution(syntax, known->as.binary.right);
	if (known->kind == PIGEN_SYNTAX_EXPR_INDEX)
		return contains_assignment_fill(syntax,expression) || requires_direct_resolution(syntax, known->as.index.base) || requires_direct_resolution(syntax, known->as.index.index);
	if (known->kind == PIGEN_SYNTAX_EXPR_SELECT)
		return contains_assignment_fill(syntax,expression) || requires_direct_resolution(syntax, known->as.select.base) || requires_direct_resolution(syntax, known->as.select.left) || requires_direct_resolution(syntax, known->as.select.right);
	if (known->kind == PIGEN_SYNTAX_EXPR_MEMBER && !known->as.member.scoped)
		return requires_direct_resolution(syntax, known->as.member.base);
	if (known->kind == PIGEN_SYNTAX_EXPR_CAST)
		return contains_contextual_record(syntax, known->as.cast.value) ||
			contains_assignment_fill(syntax, known->as.cast.value) ||
			requires_direct_resolution(syntax, known->as.cast.value);
	if (known->kind == PIGEN_SYNTAX_EXPR_CONCATENATION || known->kind == PIGEN_SYNTAX_EXPR_REPLICATION)
	{
		/* Filled children retain the dedicated local argument context. */
		if(contains_assignment_fill(syntax,expression)) return 1;
		size_t first = known->kind == PIGEN_SYNTAX_EXPR_CONCATENATION ? known->as.sequence.first_child : known->as.replication.first_child;
		size_t count = known->kind == PIGEN_SYNTAX_EXPR_CONCATENATION ? known->as.sequence.child_count : known->as.replication.child_count;
		const pigen_syntax_expr_id *children = pigen_syntax_expr_children(&syntax->expressions, first, count);
		for (size_t i = 0; children && i < count; i++)
			if (requires_direct_resolution(syntax, children[i])) return 1;
	}
	if (known->kind == PIGEN_SYNTAX_EXPR_CONDITIONAL)
		return requires_direct_resolution(syntax, known->as.conditional.condition) ||
			requires_direct_resolution(syntax, known->as.conditional.when_true) ||
			requires_direct_resolution(syntax, known->as.conditional.when_false);
	return 0;
}

static pigen_expr_id intrinsic_conversion(pigen_semantic_model *model,
	pigen_expr_id value, pigen_conversion conversion, pigen_source_span span)
{
	return conversion.kind == PIGEN_CONVERSION_IDENTITY ? value :
		pigen_expr_add_conversion(model, conversion, value, span);
}

static pigen_expr_id case_coerce(pigen_semantic_model *model,pigen_expr_id value,
    size_t width,pigen_signedness sign,pigen_source_span span)
{
    const pigen_semantic_expr *expression=pigen_expr_get(model,value);
    if (expression && pigen_data_type_numerical_interpretation(model,expression->data_type)==PIGEN_NUMERICAL_EXACT_INTEGER)
    {
        pigen_data_type_id type=pigen_data_type_sized_integral(model,width,sign,PIGEN_DATA_TYPE_STATE_TWO);
        pigen_conversion conversion;
        return pigen_data_type_resolve_explicit_conversion(model,expression->data_type,type,&conversion) ? intrinsic_conversion(model,value,conversion,span) : INVALID_ID(pigen_expr_id);
    }
    uint64_t original;
    if (!expression || !pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,expression->data_type),&original) || !original || original>SIZE_MAX)
        return INVALID_ID(pigen_expr_id);
    pigen_state_domain state=pigen_data_type_state_domain(model,expression->data_type);
    pigen_data_type_id views[2]={pigen_data_type_sized_integral(model,(size_t)original,sign,state),
        pigen_data_type_sized_integral(model,width,sign,state)};
    for(size_t i=0;i<2;i++)
    {
        expression=pigen_expr_get(model,value);pigen_conversion conversion;
        if (!expression || !pigen_data_type_resolve_explicit_conversion(model,expression->data_type,views[i],&conversion)) return INVALID_ID(pigen_expr_id);
        value=intrinsic_conversion(model,value,conversion,span);
    }
    return value;
}

static pigen_expr_id case_context(pigen_semantic_model *model,pigen_expr_id value,
    size_t width,pigen_signedness sign,size_t depth);

static pigen_expr_id case_vector_truth(pigen_semantic_model *model,pigen_expr_id value,pigen_source_span span)
{
    const pigen_semantic_expr *known=pigen_expr_get(model,value);uint64_t width=0;
    if(!known || !pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,known->data_type),&width) || !width) return INVALID_ID(pigen_expr_id);
    if(width==1) return value;
    pigen_unary_resolution resolution;
    if(!pigen_data_type_resolve_case_unary_operation(model,PIGEN_UNARY_REDUCTION_OR,known->data_type,&resolution)) return INVALID_ID(pigen_expr_id);
    return pigen_expr_add_unary(model,resolution.operation,value,span);
}

static pigen_expr_id case_self_context(pigen_semantic_model *model,pigen_expr_id value,size_t depth)
{
    const pigen_semantic_expr *expression=pigen_expr_get(model,value);uint64_t width;
    if (!expression || !pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,expression->data_type),&width) || !width || width>SIZE_MAX)
        return INVALID_ID(pigen_expr_id);
    return case_context(model,value,(size_t)width,pigen_data_type_signedness(model,expression->data_type)==PIGEN_SIGN_SIGNED ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED,depth);
}

/* Size selection operands before constructing result width/coordinates,
 * retaining the base's declared packed dimensions and aliases. */
static pigen_expr_id selection_fill_context(pigen_semantic_model *model,pigen_expr_id value)
{
    const pigen_semantic_expr *known=pigen_expr_get(model,value);
    if(!known) return INVALID_ID(pigen_expr_id);
    if(pigen_data_type_numerical_interpretation(model,known->data_type)==PIGEN_NUMERICAL_EXACT_INTEGER) return value;
    pigen_data_type_id type=known->data_type;pigen_source_span span=known->span;
    value=case_self_context(model,value,0);known=pigen_expr_get(model,value);
    pigen_conversion conversion;
    if(!known || !pigen_data_type_resolve_explicit_conversion(model,known->data_type,type,&conversion)) return INVALID_ID(pigen_expr_id);
    return intrinsic_conversion(model,value,conversion,span);
}

static pigen_expr_id selection_fill_index(pigen_semantic_model *model,pigen_expr_id base,pigen_expr_id value)
{
    const pigen_semantic_expr *array=pigen_expr_get(model,base),*index=pigen_expr_get(model,value);
    const pigen_packed_dimension *dimensions=array ? pigen_data_type_dimensions(model,array->data_type) : NULL;
    int64_t left,right;uint64_t width;
    if(!index || !dimensions || !pigen_data_type_dimension_count(model,array->data_type) ||
        !pigen_const_expr_evaluate_i64(model,dimensions[0].left,&left) ||
        !pigen_const_expr_evaluate_i64(model,dimensions[0].right,&right) ||
        !pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,index->data_type),&width)) return value;
    uint64_t a=left<0 ? (uint64_t)(-(left+1))+1 : (uint64_t)left;
    uint64_t b=right<0 ? (uint64_t)(-(right+1))+1 : (uint64_t)right;
    uint64_t maximum=a>b ? a : b;size_t needed=1;
    while(maximum>>=1) needed++;
    if(width>=needed) return value;
    pigen_data_type_id type=pigen_data_type_sized_integral(model,needed,
        pigen_data_type_signedness(model,index->data_type)==PIGEN_SIGN_SIGNED ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED,
        pigen_data_type_state_domain(model,index->data_type));
    pigen_conversion conversion;
    return pigen_data_type_resolve_explicit_conversion(model,index->data_type,type,&conversion) ? intrinsic_conversion(model,value,conversion,index->span) : INVALID_ID(pigen_expr_id);
}

/* SV propagates the common case comparison context through native arithmetic,
 * while casts, counts, concatenations and Pigen numerical subtrees keep their
 * own evaluation boundaries. Every created record still has explicit types. */
static pigen_expr_id case_context(pigen_semantic_model *model,pigen_expr_id value,
    size_t width,pigen_signedness sign,size_t depth)
{
    const pigen_semantic_expr *found=pigen_expr_get(model,value);
    if (!found || depth>128 || !width || width>SIZE_MAX/sizeof(pigen_bit_state)) return INVALID_ID(pigen_expr_id);
    pigen_semantic_expr expression=*found;
    pigen_source_span span=expression.span;
    int native=pigen_data_type_numerical_interpretation(model,expression.data_type)==PIGEN_NUMERICAL_SYSTEMVERILOG;
    if (expression.unbased_fill)
    {
        pigen_bit_state *states=pigen_resize(NULL,width*sizeof(*states));
        for(size_t i=0;i<width;i++) states[i]=(pigen_bit_state)(expression.unbased_fill-1);
        pigen_data_type_id type=pigen_data_type_sized_integral(model,width,sign,PIGEN_DATA_TYPE_STATE_FOUR);
        pigen_expr_id result=pigen_expr_add_bits(model,states,width,type,span);free(states);return result;
    }
    if (expression.is_context_cast)
    {
        pigen_expr_id operand=expression.kind==PIGEN_EXPR_GROUP ? expression.as.group.operand : expression.as.conversion.operand;
        const pigen_semantic_expr *source=pigen_expr_get(model,operand);uint64_t cast_width,source_width;
        if (!source || !pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,expression.data_type),&cast_width) || !cast_width || cast_width>SIZE_MAX)
            return INVALID_ID(pigen_expr_id);
        if (pigen_data_type_numerical_interpretation(model,source->data_type)==PIGEN_NUMERICAL_EXACT_INTEGER)
            source_width=cast_width;
        else if (!pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,source->data_type),&source_width) || !source_width || source_width>SIZE_MAX)
            return INVALID_ID(pigen_expr_id);
        operand=case_context(model,operand,(size_t)(cast_width>source_width ? cast_width : source_width),pigen_data_type_signedness(model,source->data_type)==PIGEN_SIGN_SIGNED ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED,depth+1);
        source=pigen_expr_get(model,operand);pigen_conversion conversion;
        if (!source || !pigen_data_type_resolve_explicit_conversion(model,source->data_type,expression.data_type,&conversion)) return INVALID_ID(pigen_expr_id);
        value=intrinsic_conversion(model,operand,conversion,span);
        if (value.index==PIGEN_INVALID_ID) return value;
        if (value.index==operand.index) value=pigen_expr_add_group(model,value,span);
        if (value.index==PIGEN_INVALID_ID) return value;
        model->expressions[value.index].is_context_cast=1;
        return case_coerce(model,value,width,sign,span);
    }
    if (native && expression.kind==PIGEN_EXPR_CONVERSION && !expression.is_context_cast)
        return case_context(model,expression.as.conversion.operand,width,sign,depth+1);
    if (native && expression.kind==PIGEN_EXPR_GROUP)
    {
        pigen_expr_id operand=case_context(model,expression.as.group.operand,width,sign,depth+1);
        return pigen_expr_add_group(model,operand,span);
    }
    pigen_data_type_id result=pigen_data_type_sized_integral(model,width,sign,pigen_data_type_state_domain(model,expression.data_type));
    if (native && expression.kind==PIGEN_EXPR_UNARY &&
        (expression.as.unary.operation.operator==PIGEN_UNARY_POSITIVE || expression.as.unary.operation.operator==PIGEN_UNARY_NEGATE || expression.as.unary.operation.operator==PIGEN_UNARY_BITWISE_NOT))
    {
        pigen_expr_id operand=case_context(model,expression.as.unary.operand,width,sign,depth+1);
        const pigen_semantic_expr *source=pigen_expr_get(model,operand);
        return source ? pigen_expr_add_unary(model,(pigen_unary_operation){expression.as.unary.operation.operator,source->data_type,result},operand,span) : INVALID_ID(pigen_expr_id);
    }
    if (native && expression.kind==PIGEN_EXPR_UNARY)
    {
        pigen_expr_id operand=case_self_context(model,expression.as.unary.operand,depth+1);
        const pigen_semantic_expr *source=pigen_expr_get(model,operand);
        if(source && expression.as.unary.operation.operator==PIGEN_UNARY_LOGICAL_NOT)
        {
            operand=case_vector_truth(model,operand,span);source=pigen_expr_get(model,operand);
            pigen_unary_resolution resolution;
            if(!source || !pigen_data_type_resolve_case_unary_operation(model,PIGEN_UNARY_LOGICAL_NOT,source->data_type,&resolution)) return INVALID_ID(pigen_expr_id);
            operand=intrinsic_conversion(model,operand,resolution.operand_conversion,span);
            value=pigen_expr_add_unary(model,resolution.operation,operand,span);
            return case_coerce(model,value,width,sign,span);
        }
        value=source ? pigen_expr_add_unary(model,(pigen_unary_operation){expression.as.unary.operation.operator,source->data_type,expression.data_type},operand,span) : INVALID_ID(pigen_expr_id);
        return case_coerce(model,value,width,sign,span);
    }
    if (native && expression.kind==PIGEN_EXPR_BINARY)
    {
        pigen_binary_operator operator=expression.as.binary.operation.operator;
        if (operator<=PIGEN_BINARY_ARITH_SHIFT_RIGHT || (operator>=PIGEN_BINARY_BITWISE_AND && operator<=PIGEN_BINARY_BITWISE_OR))
        {
            int count=operator==PIGEN_BINARY_POWER || (operator>=PIGEN_BINARY_SHIFT_LEFT && operator<=PIGEN_BINARY_ARITH_SHIFT_RIGHT);
            pigen_expr_id left=case_context(model,expression.as.binary.left,width,sign,depth+1);
            pigen_expr_id right=count ? case_self_context(model,expression.as.binary.right,depth+1) : case_context(model,expression.as.binary.right,width,sign,depth+1);
            const pigen_semantic_expr *lhs=pigen_expr_get(model,left),*rhs=pigen_expr_get(model,right);
            return lhs && rhs ? pigen_expr_add_binary(model,(pigen_binary_operation){operator,lhs->data_type,rhs->data_type,result},left,right,span) : INVALID_ID(pigen_expr_id);
        }
        int logical=operator==PIGEN_BINARY_LOGICAL_AND || operator==PIGEN_BINARY_LOGICAL_OR;
        pigen_expr_id left,right;
        if (logical)
        {
            left=case_self_context(model,expression.as.binary.left,depth+1);
            right=case_self_context(model,expression.as.binary.right,depth+1);
        }
        else
        {
            const pigen_semantic_expr *lhs=pigen_expr_get(model,expression.as.binary.left),*rhs=pigen_expr_get(model,expression.as.binary.right);
            uint64_t lw,rw;
            if (!lhs || !rhs || !pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,lhs->data_type),&lw) ||
                !pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,rhs->data_type),&rw) || !lw || !rw || lw>SIZE_MAX || rw>SIZE_MAX) return INVALID_ID(pigen_expr_id);
            pigen_signedness common=pigen_data_type_signedness(model,lhs->data_type)==PIGEN_SIGN_SIGNED && pigen_data_type_signedness(model,rhs->data_type)==PIGEN_SIGN_SIGNED ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED;
            size_t working=(size_t)(lw>rw ? lw : rw);
            left=case_context(model,expression.as.binary.left,working,common,depth+1);
            right=case_context(model,expression.as.binary.right,working,common,depth+1);
        }
        const pigen_semantic_expr *lhs=pigen_expr_get(model,left),*rhs=pigen_expr_get(model,right);
		if(logical && lhs && rhs)
		{
			left=case_vector_truth(model,left,span);right=case_vector_truth(model,right,span);
			lhs=pigen_expr_get(model,left);rhs=pigen_expr_get(model,right);
			pigen_binary_resolution resolution;
			if(!lhs || !rhs || !pigen_data_type_resolve_case_binary_operation(model,operator,lhs->data_type,rhs->data_type,&resolution)) return INVALID_ID(pigen_expr_id);
			left=intrinsic_conversion(model,left,resolution.left_conversion,span);
			right=intrinsic_conversion(model,right,resolution.right_conversion,span);
			value=pigen_expr_add_binary(model,resolution.operation,left,right,span);
			return case_coerce(model,value,width,sign,span);
		}
        value=lhs && rhs ? pigen_expr_add_binary(model,(pigen_binary_operation){operator,lhs->data_type,rhs->data_type,expression.data_type},left,right,span) : INVALID_ID(pigen_expr_id);
        return case_coerce(model,value,width,sign,span);
    }
    if (native && expression.kind==PIGEN_EXPR_CONDITIONAL)
    {
        pigen_expr_id condition=case_self_context(model,expression.as.conditional.condition,depth+1);
        condition=case_vector_truth(model,condition,span);
        pigen_expr_id yes=case_context(model,expression.as.conditional.when_true,width,sign,depth+1);
        pigen_expr_id no=case_context(model,expression.as.conditional.when_false,width,sign,depth+1);
        const pigen_semantic_expr *c=pigen_expr_get(model,condition),*t=pigen_expr_get(model,yes),*f=pigen_expr_get(model,no);
        return c && t && f ? pigen_expr_add_conditional(model,(pigen_conditional_operation){c->data_type,t->data_type,f->data_type,result},condition,yes,no,span) : INVALID_ID(pigen_expr_id);
    }
    if (native && expression.kind==PIGEN_EXPR_CONCATENATION)
    {
        size_t count=expression.as.sequence.child_count,first=expression.as.sequence.first_child;
        if (first>model->expression_child_count || count>model->expression_child_count-first) return INVALID_ID(pigen_expr_id);
        pigen_expr_id *children=pigen_resize(NULL,count*sizeof(*children));
        if (count) memcpy(children,model->expression_children+first,count*sizeof(*children));
        for(size_t i=0;i<count;i++) children[i]=case_self_context(model,children[i],depth+1);
        value=pigen_expr_add_concatenation(model,children,count,span);free(children);
    }
    return case_coerce(model,value,width,sign,span);
}

pigen_expr_id pigen_resolve_case_context(pigen_semantic_model *model,pigen_expr_id value,
    size_t width,pigen_signedness sign)
{
    if (!model || (sign!=PIGEN_SIGN_SIGNED && sign!=PIGEN_SIGN_UNSIGNED)) return INVALID_ID(pigen_expr_id);
    size_t expressions=model->expression_count,children=model->expression_child_count;
    pigen_expr_id result=case_context(model,value,width,sign,0);
    if (result.index==PIGEN_INVALID_ID) {model->expression_count=expressions;model->expression_child_count=children;}
    return result;
}

static pigen_expr_id resolve_runtime(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression,
	const pigen_resolve_policy *policy,pigen_semantic_error *error,pigen_literal_domain domain);

static pigen_expr_id resolve_call_argument(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression,
	const pigen_resolve_policy *policy,pigen_semantic_error *error,pigen_literal_domain domain)
{
	int filled=contains_assignment_fill(syntax,expression);
	pigen_expr_id value=resolve_runtime(syntax,model,scope,expression,policy,error,
		filled ? PIGEN_LITERAL_DOMAIN_CASE : domain);
	return filled ? selection_fill_context(model,value) : value;
}

/* An empty field is returned as its evaluated, positive-sized body plus an
 * explicit flag; it never escapes as a zero-width semantic value. */
static pigen_expr_id resolve_runtime_sequence(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression,
	const pigen_resolve_policy *policy,pigen_semantic_error *error,pigen_literal_domain domain,
	int allow_empty,int *empty)
{
	const pigen_syntax_expr *node=pigen_syntax_expr_get(&syntax->expressions,expression);
	int replicated=node && node->kind==PIGEN_SYNTAX_EXPR_REPLICATION;
	if (!node || (!replicated && node->kind!=PIGEN_SYNTAX_EXPR_CONCATENATION) || !policy || !policy->maximum_generated_bits)
		return INVALID_ID(pigen_expr_id);
 *empty=0;
	size_t first=replicated ? node->as.replication.first_child : node->as.sequence.first_child;
	size_t count=replicated ? node->as.replication.child_count : node->as.sequence.child_count;
	uint64_t repeat=1;
	if (replicated)
	{
		pigen_expr_id n=pigen_resolve_constant_expression(syntax,model,scope,node->as.replication.count,PIGEN_LITERAL_DOMAIN_PIGEN,policy,error);
		if (!pigen_const_expr_evaluate_u64(model,pigen_expr_constant(model,n),&repeat) || repeat>policy->maximum_generated_bits)
			return record_failure(error,node->location,"replication requires a nonnegative bounded constant count");
		if (!repeat && !allow_empty) return record_failure(error,node->location,"zero-count replication requires a positive-sized concatenation peer");
	}
	if (!count || count>policy->maximum_generated_bits || count>SIZE_MAX/sizeof(pigen_expr_id) || (repeat && count>policy->maximum_generated_bits/repeat))
		return record_failure(error,node->location,"invalid concatenation size");
	const pigen_syntax_expr_id *written=pigen_syntax_expr_children(&syntax->expressions,first,count);
	if (!written) return INVALID_ID(pigen_expr_id);
	pigen_expr_id *values=pigen_resize(NULL,count*sizeof(*values));
	unsigned char *ignored=pigen_resize(NULL,count);size_t positive=0;
	pigen_expr_id result=INVALID_ID(pigen_expr_id);
	for(size_t i=0;i<count;i++)
	{
		pigen_syntax_expr_id child_id=written[i];
		const pigen_syntax_expr *child=pigen_syntax_expr_get(&syntax->expressions,written[i]);int zero=0;
		while(child && child->kind==PIGEN_SYNTAX_EXPR_GROUP) {child_id=child->as.group.operand;child=pigen_syntax_expr_get(&syntax->expressions,child_id);}
		if(child && (child->kind==PIGEN_SYNTAX_EXPR_REPLICATION || child->kind==PIGEN_SYNTAX_EXPR_CONCATENATION))
			values[i]=resolve_runtime_sequence(syntax,model,scope,child_id,policy,error,domain,1,&zero);
		else values[i]=resolve_call_argument(syntax,model,scope,written[i],policy,error,domain);
		if(values[i].index==PIGEN_INVALID_ID) goto done;
		ignored[i]=(unsigned char)zero;if(!zero) positive++;
	}
	if(!positive) {result=record_failure(error,node->location,"concatenation requires a positive-sized operand");goto done;}
	/* Attach each run of ignored evaluations to its nearest value, preserving
  * authored order without introducing a deep chain for a flat source list. */
	size_t first_value=0;while(ignored[first_value]) first_value++;
	size_t kept=0;
	for(size_t i=first_value;i<count;)
	{
		pigen_expr_id value=values[i];size_t next=i+1;while(next<count && ignored[next]) next++;
		if(i==first_value && first_value)
		{
			pigen_expr_id evaluated=pigen_expr_add_concatenation(model,values,first_value,node->location.source_span);
			value=pigen_expr_add_evaluated_value(model,value,evaluated,1,node->location.source_span);
		}
		if(next>i+1)
		{
			pigen_expr_id evaluated=pigen_expr_add_concatenation(model,values+i+1,next-i-1,node->location.source_span);
			value=pigen_expr_add_evaluated_value(model,value,evaluated,0,node->location.source_span);
		}
		if(value.index==PIGEN_INVALID_ID) goto done;
		values[kept++]=value;i=next;
	}
	if(!repeat)
	{
		result=pigen_expr_add_concatenation(model,values,kept,node->location.source_span);*empty=1;goto done;
	}
	if(repeat>SIZE_MAX/kept || kept*(size_t)repeat>SIZE_MAX/sizeof(*values)) goto done;
	size_t total=kept*(size_t)repeat;
	values=pigen_resize(values,total*sizeof(*values));
	for(size_t i=kept;i<total;i++) values[i]=values[i%kept];
	result=pigen_expr_add_concatenation(model,values,total,node->location.source_span);
done:
	free(ignored);free(values);return result;
}

static int resolve_direct_expression(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model, pigen_scope_id scope,
	pigen_syntax_expr_id expression, const pigen_resolve_policy *policy,
	pigen_semantic_error *error, pigen_expr_id *result, pigen_literal_domain domain)
{
	const pigen_syntax_expr *call = pigen_syntax_expr_get(
		&syntax->expressions, expression);
	const pigen_syntax_expr *callee;
	const pigen_syntax_expr_id *arguments;
	const pigen_expanded_token *token;
	const char *name;
	size_t name_length = 0;

	if (!call) return 0;
	if (call->kind != PIGEN_SYNTAX_EXPR_CALL)
	{
		if (!requires_direct_resolution(syntax, expression))
			return 0;
		/* Filled structural indices belong to native endpoint resolution,
		 * rather than generic packed-value indexing. Keep its typed identity
		 * through the surrounding channel/member projections. */
		if(call->kind==PIGEN_SYNTAX_EXPR_INDEX || call->kind==PIGEN_SYNTAX_EXPR_MEMBER)
		{
			pigen_syntax_expr_id base=expression;
			for(size_t depth=0;depth<128;depth++)
			{
				if(pigen_resolve_protocol_reference(syntax,model,scope,base, policy).index!=PIGEN_INVALID_ID)
				{
					*result=resolve_with_policy(syntax,model,scope,expression,0,domain,policy,error);
					return 1;
				}
				const pigen_syntax_expr *part=pigen_syntax_expr_get(&syntax->expressions,base);
				if(part && part->kind==PIGEN_SYNTAX_EXPR_MEMBER) base=part->as.member.base;
				else if(part && part->kind==PIGEN_SYNTAX_EXPR_GROUP) base=part->as.group.operand;
				else break;
			}
		}
		if (call->kind == PIGEN_SYNTAX_EXPR_GROUP)
		{
			pigen_expr_id operand = resolve_runtime(syntax, model,
				scope, call->as.group.operand, policy, error, domain);
			*result = operand.index == PIGEN_INVALID_ID ? operand :
				pigen_expr_add_group(model, operand,
					call->location.source_span);
			return 1;
		}
		if (call->kind == PIGEN_SYNTAX_EXPR_UNARY)
		{
			pigen_expr_id operand = resolve_runtime(syntax, model,
				scope, call->as.unary.operand, policy, error, domain);
			const pigen_semantic_expr *known = pigen_expr_get(model, operand);
			pigen_unary_operator operation;
			pigen_unary_resolution resolution;
			if (!known) { *result = INVALID_ID(pigen_expr_id); return 1; }
			switch (call->as.unary.operator)
			{
			case PIGEN_SYNTAX_OP_POSITIVE: operation = PIGEN_UNARY_POSITIVE; break;
			case PIGEN_SYNTAX_OP_NEGATE: operation = PIGEN_UNARY_NEGATE; break;
			case PIGEN_SYNTAX_OP_LOGICAL_NOT: operation = PIGEN_UNARY_LOGICAL_NOT; break;
			case PIGEN_SYNTAX_OP_BITWISE_NOT: operation = PIGEN_UNARY_BITWISE_NOT; break;
			case PIGEN_SYNTAX_OP_REDUCTION_AND: operation = PIGEN_UNARY_REDUCTION_AND; break;
			case PIGEN_SYNTAX_OP_REDUCTION_NAND: operation = PIGEN_UNARY_REDUCTION_NAND; break;
			case PIGEN_SYNTAX_OP_REDUCTION_OR: operation = PIGEN_UNARY_REDUCTION_OR; break;
			case PIGEN_SYNTAX_OP_REDUCTION_NOR: operation = PIGEN_UNARY_REDUCTION_NOR; break;
			case PIGEN_SYNTAX_OP_REDUCTION_XOR: operation = PIGEN_UNARY_REDUCTION_XOR; break;
			case PIGEN_SYNTAX_OP_REDUCTION_XNOR: operation = PIGEN_UNARY_REDUCTION_XNOR; break;
			default: *result = INVALID_ID(pigen_expr_id); return 1;
			}
			if (!(domain==PIGEN_LITERAL_DOMAIN_CASE ? pigen_data_type_resolve_case_unary_operation : pigen_data_type_resolve_unary_operation)(model, operation,
				known->data_type, &resolution))
			{ *result = INVALID_ID(pigen_expr_id); return 1; }
			if (resolution.operand_conversion.kind != PIGEN_CONVERSION_IDENTITY)
				operand = pigen_expr_add_conversion(model,
					resolution.operand_conversion, operand, call->location.source_span);
			*result = pigen_expr_add_unary(model, resolution.operation,
				operand, call->location.source_span);
			return 1;
		}
		if (call->kind == PIGEN_SYNTAX_EXPR_BINARY)
		{
			pigen_expr_id left = resolve_runtime(syntax, model,
				scope, call->as.binary.left, policy, error, domain);
			pigen_expr_id right = resolve_runtime(syntax, model,
				scope, call->as.binary.right, policy, error, domain);
			pigen_binary_operator operation;
			pigen_binary_resolution resolution;
			if (domain==PIGEN_LITERAL_DOMAIN_CASE)
			{
				const pigen_semantic_expr *lhs=pigen_expr_get(model,left),*rhs=pigen_expr_get(model,right);
				pigen_numerical_interpretation li=lhs ? pigen_data_type_numerical_interpretation(model,lhs->data_type) : PIGEN_NUMERICAL_INVALID;
				pigen_numerical_interpretation ri=rhs ? pigen_data_type_numerical_interpretation(model,rhs->data_type) : PIGEN_NUMERICAL_INVALID;
				if ((li==PIGEN_NUMERICAL_SIGNED_INTEGER || li==PIGEN_NUMERICAL_UNSIGNED_INTEGER) &&
					pigen_syntax_expr_is_decimal_constant(syntax->expanded,&syntax->expressions,call->as.binary.right)) right=resolve_runtime(syntax,model,scope,call->as.binary.right,policy,error,PIGEN_LITERAL_DOMAIN_PIGEN);
				if ((ri==PIGEN_NUMERICAL_SIGNED_INTEGER || ri==PIGEN_NUMERICAL_UNSIGNED_INTEGER) &&
					pigen_syntax_expr_is_decimal_constant(syntax->expanded,&syntax->expressions,call->as.binary.left)) left=resolve_runtime(syntax,model,scope,call->as.binary.left,policy,error,PIGEN_LITERAL_DOMAIN_PIGEN);
			}
			const pigen_semantic_expr *l = pigen_expr_get(model, left);
			const pigen_semantic_expr *r = pigen_expr_get(model, right);

			if (!l || !r) { *result = INVALID_ID(pigen_expr_id); return 1; }
			{
				switch (call->as.binary.operator)
				{
				case PIGEN_SYNTAX_OP_POWER: operation = PIGEN_BINARY_POWER; break;
				case PIGEN_SYNTAX_OP_MULTIPLY: operation = PIGEN_BINARY_MULTIPLY; break;
				case PIGEN_SYNTAX_OP_DIVIDE: operation = PIGEN_BINARY_DIVIDE; break;
				case PIGEN_SYNTAX_OP_MODULO: operation = PIGEN_BINARY_MODULO; break;
				case PIGEN_SYNTAX_OP_ADD: operation = PIGEN_BINARY_ADD; break;
				case PIGEN_SYNTAX_OP_SUBTRACT: operation = PIGEN_BINARY_SUBTRACT; break;
				case PIGEN_SYNTAX_OP_SHIFT_LEFT: operation = PIGEN_BINARY_SHIFT_LEFT; break;
				case PIGEN_SYNTAX_OP_SHIFT_RIGHT: operation = PIGEN_BINARY_SHIFT_RIGHT; break;
				case PIGEN_SYNTAX_OP_ARITH_SHIFT_LEFT: operation = PIGEN_BINARY_ARITH_SHIFT_LEFT; break;
				case PIGEN_SYNTAX_OP_ARITH_SHIFT_RIGHT: operation = PIGEN_BINARY_ARITH_SHIFT_RIGHT; break;
				case PIGEN_SYNTAX_OP_LESS: operation = PIGEN_BINARY_LESS; break;
				case PIGEN_SYNTAX_OP_LESS_EQUAL: operation = PIGEN_BINARY_LESS_EQUAL; break;
				case PIGEN_SYNTAX_OP_GREATER: operation = PIGEN_BINARY_GREATER; break;
				case PIGEN_SYNTAX_OP_GREATER_EQUAL: operation = PIGEN_BINARY_GREATER_EQUAL; break;
				case PIGEN_SYNTAX_OP_EQUAL: operation = PIGEN_BINARY_EQUAL; break;
				case PIGEN_SYNTAX_OP_NOT_EQUAL: operation = PIGEN_BINARY_NOT_EQUAL; break;
				case PIGEN_SYNTAX_OP_CASE_EQUAL: operation = PIGEN_BINARY_CASE_EQUAL; break;
				case PIGEN_SYNTAX_OP_CASE_NOT_EQUAL: operation = PIGEN_BINARY_CASE_NOT_EQUAL; break;
				case PIGEN_SYNTAX_OP_WILDCARD_EQUAL: operation = PIGEN_BINARY_WILDCARD_EQUAL; break;
				case PIGEN_SYNTAX_OP_WILDCARD_NOT_EQUAL: operation = PIGEN_BINARY_WILDCARD_NOT_EQUAL; break;
				case PIGEN_SYNTAX_OP_BITWISE_AND: operation = PIGEN_BINARY_BITWISE_AND; break;
				case PIGEN_SYNTAX_OP_BITWISE_XOR: operation = PIGEN_BINARY_BITWISE_XOR; break;
				case PIGEN_SYNTAX_OP_BITWISE_XNOR: operation = PIGEN_BINARY_BITWISE_XNOR; break;
				case PIGEN_SYNTAX_OP_BITWISE_OR: operation = PIGEN_BINARY_BITWISE_OR; break;
				case PIGEN_SYNTAX_OP_LOGICAL_AND: operation = PIGEN_BINARY_LOGICAL_AND; break;
				case PIGEN_SYNTAX_OP_LOGICAL_OR: operation = PIGEN_BINARY_LOGICAL_OR; break;
				default: *result = INVALID_ID(pigen_expr_id); return 1;
				}
			}
			if (!(domain==PIGEN_LITERAL_DOMAIN_CASE ? pigen_data_type_resolve_case_binary_operation : pigen_data_type_resolve_binary_operation)(model, operation,
				l->data_type, r->data_type, &resolution))
				*result = INVALID_ID(pigen_expr_id);
			else
			{
				left = intrinsic_conversion(model, left, resolution.left_conversion, call->location.source_span);
				right = intrinsic_conversion(model, right, resolution.right_conversion, call->location.source_span);
				*result = pigen_expr_add_binary(model, resolution.operation,
					left, right, call->location.source_span);
			}
			return 1;
		}
		if (call->kind == PIGEN_SYNTAX_EXPR_CONDITIONAL)
		{
			pigen_expr_id c = resolve_runtime(syntax, model, scope, call->as.conditional.condition, policy, error, domain);
			pigen_expr_id t = resolve_runtime(syntax, model, scope, call->as.conditional.when_true, policy, error, domain);
			pigen_expr_id f = resolve_runtime(syntax, model, scope, call->as.conditional.when_false, policy, error, domain);
			const pigen_semantic_expr *cv = pigen_expr_get(model, c), *tv = pigen_expr_get(model, t), *fv = pigen_expr_get(model, f);
			pigen_conditional_resolution resolution;
			if (!cv || !tv || !fv || !pigen_data_type_resolve_conditional_operation(model,
				cv->data_type, tv->data_type, fv->data_type, &resolution))
			{ *result = INVALID_ID(pigen_expr_id); return 1; }
			c = intrinsic_conversion(model, c, resolution.condition_conversion, call->location.source_span);
			t = intrinsic_conversion(model, t, resolution.when_true_conversion, call->location.source_span);
			f = intrinsic_conversion(model, f, resolution.when_false_conversion, call->location.source_span);
			*result = pigen_expr_add_conditional(model, resolution.operation, c, t, f, call->location.source_span);
			return 1;
		}
		if (call->kind == PIGEN_SYNTAX_EXPR_CONCATENATION || call->kind == PIGEN_SYNTAX_EXPR_REPLICATION)
		{
			int empty=0;
			*result=resolve_runtime_sequence(syntax,model,scope,expression,policy,error,domain,0,&empty);
			return 1;
		}
		if (call->kind == PIGEN_SYNTAX_EXPR_INDEX || call->kind == PIGEN_SYNTAX_EXPR_SELECT)
		{
			int index = call->kind == PIGEN_SYNTAX_EXPR_INDEX;
			pigen_literal_domain selection_domain=contains_assignment_fill(syntax,expression) ? PIGEN_LITERAL_DOMAIN_CASE : domain;
			pigen_expr_id base = resolve_runtime(syntax, model, scope,
				index ? call->as.index.base : call->as.select.base, policy, error, selection_domain);
			pigen_expr_id left = resolve_runtime(syntax, model, scope,
				index ? call->as.index.index : call->as.select.left, policy, error, selection_domain);
			if(contains_assignment_fill(syntax,index ? call->as.index.base : call->as.select.base)) base=selection_fill_context(model,base);
			if(contains_assignment_fill(syntax,index ? call->as.index.index : call->as.select.left)) left=selection_fill_context(model,left);
			if(index && contains_assignment_fill(syntax,call->as.index.index)) left=selection_fill_index(model,base,left);
			if (index) *result = pigen_expr_add_index(model, base, left, call->location.source_span);
			else
			{
				pigen_expr_id right = resolve_runtime(syntax, model, scope, call->as.select.right, policy, error, selection_domain);
				if(contains_assignment_fill(syntax,call->as.select.right)) right=selection_fill_context(model,right);
				pigen_select_kind kind = call->as.select.kind == PIGEN_SELECT_RANGE ? PIGEN_SEMANTIC_SELECT_RANGE :
					call->as.select.kind == PIGEN_SELECT_INDEXED_UP ? PIGEN_SEMANTIC_SELECT_INDEXED_UP : PIGEN_SEMANTIC_SELECT_INDEXED_DOWN;
				*result = pigen_expr_add_select(model, base, left, right, kind, call->location.source_span);
			}
			return 1;
		}
		if (call->kind == PIGEN_SYNTAX_EXPR_MEMBER && !call->as.member.scoped)
		{
			pigen_expr_id base = resolve_runtime(syntax, model, scope,
				call->as.member.base, policy, error, domain);
			const pigen_semantic_expr *known = pigen_expr_get(model, base);
			const pigen_expanded_token *token = pigen_expanded_token_get(syntax->expanded, call->as.member.member);
			pigen_source_span name = token ? pigen_origin_spelling_span(syntax->expanded, token->origin) :
				(pigen_source_span){INVALID_ID(pigen_source_id),0,0};
			const pigen_semantic_struct_member *found = known ?
				pigen_struct_member_for_type(model, known->data_type, name) : NULL;
			const pigen_semantic_shape *shape = known ? pigen_shape_get(model,known->shape) : NULL;
			if (shape && shape->dimension_count) found = NULL;
			if (!known || !found)
			{
				if (known && error) { error->origin=call->location.origin; error->span=call->location.source_span; error->message="unknown struct member"; }
				*result=INVALID_ID(pigen_expr_id); return 1;
			}
			pigen_semantic_struct_member member = *found;
			uint64_t width;
			if (!pigen_const_expr_evaluate_u64(model, pigen_data_type_packed_width(model, member.data_type), &width) ||
				!width || member.offset > UINT64_MAX-(width-1))
			{ *result=INVALID_ID(pigen_expr_id); return 1; }
			pigen_data_type_id coordinate = pigen_data_type_sized_logic(model, 64, PIGEN_SIGN_UNSIGNED);
			pigen_expr_id high = pigen_expr_add_integer(model, member.offset+width-1, coordinate, call->location.source_span);
			pigen_expr_id low = pigen_expr_add_integer(model, member.offset, coordinate, call->location.source_span);
			*result=pigen_expr_add_member_select(model, base, high, low,
				member.data_type, member.name, call->location.source_span);
			return 1;
		}

		if (call->kind == PIGEN_SYNTAX_EXPR_CAST)
		{
			int contextual = contains_contextual_record(syntax, call->as.cast.value);
			pigen_data_type_id type = INVALID_ID(pigen_data_type_id);
			if (contextual)
			{
				type = pigen_resolve_cast_type(syntax, model, scope, call,
					INVALID_ID(pigen_data_type_id), policy, error);
				if (type.index == PIGEN_INVALID_ID) { *result = INVALID_ID(pigen_expr_id); return 1; }
			}
			pigen_literal_domain operand_domain=domain==PIGEN_LITERAL_DOMAIN_CASE ? pigen_case_cast_literal_domain(syntax,model,scope,call) : domain;
			if(contains_assignment_fill(syntax,call->as.cast.value)) operand_domain=PIGEN_LITERAL_DOMAIN_CASE;
			pigen_expr_id value = contextual ?
				pigen_resolve_assignment_value(syntax, model, scope, call->as.cast.value, type, policy, error) :
				resolve_runtime(syntax, model, scope, call->as.cast.value, policy, error, operand_domain);
			const pigen_semantic_expr *known = pigen_expr_get(model, value);
			if (known && !contextual) type = pigen_resolve_cast_type(syntax, model, scope, call,
				known->data_type, policy, error);
			/* A size cast first needs the operand's intrinsic sign/state to
			 * determine its type. Then its width supplies fill context. */
			if(known && type.index!=PIGEN_INVALID_ID && !contextual &&
				contains_assignment_fill(syntax,call->as.cast.value))
			{
				/* Preserve the operand's common sign and intermediate X/Z until
				 * the final explicit cast. The target supplies width, not sign. */
				uint64_t width=0,intrinsic=0;
				if(!pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,type),&width) ||
					!pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,known->data_type),&intrinsic) ||
					!width || !intrinsic || width>SIZE_MAX || intrinsic>SIZE_MAX)
					value=INVALID_ID(pigen_expr_id);
				else value=pigen_resolve_case_context(model,value,(size_t)(width>intrinsic ? width : intrinsic),
					pigen_data_type_signedness(model,known->data_type)==PIGEN_SIGN_SIGNED ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED);
				known=pigen_expr_get(model,value);
			}
			pigen_conversion conversion;
			*result = known && pigen_data_type_resolve_explicit_conversion(model, known->data_type, type, &conversion) ?
				intrinsic_conversion(model, value, conversion, call->location.source_span) : INVALID_ID(pigen_expr_id);
			if (domain==PIGEN_LITERAL_DOMAIN_CASE && result->index!=PIGEN_INVALID_ID)
			{
				if (result->index==value.index) *result=pigen_expr_add_group(model,*result,call->location.source_span);
				if (result->index!=PIGEN_INVALID_ID) model->expressions[result->index].is_context_cast=1;
			}
			return 1;
		}
		return 0;
	}
 {
  const pigen_symbol *routine=pigen_symbol_get(model,
   pigen_lookup_callee_symbol(syntax,model,scope,call->as.call.callee));
  if(routine && routine->kind==PIGEN_SYMBOL_RETAINED_SUBROUTINE)
  {
   record_failure(error,call->location,"retained package subroutine calls require ordinary SystemVerilog context");
   *result=INVALID_ID(pigen_expr_id);return 1;
  }
 }
	callee = pigen_syntax_expr_get(&syntax->expressions, call->as.call.callee);
	if (!callee || callee->kind != PIGEN_SYNTAX_EXPR_NAME)
		return 0;
	token = pigen_expanded_token_get(syntax->expanded,
		callee->as.atom.token);
	name = token ? pigen_expanded_token_text(syntax->expanded, token,
		&name_length) : NULL;
	if (!name) return 0;
	arguments = pigen_syntax_expr_children(&syntax->expressions,
		call->as.call.first_argument, call->as.call.argument_count);
	if (!arguments) return 0;

	if ((name_length == 7 && !memcmp(name, "$signed", 7)) ||
		(name_length == 9 && !memcmp(name, "$unsigned", 9)))
	{
		pigen_expr_id operand;
		const pigen_semantic_expr *known;
		pigen_const_expr_id width;
		uint64_t bits = 0;
		pigen_data_type_id target;
		pigen_conversion conversion;

		if (call->as.call.argument_count != 1)
			return 0;
		operand = resolve_call_argument(syntax, model, scope,
			arguments[0], policy, error, domain);
		known = pigen_expr_get(model, operand);
		if (!known) { *result = INVALID_ID(pigen_expr_id); return 1; }
		width = pigen_data_type_packed_width(model, known->data_type);
		if (!pigen_const_expr_evaluate_u64(model, width, &bits) ||
			!bits)
			{ *result = INVALID_ID(pigen_expr_id); return 1; }
		target = pigen_data_type_sized_integral(model, (size_t)bits,
			name_length == 7 ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED,
			pigen_data_type_state_domain(model,known->data_type));
		*result = !pigen_data_type_resolve_explicit_conversion(model,known->data_type,target,&conversion) ?
			INVALID_ID(pigen_expr_id) : intrinsic_conversion(model,operand,conversion,call->location.source_span);
		if(result->index==operand.index) *result=pigen_expr_add_group(model,operand,call->location.source_span);
		if(result->index!=PIGEN_INVALID_ID) model->expressions[result->index].is_context_cast=1;
		return 1;
	}
	if(name_length==6 && !memcmp(name,"$clog2",6) && call->as.call.argument_count==1)
	{
		pigen_expr_id argument=resolve_call_argument(syntax,model,scope,arguments[0],policy,error,domain);
		uint64_t value=0,bits=0;
		if(!pigen_const_expr_evaluate_u64(model,pigen_expr_constant(model,argument),&value))
		{
			if(error && !error->message) record_failure(error,call->location,"$clog2 requires an evaluable nonnegative constant argument");
			*result=INVALID_ID(pigen_expr_id);return 1;
		}
		value=value ? value-1 : 0;
		while(value) {value>>=1;bits++;}
		*result=pigen_expr_add_integer(model,bits,pigen_data_type_unsized_integer(model),call->location.source_span);
		return 1;
	}
	if (name_length == 7 && !memcmp(name, "accepts", 7) &&
		call->as.call.argument_count == 2)
	{
		pigen_expr_id destination_expression = resolve_runtime(
			syntax, model, scope, arguments[0], policy, error, domain);
		pigen_expr_id source = resolve_runtime(syntax, model,
			scope, arguments[1], policy, error, domain);
		pigen_lvalue_id destination =
			pigen_lvalue_resolve(model, destination_expression);
		const pigen_semantic_expr *source_expression =
			pigen_expr_get(model, source);
		pigen_signal_id source_signal = {PIGEN_INVALID_ID};

		if (source_expression && source_expression->kind ==
			PIGEN_EXPR_SYMBOL)
			source_signal = pigen_symbol_signal(model,
				source_expression->as.symbol);
		if (destination.index == PIGEN_INVALID_ID ||
			source_signal.index == PIGEN_INVALID_ID)
			*result = INVALID_ID(pigen_expr_id);
		else
			*result = pigen_expr_add_accepts(model, destination, source,
				source_signal, call->location.source_span);
		return 1;
	}
	if ((name_length == 5 && !memcmp(name, "valid", 5)) ||
		(name_length == 5 && !memcmp(name, "ready", 5)))
	{
		pigen_expr_id argument;
		const pigen_semantic_expr *known;
		pigen_signal_id signal;
		pigen_semantic_expr_kind kind =
			name[0] == 'v' ? PIGEN_EXPR_VALID : PIGEN_EXPR_READY;

		if (call->as.call.argument_count != 1)
			return 0;
		argument = resolve_runtime(syntax, model, scope,
			arguments[0], policy, error, domain);
		known = pigen_expr_get(model, argument);
		signal = known && known->kind == PIGEN_EXPR_SYMBOL ?
			pigen_symbol_signal(model, known->as.symbol) :
			(pigen_signal_id){PIGEN_INVALID_ID};
		*result = signal.index == PIGEN_INVALID_ID ?
			INVALID_ID(pigen_expr_id) :
			pigen_expr_add_control(model, kind, signal,
				call->location.source_span);
		return 1;
	}
	if (name_length == 4 && !memcmp(name, "peek", 4) &&
		call->as.call.argument_count == 1)
	{
		pigen_expr_id source = resolve_call_argument(syntax, model,
			scope, arguments[0], policy, error, domain);

		*result = pigen_expr_add_peek(model, source,
			call->location.source_span);
		return 1;
	}
	return 0;
}

static pigen_expr_id resolve_runtime(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression,
	const pigen_resolve_policy *policy,
	pigen_semantic_error *error, pigen_literal_domain domain)
{
	pigen_expr_id intrinsic = INVALID_ID(pigen_expr_id);

	size_t expression_count = model ? model->expression_count : 0;
	size_t child_count = model ? model->expression_child_count : 0;
	size_t constraint_count = model ? model->width_constraint_count : 0;
	if (resolve_direct_expression(syntax, model, scope, expression, policy, error,
		&intrinsic, domain))
	{
		if (model && intrinsic.index == PIGEN_INVALID_ID)
		{
			model->expression_count = expression_count;
			model->expression_child_count = child_count;
			model->width_constraint_count = constraint_count;
		}
		return intrinsic;
	}
	pigen_expr_id value=resolve_with_policy(syntax, model, scope, expression, 0,
		domain, policy, error);
	const pigen_syntax_expr *written=pigen_syntax_expr_get(&syntax->expressions,expression);
	if(value.index==PIGEN_INVALID_ID && written && written->kind==PIGEN_SYNTAX_EXPR_CALL && error && !error->message)
		record_failure(error,written->location,"unsupported call or incompatible arguments");
	return value;
}

pigen_expr_id pigen_resolve_expression(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression,
	const pigen_resolve_policy *policy,pigen_semantic_error *error)
{
	return resolve_runtime(syntax,model,scope,expression,policy,error,PIGEN_LITERAL_DOMAIN_PIGEN);
}

pigen_expr_id pigen_resolve_control_expression(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression,
	const pigen_resolve_policy *policy,pigen_semantic_error *error)
{
	if (!model) return INVALID_ID(pigen_expr_id);
	size_t expressions=model->expression_count,children=model->expression_child_count,
		constraints=model->width_constraint_count;
	pigen_expr_id value=resolve_runtime(syntax,model,scope,expression,policy,error,PIGEN_LITERAL_DOMAIN_CASE);
	const pigen_semantic_expr *node=pigen_expr_get(model,value);
	const pigen_semantic_shape *shape=node ? pigen_shape_get(model,node->shape) : NULL;
	if (node && shape && !shape->dimension_count && pigen_data_type_is_integral(model,node->data_type))
		value=case_self_context(model,value,0);
	if (value.index==PIGEN_INVALID_ID)
	{
		model->expression_count=expressions;model->expression_child_count=children;
		model->width_constraint_count=constraints;
	}
	return value;
}

pigen_expr_id pigen_resolve_case_expression(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression,
	const pigen_resolve_policy *policy,pigen_semantic_error *error)
{
	return resolve_runtime(syntax,model,scope,expression,policy,error,PIGEN_LITERAL_DOMAIN_CASE);
}

pigen_expr_id pigen_resolve_constant_expression(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression,
	pigen_literal_domain literal_domain,
	const pigen_resolve_policy *policy,
	pigen_semantic_error *error)
{
	return resolve_with_policy(syntax, model, scope, expression, 1,
		literal_domain, policy, error);
}

static pigen_expr_id record_failure(pigen_semantic_error *error,
	pigen_syntax_location location, const char *message)
{
	if (error) { error->origin=location.origin; error->span=location.source_span; error->message=message; }
	return INVALID_ID(pigen_expr_id);
}

static pigen_expr_id resolve_record_value(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model, pigen_scope_id scope, const pigen_syntax_expr *record,
	pigen_data_type_id target, const pigen_resolve_policy *policy, pigen_semantic_error *error)
{
	if (!record->as.sequence.child_count || record->as.sequence.first_child>syntax->expressions.child_count ||
		record->as.sequence.child_count>syntax->expressions.child_count-record->as.sequence.first_child)
		return record_failure(error, record->location, "invalid named record field sequence");
	pigen_data_type_id schema=target;
	pigen_symbol_id alias=INVALID_ID(pigen_symbol_id);
	size_t count = 0;
	for (size_t depth=0; depth<model->data_type_count; depth++)
	{
		alias=pigen_data_type_alias_symbol(model, schema);
		if (alias.index==PIGEN_INVALID_ID) break;
		for (size_t i=0; i<model->struct_member_count; i++)
			if (model->struct_members[i].owner.index==alias.index) count++;
		if (count) break;
		schema=pigen_data_type_alias_target(model, schema);
	}
	if (!count || alias.index==PIGEN_INVALID_ID)
		return record_failure(error, record->location, "record construction requires a packed struct or native channel target");
	pigen_semantic_struct_member *members = pigen_resize(NULL, count*sizeof(*members));
	pigen_syntax_expr_id *values = pigen_resize(NULL, count*sizeof(*values));
	pigen_expr_id *parts = pigen_resize(NULL, count*sizeof(*parts));
	size_t at=0;
	for (size_t i=0; i<model->struct_member_count; i++)
		if (model->struct_members[i].owner.index==alias.index) members[at++]=model->struct_members[i];
	/* Both declared structs and generated protocol layouts use their actual
	 * packed coordinates; authored field order does not determine the bits. */
	for (size_t i=1; i<count; i++)
	{
		pigen_semantic_struct_member member=members[i]; size_t j=i;
		while (j && members[j-1].offset<member.offset) { members[j]=members[j-1]; j--; }
		members[j]=member;
	}
	for (size_t i=0; i<count; i++) values[i]=INVALID_ID(pigen_syntax_expr_id);
	pigen_syntax_expr_id fallback=INVALID_ID(pigen_syntax_expr_id);
	pigen_expr_id result=INVALID_ID(pigen_expr_id);
	uint64_t cursor;
	if (!pigen_const_expr_evaluate_u64(model, pigen_data_type_packed_width(model,target), &cursor))
		{ record_failure(error, record->location, "record construction requires a concrete packed layout"); goto done; }
	for (size_t m=0; m<count; m++)
	{
		uint64_t bits;
		if (!pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,members[m].data_type),&bits) ||
			members[m].offset>cursor || bits!=cursor-members[m].offset)
			{ record_failure(error, record->location, "record construction requires a scalar packed record layout"); goto done; }
		cursor=members[m].offset;
	}
	if (cursor) { record_failure(error, record->location, "record construction requires a complete packed layout"); goto done; }
	for (size_t f=0; f<record->as.sequence.child_count; f++)
	{
		pigen_syntax_expr_id field_id=syntax->expressions.children[record->as.sequence.first_child+f];
		const pigen_syntax_expr *field=pigen_syntax_expr_get(&syntax->expressions, field_id);
		if (!field || field->kind!=PIGEN_SYNTAX_EXPR_RECORD_FIELD)
			{ record_failure(error, record->location, "invalid named record field"); goto done; }
		const pigen_expanded_token *token=pigen_expanded_token_get(syntax->expanded, field->as.member.member);
		size_t length=0;
		const char *name=pigen_expanded_token_text(syntax->expanded, token, &length);
		if (name && length==7 && !memcmp(name,"default",7))
		{
			if (fallback.index!=PIGEN_INVALID_ID) { record_failure(error, field->location, "duplicate record default"); goto done; }
			fallback=field->as.member.base; continue;
		}
		size_t m;
		for (m=0; m<count; m++)
		{
			size_t n=0; const char *member=pigen_source_span_text(model->sources, members[m].name, &n);
			if (name && member && length==n && !memcmp(name,member,n)) break;
		}
		if (m==count) { record_failure(error, field->location, "unknown record field"); goto done; }
		if (values[m].index!=PIGEN_INVALID_ID) { record_failure(error, field->location, "duplicate record field"); goto done; }
		values[m]=field->as.member.base;
	}
	for (size_t m=0; m<count; m++)
	{
		if (values[m].index==PIGEN_INVALID_ID) values[m]=fallback;
		if (values[m].index==PIGEN_INVALID_ID) { record_failure(error, record->location, "record construction must supply every field or an explicit default"); goto done; }
		parts[m]=pigen_resolve_assignment_value(syntax, model, scope, values[m], members[m].data_type, policy, error);
		if (parts[m].index==PIGEN_INVALID_ID) goto done;
	}
	result=pigen_expr_add_concatenation(model, parts, count, record->location.source_span);
	const pigen_semantic_expr *packed=pigen_expr_get(model, result);
	pigen_conversion conversion;
	if (packed && pigen_data_type_resolve_explicit_conversion(model, packed->data_type, target, &conversion))
		result=materialize_conversion(&(expression_materializer){.model=model}, result, conversion, record->location.source_span);
	else result=record_failure(error, record->location, "record fields cannot form the target type");
done:
	free(members); free(values); free(parts);
	return result;
}

static int contains_contextual_record(const pigen_syntax_tree *syntax,
	pigen_syntax_expr_id expression)
{
	const pigen_syntax_expr *node = pigen_syntax_expr_get(&syntax->expressions, expression);
	if (!node) return 0;
	if (node->kind == PIGEN_SYNTAX_EXPR_RECORD) return 1;
	if (node->kind == PIGEN_SYNTAX_EXPR_GROUP)
		return contains_contextual_record(syntax, node->as.group.operand);
	return node->kind == PIGEN_SYNTAX_EXPR_CONDITIONAL &&
		(contains_contextual_record(syntax, node->as.conditional.when_true) ||
		 contains_contextual_record(syntax, node->as.conditional.when_false));
}

static int contains_assignment_fill(const pigen_syntax_tree *syntax,pigen_syntax_expr_id expression)
{
	return pigen_expression_contains_unbased_fill(syntax,expression);
}

static pigen_expr_id resolve_contextual_conditional(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model, pigen_scope_id scope, const pigen_syntax_expr *node,
	pigen_data_type_id target, const pigen_resolve_policy *policy, pigen_semantic_error *error)
{
	pigen_expr_id condition = (contains_assignment_fill(syntax,node->as.conditional.condition) ? pigen_resolve_control_expression : pigen_resolve_expression)(syntax, model, scope,
		node->as.conditional.condition, policy, error);
	if (condition.index == PIGEN_INVALID_ID) return condition;
	pigen_expr_id when_true = pigen_resolve_assignment_value(syntax, model, scope,
		node->as.conditional.when_true, target, policy, error);
	if (when_true.index == PIGEN_INVALID_ID) return when_true;
	pigen_expr_id when_false = pigen_resolve_assignment_value(syntax, model, scope,
		node->as.conditional.when_false, target, policy, error);
	if (when_false.index == PIGEN_INVALID_ID) return when_false;
	const pigen_semantic_expr *c = pigen_expr_get(model, condition);
	const pigen_semantic_expr *t = pigen_expr_get(model, when_true);
	const pigen_semantic_expr *f = pigen_expr_get(model, when_false);
	pigen_conditional_resolution resolution;
	if (!c || !t || !f || !pigen_data_type_resolve_conditional_operation(model,
		c->data_type, t->data_type, f->data_type, &resolution))
		return record_failure(error, node->location, "invalid conditional record construction");
	condition = intrinsic_conversion(model, condition, resolution.condition_conversion, node->location.source_span);
	when_true = intrinsic_conversion(model, when_true, resolution.when_true_conversion, node->location.source_span);
	when_false = intrinsic_conversion(model, when_false, resolution.when_false_conversion, node->location.source_span);
	return pigen_expr_add_conditional(model, resolution.operation, condition, when_true,
		when_false, node->location.source_span);
}

static pigen_expr_id resolve_assignment_context(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression,
	pigen_data_type_id target,const pigen_resolve_policy *policy,pigen_semantic_error *error)
{
	int filled=contains_assignment_fill(syntax,expression);
	int native_target=pigen_data_type_numerical_interpretation(model,target)==PIGEN_NUMERICAL_SYSTEMVERILOG;
	size_t expressions=model->expression_count, children=model->expression_child_count,
		constraints=model->width_constraint_count;
	pigen_expr_id value=resolve_runtime(syntax,model,scope,expression,policy,error,
		(filled || native_target) ? PIGEN_LITERAL_DOMAIN_CASE : PIGEN_LITERAL_DOMAIN_PIGEN);
	const pigen_semantic_expr *known=pigen_expr_get(model,value);
	/* Pigen arithmetic and explicit numerical casts retain their family. */
	if(known && pigen_data_type_numerical_interpretation(model,known->data_type)!=PIGEN_NUMERICAL_SYSTEMVERILOG) return value;
	/* Native compound expressions need SV self/common operand sizing even
	 * without a fill. Keep ordinary leaf identities, especially mutable locals. */
	if (!filled && known && known->kind!=PIGEN_EXPR_BINARY &&
		known->kind!=PIGEN_EXPR_UNARY && known->kind!=PIGEN_EXPR_CONDITIONAL &&
		known->kind!=PIGEN_EXPR_GROUP) return value;
	if (!filled && !native_target && known)
	{
		model->expression_count=expressions;model->expression_child_count=children;
		model->width_constraint_count=constraints;
		value=resolve_runtime(syntax,model,scope,expression,policy,error,PIGEN_LITERAL_DOMAIN_CASE);
		known=pigen_expr_get(model,value);
	}
	uint64_t width=0,intrinsic=0;
	if(!known || !pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,target),&width) ||
		!pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,known->data_type),&intrinsic) ||
		!width || !intrinsic || width>SIZE_MAX || intrinsic>SIZE_MAX ||
		(policy && (width>policy->maximum_generated_bits || intrinsic>policy->maximum_generated_bits)))
		return INVALID_ID(pigen_expr_id);
	return pigen_resolve_case_context(model,value,(size_t)(width>intrinsic ? width : intrinsic),
		pigen_data_type_signedness(model,known->data_type)==PIGEN_SIGN_SIGNED ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED);
}

pigen_expr_id pigen_resolve_assignment_value(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression,
	pigen_data_type_id target, const pigen_resolve_policy *policy,
	pigen_semantic_error *error)
{
	if (!syntax || !model) return INVALID_ID(pigen_expr_id);
	size_t expression_count = model ? model->expression_count : 0;
	size_t child_count = model ? model->expression_child_count : 0;
	size_t constraint_count = model ? model->width_constraint_count : 0;
	const pigen_syntax_expr *written = pigen_syntax_expr_get(&syntax->expressions, expression);
	while (written && written->kind==PIGEN_SYNTAX_EXPR_GROUP)
		written=pigen_syntax_expr_get(&syntax->expressions, written->as.group.operand);
	int fill=assignment_fill(syntax,written);
	pigen_expr_id value = fill>=0 ? resolve_assignment_fill(model,written,target,fill,policy,error) :
		written && written->kind==PIGEN_SYNTAX_EXPR_RECORD ?
		resolve_record_value(syntax, model, scope, written, target, policy, error) :
		written && written->kind == PIGEN_SYNTAX_EXPR_CONDITIONAL &&
		(contains_contextual_record(syntax,expression) ||
		 (contains_assignment_fill(syntax,expression) && pigen_data_type_numerical_interpretation(model,target)!=PIGEN_NUMERICAL_SYSTEMVERILOG)) ?
		resolve_contextual_conditional(syntax, model, scope, written, target, policy, error) :
		resolve_assignment_context(syntax,model,scope,expression,target,policy,error);
	const pigen_semantic_expr *known = pigen_expr_get(model, value);
	pigen_conversion conversion;
	pigen_expr_id result;

	if (!known || !pigen_data_type_resolve_assignment_conversion(model,
		known->data_type, target, &conversion))
	{
		if (model)
		{
			model->expression_count = expression_count;
			model->expression_child_count = child_count;
			model->width_constraint_count = constraint_count;
		}
		if (error && !error->message)
		{
			error->span = known ? known->span :
				(pigen_source_span){INVALID_ID(pigen_source_id), 0, 0};
			error->message = "invalid assignment conversion";
		}
		return INVALID_ID(pigen_expr_id);
	}
	result = materialize_conversion(&(expression_materializer){.model=model},
		value, conversion, known->span);
	if (result.index == PIGEN_INVALID_ID)
	{
		model->expression_count = expression_count;
		model->expression_child_count = child_count;
		model->width_constraint_count = constraint_count;
	}
	return result;
}

/* Unpacked aggregates retain their shape and one expression per immediate
 * element. Packed leaf sizing continues through the existing assignment owner. */
static int array_pattern_constant(const pigen_syntax_tree *syntax,pigen_semantic_model *model,
 pigen_scope_id scope,pigen_syntax_expr_id expression,const pigen_resolve_policy *policy,int64_t *value)
{
 pigen_analyzed_expr_arena arena={0};pigen_analyzed_expr_id result;
 int ok=pigen_analyze_expression(syntax,model,scope,expression,1,PIGEN_LITERAL_DOMAIN_CASE,policy,&arena,&result,NULL);
 if(ok) ok=pigen_const_expr_evaluate_i64(model,arena.nodes[result.index].constant,value);
 pigen_free_analyzed_expr_arena(&arena);return ok;
}

static pigen_expr_id array_coordinate(pigen_semantic_model *model,int64_t coordinate,pigen_source_span span)
{
 pigen_integer_id value=pigen_integer_intern_u64(model,coordinate<0 ? (uint64_t)(-(coordinate+1))+1 : (uint64_t)coordinate);
 if(coordinate<0) value=pigen_integer_negate(model,value);
 return pigen_expr_add_exact_integer(model,value,span);
}

static pigen_expr_id convert_array_value(pigen_semantic_model *model,pigen_expr_id source,
 pigen_data_type_id target,pigen_shape_id shape,size_t depth)
{
 const pigen_semantic_expr *known=pigen_expr_get(model,source);
 const pigen_semantic_shape *to=pigen_shape_get(model,shape),*from=known ? pigen_shape_get(model,known->shape) : NULL;
 if(!known || !to || !from || depth>128 || to->dimension_count!=from->dimension_count) return INVALID_ID(pigen_expr_id);
 pigen_semantic_expr value=*known;
 if(!to->dimension_count)
 {
  pigen_conversion conversion;
  if(!pigen_data_type_resolve_assignment_conversion(model,value.data_type,target,&conversion)) return INVALID_ID(pigen_expr_id);
  return intrinsic_conversion(model,source,conversion,value.span);
 }
 uint64_t target_width,source_width;
 if(!pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,target),&target_width) ||
  !pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,value.data_type),&source_width) ||
  target_width!=source_width || pigen_data_type_state_domain(model,target)!=pigen_data_type_state_domain(model,value.data_type) ||
  (pigen_data_type_signedness(model,target)==PIGEN_SIGN_SIGNED)!=(pigen_data_type_signedness(model,value.data_type)==PIGEN_SIGN_SIGNED)) return INVALID_ID(pigen_expr_id);
 int64_t tl,tr,sl,sr;size_t tc,sc;
 if(!pigen_shape_dimension_bounds(model,shape,0,&tl,&tr,&tc) ||
  !pigen_shape_dimension_bounds(model,value.shape,0,&sl,&sr,&sc) || tc!=sc || tc>SIZE_MAX/sizeof(pigen_expr_id)) return INVALID_ID(pigen_expr_id);
 if(value.data_type.index==target.index && value.shape.index==shape.index) return source;
 pigen_shape_id element=pigen_shape_element(model,shape);
 pigen_expr_id *children=pigen_resize(NULL,tc*sizeof(*children));int ok=1;
 for(size_t i=0;i<tc;i++)
 {
  pigen_expr_id selected;
  if(value.kind==PIGEN_EXPR_ARRAY_PATTERN)
   selected=model->expression_children[value.as.sequence.first_child+i];
  else selected=pigen_expr_add_index(model,source,array_coordinate(model,sl,value.span),value.span);
  children[i]=convert_array_value(model,selected,target,element,depth+1);
  if(children[i].index==PIGEN_INVALID_ID) {ok=0;break;}
  if(i+1<tc) sl+=sl<sr ? 1 : -1;
 }
 pigen_expr_id result=ok ? pigen_expr_add_array_pattern(model,target,shape,children,tc,value.span) : INVALID_ID(pigen_expr_id);
 free(children);return result;
}

typedef struct {pigen_syntax_expr_id value;int is_default;} data_pattern_entry;

static data_pattern_entry *data_pattern_order(const pigen_syntax_tree *syntax,pigen_semantic_model *model,
 pigen_scope_id scope,const pigen_syntax_expr *node,int64_t left,int64_t right,size_t count,const pigen_resolve_policy *policy)
{
 data_pattern_entry *order=pigen_resize(NULL,count*sizeof(*order));
 for(size_t i=0;i<count;i++) order[i]=(data_pattern_entry){INVALID_ID(pigen_syntax_expr_id),0};
 const pigen_syntax_expr_id *entries=pigen_syntax_expr_children(&syntax->expressions,node->as.sequence.first_child,node->as.sequence.child_count);
 pigen_syntax_expr_id fallback=INVALID_ID(pigen_syntax_expr_id);int ok=entries!=NULL;
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
  int64_t key;int64_t low=left<right ? left : right,high=left>right ? left : right;
  if(!array_pattern_constant(syntax,model,scope,entry->as.member.pattern_key,policy,&key) || key<low || key>high) {ok=0;break;}
  size_t ordinal=(size_t)(left<=right ? (uint64_t)key-(uint64_t)left : (uint64_t)left-(uint64_t)key);
  if(order[ordinal].value.index!=PIGEN_INVALID_ID) {ok=0;break;}
  order[ordinal].value=entry->as.member.base;
 }
 for(size_t i=0;ok && i<count;i++)
 {
  if(order[i].value.index==PIGEN_INVALID_ID) order[i]=(data_pattern_entry){fallback,1};
  if(order[i].value.index==PIGEN_INVALID_ID) ok=0;
 }
 if(!ok) {free(order);return NULL;}
 return order;
}

static pigen_expr_id resolve_shaped_value(const pigen_syntax_tree *syntax,pigen_semantic_model *model,
 pigen_scope_id scope,pigen_syntax_expr_id expression,pigen_data_type_id target,pigen_shape_id shape,
 const pigen_resolve_policy *policy,pigen_semantic_error *error,size_t depth,int default_descent)
{
 const pigen_syntax_expr *node=pigen_syntax_expr_get(&syntax->expressions,expression);
 const pigen_semantic_shape *expected=pigen_shape_get(model,shape);
 while(node && node->kind==PIGEN_SYNTAX_EXPR_GROUP) node=pigen_syntax_expr_get(&syntax->expressions,node->as.group.operand);
 if(!node || !expected || depth>128) return INVALID_ID(pigen_expr_id);
 if(!expected->dimension_count)
 {
  pigen_expr_id result=pigen_resolve_assignment_value(syntax,model,scope,expression,target,policy,error);
  const pigen_semantic_expr *value=pigen_expr_get(model,result);
  return value && value->shape.index==shape.index ? result : INVALID_ID(pigen_expr_id);
 }
 size_t expressions=model->expression_count,children_count=model->expression_child_count,constraints=model->width_constraint_count;
 pigen_semantic_error saved=error ? *error : (pigen_semantic_error){0};
 int64_t left,right;size_t count;
 if(!pigen_shape_dimension_bounds(model,shape,0,&left,&right,&count) || !count ||
  count>policy->maximum_generated_bits || count>SIZE_MAX/sizeof(pigen_expr_id) || count>SIZE_MAX/sizeof(data_pattern_entry)) return INVALID_ID(pigen_expr_id);
 pigen_shape_id element=pigen_shape_element(model,shape);
 pigen_expr_id result=INVALID_ID(pigen_expr_id);
 if(node->kind==PIGEN_SYNTAX_EXPR_CONDITIONAL)
 {
  pigen_expr_id control=pigen_resolve_control_expression(syntax,model,scope,node->as.conditional.condition,policy,error);
  pigen_expr_id yes=resolve_shaped_value(syntax,model,scope,node->as.conditional.when_true,target,shape,policy,error,depth+1,default_descent);
  pigen_expr_id no=resolve_shaped_value(syntax,model,scope,node->as.conditional.when_false,target,shape,policy,error,depth+1,default_descent);
  const pigen_semantic_expr *c=pigen_expr_get(model,control);pigen_conditional_resolution operation;
  const pigen_semantic_shape *cs=c ? pigen_shape_get(model,c->shape) : NULL;
  if(c && cs && !cs->dimension_count && yes.index!=PIGEN_INVALID_ID && no.index!=PIGEN_INVALID_ID &&
   pigen_data_type_resolve_conditional_operation(model,c->data_type,target,target,&operation))
  {
   control=intrinsic_conversion(model,control,operation.condition_conversion,node->location.source_span);
   result=pigen_expr_add_conditional(model,operation.operation,control,yes,no,node->location.source_span);
  }
 }
 else if(node->kind==PIGEN_SYNTAX_EXPR_ARRAY_PATTERN || node->kind==PIGEN_SYNTAX_EXPR_ARRAY_PATTERN_REPLICATION || node->kind==PIGEN_SYNTAX_EXPR_RECORD)
 {
  int keyed=node->kind==PIGEN_SYNTAX_EXPR_RECORD,replicated=node->kind==PIGEN_SYNTAX_EXPR_ARRAY_PATTERN_REPLICATION;
  size_t body=replicated ? node->as.replication.child_count : node->as.sequence.child_count,repetitions=1;
  const pigen_syntax_expr_id *items=pigen_syntax_expr_children(&syntax->expressions,replicated ? node->as.replication.first_child : node->as.sequence.first_child,body);
  data_pattern_entry *order=keyed ? data_pattern_order(syntax,model,scope,node,left,right,count,policy) : NULL;
  int valid=body && items && (!keyed || order);
  if(replicated)
  {
   int64_t n;
   valid=valid && array_pattern_constant(syntax,model,scope,node->as.replication.count,policy,&n) && n>0 && (uint64_t)n<=SIZE_MAX;
   if(valid) repetitions=(size_t)n;
  }
  if(!keyed) valid=valid && repetitions<=count/body && body*repetitions==count;
  pigen_expr_id *children=pigen_resize(NULL,count*sizeof(*children));
  for(size_t i=0;valid && i<count;i++)
  {
   pigen_syntax_expr_id item=keyed ? order[i].value : items[i%body];
   children[i]=resolve_shaped_value(syntax,model,scope,item,target,element,policy,error,depth+1,keyed && order[i].is_default);
   valid=children[i].index!=PIGEN_INVALID_ID;
  }
  if(valid) result=pigen_expr_add_array_pattern(model,target,shape,children,count,node->location.source_span);
  free(children);free(order);
 }
 else
 {
  pigen_expr_id value=pigen_resolve_expression(syntax,model,scope,expression,policy,error);
  result=convert_array_value(model,value,target,shape,depth+1);
 }
 if(result.index!=PIGEN_INVALID_ID) return result;
 if(default_descent)
 {
  model->expression_count=expressions;model->expression_child_count=children_count;model->width_constraint_count=constraints;
  if(error) *error=saved;
  pigen_expr_id child=resolve_shaped_value(syntax,model,scope,expression,target,element,policy,error,depth+1,1);
  if(child.index!=PIGEN_INVALID_ID)
  {
   pigen_expr_id *children=pigen_resize(NULL,count*sizeof(*children));
   for(size_t i=0;i<count;i++) children[i]=child;
   result=pigen_expr_add_array_pattern(model,target,shape,children,count,node->location.source_span);free(children);
   return result;
  }
 }
 if(error && error->message) return INVALID_ID(pigen_expr_id);
 return record_failure(error,node->location,"unpacked assignment shape mismatch, incompatible elements or incomplete pattern");
}

pigen_expr_id pigen_resolve_shaped_assignment_value(const pigen_syntax_tree *syntax,pigen_semantic_model *model,
 pigen_scope_id scope,pigen_syntax_expr_id expression,pigen_data_type_id target,pigen_shape_id shape,
 const pigen_resolve_policy *policy,pigen_semantic_error *error)
{
 if(!syntax || !model) return INVALID_ID(pigen_expr_id);
 const pigen_semantic_shape *expected=pigen_shape_get(model,shape);
 if(!expected) return INVALID_ID(pigen_expr_id);
 if(!expected->dimension_count)
 {
  size_t expressions=model->expression_count,children=model->expression_child_count,constraints=model->width_constraint_count;
  pigen_expr_id result=pigen_resolve_assignment_value(syntax,model,scope,expression,target,policy,error);
  const pigen_semantic_expr *value=pigen_expr_get(model,result);
  if(!value || value->shape.index==shape.index) return result;
  model->expression_count=expressions;model->expression_child_count=children;model->width_constraint_count=constraints;
  const pigen_syntax_expr *written=pigen_syntax_expr_get(&syntax->expressions,expression);
  return written ? record_failure(error,written->location,"scalar assignment cannot receive an unpacked array") : INVALID_ID(pigen_expr_id);
 }
 pigen_resolve_policy fallback={SIZE_MAX};if(!policy) policy=&fallback;
 const pigen_syntax_expr *node=pigen_syntax_expr_get(&syntax->expressions,expression);
 if(!node) return INVALID_ID(pigen_expr_id);
 uint64_t width;size_t total=1;
 if(expected->dimension_count>128 || !pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,target),&width) || !width || width>policy->maximum_generated_bits)
  return record_failure(error,node->location,"unpacked assignment exceeds fixed storage limits");
 for(size_t d=0;d<expected->dimension_count;d++)
 {
  int64_t left,right;size_t count;
  if(!pigen_shape_dimension_bounds(model,shape,d,&left,&right,&count) || !count || count>policy->maximum_generated_bits/total)
   return record_failure(error,node->location,"unpacked assignment requires bounded fixed dimensions");
  total*=count;
 }
 if(total>policy->maximum_generated_bits/width) return record_failure(error,node->location,"unpacked assignment exceeds fixed storage limits");
 size_t expressions=model->expression_count,children=model->expression_child_count,constraints=model->width_constraint_count;
 pigen_expr_id result=resolve_shaped_value(syntax,model,scope,expression,target,shape,policy,error,0,0);
 if(result.index==PIGEN_INVALID_ID)
 {
  model->expression_count=expressions;model->expression_child_count=children;model->width_constraint_count=constraints;
 }
 return result;
}
