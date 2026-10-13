#include "check.h"
#include "pigen/sv_emit.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* The expression renderer is a file-local seam in src/sv_emit.c exposed as
 * pigen_sv_render_expression. This packet pins, by exact rendered string, one
 * case per pigen_rtl_expr kind plus the precedence parenthesization rules the
 * contract requires. On main the renderer is still a -1 stub, so every
 * successful-render assertion below is the deliberate red; the packet lands
 * later with the code that makes it green. Witnesses are built only through the
 * public arena constructors (Relies on list); the negative-literal witness
 * additionally uses pigen_rtl_expr_add_literal, the only public constructor that
 * can carry literal_negative (pigen_rtl_expr_add_integer forces it to zero). */

#define INVALID_EXPR ((pigen_rtl_expr_id){PIGEN_INVALID_ID})

static int expr_is_valid(const pigen_rtl_model *m, pigen_rtl_expr_id id)
{
	return id.index != PIGEN_INVALID_ID && pigen_rtl_expr_get(m, id) != 0;
}

/* A 32-bit 2-state vector type of the requested signedness, interned. The
 * width is a concrete value, so width_expression must be the invalid id (a
 * {0}-init field is index 0, which intern would misread as expression 0). */
static pigen_rtl_type_id vec_type(pigen_rtl_model *m, pigen_signedness sign)
{
	pigen_rtl_type d = {0};

	d.signedness = sign;
	d.state_domain = PIGEN_DATA_TYPE_STATE_TWO;
	d.width = 32;
	d.width_expression = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	return pigen_rtl_type_intern(m, &d);
}

/* Render EXPR and REQUIRE the exact string RESULT. */
static void render_is(const pigen_rtl_model *m, pigen_rtl_expr_id expr,
	const char *result)
{
	char *text = (char *)"sentinel";
	int ret = pigen_sv_render_expression(m,
		pigen_rtl_expr_get(m, expr), &text);

	REQUIRE(ret == 0);
	REQUIRE(text != NULL && text != (char *)"sentinel");
	REQUIRE(strcmp(text, result) == 0);
	free(text);
}

/* Whole-design render for the no-partial-output packet. Pins the contract's
 * failure idiom (how/should/compiler/builders/fail.md): the whole-design
 * emitter validates before committing, so a malformed finished model returns
 * -1 and leaves *output_string NULL even though a renderable prefix was
 * produced. On main the emitter is the unimplemented -1 stub, so this holds;
 * the sibling positive controls below are what fail there. */
static void near_valid(const pigen_source_manager *sources,
	const pigen_output_model *layout, const pigen_rtl_model *rtl)
{
	char *out = (char *)"sentinel";

	REQUIRE(pigen_emit_systemverilog(sources, layout, rtl, &out) == -1);
	REQUIRE(out == NULL);
}

/* Whole-design positive control: a valid, finished, named design must render.
 * Pins the "a valid prefix was renderable" half of the packet -- on main the
 * -1 stub cannot satisfy this, so it is the deliberate red. */
static void renders(const pigen_source_manager *sources,
	const pigen_output_model *layout, const pigen_rtl_model *rtl)
{
	char *out = (char *)"sentinel";

	REQUIRE(pigen_emit_systemverilog(sources, layout, rtl, &out) == 0);
	REQUIRE(out != NULL && out != (char *)"sentinel");
	REQUIRE(out[0] != 0 && out[strlen(out)] == 0);
	free(out);
}

