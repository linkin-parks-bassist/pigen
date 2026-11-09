#include "pigen/fabric_lower.h"
#include <stdlib.h>
#include <string.h>
#include "pigen/util.h"

static int token_text(const pigen_syntax_tree *syntax, pigen_token_id token,
	const char **text, size_t *length)
{
	const pigen_expanded_token *known = pigen_expanded_token_get(
		syntax->expanded, token);
	pigen_source_span span;

	if (!known) return 0;
	span = pigen_origin_spelling_span(syntax->expanded, known->origin);
	*text = pigen_source_span_text(syntax->expanded->sources, span, length);
	return *text != NULL;
}

static const pigen_semantic_instance *find_instance(
	pigen_semantic_model *model, pigen_module_id module,
	const pigen_syntax_tree *syntax, pigen_token_id name)
{
	const char *text;
	size_t length;
	size_t i;

	if (!token_text(syntax, name, &text, &length)) return NULL;
	for (i = 0; i < model->instance_count; i++)
	{
		const pigen_semantic_instance *instance = &model->instances[i];
		const pigen_symbol *symbol;

		if (instance->module.index != module.index) continue;
		symbol = pigen_symbol_get(model, instance->symbol);
		if (!symbol) continue;
		{
			const char *stext;
			size_t slength;
			pigen_source_span span = symbol->name;
			stext = pigen_source_span_text(model->sources, span, &slength);
			if (stext && slength == length && !memcmp(stext, text, length))
				return instance;
		}
	}
	return NULL;
}

static const pigen_semantic_instance_connection *find_port(
	pigen_semantic_model *model, const pigen_semantic_instance *instance,
	const pigen_syntax_tree *syntax, pigen_token_id port, const char *suffix)
{
	const char *text;
	size_t length;
	size_t i;

	if (!token_text(syntax, port, &text, &length)) return NULL;
	for (i = 0; i < instance->connection_count; i++)
	{
		const pigen_semantic_instance_connection *connection =
			&model->instance_connections[instance->first_connection + i];
		const pigen_symbol *symbol = pigen_symbol_get(model,
			connection->port_symbol);
		const char *ptext = NULL;
		size_t plength = 0;

		if (connection->port_name.source.index != PIGEN_INVALID_ID)
			ptext = pigen_source_span_text(model->sources,
				connection->port_name, &plength);
		else if (symbol)
			ptext = pigen_source_span_text(model->sources, symbol->name,
				&plength);
		if (!ptext) continue;
		if (suffix)
		{
			size_t slength = strlen(suffix);
			if (plength != length + slength) continue;
			if (memcmp(ptext, text, length) ||
				memcmp(ptext + length, suffix, slength)) continue;
		}
		else if (plength != length || memcmp(ptext, text, length))
			continue;
		return connection;
	}
	return NULL;
}

static pigen_rtl_object_id connection_payload(pigen_rtl_lowering *lowering,
	const pigen_semantic_instance_connection *connection)
{
	const pigen_semantic_expr *value;

	if (!connection || connection->value.index == PIGEN_INVALID_ID)
		return (pigen_rtl_object_id){PIGEN_INVALID_ID};
	value = pigen_expr_get(lowering->semantics, connection->value);
	if (!value || value->kind != PIGEN_EXPR_SYMBOL)
		return (pigen_rtl_object_id){PIGEN_INVALID_ID};
	{
		pigen_signal_id signal = pigen_symbol_signal(lowering->semantics,
			value->as.symbol);
		if (signal.index == PIGEN_INVALID_ID ||
			lowering->lowered_endpoint_count <= signal.index)
			return (pigen_rtl_object_id){PIGEN_INVALID_ID};
		return lowering->lowered_endpoints[signal.index].payload;
	}
}

static int connect(pigen_rtl_lowering *lowering, pigen_rtl_module_id module,
	pigen_rtl_object_id destination, pigen_rtl_object_id source,
	pigen_source_span span)
{
	const pigen_rtl_object *dest = pigen_rtl_object_get(lowering->rtl,
		destination);
	const pigen_rtl_object *src = pigen_rtl_object_get(lowering->rtl, source);
	pigen_rtl_expr_id value;

	if (!dest || !src || dest->type.index != src->type.index)
		return 0;
	value = pigen_rtl_expr_add_object(lowering->rtl, dest->type, source,
		span);
	if (value.index == PIGEN_INVALID_ID) return 0;
	return pigen_rtl_equation_add_with_owner(lowering->rtl, module,
		destination, value, span).index != PIGEN_INVALID_ID;
}

