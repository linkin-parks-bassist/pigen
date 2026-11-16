#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/predicate.h"

static pigen_expr_id binary(pigen_semantic_model *model,pigen_binary_operator op,
 pigen_expr_id left,pigen_expr_id right)
{
 pigen_binary_resolution resolution;
 assert(pigen_data_type_resolve_binary_operation(model,op,pigen_expr_get(model,left)->data_type,
  pigen_expr_get(model,right)->data_type,&resolution));
 return pigen_expr_add_binary(model,resolution.operation,left,right,(pigen_source_span){0});
}

static void check_logical_implication(pigen_semantic_model *model)
{
 pigen_data_type_id type=pigen_data_type_sized_integral(model,1,PIGEN_SIGN_UNSIGNED,PIGEN_DATA_TYPE_STATE_FOUR);
 pigen_predicate_id yes=pigen_predicate_true(model);
 for(size_t av=0;av<4;av++)for(size_t bv=0;bv<4;bv++)
 {
  pigen_bit_state astate=(pigen_bit_state)av,bstate=(pigen_bit_state)bv;
  pigen_expr_id a=pigen_expr_add_bits(model,&astate,1,type,(pigen_source_span){0});
  pigen_expr_id b=pigen_expr_add_bits(model,&bstate,1,type,(pigen_source_span){0});
  pigen_unary_resolution not_resolution;
  assert(pigen_data_type_resolve_unary_operation(model,PIGEN_UNARY_LOGICAL_NOT,type,&not_resolution));
  pigen_expr_id not_a=pigen_expr_add_unary(model,not_resolution.operation,a,(pigen_source_span){0});
  pigen_expr_id not_b=pigen_expr_add_unary(model,not_resolution.operation,b,(pigen_source_span){0});
  pigen_expr_id expressions[7];expressions[0]=a;expressions[1]=b;expressions[2]=not_a;
  expressions[3]=binary(model,PIGEN_BINARY_LOGICAL_AND,a,b);
  expressions[4]=binary(model,PIGEN_BINARY_LOGICAL_OR,a,b);
  expressions[5]=pigen_expr_add_evaluated_value(model,a,b,1,(pigen_source_span){0});
  expressions[6]=binary(model,PIGEN_BINARY_CASE_EQUAL,a,pigen_expr_add_integer(model,1,type,(pigen_source_span){0}));
  int at=av<2 ? (int)av : 2,bt=bv<2 ? (int)bv : 2;
  int truths[]={at,bt,at==2 ? 2 : !at,at==0||bt==0 ? 0 : at==1&&bt==1 ? 1 : 2,
   at==1||bt==1 ? 1 : at==0&&bt==0 ? 0 : 2,at,at==1};
  pigen_predicate_id guards[14];int active[14];
  for(size_t i=0;i<7;i++)for(int expected=0;expected<2;expected++)
  {size_t index=i*2+(size_t)expected;guards[index]=pigen_predicate_and_condition(model,yes,expressions[i],expected);active[index]=truths[i]==expected;}
  for(size_t i=0;i<14;i++)for(size_t j=0;j<14;j++)
   if(pigen_predicates_mutually_exclusive(model,guards[i],guards[j])) assert(!(active[i]&&active[j]));
  pigen_predicate_id before=pigen_predicate_and_condition(model,
   pigen_predicate_and_condition(model,yes,a,1),b,0);
  pigen_expr_id exit=binary(model,PIGEN_BINARY_LOGICAL_AND,a,not_b);
  pigen_predicate_id after=pigen_predicate_and_condition(model,yes,exit,0);
  assert(pigen_predicates_mutually_exclusive(model,before,after));
 }
}

int main(void)
{
	const char text[] = "a b";
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "predicates.pigen",
		text, strlen(text));
	pigen_source_span a_span = {source, 0, 1};
	pigen_source_span b_span = {source, 2, 3};
	pigen_semantic_model model;
	pigen_data_type_id unsized_integer_data_type;
	pigen_expr_id a;
	pigen_expr_id b;
	pigen_predicate_id true_predicate;
	pigen_predicate_id false_predicate;
	pigen_predicate_id a_true;
	pigen_predicate_id a_false;
	pigen_predicate_id a_true_b_false;
	pigen_predicate_id b_false_a_true;
	const pigen_predicate *known;
	const pigen_predicate_atom *atoms;

	pigen_semantic_init(&model, &sources);
	unsized_integer_data_type = pigen_data_type_unsized_integer(&model);
	a = pigen_expr_add_integer(&model, 1, unsized_integer_data_type, a_span);
	b = pigen_expr_add_integer(&model, 2, unsized_integer_data_type, b_span);
	true_predicate = pigen_predicate_true(&model);
	false_predicate = pigen_predicate_false(&model);
	assert(true_predicate.index == pigen_predicate_true(&model).index);
	assert(false_predicate.index == pigen_predicate_false(&model).index);
	assert(true_predicate.index != false_predicate.index);
	assert(!pigen_predicate_get(&model, true_predicate)->impossible);
	assert(pigen_predicate_get(&model, true_predicate)->atom_count == 0);
	assert(pigen_predicate_get(&model, false_predicate)->impossible);

	a_true = pigen_predicate_and_condition(&model, true_predicate, a, 1);
	a_false = pigen_predicate_and_condition(&model, true_predicate, a, 0);
	a_true_b_false = pigen_predicate_and_condition(&model, a_true, b, 0);
	b_false_a_true = pigen_predicate_and_condition(&model,
		pigen_predicate_and_condition(&model, true_predicate, b, 0), a, 1);
	assert(a_true_b_false.index == b_false_a_true.index);
	assert(pigen_predicate_and_condition(&model, a_true, a, 1).index ==
		a_true.index);
	assert(pigen_predicate_and_condition(&model, a_true, a, 0).index ==
		false_predicate.index);
	assert(pigen_predicate_and_condition(&model, false_predicate, b, 1).index ==
		false_predicate.index);
	assert(pigen_predicate_and_condition(&model, true_predicate, a, 2).index ==
		PIGEN_INVALID_ID);

	known = pigen_predicate_get(&model, a_true_b_false);
	atoms = pigen_predicate_atoms(&model, a_true_b_false);
	assert(known && known->atom_count == 2 && !known->impossible);
	assert(atoms && atoms[0].condition.index == a.index && atoms[0].expected);
	assert(atoms[1].condition.index == b.index && !atoms[1].expected);
	assert(pigen_predicates_mutually_exclusive(&model, a_true, a_false));
	assert(!pigen_predicates_mutually_exclusive(&model, a_true,
		a_true_b_false));
	assert(pigen_predicates_mutually_exclusive(&model, false_predicate,
		true_predicate));

	check_logical_implication(&model);
	pigen_free_semantic_model(&model);
	pigen_free_sources(&sources);
	puts("PASS: predicates canonicalize guards and prove branch exclusion");
	return 0;
}
