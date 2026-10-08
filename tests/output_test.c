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

	return check_finish();
}