int main(int argc, char **argv)
{
	check_init(argc, argv);

	SECTION("t10-skeleton") {
		pigen_source_manager sources = {0};
		pigen_output_model layout = {0};
		pigen_rtl_model rtl = {0};
		char *out = (char *)"sentinel";
		int ret;

		/* The stub is unimplemented: it returns -1 and leaves
		 * *output_string NULL, no partial output. */
		ret = pigen_emit_systemverilog(&sources, &layout, &rtl, &out);
		REQUIRE(ret == -1);
		REQUIRE(out == NULL);

		/* A NULL out pointer is tolerated (the stub touches nothing). */
		ret = pigen_emit_systemverilog(&sources, &layout, &rtl, NULL);
		REQUIRE(ret == -1);

		(void)out;
	}

	SECTION("t10-expr-render") {
		pigen_source_manager sources = {0};
		pigen_rtl_model rtl = {0};
		pigen_source_id src;
		pigen_source_span span;

		/* One checked source whose identifier span is the OBJECT witness. */
		src = pigen_source_add(&sources, "expr.pigen",
			"module m; logic a; endmodule\n", 29);
		REQUIRE(src.index != PIGEN_INVALID_ID);
		span = (pigen_source_span){src, 16, 17};
		REQUIRE(pigen_source_span_valid(&sources, span));

		/* INTEGER/BITS: a plain literal renders its value; a signed negative
		 * literal renders its signed value. */
		{
			pigen_rtl_type_id t = vec_type(&rtl, PIGEN_SIGN_IMPLICIT);
			pigen_rtl_expr_id plain, neg;

			REQUIRE(t.index != PIGEN_INVALID_ID);
			plain = pigen_rtl_expr_add_integer(&rtl, t, 42, span);
			neg = pigen_rtl_expr_add_literal(&rtl, PIGEN_RTL_EXPR_INTEGER, t,
				&(pigen_rtl_literal_word){7, 0, 0}, 64, 1, span);
			REQUIRE(expr_is_valid(&rtl, plain));
			REQUIRE(expr_is_valid(&rtl, neg));
			render_is(&rtl, plain, "42");
			render_is(&rtl, neg, "-7");
		}

		/* OBJECT: renders the final name whose (role, origin) identity matches
		 * the object (SOURCE role over the object's origin span). */
		{
			pigen_rtl_type_id t = vec_type(&rtl, PIGEN_SIGN_IMPLICIT);
			pigen_rtl_object_id obj;
			pigen_rtl_expr_id expr;
			pigen_rtl_name_request requests[1];
			pigen_rtl_name_id head;
			const pigen_rtl_name *name;

			obj = pigen_rtl_object_add(&rtl, span);
			REQUIRE(obj.index != PIGEN_INVALID_ID);
			expr = pigen_rtl_expr_add_object(&rtl, t, obj, span);
			REQUIRE(expr_is_valid(&rtl, expr));
			requests[0] = (pigen_rtl_name_request){PIGEN_RTL_NAME_SOURCE, span};
			head = pigen_rtl_assign_names(&rtl, &sources, requests, 1);
			REQUIRE(head.index != PIGEN_INVALID_ID);
			name = pigen_rtl_name_get(&rtl, head);
			REQUIRE(name != 0 && strcmp(name->text, "a") == 0);
			render_is(&rtl, expr, name->text);
		}

		/* UNARY: each of the four spelling cases renders its SV form. */
		{
			pigen_rtl_type_id t = vec_type(&rtl, PIGEN_SIGN_IMPLICIT);
			pigen_rtl_expr_id operand;
			pigen_unary_resolution r = {0};

			operand = pigen_rtl_expr_add_integer(&rtl, t, 1, span);
			REQUIRE(expr_is_valid(&rtl, operand));
			r.operation.operator = PIGEN_UNARY_POSITIVE;
			render_is(&rtl,
				pigen_rtl_expr_add_unary(&rtl, t, &r, operand, span), "+1");
			r.operation.operator = PIGEN_UNARY_NEGATE;
			render_is(&rtl,
				pigen_rtl_expr_add_unary(&rtl, t, &r, operand, span), "-1");
			r.operation.operator = PIGEN_UNARY_BITWISE_NOT;
			render_is(&rtl,
				pigen_rtl_expr_add_unary(&rtl, t, &r, operand, span), "~1");
			r.operation.operator = PIGEN_UNARY_LOGICAL_NOT;
			render_is(&rtl,
				pigen_rtl_expr_add_unary(&rtl, t, &r, operand, span), "!1");
		}

		/* BINARY: every required operator at its spelling, plus the
		 * precedence parenthesization rule: a strictly higher-precedence
		 * child renders unparenthesized, a same- or lower-precedence child is
		 * parenthesized. */
		{
			pigen_rtl_type_id t = vec_type(&rtl, PIGEN_SIGN_IMPLICIT);
			pigen_rtl_expr_id a, b, c, d, e, f;
			pigen_binary_resolution r = {0};

			a = pigen_rtl_expr_add_integer(&rtl, t, 1, span);
			b = pigen_rtl_expr_add_integer(&rtl, t, 2, span);
			c = pigen_rtl_expr_add_integer(&rtl, t, 3, span);
			d = pigen_rtl_expr_add_integer(&rtl, t, 4, span);
			e = pigen_rtl_expr_add_integer(&rtl, t, 5, span);
			f = pigen_rtl_expr_add_integer(&rtl, t, 6, span);
			REQUIRE(expr_is_valid(&rtl, a) && expr_is_valid(&rtl, b) &&
				expr_is_valid(&rtl, c) && expr_is_valid(&rtl, d) &&
				expr_is_valid(&rtl, e) && expr_is_valid(&rtl, f));

			r.operation.operator = PIGEN_BINARY_ADD;
			render_is(&rtl,
				pigen_rtl_expr_add_binary(&rtl, t, &r, a, b, span), "1 + 2");
			r.operation.operator = PIGEN_BINARY_SUBTRACT;
			render_is(&rtl,
				pigen_rtl_expr_add_binary(&rtl, t, &r, a, b, span), "1 - 2");
			r.operation.operator = PIGEN_BINARY_MULTIPLY;
			render_is(&rtl,
				pigen_rtl_expr_add_binary(&rtl, t, &r, a, b, span), "1 * 2");
			r.operation.operator = PIGEN_BINARY_POWER;
			render_is(&rtl,
				pigen_rtl_expr_add_binary(&rtl, t, &r, a, b, span), "1 ** 2");
			r.operation.operator = PIGEN_BINARY_LESS;
			render_is(&rtl,
				pigen_rtl_expr_add_binary(&rtl, t, &r, a, b, span), "1 < 2");
			r.operation.operator = PIGEN_BINARY_GREATER_EQUAL;
			render_is(&rtl,
				pigen_rtl_expr_add_binary(&rtl, t, &r, a, b, span),
				"1 >= 2");
			r.operation.operator = PIGEN_BINARY_EQUAL;
			render_is(&rtl,
				pigen_rtl_expr_add_binary(&rtl, t, &r, a, b, span), "1 == 2");
			r.operation.operator = PIGEN_BINARY_LOGICAL_AND;
			render_is(&rtl,
				pigen_rtl_expr_add_binary(&rtl, t, &r, a, b, span), "1 && 2");
			r.operation.operator = PIGEN_BINARY_LOGICAL_OR;
			render_is(&rtl,
				pigen_rtl_expr_add_binary(&rtl, t, &r, a, b, span), "1 || 2");

			/* Higher-precedence child under a lower-precedence parent:
			 * multiplication under addition adds no parens. */
			{
				pigen_rtl_expr_id mul;

				r.operation.operator = PIGEN_BINARY_MULTIPLY;
				mul = pigen_rtl_expr_add_binary(&rtl, t, &r, a, b, span);
				r.operation.operator = PIGEN_BINARY_ADD;
				render_is(&rtl,
					pigen_rtl_expr_add_binary(&rtl, t, &r, mul, c, span),
					"1 * 2 + 3");
			}

			/* Same-precedence child under a same-precedence parent:
			 * addition under addition needs parens. */
			{
				pigen_rtl_expr_id add;

				r.operation.operator = PIGEN_BINARY_ADD;
				add = pigen_rtl_expr_add_binary(&rtl, t, &r, a, b, span);
				render_is(&rtl,
					pigen_rtl_expr_add_binary(&rtl, t, &r, add, c, span),
					"(1 + 2) + 3");
			}

			/* Lower-precedence child under a higher-precedence parent:
			 * logical-and under multiplication needs parens. */
			{
				pigen_rtl_expr_id land;

				r.operation.operator = PIGEN_BINARY_LOGICAL_AND;
				land = pigen_rtl_expr_add_binary(&rtl, t, &r, a, b, span);
				r.operation.operator = PIGEN_BINARY_MULTIPLY;
				render_is(&rtl,
					pigen_rtl_expr_add_binary(&rtl, t, &r, land, c, span),
					"(1 && 2) * 3");
			}
		}

		/* CONDITIONAL: `cond ? a : b`. */
		{
			pigen_rtl_type_id t = vec_type(&rtl, PIGEN_SIGN_IMPLICIT);
			pigen_rtl_expr_id c, a, b;
			pigen_conditional_resolution r = {0};

			c = pigen_rtl_expr_add_integer(&rtl, t, 1, span);
			a = pigen_rtl_expr_add_integer(&rtl, t, 2, span);
			b = pigen_rtl_expr_add_integer(&rtl, t, 3, span);
			REQUIRE(expr_is_valid(&rtl, c) && expr_is_valid(&rtl, a) &&
				expr_is_valid(&rtl, b));
			render_is(&rtl,
				pigen_rtl_expr_add_conditional(&rtl, t, &r, c, a, b, span),
				"1 ? 2 : 3");
		}

		/* CONVERSION: rendered from the RTL operand and the target type only.
		 * A 32-bit signed operand converted to a wider 64-bit signed target is
		 * sign-extended to the target width and carries no semantic-owner
		 * reference (the operand integer's type is 32-bit signed, not 64). */
		{
			pigen_rtl_type_id t32 = vec_type(&rtl, PIGEN_SIGN_SIGNED);
			pigen_rtl_type d64 = {0};
			pigen_rtl_type_id t64;
			pigen_rtl_expr_id operand;
			pigen_conversion cv = {0};

			d64.signedness = PIGEN_SIGN_SIGNED;
			d64.state_domain = PIGEN_DATA_TYPE_STATE_TWO;
			d64.width = 64;
			d64.width_expression = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			t64 = pigen_rtl_type_intern(&rtl, &d64);
			REQUIRE(t32.index != PIGEN_INVALID_ID &&
				t64.index != PIGEN_INVALID_ID);
			operand = pigen_rtl_expr_add_integer(&rtl, t32, 1, span);
			REQUIRE(expr_is_valid(&rtl, operand));
			cv.kind = PIGEN_CONVERSION_INTEGER_RESIZE;
			render_is(&rtl,
				pigen_rtl_expr_add_conversion(&rtl, t64, &cv, operand, span),
				"signed'(1)");
		}

		/* INDEX: `base[idx]`. */
		{
			pigen_rtl_type_id t = vec_type(&rtl, PIGEN_SIGN_IMPLICIT);
			pigen_rtl_expr_id base, idx, expr;

			base = pigen_rtl_expr_add_integer(&rtl, t, 1, span);
			idx = pigen_rtl_expr_add_integer(&rtl, t, 2, span);
			REQUIRE(expr_is_valid(&rtl, base) && expr_is_valid(&rtl, idx));
			expr = pigen_rtl_expr_add_index(&rtl, t, base, idx, span);
			REQUIRE(expr_is_valid(&rtl, expr));
			render_is(&rtl, expr, "1[2]");
		}

		/* SELECT: `[left:right]` for a range and `[left:right]` for an indexed
		 * select, each per as.select.kind. */
		{
			pigen_rtl_type_id t = vec_type(&rtl, PIGEN_SIGN_IMPLICIT);
			pigen_rtl_expr_id base, lo, hi, expr;

			base = pigen_rtl_expr_add_integer(&rtl, t, 1, span);
			lo = pigen_rtl_expr_add_integer(&rtl, t, 2, span);
			hi = pigen_rtl_expr_add_integer(&rtl, t, 3, span);
			REQUIRE(expr_is_valid(&rtl, base) && expr_is_valid(&rtl, lo) &&
				expr_is_valid(&rtl, hi));
			expr = pigen_rtl_expr_add_select(&rtl, t, base, lo, hi,
				PIGEN_SEMANTIC_SELECT_RANGE, span);
			REQUIRE(expr_is_valid(&rtl, expr));
			render_is(&rtl, expr, "1[2:3]");
			expr = pigen_rtl_expr_add_select(&rtl, t, base, lo, hi,
				PIGEN_SEMANTIC_SELECT_INDEXED_UP, span);
			REQUIRE(expr_is_valid(&rtl, expr));
			render_is(&rtl, expr, "1[2:3]");
		}

		/* CONCATENATION: `{a,b,...}` in first_child/child_count order as
		 * returned by pigen_rtl_expr_children. */
		{
			pigen_rtl_type_id t = vec_type(&rtl, PIGEN_SIGN_IMPLICIT);
			pigen_rtl_expr_id c0, c1, c2;
			size_t child_count = 0;
			pigen_rtl_expr_id expr;

			c0 = pigen_rtl_expr_add_integer(&rtl, t, 1, span);
			c1 = pigen_rtl_expr_add_integer(&rtl, t, 2, span);
			c2 = pigen_rtl_expr_add_integer(&rtl, t, 3, span);
			REQUIRE(expr_is_valid(&rtl, c0) && expr_is_valid(&rtl, c1) &&
				expr_is_valid(&rtl, c2));
			expr = pigen_rtl_expr_add_concatenation(&rtl, t,
				(pigen_rtl_expr_id[]){c0, c1, c2}, 3, span);
			REQUIRE(expr_is_valid(&rtl, expr));

			/* The children are stored in first_child/child_count order; a
			 * reversed renderer cannot match this expected text. */
			REQUIRE(pigen_rtl_expr_children(&rtl, expr, &child_count) != 0);
			REQUIRE(child_count == 3);

			render_is(&rtl, expr, "{1,2,3}");
		}

		/* A NULL text out-param is tolerated (no partial output, returns -1). */
		{
			pigen_rtl_type_id t = vec_type(&rtl, PIGEN_SIGN_IMPLICIT);
			pigen_rtl_expr_id plain;
			char *out = (char *)"sentinel";

			plain = pigen_rtl_expr_add_integer(&rtl, t, 9, span);
			REQUIRE(expr_is_valid(&rtl, plain));
			REQUIRE(pigen_sv_render_expression(&rtl,
				pigen_rtl_expr_get(&rtl, plain), NULL) == -1);
			/* A NULL model or expression also fails cleanly. */
			REQUIRE(pigen_sv_render_expression(NULL,
				pigen_rtl_expr_get(&rtl, plain), &out) == -1);
			REQUIRE(out == NULL);
			REQUIRE(pigen_sv_render_expression(&rtl, NULL, &out) == -1);
			REQUIRE(out == NULL);
		}

		pigen_free_rtl_model(&rtl);
		pigen_free_sources(&sources);
	}

	SECTION("t10-no-partial-output") {
		pigen_source_manager sources = {0};
		pigen_rtl_model rtl = {0};
		pigen_rtl_type_id t;
		pigen_rtl_module_id mod;
		pigen_source_id src;
		pigen_rtl_name_id named;

		/* One 15-byte source for the opaque span text and the object
		 * identifier spans; a concrete 32-bit type and an owner module for
		 * the structured witnesses. The layout is built by value: the output
		 * model is a plain struct the emitter walks in array order, and its
		 * constructor API lives in src/output.c, which the sv-emit build
		 * does not link. */
		src = pigen_source_add(&sources, "no.pigen", "abcdefghijklmno", 15);
		REQUIRE(src.index != PIGEN_INVALID_ID);
		t = vec_type(&rtl, PIGEN_SIGN_IMPLICIT);
		REQUIRE(t.index != PIGEN_INVALID_ID);
		mod = pigen_rtl_module_add(&rtl, (pigen_source_span){src, 0, 4});
		REQUIRE(mod.index != PIGEN_INVALID_ID);
		/* Name the single source identifier so the object and equation
		 * witnesses can carry a resolvable final name; the unnamed witness
		 * below deliberately points past it. */
		named = pigen_rtl_assign_names(&rtl, &sources,
			&(pigen_rtl_name_request){
				(pigen_rtl_name_kind)PIGEN_RTL_NAME_SOURCE,
				(pigen_source_span){src, 0, 1}},
			1);
		REQUIRE(named.index != PIGEN_INVALID_ID);

		/* (a) Malformed coverage: the OPAQUE spans do not exactly/
		 * contiguously cover their source -- [0,4) and [4,10) leave a 5-byte
		 * gap before the 15-byte end. The monotonic-coverage gate would report
		 * that, so the emitter must refuse the whole design before committing.
		 * The exact-coverage sibling is the renderable control. */
		{
			pigen_output_item items[2];

			items[0] = (pigen_output_item){
				PIGEN_OUTPUT_OPAQUE, { (pigen_source_span){src, 0, 4} }, {0, 0}};
			items[1] = (pigen_output_item){
				PIGEN_OUTPUT_OPAQUE, { (pigen_source_span){src, 4, 10} }, {0, 0}};
			near_valid(&sources, &(pigen_output_model){
				items, 2, 0, 0, 0, 0}, &rtl);

			/* Control: the same source covered exactly [0,4),[4,10),[10,15)
			 * renders. */
			pigen_output_item ok[3];

			ok[0] = (pigen_output_item){
				PIGEN_OUTPUT_OPAQUE, { (pigen_source_span){src, 0, 4} }, {0, 0}};
			ok[1] = (pigen_output_item){
				PIGEN_OUTPUT_OPAQUE, { (pigen_source_span){src, 4, 10} }, {0, 0}};
			ok[2] = (pigen_output_item){
				PIGEN_OUTPUT_OPAQUE, { (pigen_source_span){src, 10, 15} }, {0, 0}};
			renders(&sources, &(pigen_output_model){
				ok, 3, 0, 0, 0, 0}, &rtl);
		}

		/* (b) Unnamed object: an RTL_OBJECT item whose record has no matching
		 * final name in the names arena -- its origin span points past the
		 * named identifier, so no (role, origin) identity matches. The emitter
		 * must not render it and must commit no partial output. The named
		 * sibling is the renderable control. */
		{
			pigen_rtl_object_id obj_named, obj_unnamed;
			pigen_output_item item;

			obj_named = pigen_rtl_object_add_with_owner(&rtl, mod,
				PIGEN_RTL_OBJECT_VARIABLE, t, PIGEN_SEMANTIC_INPUT,
				(pigen_signal_id){PIGEN_INVALID_ID},
				(pigen_source_span){src, 0, 1});
			obj_unnamed = pigen_rtl_object_add_with_owner(&rtl, mod,
				PIGEN_RTL_OBJECT_VARIABLE, t, PIGEN_SEMANTIC_INPUT,
				(pigen_signal_id){PIGEN_INVALID_ID},
				(pigen_source_span){src, 1, 2});
			REQUIRE(obj_named.index != PIGEN_INVALID_ID &&
				obj_unnamed.index != PIGEN_INVALID_ID);

			item = (pigen_output_item){
				PIGEN_OUTPUT_RTL_OBJECT, { .object = obj_unnamed }, {0, 0}};
			near_valid(&sources, &(pigen_output_model){
				&item, 1, 0, 0, 0, 0}, &rtl);

			/* Control: the object whose origin matches the assigned name
			 * renders. */
			item = (pigen_output_item){
				PIGEN_OUTPUT_RTL_OBJECT, { .object = obj_named }, {0, 0}};
			renders(&sources, &(pigen_output_model){
				&item, 1, 0, 0, 0, 0}, &rtl);
		}

		/* (c) Invalid child: an expression whose first_child points past the
		 * child arena (the record carries a child count but the arena holds no
		 * child at that index). The emitter must validate the child indices
		 * before rendering and commit no partial output. The in-range sibling
		 * is the renderable control. */
		{
			pigen_rtl_object_id dest;
			pigen_rtl_expr_id a, b, val_bad, val_good;
			pigen_rtl_equation_id eq_bad, eq_good;
			pigen_binary_resolution r = {0};
			pigen_rtl_expr *bexpr;
			pigen_output_item item;

			a = pigen_rtl_expr_add_integer(&rtl, t, 1,
				(pigen_source_span){src, 1, 2});
			b = pigen_rtl_expr_add_integer(&rtl, t, 2,
				(pigen_source_span){src, 2, 3});
			REQUIRE(a.index != PIGEN_INVALID_ID &&
				b.index != PIGEN_INVALID_ID);
			dest = pigen_rtl_object_add_with_owner(&rtl, mod,
				PIGEN_RTL_OBJECT_VARIABLE, t, PIGEN_SEMANTIC_INPUT,
				(pigen_signal_id){PIGEN_INVALID_ID},
				(pigen_source_span){src, 0, 1});
			REQUIRE(dest.index != PIGEN_INVALID_ID);

			r.operation.operator = PIGEN_BINARY_ADD;
			val_good = pigen_rtl_expr_add_binary(&rtl, t, &r, a, b,
				(pigen_source_span){src, 1, 3});
			val_bad = pigen_rtl_expr_add_binary(&rtl, t, &r, a, b,
				(pigen_source_span){src, 1, 3});
			REQUIRE(val_good.index != PIGEN_INVALID_ID &&
				val_bad.index != PIGEN_INVALID_ID &&
				val_good.index != val_bad.index);
			eq_good = pigen_rtl_equation_add_with_owner(&rtl, mod, dest,
				val_good, (pigen_source_span){src, 1, 3});
			eq_bad = pigen_rtl_equation_add_with_owner(&rtl, mod, dest,
				val_bad, (pigen_source_span){src, 1, 3});
			REQUIRE(eq_good.index != PIGEN_INVALID_ID &&
				eq_bad.index != PIGEN_INVALID_ID);

			/* val_bad is the second expression with children, so its
			 * first_child is 2 (after a, b) and its count is 2, with 4
			 * children in the arena. Point it past the arena: no child exists
			 * at that index. */
			bexpr = &rtl.expressions[val_bad.index];
			REQUIRE(val_bad.index < rtl.expression_count &&
				bexpr->first_child == 2 && bexpr->child_count == 2 &&
				rtl.expression_child_count == 4);
			bexpr->first_child = 999999;

			/* The witness: the equation whose value expression carries the
			 * corrupted child index. The emitter must refuse before rendering
			 * the (unrenderable) equation and commit no partial output. */
			item = (pigen_output_item){
				PIGEN_OUTPUT_RTL_EQUATION, { .equation = eq_bad }, {0, 0}};
			near_valid(&sources, &(pigen_output_model){
				&item, 1, 0, 0, 0, 0}, &rtl);

			/* Control: the in-range sibling equation renders. */
			item = (pigen_output_item){
				PIGEN_OUTPUT_RTL_EQUATION, { .equation = eq_good }, {0, 0}};
			renders(&sources, &(pigen_output_model){
				&item, 1, 0, 0, 0, 0}, &rtl);
		}

		/* (d) Out-of-bounds span: an OPAQUE span whose end lies past its
		 * source file's length. The emitter must validate the span before
		 * copying its text and commit no partial output. The in-bounds
		 * sibling is the renderable control. */
		{
			pigen_output_item item;

			item = (pigen_output_item){
				PIGEN_OUTPUT_OPAQUE, { (pigen_source_span){src, 10, 100} },
					{0, 0}};
			REQUIRE(pigen_source_span_valid(&sources,
				(pigen_source_span){src, 10, 100}) == 0);
			near_valid(&sources, &(pigen_output_model){
				&item, 1, 0, 0, 0, 0}, &rtl);

			/* Control: an in-bounds span on the same source renders. */
			item = (pigen_output_item){
				PIGEN_OUTPUT_OPAQUE, { (pigen_source_span){src, 0, 15} }, {0, 0}};
			REQUIRE(pigen_source_span_valid(&sources,
				(pigen_source_span){src, 0, 15}) == 1);
			renders(&sources, &(pigen_output_model){
				&item, 1, 0, 0, 0, 0}, &rtl);
		}

		pigen_free_rtl_model(&rtl);
		pigen_free_sources(&sources);
	}

	return check_finish();
}
