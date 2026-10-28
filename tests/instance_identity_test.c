/* Structured instance-identity owner test contract.
 *
 * (i2) of the child-instance stage: the discriminating green manifest that
 * pins pigen_instance_identity_establish (include/pigen/instance_identity.h)
 * against the behavior contract the spec and the brief require. The owner seam
 * turns one parsed ordinary SV child instantiation (a PIGEN_SYNTAX_INSTANCE
 * node) into the instance's module-scope identity in the resolved semantic
 * model: the child module the instance instantiates (matched by the module's
 * name token TEXT, never by spelling or layout) and one resolved connection per
 * source-order connection, each bound to the child module's port (named by port
 * name, positional in declared order), with that port's direction, data type,
 * shape and transfer type.
 *
 * Every witness is located through the parse (the compilation unit's modules,
 * the instance node and its connection children) and the model is built through
 * the owner APIs (the retained-core construction pattern of the route test);
 * the module's semantic record points its syntax id at the real parsed
 * PIGEN_SYNTAX_MODULE node so the owner's token-text type-name resolution runs
 * against the actual tree. Four sections, one `make instance-identity-test
 * T=<section>` entry per section in tests/contracts.json:
 *
 *   (a) establish-valid-args: a legal two-module witness (child declared first,
 *       parent second) with a named and a positional connection resolves the
 *       instance to the second module - the child, not "always the first module"
 *       - and binds each connection to the child's port; a non-INSTANCE node and
 *       each NULL argument are rejected rc == -1 leaving *identity invalid.
 *   (b) is-invalid-sentinel: the predicate is 1 for the all-sentinel identity
 *       and 0 once any single id, count or span is set; a null pointer reports 1.
 *   (c) unknown-type-name: an instance whose type name matches no module fails
 *       loudly rc == -1 leaving *identity invalid.
 *   (d) unknown-port: a named connection to a port the child does not declare,
 *       and a positional connection past the child's declared port count, each
 *       fail loudly rc == -1 leaving *identity invalid.
 */
#include "check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/data_type.h"
#include "pigen/expression.h"
#include "pigen/instance_identity.h"
#include "pigen/preprocess.h"
#include "pigen/semantic.h"
#include "pigen/source.h"
#include "pigen/syntax.h"

#define IS_INVALID_ID(id) ((id).index == PIGEN_INVALID_ID)
#define IS_INVALID_SPAN(span) ((span).source.index == PIGEN_INVALID_ID)

/* The valid two-module witness: the child `ram` is declared FIRST (its ports
 * first, then the parent `top` module that instantiates it), so the correct
 * type-name resolution must return the SECOND module, not "always the first
 * module". `top` instantiates `ram` as `r1` with one named connection
 * (`.din(a)`) and one positional connection (`b`). */
#define TWO_MODULE_WITNESS \
	"module ram(input logic din, input logic en, output logic dout);\n" \
	"endmodule\n" \
	"module top(input logic a, input logic b, input logic c);\n" \
	"\tram r1 (.din(a), b);\n" \
	"endmodule"

/* The unknown-type witness: `top` instantiates `ghost`, a module that is not
 * declared in the design, so the type-name resolution matches no module. */
#define UNKNOWN_TYPE_WITNESS \
	"module ram(input logic din);\n" \
	"endmodule\n" \
	"module top(input logic a);\n" \
	"\tghost r1 (.din(a));\n" \
	"endmodule"

/* The unknown-port witness: `top` instantiates `ram`, whose only port is `din`,
 * with a named connection to the undeclared port `.en` (the first red). */
#define UNKNOWN_NAMED_PORT_WITNESS \
	"module ram(input logic din);\n" \
	"endmodule\n" \
	"module top(input logic a, input logic b);\n" \
	"\tram r1 (.en(a), b);\n" \
	"endmodule"

/* The positional-past-count witness: `top` instantiates `ram`, whose only port
 * is `din`, with two positional connections (the second runs past the child's
 * single declared port). */
#define POSITIONAL_PAST_COUNT_WITNESS \
	"module ram(input logic din);\n" \
	"endmodule\n" \
	"module top(input logic a, input logic b);\n" \
	"\tram r1 (a, b);\n" \
	"endmodule"

typedef struct {
	pigen_source_manager sources;
	pigen_source_id source;
	pigen_preprocess_result pre;
	pigen_syntax_tree tree;
	pigen_syntax_error error;
	const pigen_syntax_node *first_module;
	const pigen_syntax_node *second_module;
	const pigen_syntax_node *instance;
} parsed;

