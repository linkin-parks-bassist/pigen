/* Canonical conjunctions for guards and conditional expression evaluation. */
#include <stdlib.h>
#include <string.h>

#include "pigen/predicate.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

static int integral_condition(const pigen_semantic_model *model,
	pigen_expr_id expression)
{
	const pigen_semantic_expr *known = pigen_expr_get(model, expression);
	const pigen_semantic_type *type = known ?
		pigen_type_get(model, known->type) : NULL;
	return type && (type->kind == PIGEN_TYPE_INTEGER ||
		type->kind == PIGEN_TYPE_LOGIC || type->kind == PIGEN_TYPE_BIT);
}

static int same_atoms(const pigen_semantic_model *model,
	const pigen_predicate *known, const pigen_predicate_atom *atoms,
	size_t atom_count)
{
	size_t i;
	if (known->atom_count != atom_count) return 0;
	for (i = 0; i < atom_count; i++)
	{
		const pigen_predicate_atom *stored =
			&model->predicate_atoms[known->first_atom + i];
		if (stored->condition.index != atoms[i].condition.index ||
			stored->expected != atoms[i].expected)
			return 0;
	}
	return 1;
}

static pigen_predicate_id intern_predicate(pigen_semantic_model *model,
	int impossible, const pigen_predicate_atom *atoms, size_t atom_count)
{
	pigen_predicate_id result;
	size_t i;

	if (!model || (impossible != 0 && impossible != 1) ||
		(impossible && atom_count) || (atom_count && !atoms) ||
		model->predicate_count >= PIGEN_INVALID_ID ||
		atom_count > SIZE_MAX - model->predicate_atom_count)
		return INVALID_ID(pigen_predicate_id);
	for (i = 0; i < model->predicate_count; i++)
	{
		const pigen_predicate *known = &model->predicates[i];
		if (known->impossible == impossible &&
			same_atoms(model, known, atoms, atom_count))
			return (pigen_predicate_id){(uint32_t)i};
	}
	if (model->predicate_count == model->predicate_capacity)
	{
		model->predicate_capacity = model->predicate_capacity ?
			model->predicate_capacity * 2 : 16;
		model->predicates = pigen_resize(model->predicates,
			model->predicate_capacity * sizeof(*model->predicates));
	}
	if (model->predicate_atom_count + atom_count >
		model->predicate_atom_capacity)
	{
		size_t capacity = model->predicate_atom_capacity ?
			model->predicate_atom_capacity * 2 : 32;
		while (capacity < model->predicate_atom_count + atom_count)
			capacity *= 2;
		model->predicate_atoms = pigen_resize(model->predicate_atoms,
			capacity * sizeof(*model->predicate_atoms));
		model->predicate_atom_capacity = capacity;
	}
	result = (pigen_predicate_id){(uint32_t)model->predicate_count};
	model->predicates[model->predicate_count++] = (pigen_predicate){
		model->predicate_atom_count, atom_count, impossible};
	if (atom_count)
		memcpy(model->predicate_atoms + model->predicate_atom_count, atoms,
			atom_count * sizeof(*atoms));
	model->predicate_atom_count += atom_count;
	return result;
}

pigen_predicate_id pigen_predicate_true(pigen_semantic_model *model)
{
	if (model && model->true_predicate.index == PIGEN_INVALID_ID)
		model->true_predicate = intern_predicate(model, 0, NULL, 0);
	return model ? model->true_predicate : INVALID_ID(pigen_predicate_id);
}

pigen_predicate_id pigen_predicate_false(pigen_semantic_model *model)
{
	if (model && model->false_predicate.index == PIGEN_INVALID_ID)
		model->false_predicate = intern_predicate(model, 1, NULL, 0);
	return model ? model->false_predicate : INVALID_ID(pigen_predicate_id);
}

pigen_predicate_id pigen_predicate_and_condition(
	pigen_semantic_model *model, pigen_predicate_id predicate,
	pigen_expr_id condition, int expected)
{
	const pigen_predicate *base = pigen_predicate_get(model, predicate);
	const pigen_predicate_atom *known;
	pigen_predicate_atom *atoms;
	pigen_predicate_id result;
	size_t at;

	if (!base || (expected != 0 && expected != 1) ||
		!integral_condition(model, condition))
		return INVALID_ID(pigen_predicate_id);
	if (base->impossible) return predicate;
	known = pigen_predicate_atoms(model, predicate);
	for (at = 0; at < base->atom_count; at++)
	{
		if (known[at].condition.index == condition.index)
			return known[at].expected == expected ? predicate :
				pigen_predicate_false(model);
		if (known[at].condition.index > condition.index) break;
	}
	atoms = pigen_resize(NULL,
		(base->atom_count + 1) * sizeof(*atoms));
	if (at) memcpy(atoms, known, at * sizeof(*atoms));
	atoms[at] = (pigen_predicate_atom){condition, expected};
	if (at < base->atom_count)
		memcpy(atoms + at + 1, known + at,
			(base->atom_count - at) * sizeof(*atoms));
	result = intern_predicate(model, 0, atoms, base->atom_count + 1);
	free(atoms);
	return result;
}

const pigen_predicate *pigen_predicate_get(
	const pigen_semantic_model *model, pigen_predicate_id predicate)
{
	if (!model || predicate.index == PIGEN_INVALID_ID ||
		predicate.index >= model->predicate_count)
		return NULL;
	return &model->predicates[predicate.index];
}

const pigen_predicate_atom *pigen_predicate_atoms(
	const pigen_semantic_model *model, pigen_predicate_id predicate)
{
	const pigen_predicate *known = pigen_predicate_get(model, predicate);
	if (!known || known->first_atom > model->predicate_atom_count ||
		known->atom_count > model->predicate_atom_count - known->first_atom)
		return NULL;
	if (!known->atom_count) return NULL;
	return model->predicate_atoms + known->first_atom;
}

int pigen_predicates_mutually_exclusive(const pigen_semantic_model *model,
	pigen_predicate_id left_id, pigen_predicate_id right_id)
{
	const pigen_predicate *left = pigen_predicate_get(model, left_id);
	const pigen_predicate *right = pigen_predicate_get(model, right_id);
	const pigen_predicate_atom *left_atoms;
	const pigen_predicate_atom *right_atoms;
	size_t i = 0;
	size_t j = 0;

	if (!left || !right) return 0;
	if (left->impossible || right->impossible) return 1;
	left_atoms = pigen_predicate_atoms(model, left_id);
	right_atoms = pigen_predicate_atoms(model, right_id);
	while (i < left->atom_count && j < right->atom_count)
	{
		if (left_atoms[i].condition.index < right_atoms[j].condition.index)
			i++;
		else if (left_atoms[i].condition.index >
			right_atoms[j].condition.index)
			j++;
		else
		{
			if (left_atoms[i].expected != right_atoms[j].expected) return 1;
			i++;
			j++;
		}
	}
	return 0;
}
