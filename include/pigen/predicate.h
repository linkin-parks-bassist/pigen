#ifndef PIGEN_PREDICATE_H
#define PIGEN_PREDICATE_H

#include "pigen/semantic.h"

pigen_predicate_id pigen_predicate_true(pigen_semantic_model *model);
pigen_predicate_id pigen_predicate_false(pigen_semantic_model *model);
pigen_predicate_id pigen_predicate_and_condition(
	pigen_semantic_model *model, pigen_predicate_id predicate,
	pigen_expr_id condition, int expected);
const pigen_predicate *pigen_predicate_get(
	const pigen_semantic_model *model, pigen_predicate_id predicate);
const pigen_predicate_atom *pigen_predicate_atoms(
	const pigen_semantic_model *model, pigen_predicate_id predicate);
int pigen_predicates_mutually_exclusive(const pigen_semantic_model *model,
	pigen_predicate_id left, pigen_predicate_id right);

#endif