/* The syntax id of a node: its position in the tree's contiguous node array. */
static pigen_syntax_id syntax_id_of(const pigen_syntax_tree *tree,
	const pigen_syntax_node *node)
{
	return (pigen_syntax_id){(uint32_t)(node - tree->nodes)};
}

static int token_is(const pigen_expanded_source *src, pigen_token_id token,
	const char *word)
{
	const pigen_expanded_token *known = pigen_expanded_token_get(src, token);
	const char *text;
	size_t length;

	if (!known) return 0;
	text = pigen_expanded_token_text(src, known, &length);
	return text && length == strlen(word) && !memcmp(text, word, length);
}

/* Parses TEXT (under NAME) and locates the compilation unit's modules (in
 * source order) and its first module-level instance node. Fills *out. */
static void in_parse(parsed *out, const char *name, const char *text)
{
	const pigen_syntax_node *root;
	const pigen_syntax_node *first_module = NULL;
	const pigen_syntax_node *second_module = NULL;
	const pigen_syntax_node *instance = NULL;
	int saw_first_module;

	memset(out, 0, sizeof *out);
	out->source = pigen_source_add(&out->sources, name, text, strlen(text));
	REQUIRE(!IS_INVALID_ID(out->source));
	{
		pigen_preprocess_error pre_error;

		memset(&pre_error, 0, sizeof pre_error);
		REQUIRE(pigen_preprocess(&out->sources, out->source, NULL, &out->pre,
			&pre_error));
	}
	{
		int ok = pigen_parse_syntax(&out->pre.expanded, &out->tree,
			&out->error);

		root = pigen_syntax_get(&out->tree, (pigen_syntax_id){0});
		REQUIRE(root && root->kind == PIGEN_SYNTAX_COMPILATION_UNIT);
		saw_first_module = 0;
		for (pigen_syntax_id child = root->first_child;
			child.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *node = pigen_syntax_get(&out->tree, child);

			REQUIRE(node);
			if (node->kind == PIGEN_SYNTAX_MODULE)
			{
				if (saw_first_module)
					second_module = node;
				else
				{
					first_module = node;
					saw_first_module = 1;
				}
			}
			child = node->next_sibling;
		}
		out->first_module = first_module;
		out->second_module = second_module;
		/* The instance lives in the last (parent) module. */
		for (pigen_syntax_id child = first_module->first_child;
			child.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *node = pigen_syntax_get(&out->tree, child);

			REQUIRE(node);
			if (node->kind == PIGEN_SYNTAX_INSTANCE) instance = node;
			child = node->next_sibling;
		}
		if (second_module)
		{
			instance = NULL;
			for (pigen_syntax_id child = second_module->first_child;
				child.index != PIGEN_INVALID_ID; )
			{
				const pigen_syntax_node *node =
					pigen_syntax_get(&out->tree, child);

				REQUIRE(node);
				if (node->kind == PIGEN_SYNTAX_INSTANCE) instance = node;
				child = node->next_sibling;
			}
		}
		out->instance = instance;
		if (!ok) return;
	}
}

static void in_free(parsed *p)
{
	pigen_free_syntax_tree(&p->tree);
	pigen_free_preprocess_result(&p->pre);
	pigen_free_sources(&p->sources);
	*p = (parsed){0};
}

/* Builds the resolved semantic model for a two-module design: a compilation
 * scope, the parent module (the module holding the instance) and the child
 * module the instance instantiates, the child's declared ports in source order
 * (each a module-scope signal read from the child's parsed port list), and
 * returns the parent and child module ids and the child's port symbol ids
 * (port_count of them). The parent and child modules' semantic records point
 * their syntax ids at the real parsed module nodes so the owner's token-text
 * type-name resolution runs against the actual tree. The caller owns the model
 * and must free it. */
struct model_witness {
	pigen_module_id parent;
	pigen_module_id child;
	pigen_symbol_id ports[4];
	size_t port_count;
	pigen_data_type_id logic;
};

/* The child's parsed module-level ports, in source order, into out (capacity
 * 4): the child's PIGEN_SYNTAX_SIGNAL_DECLARATION children, each carrying its
 * one declarator's name token, direction and data-type node. Returns the count. */
