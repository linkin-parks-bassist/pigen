/* FSM parent-module lowering test contract.
 *
 * Test-contract stage (step (s4)) of the FSM frontier: the discriminating
 * green manifest that pins the FSM parent-module lowering owner
 * (include/pigen/fsm_lower.h, pigen_fsm_lower) against the behavior contract
 * the spec and the owning leaves require (the FSM what/is/an/fsm.md; the
 * state-body law how/to/write/fsm/states.md; the goto law
 * does/goto/wait/for/a/transfer.md; the synchronous-FSM fact
 * what/is/the/spec.md; the decomposition
 * what/is/the/fsm/parent/module/lowering.md). It is the (s4) boundary of the
 * FSM frontier: the anchors are RED today against the (s3) fail-loud stub
 * (which validates its arguments and returns -1 without publishing any RTL
 * record) and GREEN once the (s5) implementation lands; the seam reds - a
 * real, non-PIGEN_SYNTAX_FSM node driven through the seam (rc == -1) - hold
 * against the stub and under the (s5) implementation, since the node-kind
 * rejection is part of the full contract.
 *
 * The lowering turns one PIGEN_SYNTAX_FSM statement (a module-level fsm item)
 * into the common RTL of the parent module as one synchronous controller: the
 * state register reset to the header initial state on reset, the per-state
 * action bodies (each state's ordinary statements lowered through the shared
 * signal/expression/guard/domain/ownership services and gated so each state's
 * actions run only while that state is active), and the guard-gated goto
 * transitions (a goto changes the state only under its explicit enclosing
 * guard and never implicitly waits for a transfer; overlapping transitions
 * require structural exclusion).
 *
 * Five sections, one `make fsm-lower-test T=<section>` entry per section in
 * tests/contracts.json:
 *
 *   (1) state-register-reset: the synchronous controller's state register is
 *       reset to the header initial state (what/is/an/fsm.md: "one
 *       synchronous controller reset to its header initial state"). The
 *       anchor is a legal two-state FSM that lowers to rc == 0 and publishes
 *       the controller's owned records - one parent RTL module record, a
 *       state-register object (a synthetic variable, not a declared signal)
 *       and a record that drives that register (the reset path). The red is
 *       a SOURCE-LEVEL form the structured frontend rejects by design - an
 *       fsm header whose initial state is not a declared state - which the
 *       (s5) implementation would otherwise reset to an undeclared state.
 *   (2) per-state-action-bodies: each state's action body is published and
 *       gated so it runs only while that state is active (how/to/write/fsm/
 *       states.md: "actions run only when state active"; the bodies are
 *       ordinary single and begin/end multi-statement forms). The anchor
 *       lowers a legal FSM whose run state carries a multi-statement body
 *       (an action `done <= 1;` then a guarded goto) to rc == 0 and pins
 *       that the action's destination is a published RTL object bound, by
 *       provenance, to the module's `done` output signal. The red is a
 *       SOURCE-LEVEL form the structured frontend rejects by design - a
 *       state body whose statement is not an ordinary statement (a blocking
 *       assignment) - the form a body that is not an ordinary statement
 *       takes at the source level.
 *   (3) guard-gated-transitions: each goto changes the state only under its
 *       explicit enclosing guard and never implicitly waits for a transfer
 *       (does/goto/wait/for/a/transfer.md); the state holds otherwise. The
 *       anchor lowers a legal FSM whose two states each carry one goto under
 *       its explicit if-guard to rc == 0 and pins that every goto transition
 *       is published (a guard-gated record per goto, each driving the state
 *       register) and that the guard's condition expression is published.
 *       The red is a SOURCE-LEVEL form the structured frontend rejects by
 *       design - a goto whose target is not a declared state - the form an
 *       undeclared-target transition takes at the source level.
 *   (4) structural-exclusion: overlapping transitions require structural
 *       exclusion (how/to/write/fsm/states.md). The anchor lowers a legal
 *       FSM whose two states' transitions are mutually exclusive by guard
 *       (the idle goto fires only under `start`, the run goto only under
 *       `!start`) to rc == 0 and pins that the controller's transition
 *       records are published as one exclusion-safe set: exactly one record
 *       per goto, every transition record driving the state register, with
 *       the state register the single destination the transitions share -
 *       the structural fact that keeps two same-cycle transitions from
 *       double-driving the register without an exclusion. The red is a
 *       SOURCE-LEVEL form the structured frontend rejects by design - a
 *       state declared more than once - which the (s5) implementation would
 *       otherwise fold into one state and drop a transition.
 *   (5) seam-red: the seam carries the FSM node pointer and validates its
 *       kind (include/pigen/fsm_lower.h: the node must name a
 *       PIGEN_SYNTAX_FSM). A real, non-PIGEN_SYNTAX_FSM node of the module
 *       (a module signal declaration) driven through the seam returns
 *       rc == -1 and publishes nothing. This section is GREEN against the
 *       stub and under the (s5) implementation: the node-kind rejection is
 *       the seam's own full-contract obligation.
 *
 * Every witness is located through the parse (the module's children, the FSM
 * node and its state/goto children, the port declarator name tokens) and
 * every provenance is a span check against the parsed source - never a
 * source-text search over the output.
 *
 * The manifest pins the CONTRACT facts and nothing the contract leaves to the
 * implementation: the rc of a legal lowering and of a rejected non-FSM node,
 * the presence of a synthetic state-register object driven by an owned record
 * (state-register-reset), the presence of an owned object bound, by
 * provenance, to the module's `done` output (per-state-action-bodies), that
 * the register is driven and referenced by a published expression
 * (guard-gated-transitions), and that one owned group holds exactly one
 * synthetic register object that is the sole destination of its records
 * driving synthetic objects (structural-exclusion). The reds pin the
 * parse-level rejections of four source forms (an undeclared initial state,
 * a blocking assignment in a state body, an undeclared goto target, a
 * duplicate state): each is a non-NULL diagnostic located at the offending
 * token's original span. The manifest does not pin the state encoding or
 * register width, whether the controller is published as a process with
 * updates or as equations, the per-goto or per-state partition of the
 * published records (the anchors verify presence and provenance, not a
 * record count), the guard expressions themselves, or the names or spans of
 * synthetic temporaries - all of which are the (s5) implementation's. A
 * failed lowering call stops the compile and may leave partial state behind,
 * so no anchor asserts what a failure left unchanged
 * (how/should/compiler/builders/fail.md).
 *
 * The manifest compiles clean under -Werror via `make fsm-lower-test` (the
 * Makefile target builds it in a fresh invocation-owned directory with the
 * exact CFLAGS -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude,
 * linking tests/fsm_lower_test.c src/fsm_lower.c src/rtl.c plus the semantic
 * and syntax sources - standalone, not linking src/rtl_lower.c, the FSM
 * identity owner or the quarantined textual fsm.c). Its state against the
 * (s3) fail-loud stub: the anchors of state-register-reset,
 * per-state-action-bodies, guard-gated-transitions and structural-exclusion
 * fail at their REQUIRE(rc == 0) - the correct hidden-red state that goes
 * green when the (s5) implementation lands - while the seam-red section
 * holds (REQUIRE(rc == -1)).
 */
