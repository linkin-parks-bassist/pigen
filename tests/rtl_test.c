#include <assert.h>
#include <stdio.h>

#include "pigen/operation.h"
#include "pigen/rtl.h"
#include "pigen/source.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})
#define IS_INVALID(id) ((id).index == PIGEN_INVALID_ID)
#define SAME_ID(a, b) ((a).index == (b).index)

int main(void)
{
	pigen_rtl_model model = {0};
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "rtl.pigen",
		"module rtl; endmodule\n", 22);
	pigen_source_span first = {source, 7, 11};
	pigen_source_span second = {source, 13, 19};
	pigen_source_span synthetic = {INVALID_ID(pigen_source_id), 0, 0};
	pigen_rtl_type_id type;
	pigen_rtl_expr_id expression;
	pigen_rtl_object_id object;
	pigen_rtl_object_id synthetic_object;
	pigen_rtl_instance_id instance;
	pigen_rtl_equation_id equation;
	pigen_rtl_update_id update;
	pigen_rtl_process_id process;
	pigen_rtl_module_id module;
	size_t i;

	assert(!pigen_rtl_type_get(&model, INVALID_ID(pigen_rtl_type_id)));
	assert(!pigen_rtl_type_get(&model, (pigen_rtl_type_id){0}));
	assert(!pigen_rtl_expr_get(&model, INVALID_ID(pigen_rtl_expr_id)));
	assert(!pigen_rtl_expr_get(&model, (pigen_rtl_expr_id){0}));
	assert(!pigen_rtl_object_get(&model, INVALID_ID(pigen_rtl_object_id)));
	assert(!pigen_rtl_object_get(&model, (pigen_rtl_object_id){0}));
	assert(!pigen_rtl_instance_get(&model, INVALID_ID(pigen_rtl_instance_id)));
	assert(!pigen_rtl_instance_get(&model, (pigen_rtl_instance_id){0}));
	assert(!pigen_rtl_equation_get(&model, INVALID_ID(pigen_rtl_equation_id)));
	assert(!pigen_rtl_equation_get(&model, (pigen_rtl_equation_id){0}));
	assert(!pigen_rtl_update_get(&model, INVALID_ID(pigen_rtl_update_id)));
	assert(!pigen_rtl_update_get(&model, (pigen_rtl_update_id){0}));
	assert(!pigen_rtl_process_get(&model, INVALID_ID(pigen_rtl_process_id)));
	assert(!pigen_rtl_process_get(&model, (pigen_rtl_process_id){0}));
	assert(!pigen_rtl_module_get(&model, INVALID_ID(pigen_rtl_module_id)));
	assert(!pigen_rtl_module_get(&model, (pigen_rtl_module_id){0}));
	assert(!pigen_rtl_type_get(NULL, INVALID_ID(pigen_rtl_type_id)));
	assert(!pigen_rtl_expr_get(NULL, INVALID_ID(pigen_rtl_expr_id)));
	assert(!pigen_rtl_object_get(NULL, INVALID_ID(pigen_rtl_object_id)));
	assert(!pigen_rtl_instance_get(NULL, INVALID_ID(pigen_rtl_instance_id)));
	assert(!pigen_rtl_equation_get(NULL, INVALID_ID(pigen_rtl_equation_id)));
	assert(!pigen_rtl_update_get(NULL, INVALID_ID(pigen_rtl_update_id)));
	assert(!pigen_rtl_process_get(NULL, INVALID_ID(pigen_rtl_process_id)));
	assert(!pigen_rtl_module_get(NULL, INVALID_ID(pigen_rtl_module_id)));
	assert(!model.type_count && !model.type_capacity);
	assert(!model.expression_count && !model.expression_capacity);
	assert(!model.object_count && !model.object_capacity);
	assert(!model.instance_count && !model.instance_capacity);
	assert(!model.equation_count && !model.equation_capacity);
	assert(!model.update_count && !model.update_capacity);
	assert(!model.process_count && !model.process_capacity);
	assert(!model.module_count && !model.module_capacity);

	type = pigen_rtl_type_add(&model, first);
	expression = pigen_rtl_expr_add(&model, first);
	object = pigen_rtl_object_add(&model, first);
	instance = pigen_rtl_instance_add(&model, first);
	equation = pigen_rtl_equation_add(&model, first);
	update = pigen_rtl_update_add(&model, first);
	process = pigen_rtl_process_add(&model, first);
	module = pigen_rtl_module_add(&model, first);

	assert(type.index == 0 && expression.index == 0 && object.index == 0 &&
		instance.index == 0 && equation.index == 0 && update.index == 0 &&
		process.index == 0 && module.index == 0);
	assert(pigen_rtl_type_get(&model, type)->origin.start == 7 &&
		pigen_rtl_type_get(&model, type)->origin.end == 11);
	assert(pigen_rtl_expr_get(&model, expression)->origin.start == 7);
	assert(pigen_rtl_object_get(&model, object)->origin.start == 7);
	assert(pigen_rtl_instance_get(&model, instance)->origin.start == 7);
	assert(pigen_rtl_equation_get(&model, equation)->origin.start == 7);
	assert(pigen_rtl_update_get(&model, update)->origin.start == 7);
	assert(pigen_rtl_process_get(&model, process)->origin.start == 7);
	assert(pigen_rtl_module_get(&model, module)->origin.start == 7);

	synthetic_object = pigen_rtl_object_add(&model, synthetic);
	assert(pigen_rtl_object_get(&model, synthetic_object)
		->origin.source.index == PIGEN_INVALID_ID);
	assert(pigen_rtl_object_get(&model, object)
		->origin.source.index == source.index);

	for (i = 0; i < 32; i++)
		(void)pigen_rtl_object_add(&model, second);
	assert(model.object_count == 34);
	assert(pigen_rtl_object_get(&model, object)->origin.start == 7);
	assert(!pigen_rtl_object_get(&model,
		(pigen_rtl_object_id){model.object_count}));
	assert(!pigen_rtl_object_get(&model,
		(pigen_rtl_object_id){model.object_count + 5}));
	assert(pigen_rtl_object_get(&model, (pigen_rtl_object_id){33})
		->origin.start == 13);

	{
		pigen_rtl_type bits4 = {
			PIGEN_SIGN_UNSIGNED, PIGEN_DATA_TYPE_STATE_TWO, 4,
			INVALID_ID(pigen_rtl_expr_id), first, NULL, 0};
		pigen_rtl_type bits4_later = bits4;
		pigen_rtl_type bits8 = bits4;
		pigen_rtl_type int4 = bits4;
		pigen_rtl_type logic4 = bits4;
		pigen_rtl_type signed_invalid = bits4;
		pigen_rtl_type symbolic_invalid = bits4;
		pigen_rtl_type_id bits4_type;
		pigen_rtl_type_id bits8_type;
		pigen_rtl_type_id int4_type;
		pigen_rtl_type_id logic4_type;
		pigen_rtl_expr_id integer;
		pigen_rtl_expr_id bits;
		pigen_rtl_expr_id object_expr;
		pigen_rtl_expr_id negated;
		pigen_rtl_expr_id sum;
		pigen_rtl_expr_id chosen;
		pigen_rtl_expr_id converted;
		pigen_rtl_expr_id indexed;
		pigen_rtl_expr_id selected;
		pigen_rtl_expr_id concatenated;
		pigen_data_type_id operand_data_type = {3};
		pigen_data_type_id result_data_type = {4};
		const pigen_rtl_expr *node;
		const pigen_rtl_type *type_record;
		const pigen_rtl_expr_id *children;
		size_t child_count;
		size_t expression_count;
		size_t child_arena_count;

		bits4_later.origin = second;
		bits8.width = 8;
		int4.signedness = PIGEN_SIGN_SIGNED;
		logic4.state_domain = PIGEN_DATA_TYPE_STATE_FOUR;
		signed_invalid.signedness = PIGEN_SIGN_INVALID;
		symbolic_invalid.width_expression = (pigen_rtl_expr_id){42};

		bits4_type = pigen_rtl_type_intern(&model, &bits4);
		assert(bits4_type.index == 1);
		assert(pigen_rtl_type_intern(&model, &bits4_later).index ==
			bits4_type.index);
		type_record = pigen_rtl_type_get(&model, bits4_type);
		assert(type_record);
		assert(type_record->signedness == PIGEN_SIGN_UNSIGNED);
		assert(type_record->state_domain == PIGEN_DATA_TYPE_STATE_TWO);
		assert(type_record->width == 4);
		assert(type_record->origin.start == 7 && type_record->origin.end == 11);
		bits8_type = pigen_rtl_type_intern(&model, &bits8);
		int4_type = pigen_rtl_type_intern(&model, &int4);
		logic4_type = pigen_rtl_type_intern(&model, &logic4);
		assert(bits8_type.index != bits4_type.index &&
			int4_type.index != bits4_type.index &&
			logic4_type.index != bits4_type.index);
		assert(IS_INVALID(pigen_rtl_type_intern(&model, &signed_invalid)));
		assert(IS_INVALID(pigen_rtl_type_intern(&model,
			&symbolic_invalid)));
		assert(IS_INVALID(pigen_rtl_type_intern(&model, NULL)));
		assert(model.type_count == 5);

		integer = pigen_rtl_expr_add_integer(&model, bits4_type, 5, first);
		assert(integer.index == 1);
		bits = pigen_rtl_expr_add_bits(&model, bits4_type, 0xa, second);
		object_expr = pigen_rtl_expr_add_object(&model, bits4_type, object,
			second);
		assert(IS_INVALID(pigen_rtl_expr_add_object(&model, bits4_type,
			INVALID_ID(pigen_rtl_object_id), second)));
		assert(IS_INVALID(pigen_rtl_expr_add_integer(&model,
			INVALID_ID(pigen_rtl_type_id), 5, first)));
		node = pigen_rtl_expr_get(&model, integer);
		assert(node && node->kind == PIGEN_RTL_EXPR_INTEGER &&
			SAME_ID(node->type, bits4_type) && node->value == 5 &&
			node->origin.start == 7 && node->origin.end == 11);
		assert(pigen_rtl_expr_get(&model, bits)->kind == PIGEN_RTL_EXPR_BITS &&
			pigen_rtl_expr_get(&model, bits)->value == 0xa);
		assert(pigen_rtl_expr_get(&model, object_expr)->kind ==
			PIGEN_RTL_EXPR_OBJECT &&
			SAME_ID(pigen_rtl_expr_get(&model, object_expr)->object, object));

		{
			pigen_unary_resolution negate = {{PIGEN_CONVERSION_IDENTITY,
				operand_data_type, result_data_type},
				{PIGEN_UNARY_NEGATE, operand_data_type, result_data_type}};
			pigen_binary_resolution add = {{PIGEN_CONVERSION_IDENTITY,
				operand_data_type, result_data_type},
				{PIGEN_CONVERSION_IDENTITY, operand_data_type,
				result_data_type},
				{PIGEN_BINARY_ADD, operand_data_type, operand_data_type,
				result_data_type}};
			pigen_conditional_resolution choose = {{
				PIGEN_CONVERSION_IDENTITY, operand_data_type,
				result_data_type},
				{PIGEN_CONVERSION_IDENTITY, operand_data_type,
				result_data_type},
				{PIGEN_CONVERSION_IDENTITY, operand_data_type,
				result_data_type},
				{operand_data_type, operand_data_type, operand_data_type,
				result_data_type}};
			pigen_conversion widen = {PIGEN_CONVERSION_INTEGER_RESIZE,
				operand_data_type, result_data_type};

			negated = pigen_rtl_expr_add_unary(&model, bits4_type, &negate,
				integer, second);
			assert(negated.index != PIGEN_INVALID_ID);
			sum = pigen_rtl_expr_add_binary(&model, bits4_type, &add,
				integer, bits, second);
			assert(sum.index != PIGEN_INVALID_ID);
			chosen = pigen_rtl_expr_add_conditional(&model, bits4_type,
				&choose, integer, bits, object_expr, second);
			assert(chosen.index != PIGEN_INVALID_ID);
			converted = pigen_rtl_expr_add_conversion(&model, bits8_type,
				&widen, integer, second);
			assert(converted.index != PIGEN_INVALID_ID);
			indexed = pigen_rtl_expr_add_index(&model, bits4_type,
				object_expr, integer, second);
			assert(indexed.index != PIGEN_INVALID_ID);
			selected = pigen_rtl_expr_add_select(&model, bits4_type,
				object_expr, integer, bits,
				PIGEN_SEMANTIC_SELECT_RANGE, second);
			assert(selected.index != PIGEN_INVALID_ID);
			assert(IS_INVALID(pigen_rtl_expr_add_select(&model, bits4_type,
				object_expr, integer, bits, (pigen_select_kind)4, second)));
			{
				const pigen_rtl_expr_id parts[] = {integer, bits,
					object_expr};

				concatenated = pigen_rtl_expr_add_concatenation(&model,
					bits4_type, parts, 3, second);
				assert(concatenated.index != PIGEN_INVALID_ID);
			}
		}

		node = pigen_rtl_expr_get(&model, negated);
		assert(node && node->kind == PIGEN_RTL_EXPR_UNARY &&
			node->as.unary.resolution.operation.operator ==
			PIGEN_UNARY_NEGATE &&
			node->as.unary.resolution.operand_conversion.kind ==
			PIGEN_CONVERSION_IDENTITY);
		node = pigen_rtl_expr_get(&model, sum);
		assert(node && node->kind == PIGEN_RTL_EXPR_BINARY &&
			node->as.binary.resolution.operation.operator ==
			PIGEN_BINARY_ADD &&
			SAME_ID(node->as.binary.resolution
				.left_conversion.source_data_type, operand_data_type) &&
			SAME_ID(node->as.binary.resolution
				.right_conversion.target_data_type, result_data_type));
		node = pigen_rtl_expr_get(&model, chosen);
		assert(node && node->kind == PIGEN_RTL_EXPR_CONDITIONAL &&
			SAME_ID(node->as.conditional.resolution
				.operation.result_data_type, result_data_type) &&
			node->as.conditional.resolution.when_false_conversion.kind ==
			PIGEN_CONVERSION_IDENTITY);
		node = pigen_rtl_expr_get(&model, converted);
		assert(node && node->kind == PIGEN_RTL_EXPR_CONVERSION &&
			node->as.conversion.conversion.kind ==
			PIGEN_CONVERSION_INTEGER_RESIZE &&
			SAME_ID(node->as.conversion.conversion.target_data_type,
			result_data_type));
		node = pigen_rtl_expr_get(&model, indexed);
		assert(node && node->kind == PIGEN_RTL_EXPR_INDEX);
		node = pigen_rtl_expr_get(&model, selected);
		assert(node && node->kind == PIGEN_RTL_EXPR_SELECT &&
			node->as.select.kind == PIGEN_SEMANTIC_SELECT_RANGE);
		node = pigen_rtl_expr_get(&model, concatenated);
		assert(node && node->kind == PIGEN_RTL_EXPR_CONCATENATION &&
			SAME_ID(node->type, bits4_type) && node->origin.start == 13);

		children = pigen_rtl_expr_children(&model, negated, &child_count);
		assert(children && child_count == 1 &&
			SAME_ID(children[0], integer));
		children = pigen_rtl_expr_children(&model, sum, &child_count);
		assert(children && child_count == 2 && SAME_ID(children[0], integer) &&
			SAME_ID(children[1], bits));
		children = pigen_rtl_expr_children(&model, chosen, &child_count);
		assert(children && child_count == 3 && SAME_ID(children[0], integer) &&
			SAME_ID(children[1], bits) &&
			SAME_ID(children[2], object_expr));
		children = pigen_rtl_expr_children(&model, indexed, &child_count);
		assert(children && child_count == 2 &&
			SAME_ID(children[0], object_expr) &&
			SAME_ID(children[1], integer));
		children = pigen_rtl_expr_children(&model, selected, &child_count);
		assert(children && child_count == 3 &&
			SAME_ID(children[0], object_expr) &&
			SAME_ID(children[1], integer) && SAME_ID(children[2], bits));
		children = pigen_rtl_expr_children(&model, concatenated,
			&child_count);
		assert(children && child_count == 3 &&
			SAME_ID(children[0], integer) && SAME_ID(children[1], bits) &&
			SAME_ID(children[2], object_expr));
		children = pigen_rtl_expr_children(&model, integer, &child_count);
		assert(!children && child_count == 0);
		assert(!pigen_rtl_expr_children(&model,
			INVALID_ID(pigen_rtl_expr_id), &child_count));
		assert(!pigen_rtl_expr_children(&model, (pigen_rtl_expr_id){99},
			&child_count));

		{
			pigen_rtl_expr_id missing = (pigen_rtl_expr_id){
				model.expression_count};
			const pigen_rtl_expr_id broken[] = {integer, missing, bits};
			const pigen_rtl_expr_id *empty = 0;
			pigen_unary_resolution bad_unary = {{
				PIGEN_CONVERSION_IDENTITY, operand_data_type,
				result_data_type},
				{PIGEN_UNARY_NEGATE, operand_data_type, result_data_type}};
			size_t before_expressions;
			size_t before_children;

			before_expressions = model.expression_count;
			before_children = model.expression_child_count;
			assert(IS_INVALID(pigen_rtl_expr_add_concatenation(&model,
				bits4_type, broken, 3, second)));
			assert(IS_INVALID(pigen_rtl_expr_add_concatenation(&model,
				bits4_type, empty, 2, second)));
			assert(IS_INVALID(pigen_rtl_expr_add_unary(&model, bits4_type,
				&bad_unary, missing, second)));
			assert(model.expression_count == before_expressions);
			assert(model.expression_child_count == before_children);
		}
		expression_count = model.expression_count;
		child_arena_count = model.expression_child_count;
		assert(expression_count == 11 && child_arena_count == 15);
	}

	{
		pigen_rtl_type descriptor = {.signedness = PIGEN_SIGN_UNSIGNED,
			.state_domain = PIGEN_DATA_TYPE_STATE_FOUR, .width = 130,
			.width_expression = INVALID_ID(pigen_rtl_expr_id)};
		pigen_rtl_type_id scalar = pigen_rtl_type_intern(&model, &descriptor);
		pigen_rtl_expr_id seven = pigen_rtl_expr_add_integer(&model, scalar, 7, first);
		pigen_rtl_expr_id zero = pigen_rtl_expr_add_integer(&model, scalar, 0, first);
		pigen_rtl_packed_dimension dims[] = {
			{{7, INVALID_ID(pigen_rtl_expr_id)}, {0, INVALID_ID(pigen_rtl_expr_id)}},
			{{-2, INVALID_ID(pigen_rtl_expr_id)}, {0, seven}}};
		descriptor.dimensions = dims;
		descriptor.dimension_count = 2;
		pigen_rtl_type_id descending = pigen_rtl_type_intern(&model, &descriptor);
		assert(!IS_INVALID(descending));
		assert(SAME_ID(descending, pigen_rtl_type_intern(&model, &descriptor)));
		dims[0].left.value = 0;
		dims[0].right.value = 7;
		pigen_rtl_type_id ascending = pigen_rtl_type_intern(&model, &descriptor);
		assert(!SAME_ID(descending, ascending));
		assert(pigen_rtl_type_get(&model, descending)->dimensions[0].left.value == 7);
		dims[1].right.expression = zero;
		assert(!SAME_ID(ascending, pigen_rtl_type_intern(&model, &descriptor)));
		size_t before_types = model.type_count;
		dims[1].right.expression = INVALID_ID(pigen_rtl_expr_id);
		dims[1].left.expression = (pigen_rtl_expr_id){model.expression_count};
		assert(IS_INVALID(pigen_rtl_type_intern(&model, &descriptor)));
		assert(model.type_count == before_types);
		descriptor.dimensions = NULL;
		assert(IS_INVALID(pigen_rtl_type_intern(&model, &descriptor)));
		descriptor.dimension_count = 0;
		descriptor.width_expression = seven;
		pigen_rtl_type_id symbolic = pigen_rtl_type_intern(&model, &descriptor);
		descriptor.width = 99;
		assert(SAME_ID(symbolic, pigen_rtl_type_intern(&model, &descriptor)));


		pigen_rtl_literal_word words[] = {{1, 2, 4}, {UINT64_MAX, 0, 0}, {3, 0, 0}};
		pigen_rtl_expr_id wide = pigen_rtl_expr_add_literal(&model,
			PIGEN_RTL_EXPR_BITS, scalar, words, 130, 0, second);
		assert(!IS_INVALID(wide));
		words[0].value = 0;
		const pigen_rtl_expr *literal = pigen_rtl_expr_get(&model, wide);
		assert(literal->literal_bit_count == 130 && literal->literal_words[0].value == 1);
		assert(literal->literal_words[0].x_mask == 2 && literal->literal_words[0].z_mask == 4);
		assert(literal->literal_words[2].value == 3 && literal->origin.start == second.start);
		words[0].x_mask = words[0].z_mask = 0;
		pigen_rtl_expr_id negative = pigen_rtl_expr_add_literal(&model,
			PIGEN_RTL_EXPR_INTEGER, scalar, words, 130, 1, first);
		assert(!IS_INVALID(negative) && pigen_rtl_expr_get(&model, negative)->literal_negative);
		size_t before_expr = model.expression_count, before_children = model.expression_child_count;
		words[2].value = 4;
		assert(IS_INVALID(pigen_rtl_expr_add_literal(&model, PIGEN_RTL_EXPR_BITS,
			scalar, words, 130, 0, first)));
		words[2].value = 3;
		words[0].x_mask = words[0].z_mask = 1;
		assert(IS_INVALID(pigen_rtl_expr_add_literal(&model, PIGEN_RTL_EXPR_BITS,
			scalar, words, 130, 0, first)));
		words[0].x_mask = 1;
		words[0].z_mask = 0;
		descriptor.state_domain = PIGEN_DATA_TYPE_STATE_TWO;
		descriptor.width_expression = INVALID_ID(pigen_rtl_expr_id);
		pigen_rtl_type_id two_state = pigen_rtl_type_intern(&model, &descriptor);
		assert(IS_INVALID(pigen_rtl_expr_add_literal(&model, PIGEN_RTL_EXPR_BITS,
			two_state, words, 130, 0, first)));
		assert(IS_INVALID(pigen_rtl_expr_add_literal(&model, PIGEN_RTL_EXPR_INTEGER,
			scalar, words, 130, 0, first)));
		assert(IS_INVALID(pigen_rtl_expr_add_literal(&model, PIGEN_RTL_EXPR_BITS,
			scalar, NULL, 130, 0, first)));
		assert(IS_INVALID(pigen_rtl_expr_add_literal(&model, PIGEN_RTL_EXPR_BITS,
			scalar, words, 0, 0, first)));
		assert(model.expression_count == before_expr && model.expression_child_count == before_children);

		pigen_rtl_expr_id parts[8];
		for (size_t i = 0; i < 8; ++i) parts[i] = i % 2 ? seven : zero;
		pigen_rtl_expr_id concat = pigen_rtl_expr_add_concatenation(&model, scalar, parts, 8, first);
		/* Fill the arena so reusing its own child slice must grow it. */
		while (model.expression_child_count + 8 <= model.expression_child_capacity)
			assert(!IS_INVALID(pigen_rtl_expr_add_concatenation(&model, scalar, parts, 8, first)));
		size_t n;
		const pigen_rtl_expr_id *children = pigen_rtl_expr_children(&model, concat, &n);
		pigen_rtl_expr_id copied = pigen_rtl_expr_add_concatenation(&model, scalar, children, n, second);
		children = pigen_rtl_expr_children(&model, copied, &n);
		assert(n == 8);
		for (size_t i = 0; i < n; ++i) assert(SAME_ID(children[i], parts[i]));
	}

	pigen_free_rtl_model(&model);
	assert(!model.types && !model.type_count && !model.type_capacity);
	assert(!model.expressions && !model.expression_count &&
		!model.expression_capacity);
	assert(!model.expression_children &&
		!model.expression_child_count && !model.expression_child_capacity);
	assert(!model.objects && !model.object_count && !model.object_capacity);
	assert(!model.instances && !model.instance_count &&
		!model.instance_capacity);
	assert(!model.equations && !model.equation_count &&
		!model.equation_capacity);
	assert(!model.updates && !model.update_count && !model.update_capacity);
	assert(!model.processes && !model.process_count &&
		!model.process_capacity);
	assert(!model.modules && !model.module_count && !model.module_capacity);

	pigen_free_sources(&sources);
	puts("PASS: rtl arena holds distinct identities with checked accessors");
	puts("PASS: canonical rtl types and typed expressions intern canonically");
	return 0;
}
