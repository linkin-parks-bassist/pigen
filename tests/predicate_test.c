#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/predicate.h"

int main(void)
{
	const char text[] = "a b";
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "predicates.pigen",
		text, strlen(text));
	pigen_source_span a_span = {source, 0, 1};
	pigen_source_span b_span = {source, 2, 3};
	pigen_semantic_model model;
	pigen_data_type_id integer_type;
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
	integer_type = pigen_data_type_integer(&model);
	a = pigen_expr_add_integer(&model, 1, integer_type, a_span);
	b = pigen_expr_add_integer(&model, 2, integer_type, b_span);
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

	pigen_free_semantic_model(&model);
	pigen_free_sources(&sources);
	puts("PASS: predicates canonicalize guards and prove branch exclusion");
	return 0;
}
