/* Task 4 adversarial contract for collision-safe, module-local RTL names.
 *
 * It gates the landed skeleton interface:
 *   pigen_rtl_assign_names(model, requests, count)
 *   pigen_rtl_name_get(model, name)
 *   pigen_rtl_name { origin, kind, text }
 *   pigen_rtl_name_request { kind, origin }
 *
 * The checked source spans below are built with pigen_source_add, so a valid
 * span is exactly what pigen_source_span_valid accepts. The allocator has no
 * source-manager access in the landed interface, so it cannot copy the
 * provenance spelling or bounds-check a span; this contract gates distinct,
 * non-empty, model-owned names plus rejection of a request whose source id is
 * invalid. It does not gate the exact copied spelling or in-bounds span
 * validation (interface gap, see the work report).
 *
 * Staged red (test-contract stage): the checked-accessor asserts and the
 * no-arena-mutation assert on an empty assign pass against the stub. The first
 * behavioral assert (section 1, a successful assignment) is the deliberate
 * red: pigen_rtl_assign_names still returns PIGEN_INVALID_ID, so it publishes
 * nothing and every later section is unreachable until the implementation
 * stage. Sections 2-5 encode the remaining required behavior so the
 * implementer cannot shortcut them; each is asserted as required behavior,
 * never as an imagined implementation. */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "pigen/rtl_name.h"
#include "pigen/source.h"

#define INVALID_NAME ((pigen_rtl_name_id){PIGEN_INVALID_ID})
#define INVALID_SOURCE ((pigen_source_id){PIGEN_INVALID_ID})
#define IS_INVALID_ID(id) ((id).index == PIGEN_INVALID_ID)
#define SAME_NAME(a, b) ((a).index == (b).index)

static int name_nonempty(const pigen_rtl_name *name)
{
	return name && name->text && name->text[0] != '\0';
}

/* Free a name arena built by this test. pigen_free_rtl_model lives in
 * src/rtl.c, which the rtl-name-test target does not link; release the
 * model-owned text bytes and the arena directly. */
static void free_name_arena(pigen_rtl_model *model)
{
	size_t i;

	for (i = 0; i < model->name_count; i++)
		free(model->names[i].text);
	free(model->names);
	model->names = 0;
	model->name_count = 0;
	model->name_capacity = 0;
}

/* Three terminal texts are mutually distinct and all non-empty. */
static int names_three_distinct(const pigen_rtl_name *a, const pigen_rtl_name *b,
	const pigen_rtl_name *c)
{
	return name_nonempty(a) && name_nonempty(b) && name_nonempty(c) &&
		strcmp(a->text, b->text) != 0 &&
		strcmp(a->text, c->text) != 0 &&
		strcmp(b->text, c->text) != 0;
}

