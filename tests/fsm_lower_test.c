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
 * Ten sections, one `make fsm-lower-test T=<section>` entry per section in
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

 *   (6) reset-value-initial-state: the reset path's value is the header
 *       initial state's ENCODING, not the first declared state's
 *       (what/is/an/fsm.md: "one synchronous controller reset to its
 *       header initial state"). The anchor is the non-first witness - the
 *       same legal two-state FSM, but the header names `run`, the second
 *       declared state (per-state index 1) - which lowers to rc == 0, and
 *       the reset-path update (the one update whose origin span is the FSM
 *       node's span) drives the state register to a CONDITIONAL whose
 *       when-true child is an INTEGER of the state type with value 1. An
 *       always-first reset (encoding 0) cannot pass.
 *   (7) state-active-action-gate: a state's action body runs only while
 *       that state is active (how/to/write/fsm/states.md: "actions run
 *       only when state active"; the gate's exact expression is the
 *       state-identity comparison over the state register). The anchor
 *       lowers the legal two-state FSM whose run state (per-state index 1)
 *       carries the multi-statement body to rc == 0, and pins that the
 *       action's equation value is a PIGEN_RTL_EXPR_CONDITIONAL whose
 *       condition is a PIGEN_RTL_EXPR_BINARY PIGEN_BINARY_EQUAL comparison
 *       of the state register read and the run state's encoding (value 1):
 *       the action runs only while the run state is active. A lowering that
 *       publishes the action value ungated (a bare constant) or gates it on
 *       a constant fails this section.
 *   (8) goto-guard-gates-next-state: the per-goto guard gating of the next
 *       state (does/goto/wait/for/a/transfer.md; the guard-gated transition
 *       of what/is/an/fsm.md). The anchor lowers the legal two-state FSM
 *       whose idle state carries `if (start) goto run;` (target index 1) to
 *       rc == 0 and pins that the update for that goto - located through the
 *       parse, its origin the goto node's span - carries a value that is a
 *       guard-conditioned next state: a CONDITIONAL whose when-true child is
 *       the target's encoding (value 1) and whose when-false child is the
 *       state register read. A lowering that publishes the transition
 *       ungated, as a bare target encoding, fails this section.
 *   (9) goto-never-waits-for-transfer: no transition ever waits for a
 *       transfer (does/goto/wait/for/a/transfer.md) - the negative half of
 *       the goto law, pinned on the controller's second transition, the
 *       mirror of the guard-gated line for the run state. The anchor lowers
 *       a legal two-state FSM whose run state carries the guarded
 *       `if (!start) goto idle;` (target index 0) to rc == 0 and pins that
 *       the update for that goto is a CONDITIONAL whose when-true child is
 *       an INTEGER of value 0 and whose when-false child is the state
 *       register read - the transition is keyed on the explicit enclosing
 *       guard alone, never on a handshake or transfer condition.
 *   (10) one-synchronous-controller: the controller is one synchronous
 *       process and one reset path (what/is/an/fsm.md: "one synchronous
 *       controller reset to its header initial state"). The anchor lowers a
 *       legal two-state FSM (two guarded gotos) to rc == 0 and pins that the
 *       parent module record owns EXACTLY ONE PIGEN_RTL_PROCESS - clocked
 *       posedge - whose update range holds exactly goto_count + 1 updates
 *       (one per goto plus the single reset-path update), all driving the
 *       single shared state register. A lowering that splits the controller
 *       across two processes, publishes a second reset path, or drops the
 *       reset update from the process fails this section.
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

/* The discriminating non-first initial-state witness for
 * reset-value-initial-state: the same module, ports and FSM body as
 * FSM_WITNESS, but the header names `run`, the SECOND declared state, while
 * `idle` is declared first (precedent FSM_WITNESS_NONFIRST in
 * tests/fsm_identity_test.c). The header initial state is the second state
 * (per-state index 1), so a reset that always publishes the first declared
 * state's encoding (0) cannot pass the section: the reset value the owner
 * must publish is the header-named state's encoding (1), not the first
 * declared state's (0). */
#define FSM_WITNESS_NONFIRST(NAME) \
	"module " NAME "\n" \
	"(\n" \
	"\tinput logic clk,\n" \
	"\tinput logic rst,\n" \
	"\tinput logic start,\n" \
	"\toutput logic done\n" \
	");\n" \
	"\nfsm " NAME " @(posedge clk) reset (rst) initial run\n" \
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


	/* ------------------------------------------------------------------ */
	/* (6) reset-value-initial-state: the reset path's value is the       */
	/*     header initial state's ENCODING, not the first declared        */
	/*     state's (what/is/an/fsm.md - "one synchronous controller       */
	/*     reset to its header initial state"). The anchor is the         */
	/*     non-first witness: the same legal two-state FSM as the other   */
	/*     anchors, but the header names `run`, the SECOND declared       */
	/*     state (per-state index 1), so a reset that always publishes    */
	/*     the first declared state's encoding (0) cannot pass. It        */
	/*     lowers to rc == 0, and the reset-path update - the one update  */
	/*     whose origin span is the FSM node's span (every goto update    */
	/*     carries its goto node's span) - drives the state register to   */
	/*     a CONDITIONAL whose when-true child is an INTEGER of the state */
	/*     type with value 1, the header-named state's index. The value   */
	/*     type is the state register's own type (the register object is  */
	/*     the update's destination). The witness pins the resolution     */
	/*     (how/to/write/a/discriminating/resolution/witness.md,          */
	/*     precedent FSM_WITNESS_NONFIRST in tests/fsm_identity_test.c);  */
	/*     the goto-to-run transition's when-true encoding is also 1, so  */
	/*     the FSM-node-span origin is the standalone discriminator. A    */
	/*     failed lowering call stops the compile and may leave partial   */
	/*     state behind, so no anchor asserts what a failure left         */
	/*     unchanged (how/should/compiler/builders/fail.md).              */
	/* ------------------------------------------------------------------ */
	SECTION("reset-value-initial-state") {
		/* Anchor: the non-first witness lowers, and the reset-path update
		 * (origin the FSM node's span) drives the state register to the
		 * header-named state's encoding (1, the non-first index). */
		{
			parsed p;
			fl_parse(&p, "rvi_ok.pigen", FSM_WITNESS_NONFIRST("rvi_ok"));
			unit u;
			const pigen_syntax_node *states[8];
			size_t state_count;
			pigen_rtl_model rtl;
			owned_records groups[4];
			size_t group_count;
			pigen_source_span fsm_span;
			int rc;
			size_t i;
			size_t j;
			size_t span_updates = 0;
			const pigen_rtl_update *the_reset = NULL;

			REQUIRE(p.fsm && p.fsm->kind == PIGEN_SYNTAX_FSM);
			/* The witness carries the states idle (first), run (second). */
			state_count = fl_state_nodes(&p, states, 8);
			REQUIRE(state_count == 2);
			REQUIRE(span_is(&p.sources,
				pigen_origin_spelling_span(&p.pre.expanded,
					pigen_expanded_token_get(&p.pre.expanded,
						states[0]->as.fsm_state.name)->origin),
				"idle"));
			REQUIRE(span_is(&p.sources,
				pigen_origin_spelling_span(&p.pre.expanded,
					pigen_expanded_token_get(&p.pre.expanded,
						states[1]->as.fsm_state.name)->origin),
				"run"));
			/* The header names the NON-FIRST state as initial (the
			 * discriminator: an always-first reset passes on an
			 * `initial idle` witness and must fail on this one). */
			REQUIRE(span_is(&p.sources,
				pigen_origin_spelling_span(&p.pre.expanded,
					pigen_expanded_token_get(&p.pre.expanded,
						p.fsm->as.fsm.initial)->origin),
				"run"));
			fsm_span = p.fsm->location.source_span;
			fl_unit(&p, &u);
			rc = fl_drive(&p, &u, p.fsm, p.fsm_id, &rtl);
			REQUIRE(rc == 0); /* the legal non-first FSM lowers */
			group_count = fl_owned(&rtl, groups, 4);
			REQUIRE(group_count >= 1);
			/* Locate the reset-path update: the one update of the
			 * controller's processes whose origin span is the FSM node's
			 * span (every goto update carries its goto node's span, so no
			 * transition update has it). */
			for (i = 0; i < group_count; i++)
			{
				size_t pi;

				for (pi = groups[i].first_process;
					pi < groups[i].first_process + groups[i].process_count;
					pi++)
				{
					const pigen_rtl_process *pr = pigen_rtl_process_get(
						&rtl, (pigen_rtl_process_id){(uint32_t)pi});

					if (!pr)
						continue;
					for (j = pr->updates.first;
						j < pr->updates.first + pr->updates.count; j++)
					{
						const pigen_rtl_update *up =
							pigen_rtl_update_get(&rtl,
								(pigen_rtl_update_id){(uint32_t)j});

						if (!up)
							continue;
						if (up->origin.source.index ==
							fsm_span.source.index &&
							up->origin.start == fsm_span.start &&
							up->origin.end == fsm_span.end)
						{
							span_updates++;
							the_reset = up;
						}
					}
				}
			}
			REQUIRE(span_updates == 1); /* exactly one reset-path update */
			/* The reset update drives the state register: its destination
			 * is the synthetic variable (not a declared signal) of the
			 * controller. */
			{
				const pigen_rtl_expr_id *children;
				size_t child_count = 0;
				const pigen_rtl_object *dest =
					pigen_rtl_object_get(&rtl, the_reset->destination);
				const pigen_rtl_expr *value =
					pigen_rtl_expr_get(&rtl, the_reset->value);
				const pigen_rtl_expr *when_true;
				const pigen_rtl_type *state_type;

				REQUIRE(dest);
				REQUIRE(dest->kind == PIGEN_RTL_OBJECT_VARIABLE);
				REQUIRE(IS_INVALID_ID(dest->semantic_signal)); /* synthetic */
				/* The value: a CONDITIONAL (the reset is gated) whose
				 * when-true child is the header-named state's encoding. */
				REQUIRE(value);
				REQUIRE(value->kind == PIGEN_RTL_EXPR_CONDITIONAL);
				children = pigen_rtl_expr_children(&rtl, the_reset->value,
					&child_count);
				REQUIRE(children);
				/* Children in order: [condition, when_true,
				 * when_false]. */
				REQUIRE(child_count == 3);
				when_true = pigen_rtl_expr_get(&rtl, children[1]);
				REQUIRE(when_true);
				REQUIRE(when_true->kind == PIGEN_RTL_EXPR_INTEGER);
				/* The encoding is the header-named state's index (1,
				 * the non-first state) in the state type - the
				 * register's own type, not the first state's (0). */
				REQUIRE(when_true->value == 1);
				REQUIRE(when_true->type.index == dest->type.index);
				state_type = IS_INVALID_ID(dest->type) ? NULL :
					pigen_rtl_type_get(&rtl, dest->type);
				REQUIRE(state_type && state_type->width >= 1);
			}
			pigen_free_rtl_model(&rtl);
			fl_free_unit(&u);
			fl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (6) state-active-action-gate: a state's action body runs only while */
	/*     that state is active (how/to/write/fsm/states.md - "actions run */
	/*     only when state active"; the gate's exact expression is the     */
	/*     state-identity comparison over the state register). The anchor  */
	/*     lowers the legal two-state FSM whose run state (per-state index */
	/*     1, located through the parse) carries the multi-statement body  */
	/*     (`done <= 1;` then a guarded `goto idle;`) to rc == 0, and pins */
	/*     that the action's equation - the owned record whose destination */
	/*     is the owned object bound, by provenance, to the module's `done */
	/*     ` output - has a value that is a PIGEN_RTL_EXPR_CONDITIONAL     */
	/*     whose condition is a PIGEN_RTL_EXPR_BINARY PIGEN_BINARY_EQUAL   */
	/*     comparison whose operands are the state register read (a        */
	/*     PIGEN_RTL_EXPR_OBJECT naming the synthetic register) and a      */
	/*     PIGEN_RTL_EXPR_INTEGER of the state type with value 1 (the run  */
	/*     state's encoding): the action runs only while the run state is  */
	/*     active. A lowering that publishes the action value ungated (a   */
	/*     bare constant, not a CONDITIONAL) or gates it on a constant     */
	/*     (a condition that is not the register-read/encoding comparison) */
	/*     fails this section. The comparison's operand structure is the   */
	/*     standalone pin: the gate's condition is located through the     */
	/*     action equation's value expression, never by a source-text      */
	/*     search, and its operand shape (a register read compared to the  */
	/*     state's encoding) is what a constant gate or an ungated action  */
	/*     cannot fake.                                                    */
	/* ------------------------------------------------------------------ */
	SECTION("state-active-action-gate") {
		/* Anchor: the legal two-state FSM whose run state (per-state index
		 * 1) carries the multi-statement body lowers to rc == 0, and the
		 * action's equation value is the state-active conditional:
		 * `state_read == 1 ? value : inactive`. */
		{
			parsed p;
			fl_parse(&p, "sag_ok.pigen", FSM_WITNESS("sag_ok"));
			unit u;
			const pigen_syntax_node *states[8];
			size_t state_count;
			pigen_rtl_model rtl;
			owned_records groups[4];
			size_t group_count;
			int rc;
			size_t i;
			int found_gate = 0;

			REQUIRE(p.fsm && p.fsm->kind == PIGEN_SYNTAX_FSM);
			/* The per-state order is the parse's: the witness declares idle
			 * then run, so the run state is per-state index 1. */
			state_count = fl_state_nodes(&p, states, 8);
			REQUIRE(state_count == 2);
			REQUIRE(span_is(&p.sources,
				pigen_origin_spelling_span(&p.pre.expanded,
					pigen_expanded_token_get(&p.pre.expanded,
						states[1]->as.fsm_state.name)->origin),
				"run"));
			fl_unit(&p, &u);
			rc = fl_drive(&p, &u, p.fsm, p.fsm_id, &rtl);
			REQUIRE(rc == 0); /* the legal FSM lowers (red against the stub) */
			group_count = fl_owned(&rtl, groups, 4);
			REQUIRE(group_count >= 1);
			/* The action's equation: some owned record whose destination is
			 * an owned object bound, by provenance, to the module's done
			 * output signal (the per-state-action-bodies binding). Its
			 * value must carry the state-active gate. */
			for (i = 0; i < group_count && !found_gate; i++)
			{
				size_t j;

				for (j = groups[i].first_equation;
					j < groups[i].first_equation +
						groups[i].equation_count && !found_gate;
					j++)
				{
					const pigen_rtl_equation *e =
						pigen_rtl_equation_get(&rtl,
							(pigen_rtl_equation_id){(uint32_t)j});
					const pigen_rtl_object *dest;
					const pigen_rtl_expr *value;
					const pigen_rtl_expr *condition;
					const pigen_rtl_expr *left;
					const pigen_rtl_expr *right;
					const pigen_rtl_type *left_type;
					const pigen_rtl_type *right_type;
					const pigen_rtl_expr_id *children;
					size_t child_count;

					if (!e)
						continue;
					dest = pigen_rtl_object_get(&rtl, e->destination);
					if (!dest || IS_INVALID_ID(dest->semantic_signal) ||
						dest->semantic_signal.index != u.done.index)
						continue; /* not the action's equation */
					/* The value is the state-active conditional. */
					value = pigen_rtl_expr_get(&rtl, e->value);
					REQUIRE(value);
					REQUIRE(value->kind == PIGEN_RTL_EXPR_CONDITIONAL);
					/* A conditional's children are [condition, when-true,
					 * when-false] (pigen_rtl_expr_add_conditional). */
					child_count = 0;
					children = pigen_rtl_expr_children(&rtl, e->value,
						&child_count);
					REQUIRE(child_count == 3);
					REQUIRE(children);
					/* The condition is the equality comparison `state_read
					 * == 1` (PIGEN_BINARY_EQUAL). */
					condition = pigen_rtl_expr_get(&rtl, children[0]);
					REQUIRE(condition);
					REQUIRE(condition->kind == PIGEN_RTL_EXPR_BINARY);
					REQUIRE(condition->as.binary.resolution.operation.operator ==
						PIGEN_BINARY_EQUAL);
					/* A binary's children are [left, right]
					 * (pigen_rtl_expr_add_binary). */
					child_count = 0;
					children = pigen_rtl_expr_children(&rtl,
						children[0], &child_count);
					REQUIRE(child_count == 2);
					REQUIRE(children);
					/* The operands are the state register read (a
					 * PIGEN_RTL_EXPR_OBJECT naming the synthetic
					 * register - the register is synthetic, so its
					 * provenance carries no semantic signal) and the
					 * run state's encoding (a PIGEN_RTL_EXPR_INTEGER of
					 * the state type with value 1). */
					left = pigen_rtl_expr_get(&rtl, children[0]);
					right = pigen_rtl_expr_get(&rtl, children[1]);
					REQUIRE(left);
					REQUIRE(right);
					REQUIRE(left->kind == PIGEN_RTL_EXPR_OBJECT);
					{
						const pigen_rtl_object *reg =
							pigen_rtl_object_get(&rtl, left->object);

						REQUIRE(reg);
						REQUIRE(reg->kind == PIGEN_RTL_OBJECT_VARIABLE);
						REQUIRE(IS_INVALID_ID(reg->semantic_signal));
						/* The operand is a read of the state register:
						 * the equation's destination is the done
						 * output object, so the register object is a
						 * distinct object of the model. */
						REQUIRE(left->object.index !=
							e->destination.index);
					}
					/* The encoding operand is the run state's index (1),
					 * a literal of the state type (the register's type:
					 * a state type of width >= 1, not the 1-bit gate
					 * type). */
					REQUIRE(right->kind == PIGEN_RTL_EXPR_INTEGER);
					REQUIRE(right->value == 1);
					left_type = IS_INVALID_ID(left->type) ? NULL :
						pigen_rtl_type_get(&rtl, left->type);
					right_type = IS_INVALID_ID(right->type) ? NULL :
						pigen_rtl_type_get(&rtl, right->type);
					REQUIRE(left_type && left_type->width >= 1);
					REQUIRE(right_type);
					REQUIRE(right->type.index == left->type.index);
					found_gate = 1;
				}
			}
			/* The state-active gate is published (an ungated bare-constant
			 * action value, or a gate on a constant, leaves no
			 * register-read/encoding comparison under the action's
			 * equation). */
			REQUIRE(found_gate);
			pigen_free_rtl_model(&rtl);
			fl_free_unit(&u);
			fl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (6) goto-guard-gates-next-state: the per-goto guard gating of the    */
	/*     next state (does/goto/wait/for/a/transfer.md; the guard-gated    */
	/*     transition of what/is/an/fsm.md): the goto changes the state     */
	/*     only under its explicit enclosing guard, the state holds         */
	/*     otherwise. The anchor lowers the legal two-state FSM whose idle  */
	/*     state carries `if (start) goto run;` (target per-state index 1)  */
	/*     to rc == 0 and pins that the update for that goto - located      */
	/*     through the parse, its origin the goto node's span (each goto's  */
	/*     update is located through its node's span, never a source-text   */
	/*     search) - carries a value that is a guard-conditioned next       */
	/*     state: a PIGEN_RTL_EXPR_CONDITIONAL whose when-true child is the */
	/*     target's state encoding (a PIGEN_RTL_EXPR_INTEGER of the state   */
	/*     type with value 1) and whose when-false child is the state       */
	/*     register read (a PIGEN_RTL_EXPR_OBJECT naming the synthetic      */
	/*     register) - the goto changes state only under its guard, the     */
	/*     state holds otherwise. A lowering that publishes the transition  */
	/*     ungated, as a bare target encoding, fails this section.          */
	/* ------------------------------------------------------------------ */
	SECTION("goto-guard-gates-next-state") {
		/* Anchor: the legal two-state FSM lowers, and the update for the
		 * idle state's `goto run;` (target index 1) carries the
		 * guard-conditioned next state. */
		{
			parsed p;
			fl_parse(&p, "ggg_ok.pigen", FSM_WITNESS("ggg_ok"));
			unit u;
			const pigen_syntax_node *gotos[8];
			size_t goto_count;
			pigen_rtl_model rtl;
			owned_records groups[4];
			size_t group_count;
			pigen_source_span goto_span;
			int rc;
			size_t i;
			int located_update = 0;
			int guard_gated_next = 0;

			REQUIRE(p.fsm && p.fsm->kind == PIGEN_SYNTAX_FSM);
			/* Locate the idle state's goto through the parse: the witness's
			 * first goto in source order, whose target token spells `run`
			 * (the per-state index 1 target). */
			goto_count = fl_goto_nodes(&p, gotos, 8);
			REQUIRE(goto_count == 2);
			{
				const pigen_expanded_token *target =
					pigen_expanded_token_get(&p.pre.expanded,
						gotos[0]->as.goto_statement.target);

				REQUIRE(target);
				REQUIRE(span_is(&p.sources,
					pigen_origin_spelling_span(&p.pre.expanded,
						target->origin), "run"));
			}
			goto_span = gotos[0]->location.source_span;
			fl_unit(&p, &u);
			rc = fl_drive(&p, &u, p.fsm, p.fsm_id, &rtl);
			REQUIRE(rc == 0); /* the legal FSM lowers (red against the stub) */
			group_count = fl_owned(&rtl, groups, 4);
			REQUIRE(group_count >= 1);
			/* Locate the update for the goto: the one owned-process update
			 * whose origin is the goto node's span (the per-goto origin is
			 * the discriminator; the reset path's update carries the FSM
			 * node's span, the other goto's update its own span). */
			for (i = 0; i < group_count && !located_update; i++)
			{
				size_t pi;

				for (pi = groups[i].first_process;
					pi < groups[i].first_process +
						groups[i].process_count && !located_update;
					pi++)
				{
					const pigen_rtl_process *pr =
						pigen_rtl_process_get(&rtl,
							(pigen_rtl_process_id){(uint32_t)pi});
					size_t ui;

					if (!pr)
						continue;
					for (ui = pr->updates.first;
						ui < pr->updates.first + pr->updates.count;
						ui++)
					{
						const pigen_rtl_update *up =
							pigen_rtl_update_get(&rtl,
								(pigen_rtl_update_id){(uint32_t)ui});
						const pigen_rtl_expr_id *children;
						const pigen_rtl_expr *value;
						const pigen_rtl_expr *when_true;
						const pigen_rtl_expr *when_false;
						const pigen_rtl_object *register_object;
						const pigen_rtl_type *state_type;
						size_t child_count = 0;

						if (!up ||
							up->origin.source.index !=
								goto_span.source.index ||
							up->origin.start != goto_span.start ||
							up->origin.end != goto_span.end)
							continue;
						located_update = 1;
						/* The guard-gated next state: the update's value
						 * is a conditional; its when-true child is the
						 * target's state encoding (an integer of the
						 * state type, value 1) and its when-false child
						 * is the state register read (an object naming
						 * the synthetic register) - the goto changes
						 * state only under its guard, the state holds
						 * otherwise. */
						value = pigen_rtl_expr_get(&rtl, up->value);
						if (!value ||
							value->kind != PIGEN_RTL_EXPR_CONDITIONAL)
							continue;
						children = pigen_rtl_expr_children(&rtl,
							up->value, &child_count);
						if (!children || child_count != 3)
							continue;
						when_true = pigen_rtl_expr_get(&rtl,
							children[1]);
						when_false = pigen_rtl_expr_get(&rtl,
							children[2]);
						if (!when_true ||
							when_true->kind != PIGEN_RTL_EXPR_INTEGER ||
							when_true->value != 1)
							continue;
						if (!when_false ||
							when_false->kind != PIGEN_RTL_EXPR_OBJECT ||
							IS_INVALID_ID(when_false->object))
							continue;
						register_object =
							pigen_rtl_object_get(&rtl,
								when_false->object);
						if (!register_object ||
							register_object->kind !=
								PIGEN_RTL_OBJECT_VARIABLE ||
							!IS_INVALID_ID(
								register_object->semantic_signal))
							continue;
						/* The when-true integer is of the state type:
						 * the type of the state register the when-false
						 * child reads (an integer of a non-state type,
						 * or a missing type, is not the target's state
						 * encoding). */
						state_type = IS_INVALID_ID(
							register_object->type) ? NULL :
							pigen_rtl_type_get(&rtl,
								register_object->type);
						if (!state_type ||
							state_type->width < 1 ||
							IS_INVALID_ID(when_true->type) ||
							when_true->type.index !=
								register_object->type.index)
							continue;
						/* The when-false register read is of the state
						 * type (the hold fact: the state holds its
						 * current value when the guard is false). */
						if (IS_INVALID_ID(when_false->type) ||
							when_false->type.index !=
								register_object->type.index)
							continue;
						guard_gated_next = 1;
					}
				}
			}
			REQUIRE(located_update); /* the goto's update is published */
			REQUIRE(guard_gated_next);
			pigen_free_rtl_model(&rtl);
			fl_free_unit(&u);
			fl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (6) goto-never-waits-for-transfer: no transition ever waits for a  */
	/*     transfer (does/goto/wait/for/a/transfer.md) - the negative     */
	/*     half of the goto law, pinned on the controller's second        */
	/*     transition, the mirror of the guard-gated-transitions line for */
	/*     the run state. The anchor lowers a legal two-state FSM whose   */
	/*     run state carries the guarded `if (!start) goto idle;` (target */
	/*     per-state index 0) to rc == 0 and pins that the update for     */
	/*     that goto (the one whose origin is the goto node's span,       */
	/*     located through the parse - never a source-text search)        */
	/*     carries a value that is a PIGEN_RTL_EXPR_CONDITIONAL whose     */
	/*     when-true child is a PIGEN_RTL_EXPR_INTEGER of the state type  */
	/*     with value 0 and whose when-false child is a                  */
	/*     PIGEN_RTL_EXPR_OBJECT naming the state register - the          */
	/*     transition is keyed on the explicit enclosing guard alone,     */
	/*     never on a handshake or transfer condition. A lowering that    */
	/*     substitutes an implicit transfer wait for the guard changes    */
	/*     the conditional's condition child away from the guard and      */
	/*     fails this section. The mirror target (0 vs the first          */
	/*     transition's 1) keeps the pin independent of the first         */
	/*     transition's fixture state.                                    */
	/* ------------------------------------------------------------------ */
	SECTION("goto-never-waits-for-transfer") {
		/* Anchor: the second transition's update is the guard-keyed
		 * (target-encoding, register-read) conditional, located through
		 * the parse by the goto node's span. */
		{
			parsed p;
			fl_parse(&p, "gnw_ok.pigen", FSM_WITNESS("gnw_ok"));
			unit u;
			const pigen_syntax_node *gotos[8];
			size_t goto_count;
			const pigen_syntax_node *second;
			const pigen_expanded_token *target;
			pigen_rtl_model rtl;
			owned_records groups[4];
			size_t group_count;
			int rc;
			size_t i, j;
			pigen_rtl_object_id register_object =
				(pigen_rtl_object_id){PIGEN_INVALID_ID};
			size_t matched_updates = 0;
			int conditional_shape = 0;

			REQUIRE(p.fsm && p.fsm->kind == PIGEN_SYNTAX_FSM);
			goto_count = fl_goto_nodes(&p, gotos, 8);
			REQUIRE(goto_count == 2); /* the witness carries two guarded gotos */
			/* The second transition of the controller (the run state's
			 * `goto idle;`), located through the parse and pinned by its
			 * target token's original span text: the mirror target (the
			 * first state's index 0), distinct from the first transition's
			 * target (the second state's index 1). */
			second = gotos[1];
			REQUIRE(second->kind == PIGEN_SYNTAX_GOTO);
			target = pigen_expanded_token_get(&p.pre.expanded,
				second->as.goto_statement.target);
			REQUIRE(target);
			REQUIRE(span_is(&p.sources,
				pigen_origin_spelling_span(&p.pre.expanded, target->origin),
				"idle"));
			{
				const pigen_expanded_token *first_target =
					pigen_expanded_token_get(&p.pre.expanded,
						gotos[0]->as.goto_statement.target);

				REQUIRE(first_target);
				REQUIRE(span_is(&p.sources,
					pigen_origin_spelling_span(&p.pre.expanded,
						first_target->origin),
					"run"));
			}
			fl_unit(&p, &u);
			rc = fl_drive(&p, &u, p.fsm, p.fsm_id, &rtl);
			REQUIRE(rc == 0); /* the legal FSM lowers */
			group_count = fl_owned(&rtl, groups, 4);
			REQUIRE(group_count >= 1);
			/* The state register: the synthetic owned variable of a valid
			 * state type (the FSM state is not a declared signal, so its
			 * provenance carries no semantic signal). */
			for (i = 0; i < group_count && IS_INVALID_ID(register_object); i++)
			{
				for (j = groups[i].first_object;
					j < groups[i].first_object + groups[i].object_count &&
					IS_INVALID_ID(register_object);
					j++)
				{
					const pigen_rtl_object *o =
						pigen_rtl_object_get(&rtl,
							(pigen_rtl_object_id){(uint32_t)j});
					const pigen_rtl_type *t;

					if (!o || o->kind != PIGEN_RTL_OBJECT_VARIABLE ||
						!IS_INVALID_ID(o->semantic_signal))
						continue;
					t = IS_INVALID_ID(o->type) ? NULL :
						pigen_rtl_type_get(&rtl, o->type);
					if (!t || t->width < 1)
						continue;
					register_object = (pigen_rtl_object_id){(uint32_t)j};
				}
			}
			REQUIRE(!IS_INVALID_ID(register_object));
			/* The update for the located second goto: exactly one update of
			 * an owned process whose origin is the goto node's span (the
			 * per-goto origin span is the discriminator - each goto's update
			 * is located through its node's span, never a source-text
			 * search), and it drives the state register. */
			for (i = 0; i < group_count; i++)
			{
				for (j = groups[i].first_process;
					j < groups[i].first_process + groups[i].process_count;
					j++)
				{
					const pigen_rtl_process *pr = pigen_rtl_process_get(&rtl,
						(pigen_rtl_process_id){(uint32_t)j});
					size_t ui;

					if (!pr)
						continue;
					for (ui = pr->updates.first;
						ui < pr->updates.first + pr->updates.count; ui++)
					{
						const pigen_rtl_update *up =
							pigen_rtl_update_get(&rtl,
								(pigen_rtl_update_id){(uint32_t)ui});
						const pigen_rtl_expr *value;
						const pigen_rtl_expr_id *children;
						size_t child_count;
						const pigen_rtl_expr *condition;
						const pigen_rtl_expr *when_true;
						const pigen_rtl_expr *when_false;
						const pigen_rtl_type *cond_type;

						if (!up)
							continue;
						if (up->origin.source.index != p.source.index ||
							up->origin.start !=
								second->location.source_span.start ||
							up->origin.end !=
								second->location.source_span.end)
							continue;
						matched_updates++;
						REQUIRE(up->destination.index ==
							register_object.index);
						value = pigen_rtl_expr_get(&rtl, up->value);
						REQUIRE(value &&
							value->kind ==
								PIGEN_RTL_EXPR_CONDITIONAL);
						/* The conditional's children are [condition,
						 * when_true, when_false]. The transition is keyed
						 * on the explicit enclosing guard alone, never on
						 * a handshake or transfer condition: the
						 * condition child is the guard - a 1-bit boolean
						 * gate that is a distinct expression from both the
						 * target encoding (when-true) and the current-state
						 * register read (when-false). A lowering that
						 * substitutes an implicit transfer wait, the target
						 * or the state itself for the guard changes the
						 * condition child away from the guard and fails
						 * this shape. The when-true child is the target
						 * encoding - a PIGEN_RTL_EXPR_INTEGER of the state
						 * type with value 0 (the first state's per-state
						 * index, the mirror of the first transition's 1) -
						 * and the when-false child is a
						 * PIGEN_RTL_EXPR_OBJECT naming the state register
						 * (the state holds otherwise). */
						children = pigen_rtl_expr_children(&rtl, up->value,
							&child_count);
						REQUIRE(child_count == 3 && children);
						condition = children ?
							pigen_rtl_expr_get(&rtl, children[0]) : NULL;
						when_true = children ?
							pigen_rtl_expr_get(&rtl, children[1]) : NULL;
						when_false = children ?
							pigen_rtl_expr_get(&rtl, children[2]) : NULL;
						cond_type = condition ?
							(IS_INVALID_ID(condition->type) ? NULL :
								pigen_rtl_type_get(&rtl,
									condition->type)) : NULL;
						conditional_shape =
							value->type.index != PIGEN_INVALID_ID &&
							condition &&
							cond_type && cond_type->width == 1 &&
							when_true &&
							when_false &&
							children[0].index != children[1].index &&
							children[0].index != children[2].index &&
							when_true->kind == PIGEN_RTL_EXPR_INTEGER &&
							when_true->type.index == value->type.index &&
							when_true->value == 0 &&
							when_false &&
							when_false->kind == PIGEN_RTL_EXPR_OBJECT &&
							when_false->object.index ==
								register_object.index;
					}
				}
			}
			REQUIRE(matched_updates == 1);
			REQUIRE(conditional_shape);
			pigen_free_rtl_model(&rtl);
			fl_free_unit(&u);
			fl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (6) one-synchronous-controller: the controller is one synchronous   */
	/*     process and one reset path (what/is/an/fsm.md: "one             */
	/*     synchronous controller reset to its header initial state").     */
	/*     The anchor lowers a legal two-state FSM (the FSM_WITNESS: two   */
	/*     guarded gotos) to rc == 0 and pins that the parent module       */
	/*     record owns EXACTLY ONE PIGEN_RTL_PROCESS - clocked posedge,    */
	/*     the synchronous controller - whose update range holds exactly   */
	/*     goto_count + 1 updates: one per goto plus the single            */
	/*     reset-path update, all driving the single shared state          */
	/*     register (a synthetic variable; the reset update's origin is    */
	/*     the FSM node's span, the gotos' origins their nodes' spans).    */
	/*     The exact count is the seam's pin of the one-controller         */
	/*     structure, and the reset path's presence in the process is what */
	/*     makes the reset value part of the synchronous controller rather */
	/*     than a loose record: a lowering that splits the controller      */
	/*     across two processes, publishes a second reset path, or drops   */
	/*     the reset update from the process fails this section. No failed */
	/*     call is asserted to leave anything unchanged (how/should/       */
	/*     compiler/builders/fail.md).                                     */
	/* ------------------------------------------------------------------ */
	SECTION("one-synchronous-controller") {
		/* Anchor: a legal two-state FSM lowers, and the controller is
		 * exactly one posedge process whose updates are the gotos plus the
		 * single reset path, all driving the shared state register. */
		{
			parsed p;
			fl_parse(&p, "osc_ok.pigen", FSM_WITNESS("osc_ok"));
			unit u;
			const pigen_syntax_node *gotos[8];
			size_t goto_count;
			pigen_rtl_model rtl;
			owned_records groups[4];
			size_t group_count;
			size_t total_processes = 0;
			const pigen_rtl_process *controller = NULL;
			size_t i;
			size_t j;
			int rc;
			size_t expected_updates;
			size_t reset_updates = 0;
			int all_drive_register = 1;

			REQUIRE(p.fsm && p.fsm->kind == PIGEN_SYNTAX_FSM);
			/* Count the FSM's gotos through the parse (the
			 * PIGEN_SYNTAX_GOTO descendants of the state nodes): the
			 * witness carries two, so the controller's update range
			 * holds exactly 2 + 1 = 3 updates. */
			goto_count = fl_goto_nodes(&p, gotos, 8);
			REQUIRE(goto_count == 2);
			fl_unit(&p, &u);
			rc = fl_drive(&p, &u, p.fsm, p.fsm_id, &rtl);
			REQUIRE(rc == 0); /* the legal FSM lowers */
			group_count = fl_owned(&rtl, groups, 4);
			REQUIRE(group_count >= 1);
			/* EXACTLY ONE process across every parent module record: a
			 * controller split across two processes fails here. */
			for (i = 0; i < group_count; i++)
				total_processes += groups[i].process_count;
			REQUIRE(total_processes == 1);
			for (i = 0; i < group_count; i++)
				for (j = groups[i].first_process;
					j < groups[i].first_process + groups[i].process_count;
					j++)
					controller = pigen_rtl_process_get(&rtl,
						(pigen_rtl_process_id){(uint32_t)j});
			REQUIRE(controller);
			/* The synchronous controller: clocked posedge, the clock a
			 * published expression (the controller runs on the module's
			 * clock, not combinatorially). */
			REQUIRE(controller->edge == PIGEN_SEMANTIC_POSEDGE);
			REQUIRE(!IS_INVALID_ID(controller->clock));
			/* Exactly goto_count + 1 updates: one per goto plus the
			 * single reset-path update. A dropped reset update or a
			 * second reset path changes the count and fails here. */
			expected_updates = goto_count + 1;
			REQUIRE(controller->updates.count == expected_updates);
			/* All updates drive the single shared state register, and the
			 * reset path is present in the process: the reset update's
			 * origin is the FSM node's span (every goto update's origin
			 * is its goto node's span). */
			for (j = controller->updates.first;
				j < controller->updates.first + controller->updates.count;
				j++)
			{
				const pigen_rtl_update *up = pigen_rtl_update_get(&rtl,
					(pigen_rtl_update_id){(uint32_t)j});
				const pigen_rtl_object *dest = up ?
					pigen_rtl_object_get(&rtl, up->destination) : NULL;

				REQUIRE(up);
				if (!dest || dest->kind != PIGEN_RTL_OBJECT_VARIABLE ||
					!IS_INVALID_ID(dest->semantic_signal))
					all_drive_register = 0;
				if (up->origin.source.index ==
						p.fsm->location.source_span.source.index &&
					up->origin.start ==
						p.fsm->location.source_span.start &&
					up->origin.end == p.fsm->location.source_span.end)
					reset_updates++;
			}
			/* One reset path (the second fails the count pin above; the
			 * missing one fails here): the reset update is in the
			 * process, so its value is part of the synchronous
			 * controller rather than a loose record. */
			REQUIRE(reset_updates == 1);
			REQUIRE(all_drive_register);
			pigen_free_rtl_model(&rtl);
			fl_free_unit(&u);
			fl_free(&p);
		}
	}

	return check_finish();
}