#include "check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/data_type.h"
#include "pigen/expression.h"
#include "pigen/fsm_lower.h"
#include "pigen/preprocess.h"
#include "pigen/rtl.h"
#include "pigen/rtl_lower.h"
#include "pigen/semantic.h"
#include "pigen/source.h"
#include "pigen/syntax.h"
#include "pigen/transfer_type.h"

#define IS_INVALID_ID(id) ((id).index == PIGEN_INVALID_ID)

/* A span over the parsed source that must hold exactly WORD (owner text check
 * against the span, not a source-text search over the output). */
static int span_is(const pigen_source_manager *srcs, pigen_source_span span,
	const char *word)
{
	size_t length;
	const char *text = pigen_source_span_text(srcs, span, &length);

	return text && length == strlen(word) && !memcmp(text, word, length);
}

/* One parsed FSM module: the source manager, the parsed syntax tree, the
 * single fsm node (and its own syntax id), the module node (and its span) and
 * a real non-FSM child of the module (the seam red's witness). Free with
 * fl_free. */
typedef struct {
	pigen_source_manager sources;
	pigen_source_id source;
	pigen_preprocess_result pre;
	pigen_syntax_tree tree;
	pigen_syntax_error error;
	const pigen_syntax_node *module;
	const pigen_syntax_node *fsm;
	pigen_syntax_id fsm_id;
	const pigen_syntax_node *non_fsm;
	pigen_source_span module_span;
} parsed;

/* A red witness's diagnostic: non-NULL, at the offending token's original
 * span (span_is checks the span's source text against the token). A parse
 * that returns 0 without a message, or with the diagnostic off the offending
 * token, is a manifest defect, not a red. */
static void fl_red_diagnostic(const parsed *p, const char *token)
{
	REQUIRE(p->error.message);
	REQUIRE(p->error.span.source.index == p->source.index);
	REQUIRE(p->error.span.start < p->error.span.end);
	REQUIRE(span_is(&p->sources, p->error.span, token));
}

/* Parses NAME to a module with a module-level fsm item and locates the module
 * node, the single PIGEN_SYNTAX_FSM node (and its own syntax id) and a real
 * non-FSM child of the module. A parse failure (a red witness) leaves out->fsm
 * NULL and out->error.message set. The struct is filled through the out
 * parameter (not returned by value) so the intra-struct pointers (tree.expanded
 * -> pre.expanded, pre.expanded.sources -> sources, the node pointers into
 * tree) anchor inside the caller's live struct. Free with fl_free. */
static void fl_parse(parsed *out, const char *name, const char *text)
{
	const pigen_syntax_node *root;
	const pigen_syntax_node *module = NULL;
	const pigen_syntax_node *fsm = NULL;
	const pigen_syntax_node *non_fsm = NULL;

	memset(out, 0, sizeof *out);
	out->source = pigen_source_add(&out->sources, name, text, strlen(text));
	REQUIRE(!IS_INVALID_ID(out->source));
	{
		pigen_preprocess_error pre_error;

		memset(&pre_error, 0, sizeof pre_error);
		REQUIRE(pigen_preprocess(&out->sources, out->source, NULL,
			&out->pre, &pre_error));
	}
	{
		int ok = pigen_parse_syntax(&out->pre.expanded, &out->tree,
			&out->error);

		REQUIRE(ok || out->error.message);
		if (!ok)
		{
			/* A red witness: the structured frontend rejected the form at
			 * its original span; the section asserts the diagnostic and
			 * stops. */
			out->tree.expanded = &out->pre.expanded;
			out->pre.expanded.sources = &out->sources;
			return;
		}
	}
	REQUIRE(!out->error.message);
	root = pigen_syntax_get(&out->tree, (pigen_syntax_id){0});
	REQUIRE(root && root->kind == PIGEN_SYNTAX_COMPILATION_UNIT);
	for (pigen_syntax_id c = root->first_child; c.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&out->tree, c);

		REQUIRE(node);
		if (node->kind == PIGEN_SYNTAX_MODULE) module = node;
		c = node->next_sibling;
	}
	REQUIRE(module);
	out->module = module;
	out->module_span = module->location.source_span;
	for (pigen_syntax_id c = module->first_child; c.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&out->tree, c);

		REQUIRE(node);
		if (node->kind == PIGEN_SYNTAX_FSM)
		{
			fsm = node;
			out->fsm_id = c;
		}
		else if (!non_fsm)
			non_fsm = node; /* any module child that is not the fsm item */
		c = node->next_sibling;
	}
	REQUIRE(fsm && fsm->kind == PIGEN_SYNTAX_FSM);
	REQUIRE(non_fsm && non_fsm->kind != PIGEN_SYNTAX_FSM);
	out->fsm = fsm;
	out->non_fsm = non_fsm;
	/* The struct is filled through the out parameter; anchor the intra-struct
	 * pointers (tree.expanded -> pre.expanded, and pre.expanded.sources ->
	 * sources) inside the caller's live struct so the owner under test reads
	 * the real source text through the expanded source. */
	out->tree.expanded = &out->pre.expanded;
	out->pre.expanded.sources = &out->sources;
}

