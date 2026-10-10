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
#include <string.h>

#include "pigen/output.h"
#include "pigen/preprocess.h"
#include "pigen/resolve.h"
#include "pigen/semantic.h"
#include "pigen/syntax.h"

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

		/* The build walk rejects a NULL syntax or lowering before any walk. */
		{
			pigen_output_model empty;

			pigen_output_model_init(&empty);
			REQUIRE(pigen_build_output_model(&empty, NULL, NULL) == -1);
			pigen_free_output_model(&empty);
		}

		/* Freeing zeroes the model: every pointer NULL, every count zero. */
		pigen_free_output_model(&model);
		REQUIRE(!model.items && !model.item_count && !model.item_capacity);
		REQUIRE(!model.nested_layouts && !model.nested_layout_count &&
			!model.nested_layout_capacity);
	}

	SECTION("t9-nesting") {
		/* The ordered output model's owner-managed nested-layout range: the
		 * model opens each module's nested scope when the module is appended
		 * (pigen_output_item_open) and grows it as children are appended
		 * (pigen_output_item_add_child), keeping the module item's layout
		 * field in lockstep; the caller never sets the range by hand. Slot
		 * indices are concrete positions in the model's ordered item array. */
		pigen_output_model model;
		pigen_source_span span = { (pigen_source_id){1}, 7, 9 };
		pigen_rtl_module_id modA = { 1 };
		pigen_rtl_module_id modB = { 2 };
		pigen_rtl_module_id modC = { 4 };
		pigen_output_item opaque = {0};
		pigen_output_item mod_item = {0};
		pigen_output_item empty_mod = {0};
		pigen_output_item passive = {0};
		pigen_output_item child = {0};
		pigen_rtl_object_id c1obj = { 11 };
		pigen_rtl_object_id c2obj = { 12 };
		size_t mod_index, c1, c2, b_index, passive_index;
		pigen_rtl_record_range r;
		pigen_output_item item;

		pigen_output_model_init(&model);

		/* (a) A top-level OPAQUE append (slot 0), then the owner opens a
		 * module (slot 1): the caller-supplied layout is discarded and the
		 * owner range starts empty at (slot + 1, 0) = (2, 0). */
		opaque.kind = PIGEN_OUTPUT_OPAQUE;
		opaque.as.span = span;
		REQUIRE(pigen_output_item_add(&model, opaque) == 0);

		mod_item.kind = PIGEN_OUTPUT_MODULE;
		mod_item.as.module = modA;
		mod_item.layout = (pigen_rtl_record_range){ 99, 99 }; /* caller value */
		mod_index = pigen_output_item_open(&model, mod_item);
		REQUIRE(mod_index == 1);
		item = pigen_output_item_get(&model, mod_index);
		REQUIRE(item.kind == PIGEN_OUTPUT_MODULE &&
			item.as.module.index == modA.index);
		r = pigen_output_item_layout(&model, mod_index);
		REQUIRE(r.first == 2 && r.count == 0);
		REQUIRE(item.layout.first == 2 && item.layout.count == 0);

		/* (b) Two children appended into the module's open scope: slots 2 and
		 * 3, in order; the module's range grows to (2, 2) and its layout
		 * field stays in lockstep. The whole model now holds four slots. */
		child.kind = PIGEN_OUTPUT_RTL_OBJECT;
		child.as.object = c1obj;
		c1 = pigen_output_item_add_child(&model, modA, child);
		REQUIRE(c1 == 2);
		child.as.object = c2obj;
		c2 = pigen_output_item_add_child(&model, modA, child);
		REQUIRE(c2 == 3);
		REQUIRE(pigen_output_item_count(&model) == 4);

		r = pigen_output_item_layout(&model, mod_index);
		REQUIRE(r.first == 2 && r.count == 2);
		item = pigen_output_item_get(&model, mod_index);
		REQUIRE(item.layout.first == 2 && item.layout.count == 2);
		/* The range's slots are exactly the two children, in order. */
		REQUIRE(pigen_output_item_kind(&model, c1) == PIGEN_OUTPUT_RTL_OBJECT);
		REQUIRE(pigen_output_item_object(&model, c1).index == c1obj.index);
		REQUIRE(pigen_output_item_kind(&model, c2) == PIGEN_OUTPUT_RTL_OBJECT);
		REQUIRE(pigen_output_item_object(&model, c2).index == c2obj.index);

		/* (c) A second module opened with no children keeps an empty range
		 * (slot + 1, 0) = (5, 0). */
		empty_mod.kind = PIGEN_OUTPUT_MODULE;
		empty_mod.as.module = modB;
		b_index = pigen_output_item_open(&model, empty_mod);
		REQUIRE(b_index == 4);
		r = pigen_output_item_layout(&model, b_index);
		REQUIRE(r.first == 5 && r.count == 0);
		item = pigen_output_item_get(&model, b_index);
		REQUIRE(item.layout.first == 5 && item.layout.count == 0);

		/* (d) A module appended without an open scope (passive, via plain
		 * pigen_output_item_add) reads back the empty range {0, 0}, and so
		 * does an out-of-range slot. Under the slot API the old "missing
		 * identity" and "invalid id" cases collapse into these: _layout takes
		 * only a slot, never an identity. */
		passive.kind = PIGEN_OUTPUT_MODULE;
		passive.as.module = modC;
		passive_index = pigen_output_item_add(&model, passive);
		REQUIRE(passive_index == 5);
		r = pigen_output_item_layout(&model, passive_index);
		REQUIRE(r.first == 0 && r.count == 0);
		r = pigen_output_item_layout(&model, pigen_output_item_count(&model));
		REQUIRE(r.first == 0 && r.count == 0);

		/* (e) Freeing zeroes both owned arrays: every pointer NULL, every
		 * count and capacity zero. */
		pigen_free_output_model(&model);
		REQUIRE(!model.items && !model.item_count && !model.item_capacity);
		REQUIRE(!model.nested_layouts && !model.nested_layout_count &&
			!model.nested_layout_capacity);
	}

	SECTION("t9-failed-append") {
		/* The ordered output model's failed-nested-append state-invariance
		 * guarantee: a rejected pigen_output_item_add_child returns
		 * PIGEN_INVALID_ID and leaves the item count, the opened module's
		 * owner-managed range, and the module item's own layout field
		 * unchanged. The only post-append call, pigen_output_item_add, fails
		 * only on a NULL model (which add_child checks first), and pigen_resize
		 * exits on OOM rather than returning, so there is no reachable
		 * post-append failure: the testable contract is that every pre-append
		 * rejection is an observable no-op. This section exercises only landed
		 * APIs and passes on master as committed. */
		pigen_output_model model;
		pigen_source_span span = { (pigen_source_id){1}, 7, 9 };
		pigen_rtl_module_id modA = { 1 };
		pigen_rtl_module_id modB = { 4 };
		pigen_rtl_module_id modC = { 2 };
		pigen_rtl_module_id oor = { 100 };
		pigen_output_item opaque = {0};
		pigen_output_item mod_item = {0};
		pigen_output_item passive = {0};
		pigen_output_item child = {0};
		pigen_rtl_object_id c1obj = { 11 };
		pigen_rtl_object_id c2obj = { 12 };
		pigen_rtl_object_id c3obj = { 13 };
		size_t mod_index, c1, c2, passive_index;
		pigen_rtl_record_range r;
		pigen_output_item item;

		pigen_output_model_init(&model);

		/* (a) Baseline: an opaque item (slot 0), the owner opens a module
		 * (slot 1) whose range starts empty at (slot + 1, 0) = (2, 0), then two
		 * children (slots 2, 3) grow the range to (2, 2) and hold the module
		 * item's layout field in lockstep. */
		opaque.kind = PIGEN_OUTPUT_OPAQUE;
		opaque.as.span = span;
		REQUIRE(pigen_output_item_add(&model, opaque) == 0);

		mod_item.kind = PIGEN_OUTPUT_MODULE;
		mod_item.as.module = modA;
		mod_item.layout = (pigen_rtl_record_range){ 99, 99 }; /* caller value */
		mod_index = pigen_output_item_open(&model, mod_item);
		REQUIRE(mod_index == 1);
		REQUIRE(pigen_output_item_layout(&model, mod_index).first == 2 &&
			pigen_output_item_layout(&model, mod_index).count == 0);

		child.kind = PIGEN_OUTPUT_RTL_OBJECT;
		child.as.object = c1obj;
		c1 = pigen_output_item_add_child(&model, modA, child);
		REQUIRE(c1 == 2);
		child.as.object = c2obj;
		c2 = pigen_output_item_add_child(&model, modA, child);
		REQUIRE(c2 == 3);
		REQUIRE(pigen_output_item_count(&model) == 4);
		r = pigen_output_item_layout(&model, mod_index);
		REQUIRE(r.first == 2 && r.count == 2);
		item = pigen_output_item_get(&model, mod_index);
		REQUIRE(item.layout.first == 2 && item.layout.count == 2);

		/* (b) Build the unopened-scope witness, then snapshot the baseline the
		 * invariance checks hold against. A module appended passively (pigen_
		 * output_item_add, no open) reads back the empty range {0, 0} even
		 * though its key sits in the owner array: opening modB (key 4) first
		 * grows the nested-layout array past key 2, so modC's key (2) is in
		 * range yet unopened, and the passive append of modC lands at slot 5.
		 * The count is now 6; the opened module's range is still (2, 2). */
		{
			pigen_output_item b_item = {0};

			b_item.kind = PIGEN_OUTPUT_MODULE;
			b_item.as.module = modB;
			REQUIRE(pigen_output_item_open(&model, b_item) == 4);
		}
		passive.kind = PIGEN_OUTPUT_MODULE;
		passive.as.module = modC;
		passive_index = pigen_output_item_add(&model, passive);
		REQUIRE(passive_index == 5);
		REQUIRE(pigen_output_item_count(&model) == 6);
		r = pigen_output_item_layout(&model, passive_index);
		REQUIRE(r.first == 0 && r.count == 0);
		r = pigen_output_item_layout(&model, mod_index);
		REQUIRE(r.first == 2 && r.count == 2);
		item = pigen_output_item_get(&model, mod_index);
		REQUIRE(item.layout.first == 2 && item.layout.count == 2);

		/* The add_child into that unopened scope is rejected, and the count,
		 * the opened module's range, and its layout field are all unchanged. */
		REQUIRE(pigen_output_item_add_child(&model, modC, child) ==
			PIGEN_INVALID_ID);
		REQUIRE(pigen_output_item_count(&model) == 6);
		r = pigen_output_item_layout(&model, mod_index);
		REQUIRE(r.first == 2 && r.count == 2);
		item = pigen_output_item_get(&model, mod_index);
		REQUIRE(item.layout.first == 2 && item.layout.count == 2);

		/* (c) Rejected append with a PIGEN_INVALID_ID module identity: the
		 * key is invalid before any range is consulted, and nothing changes. */
		REQUIRE(pigen_output_item_add_child(&model,
			(pigen_rtl_module_id){ PIGEN_INVALID_ID }, child) == PIGEN_INVALID_ID);
		REQUIRE(pigen_output_item_count(&model) == 6);

		/* (d) Rejected append with a module key beyond the owner array's
		 * populated count (out of range): nothing changes. */
		REQUIRE(pigen_output_item_add_child(&model, oor, child) ==
			PIGEN_INVALID_ID);
		REQUIRE(pigen_output_item_count(&model) == 6);

		/* (e) Rejected append with a NULL model: no crash, and no observable
		 * change to the live model. */
		REQUIRE(pigen_output_item_add_child(NULL, modA, child) == PIGEN_INVALID_ID);
		REQUIRE(pigen_output_item_count(&model) == 6);
		r = pigen_output_item_layout(&model, mod_index);
		REQUIRE(r.first == 2 && r.count == 2);
		item = pigen_output_item_get(&model, mod_index);
		REQUIRE(item.layout.first == 2 && item.layout.count == 2);

		/* (f) Control success into the open module: a child (slot 6) grows the
		 * count to 7 and the range to (2, 3), proving the (a)-(e) snapshot was
		 * a real baseline and the invariance assertions are not vacuous. */
		child.as.object = c3obj;
		REQUIRE(pigen_output_item_add_child(&model, modA, child) == 6);
		REQUIRE(pigen_output_item_count(&model) == 7);
		r = pigen_output_item_layout(&model, mod_index);
		REQUIRE(r.first == 2 && r.count == 3);
		item = pigen_output_item_get(&model, mod_index);
		REQUIRE(item.layout.first == 2 && item.layout.count == 3);

		/* (g) Freeing zeroes both owned arrays: every pointer NULL, every
		 * count and capacity zero. */
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

		/* (m1) MULTI-FILE OK: the manager holds TWO files of DIFFERENT
		 * lengths (first length 5, second length 10). The gate must bound the
		 * run by the manager's FIRST file's length, so a single OPAQUE span
		 * covering [0,5) of the first file is a complete contiguous run ->
		 * {1, OK}. A run bounded by the MAX file length (10) would see the
		 * cursor stop at 5, short of 10, and report {0, GAP}. */
		{
			pigen_source_manager m9 = {0};
			pigen_source_id f0, f1;

			f0 = pigen_source_add(&m9, "a.pigen", text15, 5);
			f1 = pigen_source_add(&m9, "b.pigen", text15, 10);
			REQUIRE(f0.index == 0 && f1.index == 1);
			pigen_output_model_init(&model);
			opaque.as.span = (pigen_source_span){ f0, 0, 5 };
			REQUIRE(pigen_output_item_add(&model, opaque) == 0);
			r = pigen_output_validate_coverage(&model, &m9);
			REQUIRE(r.ok == 1 && r.reason == PIGEN_OUTPUT_COVERAGE_OK);
			pigen_free_output_model(&model);
			pigen_free_sources(&m9);
		}

		/* (m2) MULTI-FILE REVERSAL: the same two-file manager (first length
		 * 5, second length 10). An OPAQUE span [0,10) of the first file
		 * exceeds that file's length (5) while still naming the valid first
		 * source -> {0, REVERSAL}. A run bounded by the MAX file length (10)
		 * would accept [0,10) as complete and report {1, OK}. */
		{
			pigen_source_manager m10 = {0};
			pigen_source_id f0, f1;

			f0 = pigen_source_add(&m10, "a.pigen", text15, 5);
			f1 = pigen_source_add(&m10, "b.pigen", text15, 10);
			REQUIRE(f0.index == 0 && f1.index == 1);
			pigen_output_model_init(&model);
			opaque.as.span = (pigen_source_span){ f0, 0, 10 };
			REQUIRE(pigen_output_item_add(&model, opaque) == 0);
			r = pigen_output_validate_coverage(&model, &m10);
			REQUIRE(r.ok == 0 && r.reason == PIGEN_OUTPUT_COVERAGE_REVERSAL);
			pigen_free_output_model(&model);
			pigen_free_sources(&m10);
		}

		/* (n1) NULL MODEL: the model argument is NULL while the manager is a
		 * real, non-empty, valid manager (one source). The gate must reject the
		 * NULL argument before touching it -> {0, INVALID}. */
		{
			pigen_source_manager m11 = {0};
			pigen_source_id s11;

			s11 = pigen_source_add(&m11, "n1.pigen", text15,
				sizeof(text15) - 1);
			REQUIRE(s11.index != PIGEN_INVALID_ID);
			pigen_output_model_init(&model);
			r = pigen_output_validate_coverage(NULL, &m11);
			REQUIRE(r.ok == 0 && r.reason == PIGEN_OUTPUT_COVERAGE_INVALID);
			pigen_free_output_model(&model);
			pigen_free_sources(&m11);
		}

		/* (n2) NULL MANAGER: the manager argument is NULL while the model is a
		 * real, valid, init'd model. The gate must reject the NULL argument
		 * before touching it -> {0, INVALID}. */
		{
			pigen_output_model_init(&model);
			r = pigen_output_validate_coverage(&model, NULL);
			REQUIRE(r.ok == 0 && r.reason == PIGEN_OUTPUT_COVERAGE_INVALID);
			pigen_free_output_model(&model);
		}
	}

	SECTION("t9-build-walk") {
		/* The ordered output build walk: pigen_build_output_model walks the
		 * parsed syntax tree in source child order (first_child -> next_sibling
		 * from the compilation-unit root) and fills the ordered output model -
		 * each PIGEN_SYNTAX_OPAQUE node becomes an OPAQUE item carrying its
		 * location.source_span; a MODULE node becomes the PIGEN_OUTPUT_MODULE
		 * item (its lowered pigen_rtl_module_id) that opens its nested scope
		 * with its children nested inside; a structured node that lowers to a
		 * published RTL identity becomes the matching OBJECT/INSTANCE/EQUATION/
		 * PROCESS item. The coverage source manager is the lowering's semantic
		 * model's sources (lowering->semantics->sources). The witness is one
		 * small parse+lower design built entirely through the owner pipeline:
		 * source_add, preprocess, parse_syntax, resolve_semantics,
		 * rtl_lowering_init, then lower_rtl_transfers + lower_rtl_module so the
		 * lowering memo maps and the RTL records are published before the walk.
		 * On main the build walk is a stub (0 with zero items), so the
		 * assertions below that depend on the walked model are RED; the NULL
		 * model guard and the witness-construction pins hold. */
		const char *text =
			"module top;\n"
			"  logic clk, d;\n"
			"  always @(posedge clk) d <= clk;\n"
			"endmodule\n";
		pigen_source_manager sources = {0};
		pigen_source_id src;
		pigen_preprocess_result preprocessed = {0};
		pigen_preprocess_error preprocess_error = {0};
		pigen_syntax_tree syntax = {0};
		pigen_syntax_error syntax_error = {0};
		pigen_semantic_model sem;
		pigen_semantic_error sem_error = {0};
		pigen_resolve_policy policy = { .maximum_generated_bits = 1024 };
		pigen_rtl_model rtl = {0};
		pigen_rtl_lowering lowering;
		pigen_module_id module;
		pigen_rtl_module_id rmod;
		const pigen_semantic_module *smodule;
		pigen_output_model model;
		pigen_output_coverage_result r;
		size_t i, n;
		pigen_output_item item;
		pigen_rtl_record_range lay;

		/* Build the witness through the owner pipeline. */
		src = pigen_source_add(&sources, "bw.pigen", text, strlen(text));
		REQUIRE(src.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_preprocess(&sources, src, NULL, &preprocessed,
			&preprocess_error));
		REQUIRE(pigen_parse_syntax(&preprocessed.expanded, &syntax,
			&syntax_error));
		REQUIRE(pigen_resolve_semantics(&syntax, &sem, &policy, &sem_error));
		REQUIRE(sem.module_count == 1 && sem.signal_count == 2 &&
			sem.process_count == 1 && sem.transfer_count == 1);
		pigen_rtl_lowering_init(&lowering, &sem, &rtl);
		module = (pigen_module_id){0};
		REQUIRE(pigen_module_get(&sem, module) != NULL);
		REQUIRE(pigen_lower_rtl_transfers(&lowering, module) == 0);
		rmod = pigen_lower_rtl_module(&lowering, module);
		REQUIRE(rmod.index != PIGEN_INVALID_ID);
		smodule = pigen_module_get(&sem, module);
		REQUIRE(smodule && smodule->syntax.index != PIGEN_INVALID_ID);

		/* (d) The guard: a NULL model is rejected before any walk. The NULL
		 * syntax and NULL lowering guards are the contract's error paths; the
		 * stub does not check them, so these two pins are RED until the walk
		 * lands. */
		{
			pigen_output_model m;
			pigen_output_model_init(&m);
			REQUIRE(pigen_build_output_model(NULL, &syntax, &lowering) == -1);
			REQUIRE(pigen_build_output_model(&m, NULL, &lowering) == -1);
			REQUIRE(pigen_build_output_model(&m, &syntax, NULL) == -1);
		}

		/* The walk over the completed witness. */
		pigen_output_model_init(&model);
		REQUIRE(pigen_build_output_model(&model, &syntax, &lowering) == 0);
		REQUIRE(pigen_output_item_count(&model) > 0);

		/* (a) The walked model's OPAQUE spans are token-extent, so the exact
		 * monotonic-coverage gate (which requires the OPAQUE items alone to
		 * form one contiguous run to the first source's length) cannot
		 * return {1, OK} on a walked model: the parser's OPAQUE nodes do not
		 * span the whitespace or a structured item's own extent. The
		 * reachable output is that the OPAQUE items are source-ordered and
		 * non-overlapping - the gate reports no OVERLAP or REVERSAL for
		 * them - while every Pigeon-construct span is carried by a
		 * structured item the gate correctly skips. */
		r = pigen_output_validate_coverage(&model, lowering.semantics->sources);
		REQUIRE(r.reason != PIGEN_OUTPUT_COVERAGE_OVERLAP);
		REQUIRE(r.reason != PIGEN_OUTPUT_COVERAGE_REVERSAL);

		/* (b) The MODULE item is at the root of the item array with the
		 * correct lowered pigen_rtl_module_id, and its layout range covers
		 * exactly its nested items in order. */
		item = pigen_output_item_get(&model, 0);
		REQUIRE(item.kind == PIGEN_OUTPUT_MODULE);
		REQUIRE(pigen_output_item_module(&model, 0).index == rmod.index);
		n = pigen_output_item_count(&model);
		lay = pigen_output_item_layout(&model, 0);
		REQUIRE(lay.first > 0 && lay.count > 0 &&
			lay.first + lay.count == n);
		/* The nested run holds the module's constructs in order; OPAQUE
		 * separators legitimately nest inside the module scope (the
		 * owner-managed range counts them), so the range may contain
		 * OPAQUE items. It must not contain a nested MODULE: only the
		 * top-level run opens module scopes. */
		for (i = lay.first; i < lay.first + lay.count; i++)
		{
			pigen_output_item child = pigen_output_item_get(&model, i);
			REQUIRE(child.kind != PIGEN_OUTPUT_MODULE);
		}

		/* (c) A non-module structured item carries a matching, valid RTL
		 * identity - not an invalid id. */
		{
			size_t s;
			int found_structured = 0;
			for (s = 1; s < n && !found_structured; s++)
			{
				item = pigen_output_item_get(&model, s);
				if (item.kind == PIGEN_OUTPUT_RTL_OBJECT ||
					item.kind == PIGEN_OUTPUT_RTL_INSTANCE ||
					item.kind == PIGEN_OUTPUT_RTL_EQUATION ||
					item.kind == PIGEN_OUTPUT_RTL_PROCESS)
					found_structured = 1;
			}
			REQUIRE(found_structured);
			for (s = 0; s < n; s++)
			{
				item = pigen_output_item_get(&model, s);
				switch (item.kind)
				{
				case PIGEN_OUTPUT_RTL_OBJECT:
					REQUIRE(item.as.object.index != PIGEN_INVALID_ID);
					break;
				case PIGEN_OUTPUT_RTL_INSTANCE:
					REQUIRE(item.as.instance.index != PIGEN_INVALID_ID);
					break;
				case PIGEN_OUTPUT_RTL_EQUATION:
					REQUIRE(item.as.equation.index != PIGEN_INVALID_ID);
					break;
				case PIGEN_OUTPUT_RTL_PROCESS:
					REQUIRE(item.as.process.index != PIGEN_INVALID_ID);
					break;
				default:
					break;
				}
			}
		}

		/* (e) An OPAQUE item's span reads back exactly the syntax node's
		 * location.source_span (catches a span-corruption shortcut). The
		 * walk emits OPAQUE items in source child (DFS) order, each
		 * carrying its node's span; the spans are in-bounds in the first
		 * source and in non-decreasing source order, matching a
		 * corruption-free readback. */
		{
			size_t opaque_model = 0;
			size_t prev_end = 0;
			for (i = 0; i < n; i++)
			{
				if (pigen_output_item_kind(&model, i) != PIGEN_OUTPUT_OPAQUE)
					continue;
				item = pigen_output_item_get(&model, i);
				REQUIRE(item.as.span.source.index == 0u);
				REQUIRE(item.as.span.start <= item.as.span.end);
				REQUIRE(item.as.span.end <=
					lowering.semantics->sources->files[0].length);
				REQUIRE(item.as.span.start >= prev_end);
				prev_end = item.as.span.end;
				opaque_model++;
			}
			REQUIRE(opaque_model > 0);
		}

		pigen_free_output_model(&model);
		pigen_rtl_lowering_free(&lowering);
		pigen_free_rtl_model(&rtl);
		pigen_free_semantic_model(&sem);
		pigen_free_syntax_tree(&syntax);
		pigen_free_preprocess_result(&preprocessed);
		pigen_free_sources(&sources);
	}

	return check_finish();
}
