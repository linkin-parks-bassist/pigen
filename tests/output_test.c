/* Task 9 ordered output model skeleton.
 *
 * It pins the shape of the ordered output model: the pigen_output_item kind
 * plus exactly one payload (an opaque span, or the matching structured
 * identity copied by value), the ordered slot array the model owns, and the
 * read/append/free contract. It does not test the monotonic-coverage gate,
 * the syntax-tree build walk, nested-failure restoration, the missing-
 * identity diagnostic, or any child/nesting logic; those arrive with the
 * implementation stage. */
#include "check.h"
#include <stddef.h>
#include <stdio.h>

#include "pigen/output.h"

#define IS_INVALID_ID(id) ((id).index == PIGEN_INVALID_ID)

int main(int argc, char **argv)
{
	check_init(argc, argv);

	SECTION("t9-skeleton") {
		pigen_output_model model;
		pigen_source_span span = { (pigen_source_id){1}, 7, 9 };
		pigen_rtl_object_id obj = { 42 };
		pigen_output_item opaque = {0};
		pigen_output_item structured = {0};
		size_t opaque_index, structured_index;
		pigen_source_span read_span, zero_span;
		pigen_rtl_object_id read_obj;

		pigen_output_model_init(&model);

		/* A freshly initialized model has no owned arrays and no counts. */
		REQUIRE(!model.items && !model.item_count && !model.item_capacity);
		REQUIRE(!model.nested_layouts && !model.nested_layout_count &&
			!model.nested_layout_capacity);
		REQUIRE(pigen_output_item_count(&model) == 0);

		/* Append one opaque item (a known span) and one structured item
		 * (a known RTL object identity); both read back exactly what was
		 * stored. */
		opaque.kind = PIGEN_OUTPUT_OPAQUE;
		opaque.as.span = span;
		opaque_index = pigen_output_item_add(&model, opaque);
		REQUIRE(opaque_index == 0);

		structured.kind = PIGEN_OUTPUT_RTL_OBJECT;
		structured.as.object = obj;
		structured_index = pigen_output_item_add(&model, structured);
		REQUIRE(structured_index == 1);
		REQUIRE(pigen_output_item_count(&model) == 2);

		REQUIRE(pigen_output_item_kind(&model, 0) == PIGEN_OUTPUT_OPAQUE);
		read_span = pigen_output_item_span(&model, 0);
		REQUIRE(read_span.source.index == 1 && read_span.start == 7 &&
			read_span.end == 9);

		REQUIRE(pigen_output_item_kind(&model, 1) == PIGEN_OUTPUT_RTL_OBJECT);
		read_obj = pigen_output_item_object(&model, 1);
		REQUIRE(read_obj.index == 42);

		/* Querying an item for a kind it is not returns the invalid identity
		 * (and an invalid span for a non-opaque slot). */
		REQUIRE(IS_INVALID_ID(pigen_output_item_object(&model, 0)));
		REQUIRE(IS_INVALID_ID(pigen_output_item_module(&model, 1)));
		REQUIRE(IS_INVALID_ID(pigen_output_item_instance(&model, 1)));
		REQUIRE(IS_INVALID_ID(pigen_output_item_equation(&model, 1)));
		REQUIRE(IS_INVALID_ID(pigen_output_item_process(&model, 1)));
		zero_span = pigen_output_item_span(&model, 1);
		REQUIRE(zero_span.source.index == PIGEN_INVALID_ID &&
			zero_span.start == 0 && zero_span.end == 0);

		/* The builder stub succeeds with zero items. */
		{
			pigen_output_model empty;

			pigen_output_model_init(&empty);
			REQUIRE(pigen_build_output_model(&empty) == 0);
			REQUIRE(pigen_output_item_count(&empty) == 0);
			pigen_free_output_model(&empty);
		}

		/* Freeing zeroes the model: every pointer NULL, every count zero. */
		pigen_free_output_model(&model);
		REQUIRE(!model.items && !model.item_count && !model.item_capacity);
		REQUIRE(!model.nested_layouts && !model.nested_layout_count &&
			!model.nested_layout_capacity);
	}

	SECTION("t9-nesting") {
		pigen_output_model model;
		pigen_rtl_module_id modA = { 1 };
		pigen_rtl_module_id modB = { 2 };
		pigen_rtl_module_id modC = { 4 };
		pigen_rtl_module_id missing = { 7 };
		pigen_rtl_module_id invalid = { PIGEN_INVALID_ID };
		pigen_source_span span = { (pigen_source_id){5}, 1, 3 };
		pigen_rtl_object_id child_obj = { 9 };
		pigen_rtl_instance_id child_inst = { 11 };
		pigen_rtl_record_range r;
		pigen_output_item opaque, mod_item, child, empty_mod, other_mod;
		size_t opaque_index, mod_index, c1, c2, b_index;
		pigen_rtl_module_id opened;

		pigen_output_model_init(&model);

		/* (a) Top-level append of an opaque item, then a module item opened
		 * by the owner (its layout is owner-managed, not caller-set). */
		opaque.kind = PIGEN_OUTPUT_OPAQUE;
		opaque.as.span = span;
		opaque_index = pigen_output_item_add(&model, opaque);
		REQUIRE(opaque_index == 0);
		REQUIRE(pigen_output_item_kind(&model, 0) == PIGEN_OUTPUT_OPAQUE);

		mod_item.kind = PIGEN_OUTPUT_MODULE;
		mod_item.as.module = modA;
		mod_item.layout = (pigen_rtl_record_range){ 123, 456 };
		opened = pigen_output_item_open(&model, mod_item);
		REQUIRE(opened.index == 1);
		mod_index = pigen_output_item_count(&model) - 1;
		REQUIRE(mod_index == 1);
		/* The caller-supplied layout is discarded; the owner's empty range
		 * (first = slot + 1, count 0) is recorded instead. */
		r = pigen_output_item_layout(&model, modA);
		REQUIRE(r.first == 2 && r.count == 0);
		REQUIRE(pigen_output_item_get(&model, mod_index).layout.first == 2);
		REQUIRE(pigen_output_item_get(&model, mod_index).layout.count == 0);

		/* (b) Appending children into the module's open nested scope records
		 * the range as exactly those children (first = slot + 1, count =
		 * number of children). The children occupy the next slots in order. */
		child.kind = PIGEN_OUTPUT_RTL_OBJECT;
		child.as.object = child_obj;
		c1 = pigen_output_item_add_child(&model, modA, child);
		REQUIRE(c1 == 2);
		child.kind = PIGEN_OUTPUT_RTL_INSTANCE;
		child.as.instance = child_inst;
		c2 = pigen_output_item_add_child(&model, modA, child);
		REQUIRE(c2 == 3);
		REQUIRE(pigen_output_item_count(&model) == 4);
		r = pigen_output_item_layout(&model, modA);
		REQUIRE(r.first == 2 && r.count == 2);
		/* The module item's layout field stays in lockstep with the view. */
		REQUIRE(pigen_output_item_get(&model, mod_index).layout.first == 2);
		REQUIRE(pigen_output_item_get(&model, mod_index).layout.count == 2);
		/* The range's slots are exactly the two children, in order. */
		REQUIRE(pigen_output_item_kind(&model, r.first) ==
			PIGEN_OUTPUT_RTL_OBJECT);
		REQUIRE(pigen_output_item_object(&model, r.first).index == 9);
		REQUIRE(pigen_output_item_kind(&model, r.first + 1) ==
			PIGEN_OUTPUT_RTL_INSTANCE);
		REQUIRE(pigen_output_item_instance(&model, r.first + 1).index == 11);

		/* (c) A module with no children reads back an empty range (count 0).
		 * Opening a second module with no children leaves its range empty
		 * even though the first module has children. */
		empty_mod.kind = PIGEN_OUTPUT_MODULE;
		empty_mod.as.module = modB;
		empty_mod.layout = (pigen_rtl_record_range){ 999, 999 };
		opened = pigen_output_item_open(&model, empty_mod);
		REQUIRE(opened.index == 2);
		b_index = pigen_output_item_count(&model) - 1;
		REQUIRE(b_index == 4);
		r = pigen_output_item_layout(&model, modB);
		REQUIRE(r.first == 5 && r.count == 0);
		REQUIRE(pigen_output_item_get(&model, b_index).layout.first == 5);
		REQUIRE(pigen_output_item_get(&model, b_index).layout.count == 0);

		/* (d) The accessor returns the empty range for a slot with no owned
		 * nested scope and for an out-of-range/invalid slot. A module
		 * appended without an open scope has a slot but no owned range; a
		 * missing identity and the invalid id have no range at all. */
		other_mod.kind = PIGEN_OUTPUT_MODULE;
		other_mod.as.module = modC;
		other_mod.layout = (pigen_rtl_record_range){ 0, 0 };
		pigen_output_item_add(&model, other_mod);
		r = pigen_output_item_layout(&model, modC);
		REQUIRE(r.first == 0 && r.count == 0);
		r = pigen_output_item_layout(&model, missing);
		REQUIRE(r.first == 0 && r.count == 0);
		r = pigen_output_item_layout(&model, invalid);
		REQUIRE(r.first == 0 && r.count == 0);

		/* (e) Freeing zeroes both the item and the nested-layout arrays. */
		pigen_free_output_model(&model);
		REQUIRE(!model.items && !model.item_count && !model.item_capacity);
		REQUIRE(!model.nested_layouts && !model.nested_layout_count &&
			!model.nested_layout_capacity);
	}

	return check_finish();
}
