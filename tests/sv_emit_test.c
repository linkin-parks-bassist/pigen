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

int main(int argc, char **argv)
{
	check_init(argc, argv);

	SECTION("t10-skeleton") {
		pigen_source_manager sources = {0};
		pigen_output_model layout = {0};
		pigen_rtl_model rtl = {0};
		char *out = (char *)"sentinel";
		int ret;

		/* The empty (0-item) model is a valid, complete design: a success
		 * verdict always commits a string, never NULL. The emitter must
		 * commit a newly-allocated, NUL-terminated empty string ("") that
		 * the caller frees. On main the whole-design emitter is still the
		 * -1 stub (returns -1 and leaves *output_string NULL), so ret == 0
		 * and the empty-string commit are the deliberate red this packet
		 * pins; it turns green once the guarded emitter's success commit
		 * commits the empty string instead of NULL. */
		ret = pigen_emit_systemverilog(&sources, &layout, &rtl, &out);
		REQUIRE(ret == 0);
		REQUIRE(out != NULL && strcmp(out, "") == 0);
		free(out);

		/* A NULL out pointer is a tolerated no-op: the call still succeeds
		 * (returns 0), writing nothing through that pointer. */
		ret = pigen_emit_systemverilog(&sources, &layout, &rtl, NULL);
		REQUIRE(ret == 0);
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

	return check_finish();
}
