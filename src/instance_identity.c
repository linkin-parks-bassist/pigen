#include <stddef.h>
#include <string.h>

#include "pigen/instance_identity.h"
#include "pigen/data_type.h"
#include "pigen/preprocess.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})
/* An empty span: the invalid source id with no range. pigen_source_span is a
 * three-field struct, so it cannot be zeroed through the single-field
 * INVALID_ID macro without tripping -Wmissing-field-initializers. */
#define INVALID_SPAN ((pigen_source_span){INVALID_ID(pigen_source_id), 0, 0})

/* The token's original spelling span, resolved through the expanded source's
 * token origins (as the syntax tree itself anchors its tokens). A missing or
 * unresolvable token leaves the span empty (the invalid source id with no
 * range). */
static pigen_source_span token_span(const pigen_expanded_source *expanded,
	pigen_token_id token)
{
	const pigen_expanded_token *known = pigen_expanded_token_get(expanded, token);

	return known ? pigen_origin_spelling_span(expanded, known->origin) :
		INVALID_SPAN;
}

/* True when the span's source text equals TEXT of the same length. An empty
 * text or a span over the invalid source is not a match. */
static int span_matches(const pigen_source_manager *sources,
	pigen_source_span span, const char *text, size_t length)
{
	size_t known_length;
	const char *known;

	if (length == 0 || span.source.index == PIGEN_INVALID_ID) return 0;
	known = pigen_source_span_text(sources, span, &known_length);
	return known && known_length == length && !memcmp(known, text, length);
}

/* Fills *identity with the invalid (unresolved) form: the PIGEN_INVALID_ID
 * sentinel in its type_module and syntax ids, a connection_count of 0, an empty
 * instance_name span and an invalid first connection (port, syntax and span). */
static void clear_identity(pigen_instance_identity *identity)
{
	size_t i;

	identity->type_module = INVALID_ID(pigen_module_id);
	identity->instance_name = INVALID_SPAN;
	for (i = 0; i < 32; i++)
	{
		identity->connections[i] = (pigen_instance_connection){
			INVALID_ID(pigen_symbol_id),
			(pigen_semantic_direction)0,
			INVALID_ID(pigen_data_type_id),
			INVALID_ID(pigen_shape_id),
			(pigen_transfer_type)0,
			INVALID_SPAN,
			INVALID_ID(pigen_syntax_id)
		};
	}
	identity->connection_count = 0;
	identity->span = INVALID_SPAN;
	identity->syntax = INVALID_ID(pigen_syntax_id);
}

/* The child module the instance instantiates: the model's module whose
 * PIGEN_SYNTAX_MODULE name token text equals the instance's type name token
 * text. Matched by token text (never by spelling, layout or the node's
 * children) the way the FSM owner resolves the header-named initial state, so
 * a later grammar change cannot alter the established identity. Returns the
 * module id, or the invalid id when no module's name token text matches. */
static pigen_module_id resolve_type_module(pigen_semantic_model *model,
	const pigen_syntax_tree *syntax,
	const pigen_expanded_source *expanded, const char *type_text,
	size_t type_length)
{
	size_t i;

	for (i = 0; i < model->module_count; i++)
	{
		const pigen_semantic_module *module = &model->modules[i];
		const pigen_syntax_node *node =
			pigen_syntax_get(syntax, module->syntax);
		pigen_source_span name;

		if (!node || node->kind != PIGEN_SYNTAX_MODULE) continue;
		name = token_span(expanded, node->as.module.name);
		if (span_matches(model->sources, name, type_text, type_length))
			return (pigen_module_id){(uint32_t)i};
	}
	return INVALID_ID(pigen_module_id);
}

/* Establishes the instance's module-scope identity in the model: the child
 * module the instance instantiates (the module whose name token text equals
 * the instance's type name token text) and one resolved connection per
 * source-order connection, each bound to the child module's port (a named
 * connection to the module-scope port signal whose name token text equals the
 * connection's port token, a positional connection to the next child port in
 * declared order), carrying that port's direction, data type, shape and
 * transfer type. Fills *identity and returns 0 on success. On error it returns
 * -1, leaves *identity in the invalid form (the resolution accumulates in a
 * local and is committed only on full success) and the compile stops
 * (how/should/compiler/builders/fail.md). */
