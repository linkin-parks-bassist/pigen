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

	SECTION("t10-golden") {
		/* Whole-design golden emission (Task 10). Build one finished, named
		 * witness by hand and pin the exact SystemVerilog the emitter must
		 * render. The design mixes, in output order: a leading OPAQUE comment,
		 * a MODULE header and its nested items (a signal declaration, a signed
		 * conversion, a concatenation, a primitive/instance parameter, an
		 * equation, and a guarded/clocked update), and a trailing OPAQUE comment.
		 * OPAQUE spans are copied verbatim and byte-exact; the rest is rendered
		 * from the RTL records and their final names. The golden string below is
		 * the contract-faithful rendering; the test is red only because
		 * pigen_emit_systemverilog is still the unimplemented stub (returns -1). */
		pigen_source_manager sources = {0};
		pigen_source_id design;
		pigen_source_id namefile;
		pigen_rtl_model rtl = {0};
		pigen_rtl_type_id t8;
		pigen_rtl_type_id t16;
		pigen_rtl_type_id t1;
		pigen_rtl_module_id module;
		pigen_rtl_object_id in_a;
		pigen_rtl_object_id out_b;
		pigen_rtl_object_id clk;
		pigen_rtl_object_id c_obj;
		pigen_rtl_object_id d_obj;
		pigen_rtl_object_id e_obj;
		pigen_rtl_instance_id fifo;
		pigen_rtl_equation_id eq_conv;
		pigen_rtl_equation_id eq_concat;
		pigen_rtl_update_id up;
		pigen_rtl_process_id proc;
		pigen_rtl_expr_id a_obj_expr;
		pigen_rtl_expr_id conv_expr;
		pigen_rtl_expr_id c_obj_expr;
		pigen_rtl_expr_id e_obj_expr;
		pigen_rtl_expr_id concat_expr;
		pigen_rtl_expr_id param_expr;
		pigen_rtl_expr_id clk_expr;
		pigen_rtl_expr_id d_obj_expr;
		pigen_output_item items[13];
		pigen_rtl_record_range nested[1];
		pigen_output_model layout;
		pigen_rtl_name_request requests[8];
		const char golden[] =
			"// leading golden comment\n"
			"module top (\n"
			"  input  logic [7:0] a,\n"
			"  output logic [7:0] b,\n"
			"  input  logic clk\n"
			");\n"
			"  logic [7:0] c;\n"
			"  logic [7:0] d;\n"
			"  logic [7:0] e;\n"
			"  instance #(8) (.clk(clk));\n"
			"  assign b = signed'(16'(a));\n"
			"  assign d = {c,e};\n"
			"  always @(posedge clk)\n"
			"    e <= d;\n"
			"endmodule\n"
			"// trailing golden comment\n";
		char *out = (char *)"sentinel";
		int ret;

		design = pigen_source_add(&sources, "golden.pigen",
			"// leading golden comment\nmodule top;\nendmodule\n"
			"// trailing golden comment\n", 75);
		namefile = pigen_source_add(&sources, "names.pigen",
			"top a b c d e clk\n", 18);

		/* The design source's four spans are the two opaque comments (byte-
		 * exact), the module header and the endmodule keyword. The name source
		 * carries each checked identifier spelling for the final names. */
		REQUIRE(pigen_source_span_valid(&sources,
			(pigen_source_span){design, 0, 26}));
		REQUIRE(pigen_source_span_valid(&sources,
			(pigen_source_span){design, 26, 37}));
		REQUIRE(pigen_source_span_valid(&sources,
			(pigen_source_span){design, 38, 48}));
		REQUIRE(pigen_source_span_valid(&sources,
			(pigen_source_span){design, 48, 75}));
		REQUIRE(pigen_source_span_valid(&sources,
			(pigen_source_span){namefile, 0, 3}));
		REQUIRE(pigen_source_span_valid(&sources,
			(pigen_source_span){namefile, 14, 17}));

		/* Types: an unsigned 8-bit logic, a signed 16-bit logic (the conversion
		 * target), and an unsigned 1-bit logic (the clock). */
		{
			pigen_rtl_type d8 = {PIGEN_SIGN_UNSIGNED,
				PIGEN_DATA_TYPE_STATE_TWO, 8, (pigen_rtl_expr_id){PIGEN_INVALID_ID},
				(pigen_source_span){namefile, 0, 1}, 0, 0};
			pigen_rtl_type d16 = {PIGEN_SIGN_SIGNED,
				PIGEN_DATA_TYPE_STATE_TWO, 16, (pigen_rtl_expr_id){PIGEN_INVALID_ID},
				(pigen_source_span){namefile, 0, 1}, 0, 0};
			pigen_rtl_type d1 = {PIGEN_SIGN_UNSIGNED,
				PIGEN_DATA_TYPE_STATE_TWO, 1, (pigen_rtl_expr_id){PIGEN_INVALID_ID},
				(pigen_source_span){namefile, 0, 1}, 0, 0};

			t8 = pigen_rtl_type_intern(&rtl, &d8);
			t16 = pigen_rtl_type_intern(&rtl, &d16);
			t1 = pigen_rtl_type_intern(&rtl, &d1);
			REQUIRE(t8.index != PIGEN_INVALID_ID);
			REQUIRE(t16.index != PIGEN_INVALID_ID);
			REQUIRE(t1.index != PIGEN_INVALID_ID);
		}

		/* The module and its objects, in owner order: the three ports, then the
		 * three internal signals. Origins are checked spans; the final names come
		 * from the name arena, not these spans' text. */
		module = pigen_rtl_module_add_with_owner(&rtl, (pigen_module_id){700},
			(pigen_source_span){design, 26, 37});
		REQUIRE(module.index != PIGEN_INVALID_ID);
		in_a = pigen_rtl_object_add_with_owner(&rtl, module,
			PIGEN_RTL_OBJECT_VARIABLE, t8, PIGEN_SEMANTIC_INPUT,
			(pigen_signal_id){701}, (pigen_source_span){namefile, 4, 5});
		REQUIRE(in_a.index != PIGEN_INVALID_ID);
		out_b = pigen_rtl_object_add_with_owner(&rtl, module,
			PIGEN_RTL_OBJECT_VARIABLE, t8, PIGEN_SEMANTIC_OUTPUT,
			(pigen_signal_id){702}, (pigen_source_span){namefile, 6, 7});
		REQUIRE(out_b.index != PIGEN_INVALID_ID);
		clk = pigen_rtl_object_add_with_owner(&rtl, module,
			PIGEN_RTL_OBJECT_VARIABLE, t1, PIGEN_SEMANTIC_INPUT,
			(pigen_signal_id){703}, (pigen_source_span){namefile, 14, 17});
		REQUIRE(clk.index != PIGEN_INVALID_ID);
		c_obj = pigen_rtl_object_add_with_owner(&rtl, module,
			PIGEN_RTL_OBJECT_VARIABLE, t8, PIGEN_SEMANTIC_INTERNAL,
			(pigen_signal_id){PIGEN_INVALID_ID},
			(pigen_source_span){namefile, 8, 9});
		REQUIRE(c_obj.index != PIGEN_INVALID_ID);
		d_obj = pigen_rtl_object_add_with_owner(&rtl, module,
			PIGEN_RTL_OBJECT_VARIABLE, t8, PIGEN_SEMANTIC_INTERNAL,
			(pigen_signal_id){PIGEN_INVALID_ID},
			(pigen_source_span){namefile, 10, 11});
		REQUIRE(d_obj.index != PIGEN_INVALID_ID);
		e_obj = pigen_rtl_object_add_with_owner(&rtl, module,
			PIGEN_RTL_OBJECT_VARIABLE, t8, PIGEN_SEMANTIC_INTERNAL,
			(pigen_signal_id){PIGEN_INVALID_ID},
			(pigen_source_span){namefile, 12, 13});
		REQUIRE(e_obj.index != PIGEN_INVALID_ID);

		/* Object expressions reference the objects by identity; the emitter
		 * renders each from its final name. */
		a_obj_expr = pigen_rtl_expr_add_object(&rtl, t8, in_a,
			(pigen_source_span){namefile, 4, 5});
		REQUIRE(a_obj_expr.index != PIGEN_INVALID_ID);
		c_obj_expr = pigen_rtl_expr_add_object(&rtl, t8, c_obj,
			(pigen_source_span){namefile, 8, 9});
		REQUIRE(c_obj_expr.index != PIGEN_INVALID_ID);
		e_obj_expr = pigen_rtl_expr_add_object(&rtl, t8, e_obj,
			(pigen_source_span){namefile, 12, 13});
		REQUIRE(e_obj_expr.index != PIGEN_INVALID_ID);
		clk_expr = pigen_rtl_expr_add_object(&rtl, t1, clk,
			(pigen_source_span){namefile, 14, 17});
		REQUIRE(clk_expr.index != PIGEN_INVALID_ID);
		d_obj_expr = pigen_rtl_expr_add_object(&rtl, t8, d_obj,
			(pigen_source_span){namefile, 10, 11});
		REQUIRE(d_obj_expr.index != PIGEN_INVALID_ID);

		/* A signed conversion of a (operand) to the 16-bit signed target type:
		 * rendered from the operand and the conversion node's target type. */
		{
			pigen_conversion conv = {PIGEN_CONVERSION_INTEGER_REINTERPRET,
				(pigen_data_type_id){PIGEN_INVALID_ID},
				(pigen_data_type_id){PIGEN_INVALID_ID}};

			conv_expr = pigen_rtl_expr_add_conversion(&rtl, t16, &conv,
				a_obj_expr, (pigen_source_span){namefile, 4, 5});
			REQUIRE(conv_expr.index != PIGEN_INVALID_ID);
		}

		/* A concatenation {c, e} over its two children. */
		{
			pigen_rtl_expr_id parts[2];

			parts[0] = c_obj_expr;
			parts[1] = e_obj_expr;
			concat_expr = pigen_rtl_expr_add_concatenation(&rtl, t8, parts, 2,
				(pigen_source_span){namefile, 8, 9});
			REQUIRE(concat_expr.index != PIGEN_INVALID_ID);
		}

		/* A primitive/instance parameter: an integer literal the instance
		 * carries in its ordered parameter range. */
		param_expr = pigen_rtl_expr_add_integer(&rtl, t8, 8,
			(pigen_source_span){namefile, 0, 1});
		REQUIRE(param_expr.index != PIGEN_INVALID_ID);

		/* The primitive instance: one parameter and a single connection to the
		 * clock object. Its origin is invalid, so its final name is the derived
		 * INSTANCE role name (the role stem), matching the golden's "instance". */
		{
			pigen_rtl_expr_id params[1];
			pigen_rtl_object_id conns[1];

			params[0] = param_expr;
			conns[0] = clk;
			fifo = pigen_rtl_instance_add_with_owner(&rtl, module,
				(pigen_module_id){710}, params, 1, conns, 1,
				(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
			REQUIRE(fifo.index != PIGEN_INVALID_ID);
		}

		/* The two continuous equations, in module order: the conversion and the
		 * concatenation. */
		eq_conv = pigen_rtl_equation_add_with_owner(&rtl, module, out_b,
			conv_expr, (pigen_source_span){design, 26, 37});
		REQUIRE(eq_conv.index != PIGEN_INVALID_ID);
		eq_concat = pigen_rtl_equation_add_with_owner(&rtl, module, d_obj,
			concat_expr, (pigen_source_span){design, 26, 37});
		REQUIRE(eq_concat.index != PIGEN_INVALID_ID);

		/* The guarded/clocked update, added immediately before its process so
		 * the process's update range is the last update in the arena. */
		up = pigen_rtl_update_add_with_owner(&rtl, module, e_obj, d_obj_expr,
			(pigen_source_span){design, 38, 48});
		REQUIRE(up.index != PIGEN_INVALID_ID);
		{
			pigen_rtl_update_id updates[1];

			updates[0] = up;
			proc = pigen_rtl_process_add_with_owner(&rtl, module, clk_expr,
				PIGEN_SEMANTIC_POSEDGE, updates, 1,
				(pigen_source_span){design, 38, 48});
			REQUIRE(proc.index != PIGEN_INVALID_ID);
		}

		/* The final names: each SOURCE request copies the checked identifier
		 * spelling of its span. The objects and module use checked spans; the
		 * instance is a synthetic internal role (invalid span) whose name is the
		 * derived role stem "instance". */
		for (int r = 0; r < 7; r++)
			requests[r].kind = PIGEN_RTL_NAME_SOURCE;
		requests[7].kind = PIGEN_RTL_NAME_INSTANCE;
		requests[0].origin = (pigen_source_span){namefile, 4, 5}; /* a */
		requests[1].origin = (pigen_source_span){namefile, 6, 7}; /* b */
		requests[2].origin = (pigen_source_span){namefile, 14, 17}; /* clk */
		requests[3].origin = (pigen_source_span){namefile, 8, 9}; /* c */
		requests[4].origin = (pigen_source_span){namefile, 10, 11}; /* d */
		requests[5].origin = (pigen_source_span){namefile, 12, 13}; /* e */
		requests[6].origin = (pigen_source_span){namefile, 0, 3}; /* top */
		requests[7].origin =
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};
		REQUIRE(pigen_rtl_assign_names(&rtl, &sources, requests, 8).index !=
			PIGEN_INVALID_ID);

		/* The ordered output model: a leading opaque comment, the module (with
		 * its ten nested items in emission order: the six signal declarations,
		 * the primitive, the two equations and the process), and a trailing
		 * opaque comment. The owner-managed nested range is keyed by the module
		 * identity; the module item's layout field mirrors it. */
		items[0].kind = PIGEN_OUTPUT_OPAQUE;
		items[0].as.span = (pigen_source_span){design, 0, 26};
		items[1].kind = PIGEN_OUTPUT_MODULE;
		items[1].as.module = module;
		items[1].layout = (pigen_rtl_record_range){2, 10};
		items[2].kind = PIGEN_OUTPUT_RTL_OBJECT;
		items[2].as.object = in_a;
		items[3].kind = PIGEN_OUTPUT_RTL_OBJECT;
		items[3].as.object = out_b;
		items[4].kind = PIGEN_OUTPUT_RTL_OBJECT;
		items[4].as.object = clk;
		items[5].kind = PIGEN_OUTPUT_RTL_OBJECT;
		items[5].as.object = c_obj;
		items[6].kind = PIGEN_OUTPUT_RTL_OBJECT;
		items[6].as.object = d_obj;
		items[7].kind = PIGEN_OUTPUT_RTL_OBJECT;
		items[7].as.object = e_obj;
		items[8].kind = PIGEN_OUTPUT_RTL_INSTANCE;
		items[8].as.instance = fifo;
		items[9].kind = PIGEN_OUTPUT_RTL_EQUATION;
		items[9].as.equation = eq_conv;
		items[10].kind = PIGEN_OUTPUT_RTL_EQUATION;
		items[10].as.equation = eq_concat;
		items[11].kind = PIGEN_OUTPUT_RTL_PROCESS;
		items[11].as.process = proc;
		items[12].kind = PIGEN_OUTPUT_OPAQUE;
		items[12].as.span = (pigen_source_span){design, 48, 75};
		nested[0] = (pigen_rtl_record_range){2, 10};
		layout.items = items;
		layout.item_count = 13;
		layout.item_capacity = 13;
		layout.nested_layouts = nested;
		layout.nested_layout_count = 1;
		layout.nested_layout_capacity = 1;

		/* The whole-design emission must be exactly the golden string: leading
		 * and trailing OPAQUE spans copied verbatim and byte-exact, the module
		 * header and its nested items, the signal declarations, the signed
		 * conversion, the concatenation, the primitive parameter, the equation,
		 * and the guarded/clocked update. Red on main: the stub returns -1 and
		 * leaves *output_string NULL. */
		ret = pigen_emit_systemverilog(&sources, &layout, &rtl, &out);
		REQUIRE(ret == 0);
		REQUIRE(out != NULL);
		REQUIRE(strcmp(out, golden) == 0);
		if (out)
			free(out);

		pigen_free_rtl_model(&rtl);
		pigen_free_sources(&sources);
	}

	SECTION("t10-golden") {
		/* Whole-design golden emission (Task 10). Build one finished, named
		 * witness by hand and pin the exact SystemVerilog the emitter must
		 * render. The design mixes, in output order: a leading OPAQUE comment,
		 * a MODULE header and its nested items (a signal declaration, a signed
		 * conversion, a concatenation, a primitive/instance parameter, an
		 * equation, and a guarded/clocked update), and a trailing OPAQUE comment.
		 * OPAQUE spans are copied verbatim and byte-exact; the rest is rendered
		 * from the RTL records and their final names. The golden string below is
		 * the contract-faithful rendering; the test is red only because
		 * pigen_emit_systemverilog is still the unimplemented stub (returns -1). */
		pigen_source_manager sources = {0};
		pigen_source_id design;
		pigen_source_id namefile;
		pigen_rtl_model rtl = {0};
		pigen_rtl_type_id t8;
		pigen_rtl_type_id t16;
		pigen_rtl_type_id t1;
		pigen_rtl_module_id module;
		pigen_rtl_object_id in_a;
		pigen_rtl_object_id out_b;
		pigen_rtl_object_id clk;
		pigen_rtl_object_id c_obj;
		pigen_rtl_object_id d_obj;
		pigen_rtl_object_id e_obj;
		pigen_rtl_instance_id fifo;
		pigen_rtl_equation_id eq_conv;
		pigen_rtl_equation_id eq_concat;
		pigen_rtl_update_id up;
		pigen_rtl_process_id proc;
		pigen_rtl_expr_id a_obj_expr;
		pigen_rtl_expr_id conv_expr;
		pigen_rtl_expr_id c_obj_expr;
		pigen_rtl_expr_id e_obj_expr;
		pigen_rtl_expr_id concat_expr;
		pigen_rtl_expr_id param_expr;
		pigen_rtl_expr_id clk_expr;
		pigen_rtl_expr_id d_obj_expr;
		pigen_output_item items[13];
		pigen_rtl_record_range nested[1];
		pigen_output_model layout;
		pigen_rtl_name_request requests[8];
		const char golden[] =
			"// leading golden comment\n"
			"module top (\n"
			"  input  logic [7:0] a,\n"
			"  output logic [7:0] b,\n"
			"  input  logic clk\n"
			");\n"
			"  logic [7:0] c;\n"
			"  logic [7:0] d;\n"
			"  logic [7:0] e;\n"
			"  instance #(8) (.clk(clk));\n"
			"  assign b = signed'(16'(a));\n"
			"  assign d = {c,e};\n"
			"  always @(posedge clk)\n"
			"    e <= d;\n"
			"endmodule\n"
			"// trailing golden comment\n";
		char *out = (char *)"sentinel";
		int ret;

		design = pigen_source_add(&sources, "golden.pigen",
			"// leading golden comment\nmodule top;\nendmodule\n"
			"// trailing golden comment\n", 75);
		namefile = pigen_source_add(&sources, "names.pigen",
			"top a b c d e clk\n", 18);

		/* The design source's four spans are the two opaque comments (byte-
		 * exact), the module header and the endmodule keyword. The name source
		 * carries each checked identifier spelling for the final names. */
		REQUIRE(pigen_source_span_valid(&sources,
			(pigen_source_span){design, 0, 26}));
		REQUIRE(pigen_source_span_valid(&sources,
			(pigen_source_span){design, 26, 37}));
		REQUIRE(pigen_source_span_valid(&sources,
			(pigen_source_span){design, 38, 48}));
		REQUIRE(pigen_source_span_valid(&sources,
			(pigen_source_span){design, 48, 75}));
		REQUIRE(pigen_source_span_valid(&sources,
			(pigen_source_span){namefile, 0, 3}));
		REQUIRE(pigen_source_span_valid(&sources,
			(pigen_source_span){namefile, 14, 17}));

		/* Types: an unsigned 8-bit logic, a signed 16-bit logic (the conversion
		 * target), and an unsigned 1-bit logic (the clock). */
		{
			pigen_rtl_type d8 = {PIGEN_SIGN_UNSIGNED,
				PIGEN_DATA_TYPE_STATE_TWO, 8, (pigen_rtl_expr_id){PIGEN_INVALID_ID},
				(pigen_source_span){namefile, 0, 1}, 0, 0};
			pigen_rtl_type d16 = {PIGEN_SIGN_SIGNED,
				PIGEN_DATA_TYPE_STATE_TWO, 16, (pigen_rtl_expr_id){PIGEN_INVALID_ID},
				(pigen_source_span){namefile, 0, 1}, 0, 0};
			pigen_rtl_type d1 = {PIGEN_SIGN_UNSIGNED,
				PIGEN_DATA_TYPE_STATE_TWO, 1, (pigen_rtl_expr_id){PIGEN_INVALID_ID},
				(pigen_source_span){namefile, 0, 1}, 0, 0};

			t8 = pigen_rtl_type_intern(&rtl, &d8);
			t16 = pigen_rtl_type_intern(&rtl, &d16);
			t1 = pigen_rtl_type_intern(&rtl, &d1);
			REQUIRE(t8.index != PIGEN_INVALID_ID);
			REQUIRE(t16.index != PIGEN_INVALID_ID);
			REQUIRE(t1.index != PIGEN_INVALID_ID);
		}

		/* The module and its objects, in owner order: the three ports, then the
		 * three internal signals. Origins are checked spans; the final names come
		 * from the name arena, not these spans' text. */
		module = pigen_rtl_module_add_with_owner(&rtl, (pigen_module_id){700},
			(pigen_source_span){design, 26, 37});
		REQUIRE(module.index != PIGEN_INVALID_ID);
		in_a = pigen_rtl_object_add_with_owner(&rtl, module,
			PIGEN_RTL_OBJECT_VARIABLE, t8, PIGEN_SEMANTIC_INPUT,
			(pigen_signal_id){701}, (pigen_source_span){namefile, 4, 5});
		REQUIRE(in_a.index != PIGEN_INVALID_ID);
		out_b = pigen_rtl_object_add_with_owner(&rtl, module,
			PIGEN_RTL_OBJECT_VARIABLE, t8, PIGEN_SEMANTIC_OUTPUT,
			(pigen_signal_id){702}, (pigen_source_span){namefile, 6, 7});
		REQUIRE(out_b.index != PIGEN_INVALID_ID);
		clk = pigen_rtl_object_add_with_owner(&rtl, module,
			PIGEN_RTL_OBJECT_VARIABLE, t1, PIGEN_SEMANTIC_INPUT,
			(pigen_signal_id){703}, (pigen_source_span){namefile, 14, 17});
		REQUIRE(clk.index != PIGEN_INVALID_ID);
		c_obj = pigen_rtl_object_add_with_owner(&rtl, module,
			PIGEN_RTL_OBJECT_VARIABLE, t8, PIGEN_SEMANTIC_INTERNAL,
			(pigen_signal_id){PIGEN_INVALID_ID},
			(pigen_source_span){namefile, 8, 9});
		REQUIRE(c_obj.index != PIGEN_INVALID_ID);
		d_obj = pigen_rtl_object_add_with_owner(&rtl, module,
			PIGEN_RTL_OBJECT_VARIABLE, t8, PIGEN_SEMANTIC_INTERNAL,
			(pigen_signal_id){PIGEN_INVALID_ID},
			(pigen_source_span){namefile, 10, 11});
		REQUIRE(d_obj.index != PIGEN_INVALID_ID);
		e_obj = pigen_rtl_object_add_with_owner(&rtl, module,
			PIGEN_RTL_OBJECT_VARIABLE, t8, PIGEN_SEMANTIC_INTERNAL,
			(pigen_signal_id){PIGEN_INVALID_ID},
			(pigen_source_span){namefile, 12, 13});
		REQUIRE(e_obj.index != PIGEN_INVALID_ID);

		/* Object expressions reference the objects by identity; the emitter
		 * renders each from its final name. */
		a_obj_expr = pigen_rtl_expr_add_object(&rtl, t8, in_a,
			(pigen_source_span){namefile, 4, 5});
		REQUIRE(a_obj_expr.index != PIGEN_INVALID_ID);
		c_obj_expr = pigen_rtl_expr_add_object(&rtl, t8, c_obj,
			(pigen_source_span){namefile, 8, 9});
		REQUIRE(c_obj_expr.index != PIGEN_INVALID_ID);
		e_obj_expr = pigen_rtl_expr_add_object(&rtl, t8, e_obj,
			(pigen_source_span){namefile, 12, 13});
		REQUIRE(e_obj_expr.index != PIGEN_INVALID_ID);
		clk_expr = pigen_rtl_expr_add_object(&rtl, t1, clk,
			(pigen_source_span){namefile, 14, 17});
		REQUIRE(clk_expr.index != PIGEN_INVALID_ID);
		d_obj_expr = pigen_rtl_expr_add_object(&rtl, t8, d_obj,
			(pigen_source_span){namefile, 10, 11});
		REQUIRE(d_obj_expr.index != PIGEN_INVALID_ID);

		/* A signed conversion of a (operand) to the 16-bit signed target type:
		 * rendered from the operand and the conversion node's target type. */
		{
			pigen_conversion conv = {PIGEN_CONVERSION_INTEGER_REINTERPRET,
				(pigen_data_type_id){PIGEN_INVALID_ID},
				(pigen_data_type_id){PIGEN_INVALID_ID}};

			conv_expr = pigen_rtl_expr_add_conversion(&rtl, t16, &conv,
				a_obj_expr, (pigen_source_span){namefile, 4, 5});
			REQUIRE(conv_expr.index != PIGEN_INVALID_ID);
		}

		/* A concatenation {c, e} over its two children. */
		{
			pigen_rtl_expr_id parts[2];

			parts[0] = c_obj_expr;
			parts[1] = e_obj_expr;
			concat_expr = pigen_rtl_expr_add_concatenation(&rtl, t8, parts, 2,
				(pigen_source_span){namefile, 8, 9});
			REQUIRE(concat_expr.index != PIGEN_INVALID_ID);
		}

		/* A primitive/instance parameter: an integer literal the instance
		 * carries in its ordered parameter range. */
		param_expr = pigen_rtl_expr_add_integer(&rtl, t8, 8,
			(pigen_source_span){namefile, 0, 1});
		REQUIRE(param_expr.index != PIGEN_INVALID_ID);

		/* The primitive instance: one parameter and a single connection to the
		 * clock object. Its origin is invalid, so its final name is the derived
		 * INSTANCE role name (the role stem), matching the golden's "instance". */
		{
			pigen_rtl_expr_id params[1];
			pigen_rtl_object_id conns[1];

			params[0] = param_expr;
			conns[0] = clk;
			fifo = pigen_rtl_instance_add_with_owner(&rtl, module,
				(pigen_module_id){710}, params, 1, conns, 1,
				(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
			REQUIRE(fifo.index != PIGEN_INVALID_ID);
		}

		/* The two continuous equations, in module order: the conversion and the
		 * concatenation. */
		eq_conv = pigen_rtl_equation_add_with_owner(&rtl, module, out_b,
			conv_expr, (pigen_source_span){design, 26, 37});
		REQUIRE(eq_conv.index != PIGEN_INVALID_ID);
		eq_concat = pigen_rtl_equation_add_with_owner(&rtl, module, d_obj,
			concat_expr, (pigen_source_span){design, 26, 37});
		REQUIRE(eq_concat.index != PIGEN_INVALID_ID);

		/* The guarded/clocked update, added immediately before its process so
		 * the process's update range is the last update in the arena. */
		up = pigen_rtl_update_add_with_owner(&rtl, module, e_obj, d_obj_expr,
			(pigen_source_span){design, 38, 48});
		REQUIRE(up.index != PIGEN_INVALID_ID);
		{
			pigen_rtl_update_id updates[1];

			updates[0] = up;
			proc = pigen_rtl_process_add_with_owner(&rtl, module, clk_expr,
				PIGEN_SEMANTIC_POSEDGE, updates, 1,
				(pigen_source_span){design, 38, 48});
			REQUIRE(proc.index != PIGEN_INVALID_ID);
		}

		/* The final names: each SOURCE request copies the checked identifier
		 * spelling of its span. The objects and module use checked spans; the
		 * instance is a synthetic internal role (invalid span) whose name is the
		 * derived role stem "instance". */
		for (int r = 0; r < 7; r++)
			requests[r].kind = PIGEN_RTL_NAME_SOURCE;
		requests[7].kind = PIGEN_RTL_NAME_INSTANCE;
		requests[0].origin = (pigen_source_span){namefile, 4, 5}; /* a */
		requests[1].origin = (pigen_source_span){namefile, 6, 7}; /* b */
		requests[2].origin = (pigen_source_span){namefile, 14, 17}; /* clk */
		requests[3].origin = (pigen_source_span){namefile, 8, 9}; /* c */
		requests[4].origin = (pigen_source_span){namefile, 10, 11}; /* d */
		requests[5].origin = (pigen_source_span){namefile, 12, 13}; /* e */
		requests[6].origin = (pigen_source_span){namefile, 0, 3}; /* top */
		requests[7].origin =
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};
		REQUIRE(pigen_rtl_assign_names(&rtl, &sources, requests, 8).index !=
			PIGEN_INVALID_ID);

		/* The ordered output model: a leading opaque comment, the module (with
		 * its ten nested items in emission order: the six signal declarations,
		 * the primitive, the two equations and the process), and a trailing
		 * opaque comment. The owner-managed nested range is keyed by the module
		 * identity; the module item's layout field mirrors it. */
		items[0].kind = PIGEN_OUTPUT_OPAQUE;
		items[0].as.span = (pigen_source_span){design, 0, 26};
		items[1].kind = PIGEN_OUTPUT_MODULE;
		items[1].as.module = module;
		items[1].layout = (pigen_rtl_record_range){2, 10};
		items[2].kind = PIGEN_OUTPUT_RTL_OBJECT;
		items[2].as.object = in_a;
		items[3].kind = PIGEN_OUTPUT_RTL_OBJECT;
		items[3].as.object = out_b;
		items[4].kind = PIGEN_OUTPUT_RTL_OBJECT;
		items[4].as.object = clk;
		items[5].kind = PIGEN_OUTPUT_RTL_OBJECT;
		items[5].as.object = c_obj;
		items[6].kind = PIGEN_OUTPUT_RTL_OBJECT;
		items[6].as.object = d_obj;
		items[7].kind = PIGEN_OUTPUT_RTL_OBJECT;
		items[7].as.object = e_obj;
		items[8].kind = PIGEN_OUTPUT_RTL_INSTANCE;
		items[8].as.instance = fifo;
		items[9].kind = PIGEN_OUTPUT_RTL_EQUATION;
		items[9].as.equation = eq_conv;
		items[10].kind = PIGEN_OUTPUT_RTL_EQUATION;
		items[10].as.equation = eq_concat;
		items[11].kind = PIGEN_OUTPUT_RTL_PROCESS;
		items[11].as.process = proc;
		items[12].kind = PIGEN_OUTPUT_OPAQUE;
		items[12].as.span = (pigen_source_span){design, 48, 75};
		nested[0] = (pigen_rtl_record_range){2, 10};
		layout.items = items;
		layout.item_count = 13;
		layout.item_capacity = 13;
		layout.nested_layouts = nested;
		layout.nested_layout_count = 1;
		layout.nested_layout_capacity = 1;

		/* The whole-design emission must be exactly the golden string: leading
		 * and trailing OPAQUE spans copied verbatim and byte-exact, the module
		 * header and its nested items, the signal declarations, the signed
		 * conversion, the concatenation, the primitive parameter, the equation,
		 * and the guarded/clocked update. Red on main: the stub returns -1 and
		 * leaves *output_string NULL. */
		ret = pigen_emit_systemverilog(&sources, &layout, &rtl, &out);
		REQUIRE(ret == 0);
		REQUIRE(out != NULL);
		REQUIRE(strcmp(out, golden) == 0);
		if (out)
			free(out);

		pigen_free_rtl_model(&rtl);
		pigen_free_sources(&sources);
	}

	return check_finish();
}