static size_t collect_child_ports(const parsed *p,
	const pigen_syntax_node *child_node,
	const pigen_syntax_node **out, size_t capacity)
{
	size_t count = 0;
	pigen_syntax_id child;

	for (child = child_node->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&p->tree, child);

		REQUIRE(node);
		if (node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION && count < capacity)
			out[count++] = node;
		child = node->next_sibling;
	}
	return count;
}

/* A direction's semantic form from the parsed syntax direction. */
static pigen_semantic_direction direction_of(pigen_syntax_direction d)
{
	switch (d)
	{
		case PIGEN_DIRECTION_OUTPUT: return PIGEN_SEMANTIC_OUTPUT;
		case PIGEN_DIRECTION_INOUT: return PIGEN_SEMANTIC_INOUT;
		case PIGEN_DIRECTION_INPUT:
		default: return PIGEN_SEMANTIC_INPUT;
	}
}

static struct model_witness build_model(pigen_semantic_model *m,
	const parsed *p)
{
	struct model_witness w;
	const pigen_syntax_node *port_nodes[4];
	pigen_source_span whole;
	pigen_scope_id child_scope, parent_scope;
	pigen_symbol_id parent_symbol, child_symbol, port_symbols[4];
	size_t i, port_count;

	memset(&w, 0, sizeof w);
	pigen_semantic_init(m, &p->sources);
	w.logic = pigen_data_type_sized_logic(m, 1, PIGEN_SIGN_UNSIGNED);
	REQUIRE(!IS_INVALID_ID(w.logic));
	whole = (pigen_source_span){p->source, 0, strlen(TWO_MODULE_WITNESS)};
	(void)whole;

	m->compilation_scope = pigen_scope_add(m,
		(pigen_scope_id){PIGEN_INVALID_ID},
		(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
	REQUIRE(!IS_INVALID_ID(m->compilation_scope));

	/* The child module `ram`, declared first. Its semantic record points at the
	 * real parsed first module node (name token text "ram"). */
	{
		const pigen_syntax_node *child_node = p->first_module;
		pigen_source_span child_span =
			child_node->location.source_span;

		REQUIRE(token_is(&p->pre.expanded, child_node->as.module.name, "ram"));
		REQUIRE(pigen_symbol_declare(m, m->compilation_scope,
			PIGEN_SYMBOL_MODULE, (pigen_data_type_id){PIGEN_INVALID_ID},
			child_span, child_span, &child_symbol, NULL) == PIGEN_DECLARE_OK);
		child_scope = pigen_scope_add(m, m->compilation_scope, child_span);
		REQUIRE(!IS_INVALID_ID(child_scope));
		w.child = pigen_module_add(m, syntax_id_of(&p->tree, child_node),
			child_symbol, child_scope, child_span);
		REQUIRE(!IS_INVALID_ID(w.child));
	}

	/* The child's declared ports in source order: each parsed port declarator's
	 * name token becomes a module-scope signal, its name span pinned to the
	 * token's original spelling range so the named connection's token-text match
	 * runs against real text. */
	port_count = collect_child_ports(p, p->first_module, port_nodes, 4);
	for (i = 0; i < port_count; i++)
	{
		const pigen_syntax_node *decl = port_nodes[i];
		const pigen_syntax_node *declr =
			pigen_syntax_get(&p->tree, decl->first_child);
		pigen_token_id name_token;
		const pigen_expanded_token *known;
		pigen_source_span name_span;

		REQUIRE(declr && declr->kind == PIGEN_SYNTAX_SIGNAL_DECLARATOR);
		name_token = declr->as.signal_declarator.name;
		known = pigen_expanded_token_get(&p->pre.expanded, name_token);
		REQUIRE(known);
		name_span = pigen_origin_spelling_span(&p->pre.expanded, known->origin);
		REQUIRE(!IS_INVALID_SPAN(name_span) &&
			name_span.start < name_span.end);
		REQUIRE(pigen_symbol_declare(m, child_scope, PIGEN_SYMBOL_SIGNAL,
			w.logic, name_span, name_span, &port_symbols[i], NULL) ==
			PIGEN_DECLARE_OK);
		{
			pigen_signal_id signal = pigen_signal_add(m,
				(pigen_syntax_id){(uint32_t)(10 + i)}, w.child,
				port_symbols[i], w.logic, pigen_semantic_scalar_shape(m),
				(pigen_expr_id){PIGEN_INVALID_ID},
				PIGEN_TRANSFER_TYPE_LOGIC,
				direction_of(decl->as.signal_declaration.direction),
				name_span);

			REQUIRE(!IS_INVALID_ID(signal));
		}
		w.ports[i] = port_symbols[i];
	}
	w.port_count = port_count;

	/* The parent module `top`, declared second. Its semantic record points at
	 * the real parsed second module node (name token text "top"). */
	{
		const pigen_syntax_node *parent_node = p->second_module;
		pigen_source_span parent_span =
			parent_node->location.source_span;

		REQUIRE(token_is(&p->pre.expanded, parent_node->as.module.name, "top"));
		REQUIRE(pigen_symbol_declare(m, m->compilation_scope,
			PIGEN_SYMBOL_MODULE, (pigen_data_type_id){PIGEN_INVALID_ID},
			parent_span, parent_span, &parent_symbol, NULL) ==
			PIGEN_DECLARE_OK);
		parent_scope = pigen_scope_add(m, m->compilation_scope, parent_span);
		REQUIRE(!IS_INVALID_ID(parent_scope));
		w.parent = pigen_module_add(m, syntax_id_of(&p->tree, parent_node),
			parent_symbol, parent_scope, parent_span);
		REQUIRE(!IS_INVALID_ID(w.parent));
	}
	return w;
}

/* The invalid identity: the PIGEN_INVALID_ID sentinels, zero counts and empty
 * spans, exactly what the owner seam leaves behind on a failed call. */
static int identity_is_invalid(const pigen_instance_identity *identity)
{
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

/* (a) establish-valid-args: the discriminating validation witness. On a legal
 * two-module design with the child declared first and the parent second, a
 * valid PIGEN_SYNTAX_INSTANCE node is accepted: establish returns 0 and fills a
 * real, non-sentinel identity - the type name resolves to the SECOND module
 * (the child `ram`, not "always the first module"), the instance name is a real
 * original span, and the two source-order connections (.din(a) named, b
 * positional) bind to the child's ports `din` and `en` in declared order, each
 * carrying that port's direction, data type, shape and transfer type. A
 * non-INSTANCE node and each NULL argument are rejected rc == -1 leaving
 * *identity invalid, so an establish that skips its validation cannot pass this
 * section: the valid node demands a success path the skeleton has no path to. */
static void test_establish_valid_args(pigen_source_manager *unused)
{
	parsed p;
	struct model_witness w;
	pigen_semantic_model model;
	pigen_instance_identity identity = {0};
	pigen_syntax_id inst_syntax;
	const pigen_syntax_node *instance;
	const pigen_syntax_node *conn[2];
	pigen_syntax_id child;

	(void)unused;
	in_parse(&p, "valid.pigen", TWO_MODULE_WITNESS);
	REQUIRE(p.error.message == NULL);
	REQUIRE(p.first_module && p.first_module->kind == PIGEN_SYNTAX_MODULE);
	REQUIRE(p.second_module && p.second_module->kind == PIGEN_SYNTAX_MODULE);
	instance = p.instance;
	REQUIRE(instance && instance->kind == PIGEN_SYNTAX_INSTANCE);
	REQUIRE(token_is(&p.pre.expanded, instance->as.instance.type_name, "ram"));
	REQUIRE(token_is(&p.pre.expanded, instance->as.instance.name, "r1"));
	inst_syntax = syntax_id_of(&p.tree, instance);

	/* Collect the two source-order connection children (skipping the parameter
	 * children, of which there are none here). */
	child = instance->first_child;
	REQUIRE(child.index != PIGEN_INVALID_ID);
	conn[0] = pigen_syntax_get(&p.tree, child);
	REQUIRE(conn[0] && conn[0]->kind == PIGEN_SYNTAX_INSTANCE_CONNECTION);
	child = conn[0]->next_sibling;
	conn[1] = pigen_syntax_get(&p.tree, child);
	REQUIRE(conn[1] && conn[1]->kind == PIGEN_SYNTAX_INSTANCE_CONNECTION);
	REQUIRE(conn[1]->next_sibling.index == PIGEN_INVALID_ID);

	/* The individually-invalid forms are rejected rc == -1 leaving *identity
	 * invalid: a non-INSTANCE node (the owning module node is a valid parsed
	 * node, but not the node kind this owner consumes), a NULL model and a NULL
	 * expanded source. */
	{
		const pigen_syntax_node *wrong = p.first_module;

		REQUIRE(wrong && wrong->kind != PIGEN_SYNTAX_INSTANCE);
		pigen_semantic_init(&model, &p.sources);
		REQUIRE(pigen_instance_identity_establish(&model, &p.tree,
			inst_syntax, (pigen_module_id){PIGEN_INVALID_ID}, wrong,
			&p.pre.expanded, &identity) == -1);
		REQUIRE(identity_is_invalid(&identity));
		pigen_free_semantic_model(&model);
	}
	{
		pigen_semantic_init(&model, &p.sources);
		REQUIRE(pigen_instance_identity_establish(NULL, &p.tree, inst_syntax,
			(pigen_module_id){PIGEN_INVALID_ID}, instance, &p.pre.expanded,
			&identity) == -1);
		REQUIRE(identity_is_invalid(&identity));
		pigen_free_semantic_model(&model);
	}
	{
		pigen_semantic_init(&model, &p.sources);
		REQUIRE(pigen_instance_identity_establish(&model, &p.tree,
			inst_syntax, (pigen_module_id){PIGEN_INVALID_ID}, instance, NULL,
			&identity) == -1);
		REQUIRE(identity_is_invalid(&identity));
		pigen_free_semantic_model(&model);
	}

	/* The valid form: non-null model, identity, expanded source and the
	 * PIGEN_SYNTAX_INSTANCE node. establish returns 0 and fills a real,
	 * non-sentinel identity. */
	identity = (pigen_instance_identity){0};
	w = build_model(&model, &p);
	REQUIRE(pigen_instance_identity_establish(&model, &p.tree, inst_syntax,
		w.parent, instance, &p.pre.expanded, &identity) == 0);
	REQUIRE(pigen_instance_identity_is_invalid(&identity) == 0);

	/* The type name resolves to the child `ram`, the SECOND module (not the
	 * first module of the model - here the child is also first, but the parent
	 * `top` is the module the instance lives in; an owner that returns the
	 * parent or "always the first" cannot return the child's port set). */
	REQUIRE(identity.type_module.index == w.child.index);

	/* The instance name: a real original span over the witness source. */
	REQUIRE(identity.instance_name.source.index == p.source.index);
	REQUIRE(identity.instance_name.start < identity.instance_name.end);
	REQUIRE(pigen_source_span_text(&p.sources, identity.instance_name,
		NULL) &&
		!IS_INVALID_SPAN(identity.instance_name));

	/* The two resolved connections, in source order. */
	REQUIRE(identity.connection_count == 2);

	/* Connection 0: the named `.din(a)` binds to the child's `din` port, an
	 * input LOGIC. */
	REQUIRE(identity.connections[0].port.index == w.ports[0].index);
	REQUIRE(identity.connections[0].direction == PIGEN_SEMANTIC_INPUT);
	REQUIRE(identity.connections[0].data_type.index == w.logic.index);
	REQUIRE(identity.connections[0].transfer_type ==
		PIGEN_TRANSFER_TYPE_LOGIC);
	REQUIRE(!IS_INVALID_ID(identity.connections[0].shape));
	REQUIRE(identity.connections[0].span.source.index == p.source.index);
	REQUIRE(identity.connections[0].span.start <
		identity.connections[0].span.end);
	REQUIRE(identity.connections[0].syntax.index ==
		syntax_id_of(&p.tree, conn[0]).index);

	/* Connection 1: the positional `b` binds to the child's next declared port
	 * in order, `en`, an input LOGIC. */
	REQUIRE(identity.connections[1].port.index == w.ports[1].index);
	REQUIRE(identity.connections[1].direction == PIGEN_SEMANTIC_INPUT);
	REQUIRE(identity.connections[1].data_type.index == w.logic.index);
	REQUIRE(identity.connections[1].transfer_type ==
		PIGEN_TRANSFER_TYPE_LOGIC);
	REQUIRE(!IS_INVALID_ID(identity.connections[1].shape));
	REQUIRE(identity.connections[1].span.source.index == p.source.index);
	REQUIRE(identity.connections[1].syntax.index ==
		syntax_id_of(&p.tree, conn[1]).index);

	/* The instance's original span: non-empty and over the witness source. */
	REQUIRE(identity.span.source.index == p.source.index);
	REQUIRE(identity.span.start < identity.span.end);

	/* The owning syntax node id: the PIGEN_SYNTAX_INSTANCE node itself. */
	REQUIRE(identity.syntax.index == inst_syntax.index);

	pigen_free_semantic_model(&model);
	in_free(&p);
}

/* (b) is-invalid-sentinel: the predicate is 1 for the all-sentinel identity and
 * 0 once any single field is set; a null pointer reports 1. */
static void test_is_invalid_sentinel(pigen_source_manager *sources)
{
	pigen_instance_identity identity;

	(void)sources;
	/* The all-sentinel invalid form, field by field. */
	memset(&identity, 0, sizeof identity);
	identity.type_module.index = PIGEN_INVALID_ID;
	identity.instance_name.source.index = PIGEN_INVALID_ID;
	identity.instance_name.start = 0;
	identity.instance_name.end = 0;
	identity.connection_count = 0;
	for (size_t i = 0; i < 32; i++)
	{
		identity.connections[i].port.index = PIGEN_INVALID_ID;
		identity.connections[i].direction = (pigen_semantic_direction)0;
		identity.connections[i].data_type.index = PIGEN_INVALID_ID;
		identity.connections[i].shape.index = PIGEN_INVALID_ID;
		identity.connections[i].transfer_type = (pigen_transfer_type)0;
		identity.connections[i].span.source.index = PIGEN_INVALID_ID;
		identity.connections[i].span.start = 0;
		identity.connections[i].span.end = 0;
		identity.connections[i].syntax.index = PIGEN_INVALID_ID;
	}
	identity.span.source.index = PIGEN_INVALID_ID;
	identity.span.start = 0;
	identity.span.end = 0;
	identity.syntax.index = PIGEN_INVALID_ID;
	REQUIRE(identity_is_invalid(&identity));
	REQUIRE(pigen_instance_identity_is_invalid(&identity) == 1);
	REQUIRE(pigen_instance_identity_is_invalid(NULL) == 1);

	/* Setting any single id clears the sentinel. */
	identity.type_module.index = 0;
	REQUIRE(pigen_instance_identity_is_invalid(&identity) == 0);
	identity.type_module.index = PIGEN_INVALID_ID;

	identity.syntax.index = 0;
	REQUIRE(pigen_instance_identity_is_invalid(&identity) == 0);
	identity.syntax.index = PIGEN_INVALID_ID;

	identity.connections[0].port.index = 0;
	REQUIRE(pigen_instance_identity_is_invalid(&identity) == 0);
	identity.connections[0].port.index = PIGEN_INVALID_ID;

	identity.connections[0].syntax.index = 0;
	REQUIRE(pigen_instance_identity_is_invalid(&identity) == 0);
	identity.connections[0].syntax.index = PIGEN_INVALID_ID;

	/* Setting the count clears the sentinel. */
	identity.connection_count = 1;
	REQUIRE(pigen_instance_identity_is_invalid(&identity) == 0);
	identity.connection_count = 0;

	/* A non-empty span clears the sentinel. */
	identity.instance_name.source.index = 0;
	REQUIRE(pigen_instance_identity_is_invalid(&identity) == 0);
	identity.instance_name.source.index = PIGEN_INVALID_ID;
	identity.instance_name.start = 1;
	REQUIRE(pigen_instance_identity_is_invalid(&identity) == 0);
	identity.instance_name.start = 0;
	identity.instance_name.end = 1;
	REQUIRE(pigen_instance_identity_is_invalid(&identity) == 0);
	identity.instance_name.end = 0;

	identity.span.source.index = 0;
	REQUIRE(pigen_instance_identity_is_invalid(&identity) == 0);
	identity.span.source.index = PIGEN_INVALID_ID;

	identity.connections[0].span.source.index = 0;
	REQUIRE(pigen_instance_identity_is_invalid(&identity) == 0);
	identity.connections[0].span.source.index = PIGEN_INVALID_ID;
}

/* (c) unknown-type-name: an instance whose type name matches no module fails
 * loudly rc == -1 leaving *identity invalid, so an owner that returns a
 * fabricated module id or silently succeeds cannot pass this section. */
static void test_unknown_type_name(pigen_source_manager *unused)
{
	parsed p;
	struct model_witness w;
	pigen_semantic_model model;
	pigen_instance_identity identity = {0};
	pigen_syntax_id inst_syntax;
	const pigen_syntax_node *instance;

	(void)unused;
	in_parse(&p, "unknown-type.pigen", UNKNOWN_TYPE_WITNESS);
	REQUIRE(p.error.message == NULL);
	instance = p.instance;
	REQUIRE(instance && instance->kind == PIGEN_SYNTAX_INSTANCE);
	REQUIRE(token_is(&p.pre.expanded, instance->as.instance.type_name, "ghost"));
	inst_syntax = syntax_id_of(&p.tree, instance);
	w = build_model(&model, &p);
	REQUIRE(pigen_instance_identity_establish(&model, &p.tree, inst_syntax,
		w.parent, instance, &p.pre.expanded, &identity) == -1);
	REQUIRE(identity_is_invalid(&identity));
	pigen_free_semantic_model(&model);
	in_free(&p);
}

/* (d) unknown-port: a named connection to a port the child does not declare,
 * and a positional connection past the child's declared port count, each fail
 * loudly rc == -1 leaving *identity invalid, so an owner that fabricates a port
 * or silently truncates cannot pass this section. */
static void test_unknown_port(pigen_source_manager *unused)
{
	parsed named;
	parsed positional;
	struct model_witness w;
	pigen_semantic_model model;
	pigen_instance_identity identity = {0};
	pigen_syntax_id inst_syntax;
	const pigen_syntax_node *instance;

	(void)unused;

	/* The named red: `.en(a)` names a port the child `ram` (only `din`) does
	 * not declare. */
	in_parse(&named, "unknown-named.pigen", UNKNOWN_NAMED_PORT_WITNESS);
	REQUIRE(named.error.message == NULL);
	instance = named.instance;
	REQUIRE(instance && instance->kind == PIGEN_SYNTAX_INSTANCE);
	inst_syntax = syntax_id_of(&named.tree, instance);
	{
		const pigen_syntax_node *conn =
			pigen_syntax_get(&named.tree, instance->first_child);

		REQUIRE(conn && conn->kind == PIGEN_SYNTAX_INSTANCE_CONNECTION);
		REQUIRE(!IS_INVALID_ID(conn->as.instance_connection.port));
		REQUIRE(token_is(&named.pre.expanded,
			conn->as.instance_connection.port, "en"));
	}
	w = build_model(&model, &named);
	REQUIRE(pigen_instance_identity_establish(&model, &named.tree,
		inst_syntax, w.parent, instance, &named.pre.expanded, &identity)
		== -1);
	REQUIRE(identity_is_invalid(&identity));
	pigen_free_semantic_model(&model);
	in_free(&named);

	/* The positional red: `(a, b)` runs past the child `ram`'s single declared
	 * port `din`. */
	in_parse(&positional, "positional-past.pigen", POSITIONAL_PAST_COUNT_WITNESS);
	REQUIRE(positional.error.message == NULL);
	instance = positional.instance;
	REQUIRE(instance && instance->kind == PIGEN_SYNTAX_INSTANCE);
	inst_syntax = syntax_id_of(&positional.tree, instance);
	{
		const pigen_syntax_node *conn =
			pigen_syntax_get(&positional.tree, instance->first_child);
		const pigen_syntax_node *conn2;
		pigen_syntax_id c;

		REQUIRE(conn && conn->kind == PIGEN_SYNTAX_INSTANCE_CONNECTION);
		REQUIRE(IS_INVALID_ID(conn->as.instance_connection.port));
		c = conn->next_sibling;
		conn2 = pigen_syntax_get(&positional.tree, c);
		REQUIRE(conn2 && conn2->kind == PIGEN_SYNTAX_INSTANCE_CONNECTION);
		REQUIRE(IS_INVALID_ID(conn2->as.instance_connection.port));
	}
	w = build_model(&model, &positional);
	REQUIRE(pigen_instance_identity_establish(&model, &positional.tree,
		inst_syntax, w.parent, instance, &positional.pre.expanded, &identity)
		== -1);
	REQUIRE(identity_is_invalid(&identity));
	pigen_free_semantic_model(&model);
	in_free(&positional);
}

int main(int argc, char **argv)
{
	pigen_source_manager sources = {0};

	check_init(argc, argv);

	SECTION("establish-valid-args") {
		test_establish_valid_args(&sources);
	}

	SECTION("is-invalid-sentinel") {
		test_is_invalid_sentinel(&sources);
	}

	SECTION("unknown-type-name") {
		test_unknown_type_name(&sources);
	}

	SECTION("unknown-port") {
		test_unknown_port(&sources);
	}

	return check_finish();
}