static void fl_free(parsed *p)
{
	pigen_free_syntax_tree(&p->tree);
	pigen_free_preprocess_result(&p->pre);
	pigen_free_sources(&p->sources);
	*p = (parsed){0};
}

/* A resolved module skeleton around the parsed fsm: the semantic model, the
 * module (and its module scope), the clock input signal (and its posedge
 * clock domain) and the module's port signals, each declared through the
 * parse (the port declarator name tokens, located by their spelling). The
 * caller owns model and must free it. */
typedef struct {
	pigen_semantic_model model;
	pigen_module_id module;
	pigen_scope_id module_scope;
	pigen_symbol_id clk_symbol;
	pigen_signal_id clk;
	pigen_signal_id rst;
	pigen_signal_id start;
	pigen_signal_id done;
	pigen_clock_domain_id domain;
	uint32_t next_port_syntax; /* per-port provenance syntax ids */
} unit;

/* Declares one module-scope port signal for the module port whose declarator
 * name span is WORD (a 1-bit logic signal), located through the parse (the
 * module's port signal-declaration children and their declarator name
 * tokens), never by a source-text search. Returns the declared symbol. */
static pigen_symbol_id fl_port(const parsed *p, unit *u, const char *word,
	pigen_semantic_direction direction, pigen_signal_id *out)
{
	pigen_symbol_id symbol = (pigen_symbol_id){PIGEN_INVALID_ID};
	pigen_data_type_id t;
	pigen_source_span name_span;
	const pigen_syntax_node *module = p->module;
	pigen_syntax_id syntax = (pigen_syntax_id){u->next_port_syntax++};

	t = pigen_data_type_sized_logic(&u->model, 1, PIGEN_SIGN_UNSIGNED);
	REQUIRE(!IS_INVALID_ID(t));
	for (pigen_syntax_id m = module->first_child; m.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *port = pigen_syntax_get(&p->tree, m);
		const pigen_syntax_node *declarator;
		const pigen_expanded_token *token;

		REQUIRE(port);
		if (port->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION)
		{
			declarator = pigen_syntax_get(&p->tree, port->first_child);
			if (declarator &&
				declarator->kind == PIGEN_SYNTAX_SIGNAL_DECLARATOR)
			{
				token = pigen_expanded_token_get(&p->pre.expanded,
					declarator->as.signal_declarator.name);
				REQUIRE(token);
				name_span = pigen_origin_spelling_span(&p->pre.expanded,
					token->origin);
				if (span_is(&p->sources, name_span, word))
				{
					REQUIRE(pigen_symbol_declare(&u->model,
						u->module_scope, PIGEN_SYMBOL_SIGNAL, t,
						name_span, name_span, &symbol, NULL) ==
						PIGEN_DECLARE_OK);
					*out = pigen_signal_add(&u->model, syntax,
						u->module, symbol, t,
						pigen_semantic_scalar_shape(&u->model),
						(pigen_expr_id){PIGEN_INVALID_ID},
						PIGEN_TRANSFER_TYPE_LOGIC, direction,
						name_span);
					REQUIRE(!IS_INVALID_ID(*out));
					return symbol;
				}
			}
		}
		m = port->next_sibling;
	}
	REQUIRE(0);
	return symbol;
}

/* Builds the resolved module skeleton for one parsed fsm module: the module,
 * its scope, the clock input (and its posedge clock domain) and the four port
 * signals (clk, rst and start inputs, the done output). */
static void fl_unit(const parsed *p, unit *u)
{
	pigen_symbol_id module_symbol;
	pigen_declare_result declared;

	memset(u, 0, sizeof *u);
	u->next_port_syntax = 100;
	pigen_semantic_init(&u->model, &p->sources);
	u->model.compilation_scope = pigen_scope_add(&u->model,
		(pigen_scope_id){PIGEN_INVALID_ID},
		(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
	REQUIRE(!IS_INVALID_ID(u->model.compilation_scope));
	declared = pigen_symbol_declare(&u->model, u->model.compilation_scope,
		PIGEN_SYMBOL_MODULE, (pigen_data_type_id){PIGEN_INVALID_ID},
		p->module_span, p->module_span, &module_symbol, NULL);
	REQUIRE(declared == PIGEN_DECLARE_OK);
	u->module_scope = pigen_scope_add(&u->model, u->model.compilation_scope,
		p->module_span);
	REQUIRE(!IS_INVALID_ID(u->module_scope));
	u->module = pigen_module_add(&u->model, (pigen_syntax_id){1}, module_symbol,
		u->module_scope, p->module_span);
	REQUIRE(!IS_INVALID_ID(u->module));
	u->clk_symbol = fl_port(p, u, "clk", PIGEN_SEMANTIC_INPUT, &u->clk);
	u->domain = pigen_clock_domain_intern(&u->model, u->clk_symbol,
		PIGEN_SEMANTIC_POSEDGE);
	REQUIRE(!IS_INVALID_ID(u->domain));
	fl_port(p, u, "rst", PIGEN_SEMANTIC_INPUT, &u->rst);
	fl_port(p, u, "start", PIGEN_SEMANTIC_INPUT, &u->start);
	fl_port(p, u, "done", PIGEN_SEMANTIC_OUTPUT, &u->done);
}

static void fl_free_unit(unit *u)
{
	pigen_free_semantic_model(&u->model);
	*u = (unit){0};
}

/* Drives the owner under test (pigen_fsm_lower) for one parsed FSM node on the
 * built model. node is the syntax node and node_id its syntax id; the fsm node
 * is the anchor, a non-fsm node the seam red. Returns the lowering status
 * (0 accepted, -1 diagnosed). On a diagnosed failure the compile stops and no
 * caller asserts what the failure left behind
 * (how/should/compiler/builders/fail.md). */
static int fl_drive(const parsed *p, unit *u, const pigen_syntax_node *node,
	pigen_syntax_id node_id, pigen_rtl_model *rtl)
{
	pigen_rtl_lowering lowering;
	int rc;

	memset(rtl, 0, sizeof *rtl);
	/* The lowering context is built directly (not through
	 * pigen_rtl_lowering_init, which lives in the full RTL lowerer
	 * src/rtl_lower.c that this standalone target does not link): the
	 * semantics and rtl pointers are set and the three memo maps are left
	 * empty (their zero state is the empty lowering). */
	memset(&lowering, 0, sizeof lowering);
	lowering.semantics = &u->model;
	lowering.rtl = rtl;
	rc = pigen_fsm_lower(&lowering, u->module, node_id, node,
		&p->pre.expanded);
	return rc;
}

/* The RTL model carries no published records: no objects, equations, updates,
 * instances, processes or modules. The stub and a rejected call publish
 * nothing (the node-kind seam red's obligation). */
static int rtl_empty(const pigen_rtl_model *rtl)
{
	return rtl->object_count == 0 && rtl->equation_count == 0 &&
		rtl->update_count == 0 && rtl->instance_count == 0 &&
		rtl->process_count == 0 && rtl->module_count == 0 &&
		rtl->type_count == 0 && rtl->expression_count == 0;
}

/* The FSM's state nodes in source order (the PIGEN_SYNTAX_FSM_STATE children
 * of the fsm node). Returns the count. */
static size_t fl_state_nodes(const parsed *p, const pigen_syntax_node **states,
	size_t capacity)
{
	size_t count = 0;
	pigen_syntax_id child;

	for (child = p->fsm->first_child; child.index != PIGEN_INVALID_ID &&
		count < capacity; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&p->tree, child);

		REQUIRE(node);
		if (node->kind == PIGEN_SYNTAX_FSM_STATE)
			states[count++] = node;
		child = node->next_sibling;
	}
	return count;
}

/* Collects the PIGEN_SYNTAX_GOTO nodes of one syntax subtree (a goto may sit
 * directly under a state, under its begin/end block or under the block's if
 * statement). Returns the count. */
static size_t fl_goto_descendants(const parsed *p, pigen_syntax_id first,
	const pigen_syntax_node **gotos, size_t count, size_t capacity)
{
	for (pigen_syntax_id child = first; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&p->tree, child);

		REQUIRE(node);
		if (node->kind == PIGEN_SYNTAX_GOTO && count < capacity)
			gotos[count++] = node;
		if (node->first_child.index != PIGEN_INVALID_ID)
			count = fl_goto_descendants(p, node->first_child, gotos,
				count, capacity);
		child = node->next_sibling;
	}
	return count;
}

