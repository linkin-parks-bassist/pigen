#ifndef PIGEN_INSTANCE_IDENTITY_H
#define PIGEN_INSTANCE_IDENTITY_H

#include <stddef.h>

#include "pigen/ids.h"
#include "pigen/semantic.h"
#include "pigen/syntax.h"

/* The instance identity owner: the owner seam that turns one parsed ordinary
 * SV child instantiation (a PIGEN_SYNTAX_INSTANCE node) into the instance's
 * module-scope identity in the resolved semantic model: the child module the
 * instance instantiates (the module whose name token text equals the instance
 * type name) and one resolved connection per source-order connection, each
 * bound to the child module's port (the module-scope port signal the connection
 * names, positionally or by name), with that port's direction, data type, shape
 * and transfer type.
 *
 * The identity is surface-independent (is/struct/like/child/instantiation/
 * specified.md): resolution keys only on the token TEXT of the type name, the
 * instance name and the port names (never on spelling, layout or the node's
 * children beyond their token/expr ids), so a later grammar change cannot
 * alter the established identities and fabric semantics depend only on
 * resolved instance/port identities.
 *
 * A failed call returns -1 and the compile stops; it may leave partial state
 * behind, and nothing uses the model after a failed compile, so no caller or
 * test asserts that a failure left the model unchanged
 * (how/should/compiler/builders/fail.md).
 *
 * This seam owns the instance identity on the structured path
 * (include/pigen/instance_identity.h + src/instance_identity.c); it is the
 * step-(i2) sibling of the FSM's step-(s2) owner (include/pigen/fsm_identity.h
 * + src/fsm_identity.c). The quarantined textual prototype is not touched,
 * reused or linked. */

/* The bound port of one resolved instance connection: the connected port's
 * symbol identity in the child module (the module-scope port signal the
 * connection binds to), that port's direction, data type, shape and transfer
 * type, the connection's original span and the connection node's syntax id. An
 * unresolved (invalid) connection carries the PIGEN_INVALID_ID sentinel in its
 * port and syntax ids, an invalid data type and shape, an empty span and an
 * out-of-range direction. */
typedef struct {
	pigen_symbol_id port;
	pigen_semantic_direction direction;
	pigen_data_type_id data_type;
	pigen_shape_id shape;
	pigen_transfer_type transfer_type;
	pigen_source_span span;
	pigen_syntax_id syntax;
} pigen_instance_connection;

/* The established module-scope identity of one ordinary SV child instance.
 * type_module is the child module the instance instantiates (the semantic
 * module whose PIGEN_SYNTAX_MODULE name token text equals the instance's type
 * name token text); instance_name is the instance name's original span;
 * connections is the resolved per-connection set, one entry per source-order
 * connection of the instance node (named connections resolve by port name,
 * positional connections resolve to the next child port in declared order);
 * connection_count is the number of resolved connections; span is the instance
 * node's original span; syntax is the owning instance node's syntax id (the
 * PIGEN_SYNTAX_INSTANCE node). An invalid identity carries the PIGEN_INVALID_ID
 * sentinel in its type_module and syntax ids, a connection_count of 0, an empty
 * instance_name span and an invalid first connection. */
typedef struct {
	pigen_module_id type_module;
	pigen_source_span instance_name;
	pigen_instance_connection connections[32];
	size_t connection_count;
	pigen_source_span span;
	pigen_syntax_id syntax;
} pigen_instance_identity;

/* Tests whether *identity is an invalid instance identity: the PIGEN_INVALID_ID
 * sentinel in its type_module and syntax ids, a connection_count of 0, an empty
 * instance_name span and an invalid first connection (port, syntax and span).
 * Returns 1 when the identity is invalid and 0 otherwise. */
int pigen_instance_identity_is_invalid(const pigen_instance_identity *identity);

/* Establishes the instance's module-scope identity in the model: the child
 * module the instance instantiates and one resolved connection per source-order
 * connection, each bound to the child module's port. syntax and syntax_id are
 * the parsed syntax tree and the instance node's id (the instance is a
 * PIGEN_SYNTAX_INSTANCE node); parent_module is the resolved module the
 * instance lives in; expanded is the parsed expanded source the instance node
 * was parsed from (the source that resolves the instance's token origins to
 * their original spelling spans, as the syntax tree itself does through its
 * expanded source). On success it resolves the type name to the child module
 * (the module whose name token text equals the instance's type name token
 * text, matched by token text the way the FSM owner resolves the header-named
 * initial state), resolves each source-order connection to the child's port
 * (a named connection to the module-scope port signal whose name token text
 * equals the connection's port token, a positional connection to the next child
 * port in declared order), then fills *identity. Returns 0 on success and -1 on
 * error; a failed call reports the error and the compile stops - it may leave
 * partial state behind, and no test asserts that a failure left the model
 * unchanged (how/should/compiler/builders/fail.md). */
int pigen_instance_identity_establish(pigen_semantic_model *model,
	const pigen_syntax_tree *syntax, pigen_syntax_id syntax_id,
	pigen_module_id parent_module,
	const pigen_syntax_node *instance,
	const pigen_expanded_source *expanded,
	pigen_instance_identity *identity);

#endif
