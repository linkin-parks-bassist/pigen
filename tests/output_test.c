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
			REQUIRE(pigen_build_output_model(&empty, NULL, NULL) == 0);
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
	}

	return check_finish();
}
