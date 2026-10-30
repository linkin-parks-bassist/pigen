/* Instance lowering owner test contract.
 *
 * (i3) of the child-instance stage: the discriminating green manifest that pins
 * pigen_instance_lower (include/pigen/instance_lower.h) against the behavior
 * contract the spec and the owning leaves require (the instance record model
 * include/pigen/rtl.h: a pigen_rtl_instance owned by the parent module record,
 * carrying the child module's provenance, the instance's original span and the
 * ordered source-order parameter and connection ranges; the FSM sibling
 * what/is/the/fsm/parent/module/lowering.md; the module-record composition
 * what/is/elastic/rtl/task/eight.md). The owner turns one PIGEN_SYNTAX_INSTANCE
 * statement (an ordinary SV child instantiation inside a module body) into one
 * child-instance record in the common RTL model owned by the parent module:
 * the parent's RTL module record, one VARIABLE object per source-order
 * connection bound by provenance to the parent's matching signal (a NAME atom
 * whose token text matches the signal's symbol name span text), one integer
 * expression per source-order decimal parameter, and the instance record (origin
 * the instance node's original span, semantic_module the resolved child module's
 * provenance, the ordered parameter and connection ranges). The record carries
 * the instance-name text as the only name - surface-independent.
 *
 * Three sections, one `make instance-lower-test T=<section>` entry per section
 * in tests/contracts.json:
 *
 *   (a) lower-valid-args: a legal two-module design with the instantiating
 *       parent `top` declared FIRST and the instantiated child `child` declared
 *       SECOND, one decimal parameter and a named and a positional connection.
 *       The anchor lowers to rc == 0 and pins that the published instance is
 *       owned by the parent's RTL module record, its semantic_module is the
 *       child's provenance (the SECOND-declared module's id, NOT "always the
 *       first"), its origin is the whole instance span, and its parameter and
 *       connection ranges are non-empty with owner-published contents: the
 *       connections bound by provenance to the parent's matching signals
 *       (.din(a) to a, positional b to b). The DISCRIMINATING module-order
 *       discipline (the corrected (i2) TWO_MODULE_WITNESS parent-first shape): a
 *       resolve_type_module replaced with `return (pigen_module_id){0}` returns
 *       the parent, not the child, and fails the semantic_module == child
 *       assertion - the mutation that must be caught before the packet is
 *       accepted; a resolve_parent_signal replaced with a fixed/first-parent-
 *       signal return (ignoring the connection atom text) fails the
 *       semantic_signal == expected-signal binding assertion - the second
 *       mutation the packet must catch. A non-instance node and each NULL
 *       argument are rejected rc == -1 leaving *out invalid.
 *   (b) seam-red: a real, non-PIGEN_SYNTAX_INSTANCE node of the parent module
 *       driven through the seam returns rc == -1 and publishes nothing. The
 *       node-kind rejection is the seam's own full-contract obligation.
 *   (c) connection-reds: a failed connection-expression lowering - a NAME atom
 *       matching no parent signal, and a connection expression that is not a
 *       NAME atom - each returns rc == -1. Each is discriminating: a mutation
 *       that publishes on the rejected path fails the unknown-signal red.
 *
 * Every witness is located through the parse (the compilation unit's modules,
 * the instance node and its connection and parameter children) and every
 * provenance is a span check against the parsed source - never a source-text
 * search over the output. The manifest pins the CONTRACT facts and nothing the
 * contract leaves to the implementation (the exact parameter RTL type width and
 * the object type descriptors are the implementation's). A failed lowering call
 * stops the compile and may leave partial state behind, so no anchor asserts
 * what a failure left unchanged (how/should/compiler/builders/fail.md).
 */
#include "check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/data_type.h"
#include "pigen/expression.h"
#include "pigen/instance_lower.h"
#include "pigen/preprocess.h"
#include "pigen/rtl.h"
#include "pigen/rtl_lower.h"
#include "pigen/semantic.h"
#include "pigen/source.h"
#include "pigen/syntax.h"

