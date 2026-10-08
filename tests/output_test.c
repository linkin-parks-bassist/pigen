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

	SECTION("t9-coverage") {
		/* The ordered output model's exact monotonic-coverage gate: the
		 * model's OPAQUE spans must form a contiguous, non-overlapping,
		 * monotonically increasing run over one source, bounded by that
		 * source's length. Structured (non-OPAQUE) items are skipped by the
		 * walk and carry no span. */
		const char text15[16] = "abcdefghijklmno";
		const char text20[21] = "abcdefghijklmnopqrst";
		pigen_source_manager manager = {0};
		pigen_source_id src;
		pigen_output_model model;
		pigen_output_item opaque, structured;
		pigen_output_coverage_result r;

		src = pigen_source_add(&manager, "cov.pigen", text15,
			sizeof(text15) - 1);
		REQUIRE(src.index != PIGEN_INVALID_ID);

		opaque.kind = PIGEN_OUTPUT_OPAQUE;
		structured.kind = PIGEN_OUTPUT_RTL_OBJECT;
		structured.as.object = (pigen_rtl_object_id){ 7 };

		/* (a) OK: three OPAQUE spans [0,4),[4,9),[9,15) all from the same
		 * source, end == source length 15 -> {1, OK}. */
		pigen_output_model_init(&model);
		opaque.as.span = (pigen_source_span){ src, 0, 4 };
		REQUIRE(pigen_output_item_add(&model, opaque) == 0);
		opaque.as.span = (pigen_source_span){ src, 4, 9 };
		REQUIRE(pigen_output_item_add(&model, opaque) == 1);
		opaque.as.span = (pigen_source_span){ src, 9, 15 };
		REQUIRE(pigen_output_item_add(&model, opaque) == 2);
		r = pigen_output_validate_coverage(&model, &manager);
		REQUIRE(r.ok == 1 && r.reason == PIGEN_OUTPUT_COVERAGE_OK);
		pigen_free_output_model(&model);

		/* (b) GAP: spans [0,4),[6,9) over a 9-char source (a 2-char hole)
		 * -> {0, GAP}. */
		{
			pigen_source_manager m2 = {0};
			pigen_source_id s2;

			s2 = pigen_source_add(&m2, "gap.pigen", text15, 9);
			pigen_output_model_init(&model);
			opaque.as.span = (pigen_source_span){ s2, 0, 4 };
			REQUIRE(pigen_output_item_add(&model, opaque) == 0);
			opaque.as.span = (pigen_source_span){ s2, 6, 9 };
			REQUIRE(pigen_output_item_add(&model, opaque) == 1);
			r = pigen_output_validate_coverage(&model, &m2);
			REQUIRE(r.ok == 0 && r.reason == PIGEN_OUTPUT_COVERAGE_GAP);
			pigen_free_output_model(&model);
			pigen_free_sources(&m2);
		}

		/* (c) OVERLAP: spans [0,4),[3,9) over a 9-char source (3 < 4)
		 * -> {0, OVERLAP}. */
		{
			pigen_source_manager m3 = {0};
			pigen_source_id s3;

			s3 = pigen_source_add(&m3, "ovl.pigen", text15, 9);
			pigen_output_model_init(&model);
			opaque.as.span = (pigen_source_span){ s3, 0, 4 };
			REQUIRE(pigen_output_item_add(&model, opaque) == 0);
			opaque.as.span = (pigen_source_span){ s3, 3, 9 };
			REQUIRE(pigen_output_item_add(&model, opaque) == 1);
			r = pigen_output_validate_coverage(&model, &m3);
			REQUIRE(r.ok == 0 && r.reason == PIGEN_OUTPUT_COVERAGE_OVERLAP);
			pigen_free_output_model(&model);
			pigen_free_sources(&m3);
		}

		/* (d) REVERSAL: a span [9,9) (end == start) and a span [9,4)
		 * (end < start), each alone -> {0, REVERSAL}. */
		{
			pigen_source_manager m4 = {0};
			pigen_source_id s4;

			s4 = pigen_source_add(&m4, "rev.pigen", text20,
				sizeof(text20) - 1);
			pigen_output_model_init(&model);
			opaque.as.span = (pigen_source_span){ s4, 9, 9 };
			REQUIRE(pigen_output_item_add(&model, opaque) == 0);
			r = pigen_output_validate_coverage(&model, &m4);
			REQUIRE(r.ok == 0 && r.reason == PIGEN_OUTPUT_COVERAGE_REVERSAL);
			pigen_free_output_model(&model);

			pigen_output_model_init(&model);
			opaque.as.span = (pigen_source_span){ s4, 9, 4 };
			REQUIRE(pigen_output_item_add(&model, opaque) == 0);
			r = pigen_output_validate_coverage(&model, &m4);
			REQUIRE(r.ok == 0 && r.reason == PIGEN_OUTPUT_COVERAGE_REVERSAL);
			pigen_free_output_model(&model);
			pigen_free_sources(&m4);
		}

		/* (e) WRONG_SOURCE: a span whose source id is a different, valid,
		 * separately-added source than the manager's first -> {0,
		 * WRONG_SOURCE}. */
		{
			pigen_source_manager m5 = {0};
			pigen_source_id first, other;

			first = pigen_source_add(&m5, "a.pigen", text15,
				sizeof(text15) - 1);
			other = pigen_source_add(&m5, "b.pigen", text15,
				sizeof(text15) - 1);
			pigen_output_model_init(&model);
			opaque.as.span = (pigen_source_span){ other, 0, 4 };
			REQUIRE(pigen_output_item_add(&model, opaque) == 0);
			r = pigen_output_validate_coverage(&model, &m5);
			REQUIRE(r.ok == 0 && r.reason == PIGEN_OUTPUT_COVERAGE_WRONG_SOURCE);
			pigen_free_output_model(&model);
			/* first is the manager's single source; the spans above point at
			 * other, so the manager's own id is only referenced here to keep
			 * it used. */
			REQUIRE(first.index != other.index);
			pigen_free_sources(&m5);
		}

		/* (f) INVALID: an all-invalid span (source id PIGEN_INVALID_ID, 0,0)
		 * -> {0, INVALID}. */
		{
			pigen_source_manager m6 = {0};
			pigen_source_id s6;

			s6 = pigen_source_add(&m6, "inv.pigen", text15,
				sizeof(text15) - 1);
			pigen_output_model_init(&model);
			opaque.as.span = (pigen_source_span){ (pigen_source_id){
				PIGEN_INVALID_ID }, 0, 0 };
			REQUIRE(pigen_output_item_add(&model, opaque) == 0);
			r = pigen_output_validate_coverage(&model, &m6);
			REQUIRE(r.ok == 0 && r.reason == PIGEN_OUTPUT_COVERAGE_INVALID);
			pigen_free_output_model(&model);
			(void)s6;
			pigen_free_sources(&m6);
		}

		/* (g) EMPTY: a model with only structured items, and a freshly
		 * init'd zero-item model, both -> {0, EMPTY}. */
		{
			pigen_source_manager m7 = {0};

			pigen_source_add(&m7, "emp.pigen", text15,
				sizeof(text15) - 1);
			pigen_output_model_init(&model);
			structured.as.object = (pigen_rtl_object_id){ 7 };
			REQUIRE(pigen_output_item_add(&model, structured) == 0);
			r = pigen_output_validate_coverage(&model, &m7);
			REQUIRE(r.ok == 0 && r.reason == PIGEN_OUTPUT_COVERAGE_EMPTY);
			pigen_free_output_model(&model);

			pigen_output_model_init(&model);
			r = pigen_output_validate_coverage(&model, &m7);
			REQUIRE(r.ok == 0 && r.reason == PIGEN_OUTPUT_COVERAGE_EMPTY);
			pigen_free_output_model(&model);
			pigen_free_sources(&m7);
		}

		/* A structured item interleaved between two OPAQUE spans is skipped
		 * by the walk and does not disturb the coverage cursor -> {1, OK}. */
		{
			pigen_source_manager m8 = {0};
			pigen_source_id s8;

			s8 = pigen_source_add(&m8, "mix.pigen", text15,
				sizeof(text15) - 1);
			pigen_output_model_init(&model);
			opaque.as.span = (pigen_source_span){ s8, 0, 4 };
			REQUIRE(pigen_output_item_add(&model, opaque) == 0);
			structured.as.object = (pigen_rtl_object_id){ 7 };
			REQUIRE(pigen_output_item_add(&model, structured) == 1);
			opaque.as.span = (pigen_source_span){ s8, 4, 15 };
			REQUIRE(pigen_output_item_add(&model, opaque) == 2);
			r = pigen_output_validate_coverage(&model, &m8);
			REQUIRE(r.ok == 1 && r.reason == PIGEN_OUTPUT_COVERAGE_OK);
			pigen_free_output_model(&model);
			pigen_free_sources(&m8);
		}
	}

	return check_finish();
}