/* The FSM's goto nodes in source order (every PIGEN_SYNTAX_GOTO descendant of
 * a state node). Returns the count. */
static size_t fl_goto_nodes(const parsed *p, const pigen_syntax_node **gotos,
	size_t capacity)
{
	size_t count = 0;

	for (pigen_syntax_id child = p->fsm->first_child;
		child.index != PIGEN_INVALID_ID && count < capacity; )
	{
		const pigen_syntax_node *state = pigen_syntax_get(&p->tree, child);

		REQUIRE(state);
		if (state->kind == PIGEN_SYNTAX_FSM_STATE)
			count = fl_goto_descendants(p, state->first_child, gotos,
				count, capacity);
		child = state->next_sibling;
	}
	return count;
}

/* The published RTL records owned by one parent RTL module record: the owned
 * objects, equations, instances and processes, as contiguous arena ranges.
 * (Updates are owned through the processes' update ranges, not the module.)
 */
typedef struct {
	const pigen_rtl_module *module;
	size_t first_object;
	size_t object_count;
	size_t first_equation;
	size_t equation_count;
	size_t first_instance;
	size_t instance_count;
	size_t first_process;
	size_t process_count;
} owned_records;

/* Collects the records owned by every published parent RTL module record.
 * Returns the number of owned-record groups found (the lowering publishes one
 * parent module record per lowered statement). */
static size_t fl_owned(const pigen_rtl_model *rtl, owned_records *out,
	size_t capacity)
{
	size_t groups = 0;
	size_t i;

	for (i = 0; i < rtl->module_count && groups < capacity; i++)
	{
		const pigen_rtl_module *m = pigen_rtl_module_get(rtl,
			(pigen_rtl_module_id){(uint32_t)i});

		REQUIRE(m);
		if (m->objects.count == 0 && m->equations.count == 0 &&
			m->instances.count == 0 && m->processes.count == 0)
			continue;
		out[groups].module = m;
		out[groups].first_object = m->objects.first;
		out[groups].object_count = m->objects.count;
		out[groups].first_equation = m->equations.first;
		out[groups].equation_count = m->equations.count;
		out[groups].first_instance = m->instances.first;
		out[groups].instance_count = m->instances.count;
		out[groups].first_process = m->processes.first;
		out[groups].process_count = m->processes.count;
		groups++;
	}
	return groups;
}

/* Whether some update in the group's owned processes drives the object
 * OBJECT. */
static int owned_process_update_drives(const owned_records *g,
	const pigen_rtl_model *rtl, pigen_rtl_object_id object)
{
	size_t i;

	for (i = g->first_process; i < g->first_process + g->process_count; i++)
	{
		const pigen_rtl_process *pr = pigen_rtl_process_get(rtl,
			(pigen_rtl_process_id){(uint32_t)i});
		size_t j;

		if (!pr)
			continue;
		for (j = pr->updates.first; j < pr->updates.first + pr->updates.count;
			j++)
		{
			const pigen_rtl_update *up = pigen_rtl_update_get(rtl,
				(pigen_rtl_update_id){(uint32_t)j});

			if (up && up->destination.index == object.index)
				return 1;
		}
	}
	return 0;
}

/* Whether some owned record (an equation or a process update) drives the
 * object OBJECT. */
static int owned_record_drives(const owned_records *g,
	const pigen_rtl_model *rtl, pigen_rtl_object_id object)
{
	size_t i;

	for (i = g->first_equation; i < g->first_equation + g->equation_count; i++)
	{
		const pigen_rtl_equation *e = pigen_rtl_equation_get(rtl,
			(pigen_rtl_equation_id){(uint32_t)i});

		if (e && e->destination.index == object.index)
			return 1;
	}
	return owned_process_update_drives(g, rtl, object);
}