#define IS_INVALID_ID(id) ((id).index == PIGEN_INVALID_ID)
#define IS_INVALID_SPAN(span) ((span).source.index == PIGEN_INVALID_ID)

/* The valid two-module witness: the instantiating parent `top` is declared
 * FIRST (it instantiates `child` as `r0` with one decimal parameter and a named
 * and a positional connection), so the correct type-name resolution must return
 * the SECOND module (the child `child`), not "always the first module". */
#define TWO_MODULE_WITNESS \
	"module top(input logic a, input logic b, input logic c);\n" \
	"\tchild #(4) r0 (.din(a), b);\n" \
	"endmodule\n" \
	"module child(input logic din, input logic en, output logic dout);\n" \
	"endmodule"

/* The unknown-signal witness: `top` instantiates `child` with a named
 * connection `.din(q)` whose NAME atom `q` matches no parent signal. */
#define UNKNOWN_SIGNAL_WITNESS \
	"module top(input logic a, input logic b, input logic c);\n" \
	"\tchild r0 (.din(q), b);\n" \
	"endmodule\n" \
	"module child(input logic din, input logic en, output logic dout);\n" \
	"endmodule"

/* The non-atom witness: `top` instantiates `child` with a named connection
 * `.din(a + 1)` whose expression is a binary operation, not a NAME atom. */
#define NON_ATOM_WITNESS \
	"module top(input logic a, input logic b, input logic c);\n" \
	"\tchild r0 (.din(a + 1), b);\n" \
	"endmodule\n" \
	"module child(input logic din, input logic en, output logic dout);\n" \
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
	const pigen_syntax_node *non_instance;
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

/* The expression arena's node for a syntax expression id. */
static const pigen_syntax_expr *expr_of(const pigen_syntax_tree *tree,
	pigen_syntax_expr_id id)
{
	return pigen_syntax_expr_get(&tree->expressions, id);
}

/* The node at the raw node-array index RAW within the tree that owns NODE: the
 * instance's first_parameter and first_connection are raw node-array indices,
 * recovered against the node's own position the way the owner does. */
static const pigen_syntax_node *inst_node(const pigen_syntax_node *instance,
	pigen_syntax_id syntax, size_t raw)
{
	return (instance - syntax.index) + raw;
}

/* Parses TEXT (under NAME) and locates the compilation unit's modules (in
 * source order), the first module-level instance node and a real non-instance
 * child of the parent (first) module (the seam red's witness). Fills *out. */
static void il_parse(parsed *out, const char *name, const char *text)
{
	const pigen_syntax_node *root;
	const pigen_syntax_node *first_module = NULL;
	const pigen_syntax_node *second_module = NULL;
	const pigen_syntax_node *instance = NULL;
	const pigen_syntax_node *non_instance = NULL;
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
		/* The instance and a non-instance child live in the first (parent)
		 * module. */
		REQUIRE(first_module);
		for (pigen_syntax_id child = first_module->first_child;
			child.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *node = pigen_syntax_get(&out->tree, child);

			REQUIRE(node);
			if (node->kind == PIGEN_SYNTAX_INSTANCE)
				instance = node;
			else if (!non_instance)
				non_instance = node;
			child = node->next_sibling;
		}
		out->instance = instance;
		out->non_instance = non_instance;
		if (!ok) return;
	}
}