int pigen_fabric_lower(pigen_rtl_lowering *lowering, pigen_module_id module,
	const pigen_syntax_tree *syntax)
{
	pigen_semantic_model *model;
	const pigen_semantic_module *owner;
	pigen_rtl_module_id rtl_module = (pigen_rtl_module_id){PIGEN_INVALID_ID};
	const pigen_syntax_node *module_node;
	pigen_syntax_id child;
	size_t i;

	if (!lowering || !lowering->semantics || !lowering->rtl || !syntax)
		return -1;
	model = lowering->semantics;
	owner = pigen_module_get(model, module);
	if (!owner) return -1;
	module_node = pigen_syntax_get(syntax, owner->syntax);
	if (!module_node) return -1;
	for (i = 0; i < lowering->rtl->module_count; i++)
		if (lowering->rtl->modules[i].semantic_module.index == module.index)
		{
			rtl_module = (pigen_rtl_module_id){(uint32_t)i};
			break;
		}
	if (rtl_module.index == PIGEN_INVALID_ID) return -1;

	for (child = module_node->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(syntax, child);
		pigen_syntax_id edge;

		if (!node) break;
		if (node->kind != PIGEN_SYNTAX_FABRIC)
		{
			child = node->next_sibling;
			continue;
		}
		for (edge = node->first_child; edge.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *connection = pigen_syntax_get(syntax,
				edge);
			const pigen_semantic_instance *source;
			const pigen_semantic_instance *destination;
			const pigen_semantic_instance_connection *sp, *sv, *sr;
			const pigen_semantic_instance_connection *dp, *dv, *dr;
			pigen_rtl_object_id sp_obj, sv_obj, sr_obj, dp_obj, dv_obj, dr_obj;

			if (!connection) break;
			if (connection->kind != PIGEN_SYNTAX_FABRIC_CONNECTION)
			{
				edge = connection->next_sibling;
				continue;
			}
			source = find_instance(model, module, syntax,
				connection->as.fabric_connection.left_instance);
			destination = find_instance(model, module, syntax,
				connection->as.fabric_connection.right_instance);
			if (!source || !destination) return -1;
			sp = find_port(model, source, syntax,
				connection->as.fabric_connection.left_port, NULL);
			sv = find_port(model, source, syntax,
				connection->as.fabric_connection.left_port, "_valid");
			sr = find_port(model, source, syntax,
				connection->as.fabric_connection.left_port, "_ready");
			dp = find_port(model, destination, syntax,
				connection->as.fabric_connection.right_port, NULL);
			dv = find_port(model, destination, syntax,
				connection->as.fabric_connection.right_port, "_valid");
			dr = find_port(model, destination, syntax,
				connection->as.fabric_connection.right_port, "_ready");
			if (!sp || !dp) return -1;
			sp_obj = connection_payload(lowering, sp);
			dp_obj = connection_payload(lowering, dp);
			sv_obj = connection_payload(lowering, sv);
			dv_obj = connection_payload(lowering, dv);
			sr_obj = connection_payload(lowering, sr);
			dr_obj = connection_payload(lowering, dr);
			if (sp_obj.index == PIGEN_INVALID_ID ||
				dp_obj.index == PIGEN_INVALID_ID)
				return -1;
			if (!connect(lowering, rtl_module, dp_obj, sp_obj,
				connection->location.source_span)) return -1;
			if (sv_obj.index != PIGEN_INVALID_ID &&
				dv_obj.index != PIGEN_INVALID_ID &&
				!connect(lowering, rtl_module, dv_obj, sv_obj,
					connection->location.source_span)) return -1;
			if (sr_obj.index != PIGEN_INVALID_ID &&
				dr_obj.index != PIGEN_INVALID_ID &&
				!connect(lowering, rtl_module, sr_obj, dr_obj,
					connection->location.source_span)) return -1;
			edge = connection->next_sibling;
		}
		child = node->next_sibling;
	}
	return 0;
}