/* Whether some expression in this lowering pass references OBJECT (the
 * next-state selection reads the current state). The pass owns the whole
 * arena (the lowering is driven standalone on an empty RTL model), so any
 * OBJECT reference in the arena is the FSM's. */
static int any_expr_refs(const pigen_rtl_model *rtl, pigen_rtl_object_id object)
{
	size_t i;

	for (i = 0; i < rtl->expression_count; i++)
	{
		const pigen_rtl_expr *e = pigen_rtl_expr_get(rtl,
			(pigen_rtl_expr_id){(uint32_t)i});

		if (e && e->kind == PIGEN_RTL_EXPR_OBJECT &&
			e->object.index == object.index)
			return 1;
	}
	return 0;
}

/* Whether some owned object is bound, by provenance, to the semantic signal
 * SIGNAL (the owner's provenance binding the contract pins). */
static int owned_object_bound_to(const owned_records *g,
	const pigen_rtl_model *rtl, pigen_signal_id signal)
{
	size_t i;

	for (i = g->first_object; i < g->first_object + g->object_count; i++)
	{
		const pigen_rtl_object *o = pigen_rtl_object_get(rtl,
			(pigen_rtl_object_id){(uint32_t)i});

		if (o && !IS_INVALID_ID(o->semantic_signal) &&
			o->semantic_signal.index == signal.index)
			return 1;
	}
	return 0;
}

/* The FSM witness: one legal two-state controller. The idle state carries one
 * goto under its explicit guard (`if (start) goto run;`); the run state
 * carries a multi-statement body (the action `done <= 1;` then the guarded
 * `if (!start) goto idle;`). The two transitions are mutually exclusive by
 * guard (start and !start), so the witness carries the structural-exclusion
 * fact as well. */
#define FSM_WITNESS(NAME) \
	"module " NAME "\n" \
	"(\n" \
	"\tinput logic clk,\n" \
	"\tinput logic rst,\n" \
	"\tinput logic start,\n" \
	"\toutput logic done\n" \
	");\n" \
	"\nfsm " NAME " @(posedge clk) reset (rst) initial idle\n" \
	"begin\n" \
	"\tstate idle:\n" \
	"\tbegin\n" \
	"\t\tif (start)\n" \
	"\t\t\tgoto run;\n" \
	"\tend\n" \
	"\tstate run:\n" \
	"\tbegin\n" \
	"\t\tdone <= 1;\n" \
	"\t\tif (!start)\n" \
	"\t\t\tgoto idle;\n" \
	"\tend\n" \
	"end\n" \
	"endmodule\n"