static void il_free(parsed *p)
{
	pigen_free_syntax_tree(&p->tree);
	pigen_free_preprocess_result(&p->pre);
	pigen_free_sources(&p->sources);
	*p = (parsed){0};
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

struct model_witness {
	pigen_module_id parent;
	pigen_module_id child;
	pigen_symbol_id signals[4];
	pigen_signal_id signal_ids[4];
	size_t signal_count;
	pigen_data_type_id logic;
};

/* Declares the parsed module-level ports of NODE (its PIGEN_SYNTAX_SIGNAL_
 * DECLARATION children, each carrying its one declarator's name token) as
 * module-scope signals of MODULE, each name span pinned to the token's original
 * spelling range so the connection NAME-atom token-text match runs against real
 * text. Fills out and out_ids (capacity 4, the declared symbol ids and the
 * pigen_signal_id of each declared signal, in source order) and returns the
 * count. */
static size_t declare_ports(pigen_semantic_model *m, pigen_scope_id scope,
	pigen_module_id module, const parsed *p, const pigen_syntax_node *node,
	pigen_data_type_id logic, pigen_symbol_id *out,
	pigen_signal_id *out_ids)
{
	size_t count = 0;
	pigen_syntax_id child;

	for (child = node->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *decl = pigen_syntax_get(&p->tree, child);
		const pigen_syntax_node *declr;
		pigen_token_id name_token;
		const pigen_expanded_token *known;
		pigen_source_span name_span;
		pigen_symbol_id symbol;
		pigen_signal_id signal;

		REQUIRE(decl);
		if (decl->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION && count < 4)
		{
			declr = pigen_syntax_get(&p->tree, decl->first_child);
			REQUIRE(declr && declr->kind == PIGEN_SYNTAX_SIGNAL_DECLARATOR);
			name_token = declr->as.signal_declarator.name;
			known = pigen_expanded_token_get(&p->pre.expanded, name_token);
			REQUIRE(known);
			name_span = pigen_origin_spelling_span(&p->pre.expanded,
				known->origin);
			REQUIRE(!IS_INVALID_SPAN(name_span) &&
				name_span.start < name_span.end);
			REQUIRE(pigen_symbol_declare(m, scope, PIGEN_SYMBOL_SIGNAL, logic,
				name_span, name_span, &symbol, NULL) == PIGEN_DECLARE_OK);
			signal = pigen_signal_add(m,
				(pigen_syntax_id){(uint32_t)(20 + count)}, module, symbol,
				logic, pigen_semantic_scalar_shape(m),
				(pigen_expr_id){PIGEN_INVALID_ID},
				PIGEN_TRANSFER_TYPE_LOGIC,
				direction_of(decl->as.signal_declaration.direction),
				name_span);
			REQUIRE(!IS_INVALID_ID(signal));
			out[count] = symbol;
			out_ids[count] = signal;
			count++;
		}
		child = decl->next_sibling;
	}
	return count;
}

/* Builds the resolved semantic model for a two-module design: a compilation
 * scope, the parent module (declared first) and the child module (declared
 * second), the parent's declared ports as module-scope signals (each name span
 * pinned to its token's original spelling range). Each module's symbol name span
 * is pinned to its name token's original spelling range (the production
 * add_module shape, src/resolve.c), so the owner's token-text type-name
 * resolution runs against real text. The caller owns the model and must free
 * it. */
static struct model_witness build_model(pigen_semantic_model *m,
	const parsed *p)
{
	struct model_witness w;
	pigen_scope_id parent_scope, child_scope;
	pigen_symbol_id parent_symbol, child_symbol;

	memset(&w, 0, sizeof w);
	pigen_semantic_init(m, &p->sources);
	w.logic = pigen_data_type_sized_logic(m, 1, PIGEN_SIGN_UNSIGNED);
	REQUIRE(!IS_INVALID_ID(w.logic));

	m->compilation_scope = pigen_scope_add(m,
		(pigen_scope_id){PIGEN_INVALID_ID},
		(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
	REQUIRE(!IS_INVALID_ID(m->compilation_scope));

	/* The parent module `top`, declared FIRST. Its symbol name span is the name
	 * token's spelling range; its semantic record points at the real parsed
	 * first module node. */
	{
		const pigen_syntax_node *parent_node = p->first_module;
		const pigen_expanded_token *known;
		pigen_source_span name_span, parent_span;

		REQUIRE(parent_node && parent_node->kind == PIGEN_SYNTAX_MODULE);
		REQUIRE(token_is(&p->pre.expanded, parent_node->as.module.name, "top"));
		known = pigen_expanded_token_get(&p->pre.expanded,
			parent_node->as.module.name);
		REQUIRE(known);
		name_span = pigen_origin_spelling_span(&p->pre.expanded, known->origin);
		parent_span = parent_node->location.source_span;
		REQUIRE(!IS_INVALID_SPAN(name_span) && name_span.start < name_span.end);
		REQUIRE(pigen_symbol_declare(m, m->compilation_scope,
			PIGEN_SYMBOL_MODULE, (pigen_data_type_id){PIGEN_INVALID_ID},
			name_span, parent_span, &parent_symbol, NULL) == PIGEN_DECLARE_OK);
		parent_scope = pigen_scope_add(m, m->compilation_scope, parent_span);
		REQUIRE(!IS_INVALID_ID(parent_scope));
		w.parent = pigen_module_add(m, syntax_id_of(&p->tree, parent_node),
			parent_symbol, parent_scope, parent_span);
		REQUIRE(!IS_INVALID_ID(w.parent));
		w.signal_count = declare_ports(m, parent_scope, w.parent, p,
			parent_node, w.logic, w.signals, w.signal_ids);
		REQUIRE(w.signal_count == 3);
	}

	/* The child module `child`, declared SECOND. Its symbol name span is the
	 * name token's spelling range; its semantic record points at the real parsed
	 * second module node. */
	{
		const pigen_syntax_node *child_node = p->second_module;
		const pigen_expanded_token *known;
		pigen_source_span name_span, child_span;

		REQUIRE(child_node && child_node->kind == PIGEN_SYNTAX_MODULE);
		REQUIRE(token_is(&p->pre.expanded, child_node->as.module.name, "child"));
		known = pigen_expanded_token_get(&p->pre.expanded,
			child_node->as.module.name);
		REQUIRE(known);
		name_span = pigen_origin_spelling_span(&p->pre.expanded, known->origin);
		child_span = child_node->location.source_span;
		REQUIRE(!IS_INVALID_SPAN(name_span) && name_span.start < name_span.end);
		REQUIRE(pigen_symbol_declare(m, m->compilation_scope,
			PIGEN_SYMBOL_MODULE, (pigen_data_type_id){PIGEN_INVALID_ID},
			name_span, child_span, &child_symbol, NULL) == PIGEN_DECLARE_OK);
		child_scope = pigen_scope_add(m, m->compilation_scope, child_span);
		REQUIRE(!IS_INVALID_ID(child_scope));
		w.child = pigen_module_add(m, syntax_id_of(&p->tree, child_node),
			child_symbol, child_scope, child_span);
		REQUIRE(!IS_INVALID_ID(w.child));
	}
	return w;
}

/* (a) lower-valid-args: the discriminating validation witness. On a legal
 * two-module design with the parent declared first and the child second, a
 * valid PIGEN_SYNTAX_INSTANCE node is accepted: lower returns 0 and publishes a
 * pigen_rtl_instance owned by the parent's RTL module record, its
 * semantic_module the child's provenance (the SECOND module, not "always the
 * first"), its origin the whole instance span, and non-empty parameter and
 * connection ranges with owner-published contents, the connections each bound
 * by provenance to the parent's matching signal. A non-instance node and each
 * NULL argument are rejected rc == -1 leaving *out invalid, so an owner that
 * skips its validation cannot pass this section. */
static void test_lower_valid_args(void)
{
	parsed p;
	pigen_semantic_model model;
	pigen_rtl_model rtl;
	struct model_witness w;
	pigen_rtl_instance_id out = (pigen_rtl_instance_id){PIGEN_INVALID_ID};
	pigen_syntax_id inst_syntax;
	const pigen_syntax_node *instance;
	const pigen_syntax_node *param;
	const pigen_syntax_node *conn[2];

	il_parse(&p, "valid.pigen", TWO_MODULE_WITNESS);
	REQUIRE(p.error.message == NULL);
	REQUIRE(p.first_module && p.first_module->kind == PIGEN_SYNTAX_MODULE);
	REQUIRE(p.second_module && p.second_module->kind == PIGEN_SYNTAX_MODULE);
	instance = p.instance;
	REQUIRE(instance && instance->kind == PIGEN_SYNTAX_INSTANCE);
	REQUIRE(token_is(&p.pre.expanded, instance->as.instance.type_name, "child"));
	REQUIRE(token_is(&p.pre.expanded, instance->as.instance.name, "r0"));
	REQUIRE(instance->as.instance.parameter_count == 1);
	REQUIRE(instance->as.instance.connection_count == 2);
	inst_syntax = syntax_id_of(&p.tree, instance);

	/* The one parameter and the two source-order connections. */
	param = inst_node(instance, inst_syntax,
		instance->as.instance.first_parameter);
	REQUIRE(param && param->kind == PIGEN_SYNTAX_PARAMETER);
	conn[0] = inst_node(instance, inst_syntax,
		instance->as.instance.first_connection);
	REQUIRE(conn[0] && conn[0]->kind == PIGEN_SYNTAX_INSTANCE_CONNECTION);
	conn[1] = inst_node(instance, inst_syntax,
		instance->as.instance.first_connection + 1);
	REQUIRE(conn[1] && conn[1]->kind == PIGEN_SYNTAX_INSTANCE_CONNECTION);
	/* The first connection is the named `.din(a)` (a port token is set), the
	 * second the positional `b` (no port token). */
	REQUIRE(!IS_INVALID_ID(conn[0]->as.instance_connection.port));
	REQUIRE(IS_INVALID_ID(conn[1]->as.instance_connection.port));

	/* A non-instance node is rejected rc == -1 leaving *out invalid, so an
	 * owner that skips its node-kind check cannot pass this section. */
	{
		const pigen_syntax_node *wrong = p.non_instance;

		REQUIRE(wrong && wrong->kind != PIGEN_SYNTAX_INSTANCE);
		pigen_semantic_init(&model, &p.sources);
		w = build_model(&model, &p);
		{
			pigen_rtl_lowering lowering = {0};

			lowering.semantics = &model;
			lowering.rtl = &rtl;
			REQUIRE(pigen_instance_lower(&lowering, w.parent,
				syntax_id_of(&p.tree, wrong), wrong, &out) == -1);
			REQUIRE(IS_INVALID_ID(out));
		}
		pigen_free_semantic_model(&model);
	}
	/* Each NULL argument is rejected rc == -1 leaving *out invalid. */
	pigen_semantic_init(&model, &p.sources);
	w = build_model(&model, &p);
	{
		pigen_rtl_lowering lowering = {0};

		lowering.semantics = &model;
		lowering.rtl = &rtl;
		REQUIRE(pigen_instance_lower(NULL, w.parent, inst_syntax, instance,
			&out) == -1);
		REQUIRE(IS_INVALID_ID(out));
		REQUIRE(pigen_instance_lower(&lowering,
			(pigen_module_id){PIGEN_INVALID_ID}, inst_syntax, instance, &out)
			== -1);
		REQUIRE(IS_INVALID_ID(out));
		REQUIRE(pigen_instance_lower(&lowering, w.parent, inst_syntax, NULL,
			&out) == -1);
		REQUIRE(IS_INVALID_ID(out));
	}

	/* The valid form: lower returns 0 and publishes the instance record owned
	 * by the parent's RTL module record, with the child's provenance, the whole
	 * instance origin and the owner-published parameter and connection ranges. */
	memset(&rtl, 0, sizeof rtl);
	{
		pigen_rtl_lowering lowering = {0};

		memset(&out, 0, sizeof out);
		lowering.semantics = &model;
		lowering.rtl = &rtl;
		REQUIRE(pigen_instance_lower(&lowering, w.parent, inst_syntax, instance,
			&out) == 0);
		REQUIRE(!IS_INVALID_ID(out));
		{
			const pigen_rtl_instance *inst =
				pigen_rtl_instance_get(&rtl, out);
			const pigen_rtl_module *owner;
			size_t i;

			REQUIRE(inst);
			/* (a) Owned by the parent's RTL module record. */
			REQUIRE(rtl.module_count == 1);
			owner = pigen_rtl_module_get(&rtl, (pigen_rtl_module_id){0});
			REQUIRE(owner &&
				owner->semantic_module.index == w.parent.index);
			REQUIRE(inst->module.index == 0);
			/* The child's provenance: the SECOND-declared module, not the
			 * parent and not "always the first". */
			REQUIRE(inst->semantic_module.index == w.child.index);
			REQUIRE(w.child.index != w.parent.index);
			/* Origin: the whole instance span. */
			REQUIRE(inst->origin.source.index == p.source.index);
			REQUIRE(inst->origin.start == instance->location.source_span.start);
			REQUIRE(inst->origin.end == instance->location.source_span.end);
			/* One decimal parameter: an integer expression with the witness
			 * value. */
			REQUIRE(inst->parameters.count == 1);
			{
				const pigen_rtl_expr *pe = pigen_rtl_expr_get(&rtl,
					rtl.instance_parameters[inst->parameters.first]);

				REQUIRE(pe && pe->kind == PIGEN_RTL_EXPR_INTEGER &&
					pe->value == 4);
			}
			/* Two connections: parent-owned VARIABLE objects, each bound by provenance
			 * to the parent's MATCHING signal (the connection NAME atom's token text
			 * equals the signal's symbol name span text): .din(a) to a, positional b
			 * to b. A resolver that ignores the atom text and returns a fixed/first
			 * parent signal fails this binding check. */
			REQUIRE(inst->connections.count == 2);
			for (i = 0; i < 2; i++)
			{
				const pigen_rtl_object *co = pigen_rtl_object_get(&rtl,
					rtl.instance_connections[inst->connections.first + i]);

				REQUIRE(co);
				REQUIRE(co->kind == PIGEN_RTL_OBJECT_VARIABLE);
				REQUIRE(co->module.index == 0);
				REQUIRE(co->semantic_signal.index == w.signal_ids[i].index);
			}
		}
	}
	pigen_free_rtl_model(&rtl);
	pigen_free_semantic_model(&model);
	il_free(&p);
}

/* (b) seam-red: the seam carries the instance node pointer and validates its
 * kind (include/pigen/instance_lower.h: the node must name a
 * PIGEN_SYNTAX_INSTANCE). A real, non-PIGEN_SYNTAX_INSTANCE node of the parent
 * module (a port declaration) driven through the seam returns rc == -1 and
 * publishes nothing. */
static void test_seam_red(void)
{
	parsed p;
	pigen_semantic_model model;
	pigen_rtl_model rtl;
	struct model_witness w;
	pigen_rtl_instance_id out = (pigen_rtl_instance_id){PIGEN_INVALID_ID};
	const pigen_syntax_node *wrong;

	il_parse(&p, "seam.pigen", TWO_MODULE_WITNESS);
	REQUIRE(p.error.message == NULL);
	wrong = p.non_instance;
	REQUIRE(wrong && wrong->kind != PIGEN_SYNTAX_INSTANCE);
	w = build_model(&model, &p);
	{
		pigen_rtl_lowering lowering = {0};
		int rc;

		memset(&rtl, 0, sizeof rtl);
		lowering.semantics = &model;
		lowering.rtl = &rtl;
		rc = pigen_instance_lower(&lowering, w.parent,
			syntax_id_of(&p.tree, wrong), wrong, &out);
		REQUIRE(rc == -1);
		REQUIRE(IS_INVALID_ID(out));
	}
	pigen_free_rtl_model(&rtl);
	pigen_free_semantic_model(&model);
	il_free(&p);
}

/* (c) connection-reds: a failed connection-expression lowering - a NAME atom
 * matching no parent signal, and a connection expression that is not a NAME
 * atom - each returns rc == -1. Each is discriminating: a mutation that
 * publishes on the rejected path fails the unknown-signal red. */
static void test_connection_reds(void)
{
	/* The unknown-signal red: `.din(q)` names a signal the parent does not
	 * declare. The connection's expression is a NAME atom (the discriminator:
	 * the owner rejects it because no parent signal matches, not because it is
	 * not an atom). */
	{
		parsed p;
		pigen_semantic_model model;
		pigen_rtl_model rtl;
		struct model_witness w;
		pigen_rtl_instance_id out = (pigen_rtl_instance_id){PIGEN_INVALID_ID};
		pigen_syntax_id inst_syntax;
		const pigen_syntax_node *instance;
		const pigen_syntax_node *conn;
		const pigen_syntax_expr *atom;

		il_parse(&p, "unknown-signal.pigen", UNKNOWN_SIGNAL_WITNESS);
		REQUIRE(p.error.message == NULL);
		instance = p.instance;
		REQUIRE(instance && instance->kind == PIGEN_SYNTAX_INSTANCE);
		inst_syntax = syntax_id_of(&p.tree, instance);
		conn = inst_node(instance, inst_syntax,
			instance->as.instance.first_connection);
		REQUIRE(conn && conn->kind == PIGEN_SYNTAX_INSTANCE_CONNECTION);
		atom = expr_of(&p.tree, conn->as.instance_connection.expression);
		REQUIRE(atom && atom->kind == PIGEN_SYNTAX_EXPR_NAME);
		w = build_model(&model, &p);
		{
			pigen_rtl_lowering lowering = {0};
			int rc;

			memset(&rtl, 0, sizeof rtl);
			lowering.semantics = &model;
			lowering.rtl = &rtl;
			rc = pigen_instance_lower(&lowering, w.parent, inst_syntax,
				instance, &out);
			REQUIRE(rc == -1);
			REQUIRE(IS_INVALID_ID(out));
		}
		pigen_free_rtl_model(&rtl);
		pigen_free_semantic_model(&model);
		il_free(&p);
	}

	/* The non-atom red: `.din(a + 1)` is a binary operation, not a NAME atom. */
	{
		parsed p;
		pigen_semantic_model model;
		pigen_rtl_model rtl;
		struct model_witness w;
		pigen_rtl_instance_id out = (pigen_rtl_instance_id){PIGEN_INVALID_ID};
		pigen_syntax_id inst_syntax;
		const pigen_syntax_node *instance;
		const pigen_syntax_node *conn;
		const pigen_syntax_expr *atom;

		il_parse(&p, "non-atom.pigen", NON_ATOM_WITNESS);
		REQUIRE(p.error.message == NULL);
		instance = p.instance;
		REQUIRE(instance && instance->kind == PIGEN_SYNTAX_INSTANCE);
		inst_syntax = syntax_id_of(&p.tree, instance);
		conn = inst_node(instance, inst_syntax,
			instance->as.instance.first_connection);
		REQUIRE(conn && conn->kind == PIGEN_SYNTAX_INSTANCE_CONNECTION);
		atom = expr_of(&p.tree, conn->as.instance_connection.expression);
		REQUIRE(atom && atom->kind == PIGEN_SYNTAX_EXPR_BINARY);
		w = build_model(&model, &p);
		{
			pigen_rtl_lowering lowering = {0};
			int rc;

			memset(&rtl, 0, sizeof rtl);
			lowering.semantics = &model;
			lowering.rtl = &rtl;
			rc = pigen_instance_lower(&lowering, w.parent, inst_syntax,
				instance, &out);
			REQUIRE(rc == -1);
			REQUIRE(IS_INVALID_ID(out));
		}
		pigen_free_rtl_model(&rtl);
		pigen_free_semantic_model(&model);
		il_free(&p);
	}
}

int main(int argc, char **argv)
{
	check_init(argc, argv);

	SECTION("lower-valid-args") {
		test_lower_valid_args();
	}

	SECTION("seam-red") {
		test_seam_red();
	}

	SECTION("connection-reds") {
		test_connection_reds();
	}

	return check_finish();
}