int main(void)
{
	pigen_source_manager sources = {0};
	pigen_source_id source;
	pigen_rtl_model model = {0};

	source = pigen_source_add(&sources, "names.pigen",
		"module names; logic value; logic value_valid; "
		"logic value__pigen_valid; endmodule\n", 82);
	assert(pigen_source_span_valid(&sources, (pigen_source_span){source, 20, 25}));
	assert(!pigen_source_span_valid(&sources,
		(pigen_source_span){INVALID_SOURCE, 20, 25}));

	/* Checked accessors and the no-arena-mutation empty assign: these pass
	 * against the stub and must never regress. */
	assert(!pigen_rtl_name_get(&model, INVALID_NAME));
	assert(!pigen_rtl_name_get(&model, (pigen_rtl_name_id){0}));
	assert(!pigen_rtl_name_get(NULL, (pigen_rtl_name_id){0}));
	assert(IS_INVALID_ID(pigen_rtl_assign_names(&model, 0, 0)));
	assert(IS_INVALID_ID(pigen_rtl_assign_names(NULL, 0, 0)));
	assert(!model.name_count && !model.name_capacity);

	/* (1) Collision suffixing. The three distinct source spellings (value,
	 * value_valid, value__pigen_valid) resolve to three distinct non-empty,
	 * model-owned terminal names with no overlap. Request order defines
	 * identity 0,1,2; each source request must yield its own terminal name
	 * and the three must not collide. The allocator has no source-manager
	 * access in the landed interface, so this gates distinctness and
	 * model-ownership, not the exact copied spelling (see work report). */
	{
		const pigen_rtl_name_request requests[3] = {
			{PIGEN_RTL_NAME_SOURCE, {source, 20, 25}},
			{PIGEN_RTL_NAME_SOURCE, {source, 33, 44}},
			{PIGEN_RTL_NAME_SOURCE, {source, 52, 70}},
		};
		const pigen_rtl_name *n0;
		const pigen_rtl_name *n1;
		const pigen_rtl_name *n2;
		pigen_rtl_name_id head;
		size_t before;

		/* The three checked spans are distinct, non-empty source spellings. */
		assert(pigen_source_span_valid(&sources, (pigen_source_span){source, 20, 25}));
		assert(pigen_source_span_valid(&sources, (pigen_source_span){source, 33, 44}));
		assert(pigen_source_span_valid(&sources, (pigen_source_span){source, 52, 70}));

		before = model.name_count;
		head = pigen_rtl_assign_names(&model, requests, 3);
		assert(!IS_INVALID_ID(head)); /* Deliberate red: stub publishes nothing. */
		assert(SAME_NAME(head, (pigen_rtl_name_id){0}));
		assert(model.name_count == before + 3);

		n0 = pigen_rtl_name_get(&model, (pigen_rtl_name_id){0});
		n1 = pigen_rtl_name_get(&model, (pigen_rtl_name_id){1});
		n2 = pigen_rtl_name_get(&model, (pigen_rtl_name_id){2});
		assert(names_three_distinct(n0, n1, n2));
		assert(n0->kind == PIGEN_RTL_NAME_SOURCE &&
			n1->kind == PIGEN_RTL_NAME_SOURCE &&
			n2->kind == PIGEN_RTL_NAME_SOURCE);
		assert(n0->origin.source.index == source.index &&
			n1->origin.source.index == source.index &&
			n2->origin.source.index == source.index);
	}

	/* (2) Repeat stability. Reassigning the same request set yields identical
	 * terminal text and no duplicate-name growth. */
	{
		const pigen_rtl_name_request requests[3] = {
			{PIGEN_RTL_NAME_SOURCE, {source, 20, 25}},
			{PIGEN_RTL_NAME_SOURCE, {source, 33, 44}},
			{PIGEN_RTL_NAME_SOURCE, {source, 52, 70}},
		};
		const pigen_rtl_name *n0;
		const pigen_rtl_name *n1;
		const pigen_rtl_name *n2;
		pigen_rtl_name_id head;
		size_t before;

		n0 = pigen_rtl_name_get(&model, (pigen_rtl_name_id){0});
		n1 = pigen_rtl_name_get(&model, (pigen_rtl_name_id){1});
		n2 = pigen_rtl_name_get(&model, (pigen_rtl_name_id){2});
		before = model.name_count;
		head = pigen_rtl_assign_names(&model, requests, 3);
		assert(!IS_INVALID_ID(head));
		assert(model.name_count == before); /* No duplicate growth. */
		assert(SAME_NAME(head, (pigen_rtl_name_id){0}));
		assert(strcmp(n0->text,
			pigen_rtl_name_get(&model, (pigen_rtl_name_id){0})->text) == 0);
		assert(strcmp(n1->text,
			pigen_rtl_name_get(&model, (pigen_rtl_name_id){1})->text) == 0);
		assert(strcmp(n2->text,
			pigen_rtl_name_get(&model, (pigen_rtl_name_id){2})->text) == 0);
	}

	/* (3) Byte-identical independent builds. A second, separately built model
	 * from the same request set yields byte-identical terminal text. */
	{
		pigen_rtl_model second = {0};
		const pigen_rtl_name_request requests[3] = {
			{PIGEN_RTL_NAME_SOURCE, {source, 20, 25}},
			{PIGEN_RTL_NAME_SOURCE, {source, 33, 44}},
			{PIGEN_RTL_NAME_SOURCE, {source, 52, 70}},
		};
		const pigen_rtl_name *a0;
		const pigen_rtl_name *a1;
		const pigen_rtl_name *a2;
		const pigen_rtl_name *b0;
		const pigen_rtl_name *b1;
		const pigen_rtl_name *b2;
		pigen_rtl_name_id head;

		head = pigen_rtl_assign_names(&second, requests, 3);
		assert(!IS_INVALID_ID(head));
		a0 = pigen_rtl_name_get(&model, (pigen_rtl_name_id){0});
		a1 = pigen_rtl_name_get(&model, (pigen_rtl_name_id){1});
		a2 = pigen_rtl_name_get(&model, (pigen_rtl_name_id){2});
		b0 = pigen_rtl_name_get(&second, (pigen_rtl_name_id){0});
		b1 = pigen_rtl_name_get(&second, (pigen_rtl_name_id){1});
		b2 = pigen_rtl_name_get(&second, (pigen_rtl_name_id){2});
		assert(strcmp(a0->text, b0->text) == 0);
		assert(strcmp(a1->text, b1->text) == 0);
		assert(strcmp(a2->text, b2->text) == 0);
		assert(second.name_count == model.name_count);
		assert(!second.names || second.names != model.names);
		free_name_arena(&second);
	}

	/* (4) Invalid source span publishes no partial names. A SOURCE request
	 * whose span is not checked/valid fails the whole assignment: it returns
	 * an invalid id, leaves name_count unchanged, and leaves every earlier
	 * name retrievable but unchanged (no partial set is published). The
	 * allocator has no source-manager access, so "not checked/valid" here is
	 * a span whose source id is invalid - the only validity it can see. */
	{
		const pigen_rtl_name_request requests[2] = {
			{PIGEN_RTL_NAME_SOURCE, {source, 20, 25}},
			{PIGEN_RTL_NAME_SOURCE, {INVALID_SOURCE, 20, 25}},
		};
		const pigen_rtl_name_request good[1] = {
			{PIGEN_RTL_NAME_SOURCE, {source, 20, 25}},
		};
		const pigen_rtl_name *n0;
		pigen_rtl_name_id head;
		size_t before;

		assert(!pigen_source_span_valid(&sources,
			(pigen_source_span){INVALID_SOURCE, 20, 25}));
		assert(pigen_source_span_valid(&sources,
			(pigen_source_span){source, 20, 25}));

		before = model.name_count;
		n0 = pigen_rtl_name_get(&model, (pigen_rtl_name_id){0});
		head = pigen_rtl_assign_names(&model, requests, 2);
		assert(IS_INVALID_ID(head));
		assert(model.name_count == before);
		assert(pigen_rtl_name_get(&model, (pigen_rtl_name_id){0})->text &&
			strcmp(pigen_rtl_name_get(&model, (pigen_rtl_name_id){0})->text,
				n0->text) == 0);

		/* The single valid request alone is still a successful assignment. */
		head = pigen_rtl_assign_names(&model, good, 1);
		assert(!IS_INVALID_ID(head));
		assert(SAME_NAME(head, (pigen_rtl_name_id){0}));
		assert(model.name_count == before);
	}

	/* (5) Synthetic internal roles. PAYLOAD/VALID/READY/INSTANCE/TEMPORARY
	 * with an invalid source span are accepted and each gets a distinct,
	 * non-empty, derived (model-owned) name; their origin may stay invalid. */
	{
		pigen_rtl_model synth = {0};
		const pigen_rtl_name_request requests[5] = {
			{PIGEN_RTL_NAME_PAYLOAD, {INVALID_SOURCE, 0, 0}},
			{PIGEN_RTL_NAME_VALID, {INVALID_SOURCE, 0, 0}},
			{PIGEN_RTL_NAME_READY, {INVALID_SOURCE, 0, 0}},
			{PIGEN_RTL_NAME_INSTANCE, {INVALID_SOURCE, 0, 0}},
			{PIGEN_RTL_NAME_TEMPORARY, {INVALID_SOURCE, 0, 0}},
		};
		const pigen_rtl_name *n[5];
		pigen_rtl_name_id head;
		size_t i;
		size_t j;

		head = pigen_rtl_assign_names(&synth, requests, 5);
		assert(!IS_INVALID_ID(head));
		assert(synth.name_count == 5);
		for (i = 0; i < 5; i++) {
			n[i] = pigen_rtl_name_get(&synth, (pigen_rtl_name_id){i});
			assert(name_nonempty(n[i]));
			assert(n[i]->origin.source.index == PIGEN_INVALID_ID);
		}
		for (i = 0; i < 5; i++)
			for (j = i + 1; j < 5; j++)
				assert(strcmp(n[i]->text, n[j]->text) != 0);
		free_name_arena(&synth);
	}

	/* Cleanup: the model owns its name texts and releases them. */
	free_name_arena(&model);
	assert(!model.names && !model.name_count && !model.name_capacity);
	pigen_free_sources(&sources);
	puts("PASS: rtl names resolve collisions and hold stable model-owned text");
	puts("PASS: rtl names publish nothing on an invalid source span");
	puts("PASS: rtl names assign derived names to synthetic internal roles");
	return 0;
}