int pigen_instance_identity_establish(pigen_semantic_model *model,
	const pigen_syntax_tree *syntax, pigen_syntax_id syntax_id,
	pigen_module_id parent_module,
	const pigen_syntax_node *instance,
	const pigen_expanded_source *expanded,
	pigen_instance_identity *identity)
{
	pigen_source_span type_span;
	const char *type_text;
	size_t type_length;
	pigen_module_id type_module;
	pigen_source_span instance_name;
	size_t ports[64];
	size_t port_count = 0;
	size_t i;
	/* The identity is resolved into `result` and committed to *identity only on
	 * full success; every early return leaves *identity in the invalid form
	 * (the compile stops at the first failed resolution). */
	pigen_instance_identity result;

	(void)parent_module;

	clear_identity(identity);
	clear_identity(&result);
	if (!model || !syntax || !expanded || !instance ||
		instance->kind != PIGEN_SYNTAX_INSTANCE)
		return -1;

	/* The instance's type name: the type-name token's original spelling span and
	 * its text. Resolution keys only on this token text (surface-independent). */
	type_span = token_span(expanded, instance->as.instance.type_name);
	type_text = pigen_source_span_text(model->sources, type_span, &type_length);
	if (!type_text || type_length == 0)
		return -1;

	/* The child module the instance instantiates: the model's module whose name
	 * token text equals the type name's token text. An unknown type name (no
	 * module matches) fails loudly at the offending token's span. */
	type_module = resolve_type_module(model, syntax, expanded,
		type_text, type_length);
	if (type_module.index == PIGEN_INVALID_ID)
		return -1;

	/* The instance name's original span (the instance-name token's original
	 * spelling span, resolved through the expanded source). */
	instance_name = token_span(expanded, instance->as.instance.name);
	if (instance_name.source.index == PIGEN_INVALID_ID ||
		instance_name.start == instance_name.end)
		return -1;

	/* The child module's declared ports in declaration order: the signals bound
	 * to the child module, walked in arena (addition) order. A named connection
	 * resolves to the port whose name token text equals the connection's port
	 * token; a positional connection resolves to the next port in this order. */
	for (i = 0; i < model->signal_count && port_count < 64; i++)
	{
		const pigen_semantic_signal *signal = &model->signals[i];

		if (signal->module.index == type_module.index)
			ports[port_count++] = i;
	}

	/* Resolve each source-order connection in turn. */
	for (pigen_syntax_id child = instance->first_child;
		child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *conn = pigen_syntax_get(syntax, child);

		if (!conn) break;
		if (conn->kind == PIGEN_SYNTAX_INSTANCE_CONNECTION &&
			result.connection_count < 32)
		{
			pigen_instance_connection *slot =
				&result.connections[result.connection_count];
			const pigen_semantic_signal *signal;
			const pigen_symbol *symbol;
			size_t bound;

			/* A named connection binds to the child port whose name token text
			 * equals the connection's port token; a named connection to a port
			 * the child does not declare fails loudly at the offending token's
			 * span. A positional connection binds to the next child port in
			 * declared order; a positional connection past the child's declared
			 * port count fails loudly at the connection's span. */
			bound = PIGEN_INVALID_ID;
			if (conn->as.instance_connection.port.index != PIGEN_INVALID_ID)
			{
				pigen_source_span port_span =
					token_span(expanded,
						conn->as.instance_connection.port);
				size_t port_length;
				const char *port_text =
					pigen_source_span_text(model->sources, port_span,
						&port_length);
				size_t p;

				if (!port_text || port_length == 0)
					return -1;
				for (p = 0; p < port_count; p++)
				{
					const pigen_semantic_signal *candidate =
						&model->signals[ports[p]];
					const pigen_symbol *candidate_symbol =
						pigen_symbol_get(model, candidate->symbol);

					if (candidate_symbol && span_matches(model->sources,
						candidate_symbol->name, port_text, port_length))
					{
						bound = ports[p];
						break;
					}
				}
				if (bound == PIGEN_INVALID_ID)
					return -1;
			}
			else
			{
				if (result.connection_count >= port_count)
					return -1;
				bound = ports[result.connection_count];
			}

			signal = &model->signals[bound];
			symbol = pigen_symbol_get(model, signal->symbol);
			if (!symbol)
				return -1;
			slot->port = signal->symbol;
			slot->direction = signal->direction;
			slot->data_type = signal->data_type;
			slot->shape = signal->shape;
			slot->transfer_type = signal->transfer_type;
			slot->span = conn->location.source_span;
			slot->syntax = child;
			result.connection_count++;
		}
		child = conn->next_sibling;
	}

	result.type_module = type_module;
	result.instance_name = instance_name;
	result.span = instance->location.source_span;
	result.syntax = syntax_id;
	*identity = result;
	return 0;
}

/* Tests whether *identity is an invalid instance identity: the PIGEN_INVALID_ID
 * sentinel in its type_module and syntax ids, a connection_count of 0, an empty
 * instance_name span and an invalid first connection (port, syntax and span). */
int pigen_instance_identity_is_invalid(const pigen_instance_identity *identity)
{
	if (!identity) return 1;
	return identity->type_module.index == PIGEN_INVALID_ID &&
		identity->instance_name.source.index == PIGEN_INVALID_ID &&
		identity->instance_name.start == 0 &&
		identity->instance_name.end == 0 &&
		identity->connection_count == 0 &&
		identity->connections[0].port.index == PIGEN_INVALID_ID &&
		identity->connections[0].syntax.index == PIGEN_INVALID_ID &&
		identity->connections[0].span.source.index == PIGEN_INVALID_ID &&
		identity->span.source.index == PIGEN_INVALID_ID &&
		identity->syntax.index == PIGEN_INVALID_ID;
}