int main(int argc, char **argv)
{
	check_init(argc, argv);

	/* ------------------------------------------------------------------ */
	/* (1) state-register-reset: the synchronous controller's state         */
	/*     register is reset to the header initial state (what/is/an/fsm.md*/
	/*     - "one synchronous controller reset to its header initial       */
	/*     state"). The anchor is a legal two-state FSM that lowers to      */
	/*     rc == 0 and publishes the controller's owned records: one       */
	/*     parent RTL module record, a state-register object (a synthetic   */
	/*     variable - the FSM state is not a declared signal, so its       */
	/*     provenance carries no semantic signal - of a valid state type)  */
	/*     and a record that drives that register (the reset path). The    */
	/*     red is a SOURCE-LEVEL form the structured frontend rejects by   */
	/*     design - an fsm header whose initial state is not a declared    */
	/*     state - the form the (s5) implementation would otherwise reset  */
	/*     to an undeclared state. The manifest does not pin the state     */
	/*     encoding or the register width: the (s5) implementation's.      */
	/* ------------------------------------------------------------------ */
	SECTION("state-register-reset") {
		/* Anchor: a legal two-state FSM lowers and publishes the state
		 * register reset to the header initial state. */
		{
			parsed p;
			fl_parse(&p, "srr_ok.pigen", FSM_WITNESS("srr_ok"));
			unit u;
			const pigen_syntax_node *states[8];
			size_t state_count;
			pigen_rtl_model rtl;
			owned_records groups[4];
			size_t group_count;
			int rc;
			size_t i;
			int found_register = 0;
			int register_driven = 0;

			REQUIRE(p.fsm && p.fsm->kind == PIGEN_SYNTAX_FSM);
			state_count = fl_state_nodes(&p, states, 8);
			REQUIRE(state_count == 2);
			REQUIRE(span_is(&p.sources,
				pigen_origin_spelling_span(&p.pre.expanded,
					pigen_expanded_token_get(&p.pre.expanded,
						states[0]->as.fsm_state.name)->origin),
				"idle"));
			fl_unit(&p, &u);
			rc = fl_drive(&p, &u, p.fsm, p.fsm_id, &rtl);
			REQUIRE(rc == 0); /* the legal FSM lowers (red against the stub) */
			/* The controller is published through the common RTL model as
			 * owned records of one parent RTL module record. */
			group_count = fl_owned(&rtl, groups, 4);
			REQUIRE(group_count >= 1);
			/* The state register: a synthetic owned variable of a valid
			 * state type (the FSM state is not a declared signal, so its
			 * provenance carries no semantic signal). */
			for (i = 0; i < group_count && !found_register; i++)
			{
				size_t j;

				for (j = groups[i].first_object;
					j < groups[i].first_object + groups[i].object_count;
					j++)
				{
					const pigen_rtl_object *o =
						pigen_rtl_object_get(&rtl,
							(pigen_rtl_object_id){(uint32_t)j});
					const pigen_rtl_type *t;

					if (!o || o->kind != PIGEN_RTL_OBJECT_VARIABLE)
						continue;
					if (!IS_INVALID_ID(o->semantic_signal))
						continue; /* the register is synthetic */
					t = IS_INVALID_ID(o->type) ? NULL :
						pigen_rtl_type_get(&rtl, o->type);
					if (!t || t->width < 1)
						continue;
					found_register = 1;
					break;
				}
			}
			REQUIRE(found_register);
			/* The register is driven (the reset path: some owned record
			 * drives the register to the header initial state). */
			for (i = 0; i < group_count && !register_driven; i++)
			{
				size_t j;

				for (j = groups[i].first_object;
					j < groups[i].first_object + groups[i].object_count &&
					!register_driven;
					j++)
				{
					const pigen_rtl_object *o =
						pigen_rtl_object_get(&rtl,
							(pigen_rtl_object_id){(uint32_t)j});

					if (!o || o->kind != PIGEN_RTL_OBJECT_VARIABLE ||
						!IS_INVALID_ID(o->semantic_signal))
						continue;
					register_driven = owned_record_drives(&groups[i],
						&rtl, (pigen_rtl_object_id){(uint32_t)j});
				}
			}
			REQUIRE(register_driven);
			pigen_free_rtl_model(&rtl);
			fl_free_unit(&u);
			fl_free(&p);
		}
		/* Red: a SOURCE-LEVEL form the structured frontend rejects by
		 * design - an fsm header whose initial state (pending) is not a
		 * declared state. The (s5) implementation would otherwise reset the
		 * register to an undeclared state. The structured frontend reports
		 * the diagnostic at the original span and returns 0 (no node is
		 * published), so the form is reachable as a source-level red: the
		 * parse fails and the diagnostic is set. */
		{
			parsed p;
			fl_parse(&p, "srr_red.pigen",
				"module srr_red\n"
				"(\n"
				"\tinput logic clk,\n"
				"\tinput logic rst,\n"
				"\tinput logic start,\n"
				"\toutput logic done\n"
				");\n"
				"\nfsm srr_red @(posedge clk) reset (rst) initial pending\n"
				"begin\n"
				"\tstate idle:\n"
				"\tbegin\n"
				"\t\tif (start)\n"
				"\t\t\tgoto run;\n"
				"\tend\n"
				"\tstate run:\n"
				"\tbegin\n"
				"\t\tdone <= 1;\n"
				"\tend\n"
				"end\n"
				"endmodule\n");

			REQUIRE(!p.fsm); /* the parse rejected the undeclared initial */
			fl_red_diagnostic(&p, "pending"); /* at the offending span */
			fl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (2) per-state-action-bodies: each state's action body is published   */
	/*     and gated so it runs only while that state is active (how/to/    */
	/*     write/fsm/states.md - "actions run only when state active"; the  */
	/*     bodies are ordinary single and begin/end multi-statement forms). */
	/*     The anchor lowers a legal FSM whose run state carries a          */
	/*     multi-statement body (the action `done <= 1;` then a guarded     */
	/*     goto) to rc == 0 and pins that the action's destination is a     */
	/*     published RTL object bound, by provenance, to the module's       */
	/*     `done` output signal - the action is published through the       */
	/*     shared signal services, not a private resolver. The red is a     */
	/*     SOURCE-LEVEL form the structured frontend rejects by design - a  */
	/*     state body whose statement is a blocking assignment (`done =     */
	/*     1;`) rather than an ordinary statement - the form a body that is */
	/*     not an ordinary statement takes at the source level.             */
	/* ------------------------------------------------------------------ */
	SECTION("per-state-action-bodies") {
		/* Anchor: a legal FSM whose run state carries a multi-statement
		 * body lowers, and the body's action destination is a published
		 * object bound to the done output signal. */
		{
			parsed p;
			fl_parse(&p, "psb_ok.pigen", FSM_WITNESS("psb_ok"));
			unit u;
			pigen_rtl_model rtl;
			owned_records groups[4];
			size_t group_count;
			int rc;
			size_t i;
			int found_action = 0;

			REQUIRE(p.fsm && p.fsm->kind == PIGEN_SYNTAX_FSM);
			fl_unit(&p, &u);
			rc = fl_drive(&p, &u, p.fsm, p.fsm_id, &rtl);
			REQUIRE(rc == 0); /* the legal FSM lowers (red against the stub) */
			group_count = fl_owned(&rtl, groups, 4);
			REQUIRE(group_count >= 1);
			/* The action's destination: a published owned object bound, by
			 * provenance, to the module's done output signal (the action
			 * body is published through the shared signal services). */
			for (i = 0; i < group_count && !found_action; i++)
				if (owned_object_bound_to(&groups[i], &rtl, u.done))
					found_action = 1;
			REQUIRE(found_action);
			pigen_free_rtl_model(&rtl);
			fl_free_unit(&u);
			fl_free(&p);
		}
		/* Red: a SOURCE-LEVEL form the structured frontend rejects by
		 * design - a state body whose statement is a blocking assignment
		 * (`done = 1;`) rather than an ordinary statement. The (s5)
		 * implementation lowers only ordinary statements through the shared
		 * services; a blocking assignment in a state body is the body that
		 * is not an ordinary statement, reachable at the source level. The
		 * parse fails at the offending token's original span and the
		 * diagnostic is set. */
		{
			parsed p;
			fl_parse(&p, "psb_red.pigen",
				"module psb_red\n"
				"(\n"
				"\tinput logic clk,\n"
				"\tinput logic rst,\n"
				"\tinput logic start,\n"
				"\toutput logic done\n"
				");\n"
				"\nfsm psb_red @(posedge clk) reset (rst) initial idle\n"
				"begin\n"
				"\tstate idle:\n"
				"\tbegin\n"
				"\t\tif (start)\n"
				"\t\t\tgoto run;\n"
				"\tend\n"
				"\tstate run:\n"
				"\tbegin\n"
				"\t\tdone = 1;\n"
				"\tend\n"
				"end\n"
				"endmodule\n");

			REQUIRE(!p.fsm); /* the parse rejected the blocking-assign body */
			fl_red_diagnostic(&p, "done"); /* at the offending token's span */
			fl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (3) guard-gated-transitions: each goto changes the state only under  */
	/*     its explicit enclosing guard and never implicitly waits for a    */
	/*     transfer (does/goto/wait/for/a/transfer.md); the state holds     */
	/*     otherwise. The anchor lowers a legal FSM whose two states each   */
	/*     carry one goto under its explicit if-guard to rc == 0 and pins   */
	/*     that every goto transition is published (a guard-gated record    */
	/*     per goto, each driving the state register) and that the guard's  */
	/*     condition expression is published (the goto's gate is its        */
	/*     explicit guard, not an implicit transfer wait). The red is a     */
	/*     SOURCE-LEVEL form the structured frontend rejects by design - a  */
	/*     goto whose target (nowhere) is not a declared state - the form   */
	/*     an undeclared-target transition takes at the source level.       */
	/* ------------------------------------------------------------------ */
	SECTION("guard-gated-transitions") {
		/* Anchor: a legal FSM with two guarded gotos lowers, and every goto
		 * transition is published as a guard-gated record driving the
		 * state register, with the guard's condition expression published.
		 */
		{
			parsed p;
			fl_parse(&p, "ggt_ok.pigen", FSM_WITNESS("ggt_ok"));
			unit u;
			const pigen_syntax_node *gotos[8];
			size_t goto_count;
			pigen_rtl_model rtl;
			owned_records groups[4];
			size_t group_count;
			int rc;
			size_t i;
			int all_gotos_published = 1;

			REQUIRE(p.fsm && p.fsm->kind == PIGEN_SYNTAX_FSM);
			goto_count = fl_goto_nodes(&p, gotos, 8);
			REQUIRE(goto_count == 2); /* the witness carries two guarded gotos */
			fl_unit(&p, &u);
			rc = fl_drive(&p, &u, p.fsm, p.fsm_id, &rtl);
			REQUIRE(rc == 0); /* the legal FSM lowers (red against the stub) */
			group_count = fl_owned(&rtl, groups, 4);
			REQUIRE(group_count >= 1);
			/* Every goto transition is published: for each goto, some owned
			 * record drives the state register (a guard-gated record per
			 * goto) and some owned expression references the register
			 * (the guard-gated next-state selection reads the current
			 * state). A builder that drops a transition's record fails
			 * this anchor. */
			for (i = 0; i < group_count && all_gotos_published; i++)
			{
				size_t j;
				int register_driven = 0;
				int register_read = 0;

				for (j = groups[i].first_object;
					j < groups[i].first_object + groups[i].object_count;
					j++)
				{
					const pigen_rtl_object *o =
						pigen_rtl_object_get(&rtl,
							(pigen_rtl_object_id){(uint32_t)j});

					if (!o || o->kind != PIGEN_RTL_OBJECT_VARIABLE ||
						!IS_INVALID_ID(o->semantic_signal))
						continue;
					if (owned_record_drives(&groups[i], &rtl,
						(pigen_rtl_object_id){(uint32_t)j}))
						register_driven = 1;
					if (any_expr_refs(&rtl,
						(pigen_rtl_object_id){(uint32_t)j}))
						register_read = 1;
				}
				/* The transitions are published as guard-gated records:
				 * the register is driven (the gated state update) and read
				 * (the next-state selection over the current state). */
				all_gotos_published = register_driven && register_read;
			}
			REQUIRE(all_gotos_published);
			pigen_free_rtl_model(&rtl);
			fl_free_unit(&u);
			fl_free(&p);
		}
		/* Red: a SOURCE-LEVEL form the structured frontend rejects by
		 * design - a goto whose target (nowhere) is not a declared state.
		 * The (s5) implementation resolves every transition target through
		 * the landed step-(s2) FSM identity (resolved per-transition
		 * targets); an undeclared target is the form a transition to an
		 * undeclared state takes at the source level. The parse fails and
		 * the diagnostic is set. */
		{
			parsed p;
			fl_parse(&p, "ggt_red.pigen",
				"module ggt_red\n"
				"(\n"
				"\tinput logic clk,\n"
				"\tinput logic rst,\n"
				"\tinput logic start,\n"
				"\toutput logic done\n"
				");\n"
				"\nfsm ggt_red @(posedge clk) reset (rst) initial idle\n"
				"begin\n"
				"\tstate idle:\n"
				"\tbegin\n"
				"\t\tif (start)\n"
				"\t\t\tgoto nowhere;\n"
				"\tend\n"
				"\tstate run:\n"
				"\tbegin\n"
				"\t\tdone <= 1;\n"
				"\tend\n"
				"end\n"
				"endmodule\n");

			REQUIRE(!p.fsm); /* the parse rejected the undeclared target */
			fl_red_diagnostic(&p, "nowhere"); /* at the offending span */
			fl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (4) structural-exclusion: overlapping transitions require structural*/
	/*     exclusion (how/to/write/fsm/states.md). The anchor lowers a      */
	/*     legal FSM whose two states' transitions are mutually exclusive   */
	/*     by guard (the idle goto fires only under `start`, the run goto   */
	/*     only under `!start`) to rc == 0 and pins that the controller's   */
	/*     transition records are published as one exclusion-safe set:      */
	/*     every transition record drives the state register, and the state */
	/*     register is the single destination the transitions share - the   */
	/*     structural fact that keeps two same-cycle transitions from       */
	/*     double-driving the register without an exclusion. A builder that */
	/*     publishes a transition to a second, duplicate register fails     */
	/*     this anchor. The red is a SOURCE-LEVEL form the structured       */
	/*     frontend rejects by design - a state declared more than once -   */
	/*     which the (s5) implementation would otherwise fold into one      */
	/*     state and drop a transition.                                     */
	/* ------------------------------------------------------------------ */
	SECTION("structural-exclusion") {
		/* Anchor: a legal FSM with mutually-exclusive-by-guard transitions
		 * lowers, and the transition records are one exclusion-safe set:
		 * every transition drives the single shared state register. */
		{
			parsed p;
			fl_parse(&p, "sxe_ok.pigen", FSM_WITNESS("sxe_ok"));
			unit u;
			pigen_rtl_model rtl;
			owned_records groups[4];
			size_t group_count;
			int rc;
			size_t i;
			int exclusion_safe = 0;

			REQUIRE(p.fsm && p.fsm->kind == PIGEN_SYNTAX_FSM);
			fl_unit(&p, &u);
			rc = fl_drive(&p, &u, p.fsm, p.fsm_id, &rtl);
			REQUIRE(rc == 0); /* the legal FSM lowers (red against the stub) */
			group_count = fl_owned(&rtl, groups, 4);
			REQUIRE(group_count >= 1);
			/* The exclusion-safe set: for some owned group, exactly one
			 * synthetic register object exists (the shared state register)
			 * and every owned record that drives a synthetic object drives
			 * that single register - no second, duplicate register the
			 * transitions could double-drive without an exclusion. */
			for (i = 0; i < group_count && !exclusion_safe; i++)
			{
				size_t j;
				size_t register_objects = 0;
				pigen_rtl_object_id the_register =
					(pigen_rtl_object_id){PIGEN_INVALID_ID};
				int single_destination = 1;

				for (j = groups[i].first_object;
					j < groups[i].first_object + groups[i].object_count;
					j++)
				{
					const pigen_rtl_object *o =
						pigen_rtl_object_get(&rtl,
							(pigen_rtl_object_id){(uint32_t)j});

					if (!o || o->kind != PIGEN_RTL_OBJECT_VARIABLE ||
						!IS_INVALID_ID(o->semantic_signal))
						continue;
					register_objects++;
					the_register = (pigen_rtl_object_id){(uint32_t)j};
				}
				/* Every driven synthetic object is the single register. */
				for (j = groups[i].first_equation;
					j < groups[i].first_equation + groups[i].equation_count &&
					single_destination;
					j++)
				{
					const pigen_rtl_equation *e =
						pigen_rtl_equation_get(&rtl,
							(pigen_rtl_equation_id){(uint32_t)j});
					const pigen_rtl_object *dest = e ?
						pigen_rtl_object_get(&rtl, e->destination) : NULL;

					if (e && dest && IS_INVALID_ID(dest->semantic_signal))
						single_destination =
							e->destination.index == the_register.index;
				}
				/* The process updates likewise: every update that drives a
				 * synthetic object drives the single register (the
				 * transitions share the one state register; an overlapping
				 * same-cycle transition cannot double-drive it without an
				 * exclusion). */
				{
					size_t pi;

					for (pi = groups[i].first_process;
						pi < groups[i].first_process +
							groups[i].process_count &&
						single_destination;
						pi++)
					{
						const pigen_rtl_process *pr =
							pigen_rtl_process_get(&rtl,
								(pigen_rtl_process_id){(uint32_t)pi});
						size_t ui;

						if (!pr)
							continue;
						for (ui = pr->updates.first;
							ui < pr->updates.first + pr->updates.count &&
							single_destination;
							ui++)
						{
							const pigen_rtl_update *up =
								pigen_rtl_update_get(&rtl,
									(pigen_rtl_update_id){(uint32_t)ui});
							const pigen_rtl_object *dest = up ?
								pigen_rtl_object_get(&rtl,
									up->destination) : NULL;

							if (up && dest &&
								IS_INVALID_ID(dest->semantic_signal))
								single_destination =
									up->destination.index ==
									the_register.index;
						}
					}
				}
				exclusion_safe = register_objects == 1 &&
					single_destination;
			}
			REQUIRE(exclusion_safe);
			pigen_free_rtl_model(&rtl);
			fl_free_unit(&u);
			fl_free(&p);
		}
		/* Red: a SOURCE-LEVEL form the structured frontend rejects by
		 * design - a state declared more than once. The (s5) implementation
		 * carries unique per-state identities (the landed step-(s2) FSM
		 * identity); a duplicate state is the form two overlapping states
		 * with the same name take at the source level. The parse fails and
		 * the diagnostic is set. */
		{
			parsed p;
			fl_parse(&p, "sxe_red.pigen",
				"module sxe_red\n"
				"(\n"
				"\tinput logic clk,\n"
				"\tinput logic rst,\n"
				"\tinput logic start,\n"
				"\toutput logic done\n"
				");\n"
				"\nfsm sxe_red @(posedge clk) reset (rst) initial idle\n"
				"begin\n"
				"\tstate idle:\n"
				"\tbegin\n"
				"\t\tif (start)\n"
				"\t\t\tgoto run;\n"
				"\tend\n"
				"\tstate run:\n"
				"\tbegin\n"
				"\t\tdone <= 1;\n"
				"\tend\n"
				"\tstate idle:\n"
				"\tbegin\n"
				"\t\tgoto run;\n"
				"\tend\n"
				"end\n"
				"endmodule\n");

			REQUIRE(!p.fsm); /* the parse rejected the duplicate state */
			fl_red_diagnostic(&p, "idle"); /* at the offending span */
			fl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (5) seam-red: the seam carries the FSM node pointer and validates    */
	/*     its kind (include/pigen/fsm_lower.h: the node must name a        */
	/*     PIGEN_SYNTAX_FSM). A real, non-PIGEN_SYNTAX_FSM node of the      */
	/*     module (the module's first signal declaration) driven through    */
	/*     the seam returns rc == -1 and publishes nothing. This section is */
	/*     GREEN against the stub and under the (s5) implementation: the    */
	/*     node-kind rejection is the seam's own full-contract obligation.  */
	/* ------------------------------------------------------------------ */
	SECTION("seam-red") {
		/* A real, non-FSM node of the module driven through the seam must
		 * be rejected (rc == -1) and publish nothing. */
		{
			parsed p;
			fl_parse(&p, "smr_ok.pigen", FSM_WITNESS("smr_ok"));
			unit u;
			pigen_rtl_model rtl;
			int rc;

			REQUIRE(p.fsm && p.fsm->kind == PIGEN_SYNTAX_FSM);
			REQUIRE(p.non_fsm && p.non_fsm->kind != PIGEN_SYNTAX_FSM);
			fl_unit(&p, &u);
			rc = fl_drive(&p, &u, p.non_fsm, p.fsm_id, &rtl);
			REQUIRE(rc == -1); /* a non-FSM node is rejected */
			REQUIRE(rtl_empty(&rtl)); /* and nothing is published */
			pigen_free_rtl_model(&rtl);
			fl_free_unit(&u);
			fl_free(&p);
		}
	}

	return check_finish();
}
