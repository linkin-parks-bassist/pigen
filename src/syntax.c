/* Source-spanned syntax nodes for modules and internal signal declarations. */
#include <stdlib.h>
#include <string.h>

#include "pigen/lexer.h"
#include "pigen/syntax.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})
#define INVALID_SYNTAX ((pigen_syntax_id){PIGEN_INVALID_ID})

typedef enum {
	TYPEDEF_NAME_ELIGIBLE,
	TYPEDEF_NAME_OPAQUE_SHADOW,
	TYPEDEF_NAME_PROTOCOL
} typedef_name_fact_kind;

typedef struct {
	pigen_token_id name;
	pigen_syntax_id scope;
	typedef_name_fact_kind kind;
	pigen_protocol_family family;
} typedef_name_fact;

typedef enum {
	PIPELINE_SCOPE_PIPELINE,
	PIPELINE_SCOPE_STAGE
} pipeline_scope_kind;

typedef struct {
	pigen_token_id name;
	pipeline_scope_kind kind;
	size_t index;
} pipeline_field;

typedef struct {
	pipeline_field *fields;
	size_t count;
	size_t capacity;
} pipeline_scope;

#define MAX_PIPELINE_SCOPES 16

typedef struct {
	pipeline_scope scopes[MAX_PIPELINE_SCOPES];
	size_t depth;
} pipeline_scope_stack;

typedef struct {
	pigen_token_id *names;
	size_t count;
	size_t capacity;
} fsm_state_names;

typedef struct {
	size_t module_token;
	size_t instance_token;
} pending_parameterized_instance;

typedef struct {
	const pigen_expanded_source *expanded;
	pigen_syntax_tree *tree;
	pigen_syntax_error *error;
	typedef_name_fact *typedef_name_facts;
	size_t typedef_name_fact_count;
	size_t typedef_name_fact_capacity;
	pigen_token_id *parameterized_module_names;
	size_t parameterized_module_count;
	size_t parameterized_module_capacity;
	pending_parameterized_instance *pending_parameterized_instances;
	size_t pending_parameterized_instance_count;
	size_t pending_parameterized_instance_capacity;
	pigen_token_id *function_names;
	size_t function_name_count;
	size_t function_name_capacity;
	pipeline_scope_stack pipeline_scopes;
	int pipeline_failed;
	int when_failed;
	size_t generate_depth;
	int combinational_body;
	size_t combinational_depth;
	int fsm_body_failed;
	const fsm_state_names *fsm_states;
	size_t source_length;
	size_t opaque_byte_cursor;
} syntax_parser;

static int node_has_function_call(syntax_parser *parser,
	pigen_syntax_id node_id, size_t depth);
static int node_has_structured_construct(syntax_parser *parser,
	pigen_syntax_id node_id, size_t depth);
static int retained_procedural_end(syntax_parser *parser, size_t start,
	size_t limit, size_t *end_out);
static int retained_process_has_structured(syntax_parser *parser,
	size_t start, size_t end);
static void remember_function_name(syntax_parser *parser, size_t start,
	size_t end);

typedef struct {
	const syntax_parser *parser;
	pigen_syntax_id scope;
} structured_type_query;

typedef struct {
	size_t syntax_node_count;
	size_t expression_node_count;
	size_t expression_child_count;
	size_t type_node_count;
	size_t type_argument_count;
	size_t shape_dimension_count;
	size_t retained_scope_count,retained_name_count;
	pigen_syntax_error error;
} syntax_checkpoint;

typedef size_t syntax_cursor;

static syntax_checkpoint save_checkpoint(const syntax_parser *parser)
{
	syntax_checkpoint checkpoint = {0};
	checkpoint.syntax_node_count = parser->tree->node_count;
	checkpoint.expression_node_count = parser->tree->expressions.node_count;
	checkpoint.expression_child_count = parser->tree->expressions.child_count;
	checkpoint.type_node_count = parser->tree->types.node_count;
	checkpoint.type_argument_count = parser->tree->types.argument_count;
	checkpoint.shape_dimension_count = parser->tree->shape_dimension_count;
	checkpoint.retained_scope_count = parser->tree->retained_scope_count;
	checkpoint.retained_name_count = parser->tree->retained_name_count;
	if (parser->error) checkpoint.error = *parser->error;
	return checkpoint;
}

static void restore_checkpoint(syntax_parser *parser,
	syntax_checkpoint checkpoint)
{
	parser->tree->node_count = checkpoint.syntax_node_count;
	parser->tree->expressions.node_count = checkpoint.expression_node_count;
	parser->tree->expressions.child_count = checkpoint.expression_child_count;
	parser->tree->types.node_count = checkpoint.type_node_count;
	parser->tree->types.argument_count = checkpoint.type_argument_count;
	parser->tree->shape_dimension_count = checkpoint.shape_dimension_count;
	parser->tree->retained_scope_count = checkpoint.retained_scope_count;
	parser->tree->retained_name_count = checkpoint.retained_name_count;
	if (parser->error) *parser->error = checkpoint.error;
}

static const pigen_expanded_token *token_at(const syntax_parser *parser,
	size_t at)
{
	return pigen_expanded_token_get(parser->expanded,
		(pigen_token_id){(uint32_t)at});
}

static pigen_source_span invalid_source_span(void)
{
	return (pigen_source_span){{PIGEN_INVALID_ID}, 0, 0};
}

static pigen_syntax_location range_location(const syntax_parser *parser,
	size_t first,
	size_t after)
{
	return pigen_syntax_location_from_extent(parser->expanded, first, after);
}

static int token_is(const syntax_parser *parser, size_t at, const char *text)
{
	const pigen_expanded_token *token = token_at(parser, at);
	const char *known;
	size_t length;

	if (!token) return 0;
	known = pigen_expanded_token_text(parser->expanded, token, &length);
	return known && length == strlen(text) && !memcmp(known, text, length);
}

static int token_text_equal(const syntax_parser *parser, size_t left,
	size_t right)
{
	const pigen_expanded_token *a = token_at(parser, left);
	const pigen_expanded_token *b = token_at(parser, right);
	const char *ta, *tb;
	size_t la, lb;

	if (!a || !b) return 0;
	ta = pigen_expanded_token_text(parser->expanded, a, &la);
	tb = pigen_expanded_token_text(parser->expanded, b, &lb);
	return ta && tb && la == lb && !memcmp(ta, tb, la);
}

static int remember_parameterized_module(syntax_parser *parser, size_t token)
{
	if (parser->parameterized_module_count ==
		parser->parameterized_module_capacity)
	{
		size_t capacity = parser->parameterized_module_capacity ?
			parser->parameterized_module_capacity * 2 : 8;
		pigen_token_id *names = pigen_resize(parser->parameterized_module_names,
			capacity * sizeof(*names));

		if (!names) return 0;
		parser->parameterized_module_names = names;
		parser->parameterized_module_capacity = capacity;
	}
	parser->parameterized_module_names[parser->parameterized_module_count++] =
		(pigen_token_id){(uint32_t)token};
	return 1;
}

static int remember_parameterized_instance(syntax_parser *parser,
	size_t module_token, size_t instance_token)
{
	if (parser->pending_parameterized_instance_count ==
		parser->pending_parameterized_instance_capacity)
	{
		size_t capacity = parser->pending_parameterized_instance_capacity ?
			parser->pending_parameterized_instance_capacity * 2 : 8;
		pending_parameterized_instance *instances = pigen_resize(
			parser->pending_parameterized_instances,
			capacity * sizeof(*instances));

		if (!instances) return 0;
		parser->pending_parameterized_instances = instances;
		parser->pending_parameterized_instance_capacity = capacity;
	}
	parser->pending_parameterized_instances[
		parser->pending_parameterized_instance_count++] =
		(pending_parameterized_instance){module_token, instance_token};
	return 1;
}

static void free_parser_state(syntax_parser *parser)
{
	free(parser->typedef_name_facts);
	free(parser->parameterized_module_names);
	free(parser->pending_parameterized_instances);
	free(parser->function_names);
}

static int tokens_equal(const syntax_parser *parser, pigen_token_id left,
	pigen_token_id right)
{
	const pigen_expanded_token *left_token = token_at(parser, left.index);
	const pigen_expanded_token *right_token = token_at(parser, right.index);
	const char *left_text;
	const char *right_text;
	size_t left_length;
	size_t right_length;

	if (!left_token || !right_token) return 0;
	left_text = pigen_expanded_token_text(parser->expanded, left_token,
		&left_length);
	right_text = pigen_expanded_token_text(parser->expanded, right_token,
		&right_length);
	return left_text && right_text && left_length == right_length &&
		!memcmp(left_text, right_text, left_length);
}
static int module_dynamic_name(syntax_parser *parser, pigen_syntax_id scope,
	pigen_token_id name, int protocol_only)
{
	const pigen_syntax_node *owner = pigen_syntax_get(parser->tree,scope);
	if (!owner) return 0;
	for (pigen_syntax_id child=owner->first_child; child.index!=PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node=pigen_syntax_get(parser->tree,child);
		if (!node) break;
		if (node->kind==PIGEN_SYNTAX_SIGNAL_DECLARATION)
		{
			int native=node->as.signal_declaration.protocol.family!=PIGEN_PROTOCOL_NONE;
			pigen_transfer_type transfer=node->as.signal_declaration.transfer_type.transfer_type;
			int buffered=node->as.signal_declaration.has_transfer_type &&
				(transfer==PIGEN_TRANSFER_TYPE_BUF || transfer==PIGEN_TRANSFER_TYPE_PORT ||
				 transfer==PIGEN_TRANSFER_TYPE_FIFO || transfer==PIGEN_TRANSFER_TYPE_SKID);
			for (pigen_syntax_id declarator=node->first_child; declarator.index!=PIGEN_INVALID_ID; )
			{
				const pigen_syntax_node *known=pigen_syntax_get(parser->tree,declarator);
				if (!known) break;
				if (known->kind==PIGEN_SYNTAX_SIGNAL_DECLARATOR && tokens_equal(parser,known->as.signal_declarator.name,name))
					return native || (!protocol_only && buffered);
				declarator=known->next_sibling;
			}
		}
		child=node->next_sibling;
	}
	return owner->kind==PIGEN_SYNTAX_MODULE ? 0 : module_dynamic_name(parser,owner->parent,name,protocol_only);
}

static int module_buffered_name(syntax_parser *parser, pigen_syntax_id module,
	pigen_token_id name)
{
	return module_dynamic_name(parser, module, name, 0);
}

static int process_has_protocol_reference(syntax_parser *parser,
	pigen_syntax_id module, size_t start, size_t end)
{
	for (size_t at = start + 1; at < end; at++)
	{
		const pigen_expanded_token *token = token_at(parser, at);
		if (token && token->kind == PIGEN_TOKEN_IDENTIFIER && module_dynamic_name(parser, module,
			(pigen_token_id){(uint32_t)at}, 1)) return 1;
	}
	return 0;
}

static int process_has_buffered_transfer(syntax_parser *parser,
	pigen_syntax_id module, size_t start, size_t end)
{
	for (size_t at = start + 1; at < end; at++)
	{
		const pigen_expanded_token *token = token_at(parser, at);

		if (!token || token->kind != PIGEN_TOKEN_IDENTIFIER) continue;
		if (module_buffered_name(parser, module,
			(pigen_token_id){(uint32_t)at}) &&
			token_is(parser, at + 1, "<="))
			return 1;
	}
	return 0;
}


static const typedef_name_fact *typedef_name_fact_in_scope(
	const syntax_parser *parser, pigen_token_id name, pigen_syntax_id scope)
{
	size_t i = parser->typedef_name_fact_count;

	while (i)
	{
		const typedef_name_fact *known = &parser->typedef_name_facts[--i];
		if (known->scope.index == scope.index &&
			tokens_equal(parser, known->name, name))
			return known;
	}
	return NULL;
}

static int structured_type_name_exists(void *context, pigen_token_id name)
{
	const structured_type_query *query = context;
	const typedef_name_fact *known;

	if (!query || !query->parser) return 0;
	known = typedef_name_fact_in_scope(query->parser, name, query->scope);
	if (!known && query->scope.index != 0)
		known = typedef_name_fact_in_scope(query->parser, name,
			(pigen_syntax_id){0});
	if (!known)
	{
		size_t i = query->parser->typedef_name_fact_count;

		while (i)
		{
			const typedef_name_fact *fact =
				&query->parser->typedef_name_facts[--i];
			if (tokens_equal(query->parser, fact->name, name))
			{
				known = fact;
				break;
			}
		}
	}
	return known && known->kind != TYPEDEF_NAME_OPAQUE_SHADOW;
}

static void add_typedef_name_fact(syntax_parser *parser,
	pigen_token_id name, pigen_syntax_id scope, typedef_name_fact_kind kind)
{
	if (parser->typedef_name_fact_count ==
		parser->typedef_name_fact_capacity)
	{
		parser->typedef_name_fact_capacity =
			parser->typedef_name_fact_capacity ?
			parser->typedef_name_fact_capacity * 2 : 8;
		parser->typedef_name_facts = pigen_resize(
			parser->typedef_name_facts,
			parser->typedef_name_fact_capacity *
				sizeof(*parser->typedef_name_facts));
	}
	parser->typedef_name_facts[parser->typedef_name_fact_count++] =
		(typedef_name_fact){name, scope, kind,PIGEN_PROTOCOL_NONE};
}

/* Retained declarations still introduce lexical value-name barriers. Parse
 * their type in temporary arenas, then inspect declarator names only. */
static int identifier(const syntax_parser *parser, size_t at);
static int retained_declaration_shadows(const syntax_parser *parser,const pigen_syntax_node *node,pigen_token_id name)
{
 if(node->kind!=PIGEN_SYNTAX_RETAINED) return 0;
 size_t first=node->location.extent.first.index,last=node->location.extent.after.index;
 int net=token_is(parser,first,"wire") || token_is(parser,first,"tri") || token_is(parser,first,"reg");
 int variable=token_is(parser,first,"logic") || token_is(parser,first,"bit") ||
  token_is(parser,first,"byte") || token_is(parser,first,"shortint") || token_is(parser,first,"int") ||
  token_is(parser,first,"integer") || token_is(parser,first,"longint") || token_is(parser,first,"time") ||
  token_is(parser,first,"real") || token_is(parser,first,"shortreal") || token_is(parser,first,"realtime");
 if(!net && !variable) return 0;
 size_t type_start=first+(net ? 1 : 0),declarator=type_start;
 pigen_syntax_expr_arena expressions={0};pigen_syntax_type_arena types={0};pigen_syntax_type_id type;
 int parsed=pigen_parse_type_prefix(parser->expanded,type_start,last,&expressions,&types,&type,&declarator,NULL);
 pigen_free_syntax_expr_arena(&expressions);pigen_free_syntax_type_arena(&types);
 if(!parsed) return 0;
 /* An implicit net's first identifier is its name rather than a data type. */
 if(net && !identifier(parser,declarator) && identifier(parser,type_start))
 {
  declarator=type_start;
  if(token_is(parser,declarator,"signed") || token_is(parser,declarator,"unsigned")) declarator++;
 }
 if(!identifier(parser,declarator)) return 0;
 size_t depth=0;int expect_name=1;
 for(size_t t=declarator;t<last;t++)
 {
  if(expect_name)
  {
   if(!identifier(parser,t)) return 0;
   if(tokens_equal(parser,(pigen_token_id){(uint32_t)t},name)) return 1;
   expect_name=0;continue;
  }
  if(token_is(parser,t,"[") || token_is(parser,t,"(") || token_is(parser,t,"{")) {depth++;continue;}
  if(token_is(parser,t,"]") || token_is(parser,t,")") || token_is(parser,t,"}")) {if(depth) depth--;continue;}
  if(!depth && token_is(parser,t,",")) expect_name=1;
 }
 return 0;
}

static int protocol_scope_value_shadows(const syntax_parser *parser,const pigen_syntax_node *owner,
 pigen_token_id name,size_t at)
{
 for(pigen_syntax_id id=owner->first_child;id.index!=PIGEN_INVALID_ID;)
  {
   const pigen_syntax_node *node=pigen_syntax_get(parser->tree,id);
   if(!node) break;
   if(node->location.extent.first.index<at)
   {
    if(retained_declaration_shadows(parser,node,name)) return 1;
    if(node->kind==PIGEN_SYNTAX_PARAMETER && tokens_equal(parser,node->as.parameter.name,name)) return 1;
    if(node->kind==PIGEN_SYNTAX_SIGNAL_DECLARATION)
     for(pigen_syntax_id item=node->first_child;item.index!=PIGEN_INVALID_ID;)
     {
      const pigen_syntax_node *declarator=pigen_syntax_get(parser->tree,item);
      if(!declarator) break;
      if(tokens_equal(parser,declarator->as.signal_declarator.name,name)) return 1;
      item=declarator->next_sibling;
     }
   }
   id=node->next_sibling;
  }
 return 0;
}

static size_t protocol_alias_after(const syntax_parser *parser,size_t at)
{return token_is(parser,at+1,"::") ? at+3 : at+1;}

static pigen_syntax_id structured_package_name(const syntax_parser *parser,pigen_token_id name,size_t at)
{
 for(pigen_syntax_id id=parser->tree->nodes[0].first_child;id.index!=PIGEN_INVALID_ID;)
 {
  const pigen_syntax_node *node=pigen_syntax_get(parser->tree,id);
  if(node->kind==PIGEN_SYNTAX_PACKAGE && node->location.extent.first.index<at && tokens_equal(parser,node->as.module.name,name)) return id;
  id=node->next_sibling;
 }
 return INVALID_SYNTAX;
}

static int native_import_at(const syntax_parser *parser,size_t at,size_t limit)
{
 if(!token_is(parser,at,"import") && !token_is(parser,at,"export")) return 0;
 for(size_t entry=at+1;entry+2<limit;)
 {
  if(!token_is(parser,entry+1,"::")) return 0;
  pigen_syntax_id package=structured_package_name(parser,(pigen_token_id){(uint32_t)entry},at);
  for(size_t i=0;i<parser->typedef_name_fact_count;i++)
  {
   const typedef_name_fact *fact=&parser->typedef_name_facts[i];
   if(fact->scope.index==package.index && fact->kind==TYPEDEF_NAME_PROTOCOL &&
    (token_is(parser,entry+2,"*") || tokens_equal(parser,fact->name,(pigen_token_id){(uint32_t)(entry+2)}))) return 1;
  }
  entry+=3;if(!token_is(parser,entry,",")) break;entry++;
 }
 return 0;
}

static const typedef_name_fact *imported_type_fact(const syntax_parser *parser,const pigen_syntax_node *owner,pigen_token_id name,size_t at)
{
 const typedef_name_fact *selected=NULL;
 for(pigen_syntax_id id=owner->first_child;id.index!=PIGEN_INVALID_ID;)
 {
  const pigen_syntax_node *item=pigen_syntax_get(parser->tree,id);
  if(item->kind==PIGEN_SYNTAX_IMPORT && item->as.package_import.is_structured && item->location.extent.first.index<at &&
     (item->as.package_import.wildcard || tokens_equal(parser,item->as.package_import.member,name)))
  {
   pigen_syntax_id package=structured_package_name(parser,item->as.package_import.package,at);
   const typedef_name_fact *fact=typedef_name_fact_in_scope(parser,name,package);
   if(fact)
   {
    if(!item->as.package_import.wildcard) return fact;
    if(!selected || fact->kind==TYPEDEF_NAME_PROTOCOL) selected=fact;
   }
  }
  id=item->next_sibling;
 }
 return selected;
}

static pigen_protocol_family protocol_alias_at(const syntax_parser *parser,pigen_syntax_id scope,size_t at)
{
 if(token_is(parser,at+1,"::"))
 {
  for(pigen_syntax_id lexical=scope;lexical.index!=PIGEN_INVALID_ID;)
  {
   const pigen_syntax_node *owner=pigen_syntax_get(parser->tree,lexical);
   if(!owner) break;
   if(protocol_scope_value_shadows(parser,owner,(pigen_token_id){(uint32_t)at},at)) return PIGEN_PROTOCOL_NONE;
   lexical=owner->parent;
  }
  for(pigen_syntax_id id=parser->tree->nodes[0].first_child;id.index!=PIGEN_INVALID_ID;)
  {
   const pigen_syntax_node *package=pigen_syntax_get(parser->tree,id);
   if(package->kind==PIGEN_SYNTAX_PACKAGE && package->location.extent.first.index<at &&
      tokens_equal(parser,package->as.module.name,(pigen_token_id){(uint32_t)at}))
   {
    const typedef_name_fact *fact=typedef_name_fact_in_scope(parser,(pigen_token_id){(uint32_t)(at+2)},id);
    return fact && fact->kind==TYPEDEF_NAME_PROTOCOL ? fact->family : PIGEN_PROTOCOL_NONE;
   }
   id=package->next_sibling;
  }
  return PIGEN_PROTOCOL_NONE;
 }
 pigen_token_id name={(uint32_t)at};
 for(size_t depth=0;scope.index!=PIGEN_INVALID_ID && depth<=parser->tree->node_count;depth++)
 {
  const pigen_syntax_node *owner=pigen_syntax_get(parser->tree,scope);
  if(!owner) break;
  const typedef_name_fact *known=typedef_name_fact_in_scope(parser,name,scope);
  if(known && known->name.index<at) return known->kind==TYPEDEF_NAME_PROTOCOL ? known->family : PIGEN_PROTOCOL_NONE;
  if(protocol_scope_value_shadows(parser,owner,name,at)) return PIGEN_PROTOCOL_NONE;
  const typedef_name_fact *imported=imported_type_fact(parser,owner,name,at);
  if(imported) return imported->kind==TYPEDEF_NAME_PROTOCOL ? imported->family : PIGEN_PROTOCOL_NONE;
  scope=owner->parent;
 }
 return PIGEN_PROTOCOL_NONE;
}

static pigen_syntax_type_ownership declaration_type_ownership(
	const syntax_parser *parser, pigen_syntax_id scope,
	pigen_syntax_type_id type)
{
	structured_type_query query = {parser, scope};
	const pigen_syntax_type *written=pigen_syntax_type_get(&parser->tree->types,type);
	if(written && written->has_package)
	{
	 for(pigen_syntax_id id=parser->tree->nodes[0].first_child;id.index!=PIGEN_INVALID_ID;)
	 {
	  const pigen_syntax_node *package=pigen_syntax_get(parser->tree,id);
	  if(package->kind==PIGEN_SYNTAX_PACKAGE && package->location.extent.first.index<written->package.index &&
	     tokens_equal(parser,package->as.module.name,written->package)) return PIGEN_SYNTAX_TYPE_STRUCTURED;
	  id=package->next_sibling;
	 }

	 return PIGEN_SYNTAX_TYPE_UNOWNED;
	}

	return pigen_syntax_type_declaration_ownership(parser->expanded,
		&parser->tree->types, type, structured_type_name_exists, &query);
}

static int identifier(const syntax_parser *parser, size_t at)
{
	const pigen_expanded_token *token = token_at(parser, at);
	return token && token->kind == PIGEN_TOKEN_IDENTIFIER;
}

static int fail(syntax_parser *parser, size_t at, const char *message)
{
	if (parser->error)
	{
		const pigen_expanded_token *token = token_at(parser, at);
		parser->error->origin = token ? token->origin :
			INVALID_ID(pigen_origin_id);
		parser->error->span = token ? pigen_origin_expansion_span(
			parser->expanded, token->origin) : invalid_source_span();
		parser->error->message = message;
	}
	return 0;
}

static pigen_syntax_id add_node(syntax_parser *parser, pigen_syntax_node node)
{
	pigen_syntax_id result;
	pigen_syntax_tree *tree = parser->tree;

	if (tree->node_count == PIGEN_INVALID_ID)
		pigen_fail("too many syntax nodes");
	if (tree->node_count == tree->node_capacity)
	{
		tree->node_capacity = tree->node_capacity ? tree->node_capacity * 2 : 32;
		tree->nodes = pigen_resize(tree->nodes,
			tree->node_capacity * sizeof(*tree->nodes));
	}
	result = (pigen_syntax_id){(uint32_t)tree->node_count};
	tree->nodes[tree->node_count++] = node;
	return result;
}

static void add_child(syntax_parser *parser, pigen_syntax_id parent,
	pigen_syntax_id child)
{
	pigen_syntax_node *owner = &parser->tree->nodes[parent.index];
	parser->tree->nodes[child.index].parent = parent;
	if (owner->last_child.index == PIGEN_INVALID_ID)
		owner->first_child = child;
	else
		parser->tree->nodes[owner->last_child.index].next_sibling = child;
	owner->last_child = child;
}

static void add_opaque_bytes(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t end);
/* Emit the bytes between the previous emitted item and the first byte of
 * token `start` as OPAQUE filler. Opaque gaps are byte-exact so the emitter's
 * monotonic-coverage gate sees one contiguous cover of the whole source; the
 * token-extent form left whitespace uncovered and let the module-tail span
 * overlap the last parsed process. The `end` cursor is retained for the
 * historical call shape; coverage is driven by the byte cursor. */
static void add_opaque(syntax_parser *parser, pigen_syntax_id parent,
	syntax_cursor start, syntax_cursor end)
{
	const pigen_expanded_token *token;
	size_t start_byte;

	/* The gap runs to the first byte of the next parsed item (`end`); `start`
	 * records the previous item's end for the historical call shape. */
	(void)start;
	if (end >= parser->expanded->token_count) return;
	token = &parser->expanded->tokens[end];
	if (token->kind == PIGEN_TOKEN_EOF)
		start_byte = parser->source_length;
	else
	{
		pigen_source_span expansion = pigen_origin_expansion_span(parser->expanded,
			token->origin);
		start_byte = expansion.start;
		const pigen_token_origin *origin = pigen_origin_get(parser->expanded, token->origin);
		const pigen_source_file *file = pigen_source_get(parser->expanded->sources,
			parser->expanded->root_source);
		/* Expansion spans begin at the macro name. Its invocation backtick
		 * belongs to the owned replacement, not the preceding opaque gap. */
		if (origin && origin->kind != PIGEN_ORIGIN_SOURCE && file &&
			expansion.source.index == parser->expanded->root_source.index &&
			start_byte && start_byte <= file->length && file->text[start_byte-1] == '`')
			start_byte--;
	}
	add_opaque_bytes(parser, parent, parser->opaque_byte_cursor, start_byte);
}

/* Advance the opaque byte cursor to the end of the last consumed token. */
static void advance_opaque_byte_cursor(syntax_parser *parser, size_t after)
{
	size_t end_byte;

	if (!after || after > parser->expanded->token_count) return;
	end_byte = pigen_origin_expansion_span(parser->expanded,
		parser->expanded->tokens[after - 1].origin).end;
	if (end_byte > parser->opaque_byte_cursor)
		parser->opaque_byte_cursor = end_byte;
}

/* Emit an OPAQUE node with a byte-extent span (start..end are byte offsets
 * in the source). Used to make the OPAQUE spans byte-contiguous across the
 * whole source, covering whitespace and trailing text that token-extent spans
 * miss. The emitter's monotonic-coverage gate requires OPAQUE spans to be
 * contiguous and reach the file length; token-extent spans cannot cover
 * whitespace between tokens or text after the last token. */
static void add_opaque_bytes(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t end)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id id;

	if (start >= end) return;
	node.kind = PIGEN_SYNTAX_OPAQUE;
	node.location.source_span = (pigen_source_span){
		parser->expanded->root_source, start, end};
	node.location.origin = parser->expanded->tokens[0].origin;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	id = add_node(parser, node);
	add_child(parser, parent, id);
}

static int transfer_type_at(const syntax_parser *parser, size_t at,
	pigen_transfer_type *transfer_type)
{
	const pigen_expanded_token *token = token_at(parser, at);
	const pigen_transfer_type_descriptor *descriptor;
	const char *spelling;
	size_t length;

	if (!token || token->kind != PIGEN_TOKEN_IDENTIFIER) return 0;
	spelling = pigen_expanded_token_text(parser->expanded, token, &length);
	if (!pigen_transfer_type_from_spelling(spelling, length, transfer_type))
		return 0;
	descriptor = pigen_transfer_type_descriptor_get(*transfer_type);
	return descriptor && descriptor->is_concrete;
}

static int systemverilog_prefix_transfer_type_at(
	const syntax_parser *parser, size_t at, pigen_transfer_type *transfer_type)
{
	const pigen_transfer_type_descriptor *descriptor;

	if (!transfer_type_at(parser, at, transfer_type)) return 0;
	descriptor = pigen_transfer_type_descriptor_get(*transfer_type);
	return descriptor && descriptor->is_systemverilog_prefix;
}

static size_t matching_bracket(const syntax_parser *parser, size_t open,
	size_t limit)
{
	size_t at;
	size_t depth = 0;
	for (at = open; at < limit; at++)
	{
		if (token_is(parser, at, "[")) depth++;
		else if (token_is(parser, at, "]") && !--depth) return at;
	}
	return limit;
}

static int add_shape_dimension(syntax_parser *parser, size_t open,
	size_t close)
{
	pigen_syntax_tree *tree = parser->tree;
	pigen_syntax_shape_dimension dimension = {0};
	size_t colon = open + 1;
	size_t depth = 0;

	for (; colon < close; colon++)
	{
		if (token_is(parser, colon, "(") || token_is(parser, colon, "[") ||
			token_is(parser, colon, "{")) depth++;
		else if (token_is(parser, colon, ")") ||
			token_is(parser, colon, "]") || token_is(parser, colon, "}")) depth--;
		else if (!depth && token_is(parser, colon, ":")) break;
	}
	if (colon == open + 1 || colon + 1 == close)
		return fail(parser, open, "signal dimension requires an expression");
	dimension.location = range_location(parser, open, close + 1);
	if (colon == close)
	{
		dimension.form = PIGEN_SYNTAX_SHAPE_DIMENSION_COUNT;
		if (!pigen_parse_expression(parser->expanded, open + 1, close,
			&tree->expressions, &tree->types, &dimension.as.count,
			parser->error)) return 0;
	}
	else
	{
		dimension.form = PIGEN_SYNTAX_SHAPE_DIMENSION_RANGE;
		if (!pigen_parse_expression(parser->expanded, open + 1, colon,
			&tree->expressions, &tree->types, &dimension.as.range.left,
			parser->error) ||
			!pigen_parse_expression(parser->expanded, colon + 1, close,
				&tree->expressions, &tree->types, &dimension.as.range.right,
				parser->error))
			return 0;
	}
	if (tree->shape_dimension_count == tree->shape_dimension_capacity)
	{
		tree->shape_dimension_capacity = tree->shape_dimension_capacity ?
			tree->shape_dimension_capacity * 2 : 16;
		tree->shape_dimensions = pigen_resize(tree->shape_dimensions,
			tree->shape_dimension_capacity * sizeof(*tree->shape_dimensions));
	}
	tree->shape_dimensions[tree->shape_dimension_count++] = dimension;
	return 1;
}

static size_t top_level_token(const syntax_parser *parser,size_t first,size_t after,const char *wanted);

static pigen_syntax_id add_signal_declarator(syntax_parser *parser,
	pigen_syntax_id declaration, size_t name, size_t limit, size_t *after)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id id;
	size_t at = name + 1;

	node.kind = PIGEN_SYNTAX_SIGNAL_DECLARATOR;
	node.as.signal_declarator.first_shape_dimension =
		parser->tree->shape_dimension_count;
	while (at < limit && token_is(parser, at, "["))
	{
		size_t close = matching_bracket(parser, at, limit);
		if (close == limit)
		{
			fail(parser, at, "unterminated signal dimension");
			return INVALID_SYNTAX;
		}
		if (!add_shape_dimension(parser, at, close)) return INVALID_SYNTAX;
		node.as.signal_declarator.dimension_count++;
		at = close + 1;
	}
 if(parser->combinational_body && at<limit && token_is(parser,at,"="))
 {
  size_t end=top_level_token(parser,at+1,limit,",");
  if(!pigen_parse_expression(parser->expanded,at+1,end,&parser->tree->expressions,
    &parser->tree->types,&node.as.signal_declarator.initializer,parser->error)) return INVALID_SYNTAX;
  node.as.signal_declarator.has_initializer=1;at=end;
 }
	node.location = range_location(parser, name, at);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.signal_declarator.name = (pigen_token_id){(uint32_t)name};
	id = add_node(parser, node);
	add_child(parser, declaration, id);
	*after = at;
	return id;
}

static int declarator_candidate(const syntax_parser *parser, size_t start,
	size_t after)
{
	size_t at = start;

	if (!identifier(parser, at)) return 0;
	at++;
	while (at < after && token_is(parser, at, "["))
	{
		size_t close = matching_bracket(parser, at, after);
		if (close == after) return 0;
		at = close + 1;
	}
	return at == after;
}

typedef enum {
	DECLARATION_NOT_RECOGNIZED,
	DECLARATION_RECOGNIZED,
	DECLARATION_INVALID
} declaration_parse_result;

static pigen_syntax_direction parse_direction(const syntax_parser *parser,
	size_t *at)
{
	if (token_is(parser, *at, "input"))
	{
		(*at)++;
		return PIGEN_DIRECTION_INPUT;
	}
	if (token_is(parser, *at, "output"))
	{
		(*at)++;
		return PIGEN_DIRECTION_OUTPUT;
	}
	if (token_is(parser, *at, "inout"))
	{
		(*at)++;
		return PIGEN_DIRECTION_INOUT;
	}
	return PIGEN_DIRECTION_INTERNAL;
}

static int leading_transfer_first_declaration(syntax_parser *parser,
	pigen_syntax_id scope, size_t transfer_at, size_t limit)
{
	const pigen_transfer_type_descriptor *descriptor;
	pigen_transfer_type transfer_type;
	pigen_syntax_type_id data_type;
	pigen_syntax_type_ownership ownership;
	syntax_checkpoint checkpoint;
	size_t name;
	int result = 0;

	if (!transfer_type_at(parser, transfer_at, &transfer_type)) return 0;
	descriptor = pigen_transfer_type_descriptor_get(transfer_type);
	if (!descriptor || descriptor->is_static) return 0;
	checkpoint = save_checkpoint(parser);
	if (pigen_parse_type_prefix(parser->expanded, transfer_at + 1, limit,
		&parser->tree->expressions, &parser->tree->types, &data_type, &name,
		parser->error))
	{
		ownership = declaration_type_ownership(parser, scope, data_type);
		result = ownership != PIGEN_SYNTAX_TYPE_UNOWNED &&
			identifier(parser, name);
	}
	restore_checkpoint(parser, checkpoint);
	return result;
}

static pigen_protocol_family protocol_at(const syntax_parser *parser, size_t at)
{
	const pigen_expanded_token *token = pigen_expanded_token_get(parser->expanded,
		(pigen_token_id){(uint32_t)at});
	size_t length = 0;
	const char *text = token ? pigen_expanded_token_text(parser->expanded, token, &length) : NULL;
	pigen_protocol_family family;
	if (pigen_protocol_family_from_spelling(text, length, &family)) return family;
	return PIGEN_PROTOCOL_NONE;
}

static int parse_protocol_occurrence(syntax_parser *parser, size_t *at,
	size_t limit, pigen_syntax_protocol_occurrence *protocol)
{
	static const char *const options[] = {
		"addr", "id", "dest", "user", "awuser", "wuser", "buser", "aruser", "ruser",
		"keep", "strb", "last", "clock", "reset", "resetn"
	};
	size_t start = *at;
	protocol->family = protocol_at(parser, *at);
	*at=protocol_alias_after(parser,*at);
	for (size_t i = 0; i < PIGEN_PROTOCOL_OPTION_COUNT; i++)
		protocol->options[i] = INVALID_ID(pigen_syntax_expr_id);
	if (token_is(parser, *at, "["))
	{
		size_t close = matching_bracket(parser, *at, limit);
		if (close == limit) return fail(parser, *at, "unterminated AXI configuration");
		(*at)++;
		if (*at == close) return fail(parser, *at, "AXI configuration requires named options");
		while (*at < close)
		{
			size_t option;
			for (option = 0; option < PIGEN_PROTOCOL_OPTION_COUNT; option++)
				if (token_is(parser, *at, options[option])) break;
			if (option == PIGEN_PROTOCOL_OPTION_COUNT)
				return fail(parser, *at, "unknown AXI configuration option");
			if (protocol->present & (1u << option))
				return fail(parser, *at, "duplicate AXI configuration option");
			protocol->present |= 1u << option;
			if (!token_is(parser, *at + 1, "="))
				return fail(parser, *at + 1, "AXI option requires `=` and a constant expression");
			*at += 2;
			size_t first = *at, depth = 0;
			for (; *at < close; (*at)++)
			{
				if (token_is(parser, *at, "(") || token_is(parser, *at, "[") ||
					token_is(parser, *at, "{")) depth++;
				else if (token_is(parser, *at, ")") || token_is(parser, *at, "]") ||
					token_is(parser, *at, "}")) { if (depth) depth--; }
				else if (!depth && token_is(parser, *at, ",")) break;
			}
			if (first == *at) return fail(parser, *at, "AXI option requires a constant expression");
			if (!pigen_parse_expression(parser->expanded, first, *at,
				&parser->tree->expressions, &parser->tree->types,
				&protocol->options[option], parser->error)) return 0;
			if (*at < close && ++*at == close)
				return fail(parser, *at, "AXI configuration cannot end with a comma");
		}
		*at = close + 1;
	}
	protocol->location = range_location(parser, start, *at);
	return 1;
}

static int protocol_alias_construct_at(const syntax_parser *parser,pigen_syntax_id scope,size_t at,size_t last)
{
 if(native_import_at(parser,at,last)) return 1;
 if(protocol_alias_at(parser,scope,at)==PIGEN_PROTOCOL_NONE) return 0;
 size_t next=protocol_alias_after(parser,at);
 if(token_is(parser,next,"'") && token_is(parser,next+1,"(")) return 1;
 while(next<last && token_is(parser,next,"["))
 {
  size_t close=matching_bracket(parser,next,last);
  if(close==last) return 1;
  next=close+1;
 }
 pigen_transfer_type transfer;
 if(transfer_type_at(parser,next,&transfer)) return 1;
 return next<last && identifier(parser,next) && !token_is(parser,next,"inside") && !token_is(parser,next,"dist");
}

static int parse_transfer_type_occurrence(syntax_parser *parser, size_t *at,
	size_t limit, pigen_syntax_transfer_type_occurrence *occurrence)
{
	pigen_transfer_type transfer_type;
	const pigen_transfer_type_descriptor *descriptor;
	size_t keyword = *at;

	if (!transfer_type_at(parser, keyword, &transfer_type)) return 0;
	descriptor = pigen_transfer_type_descriptor_get(transfer_type);
	if (!descriptor) return 0;
	occurrence->transfer_type = transfer_type;
	occurrence->location = range_location(parser, keyword, keyword + 1);
	occurrence->argument = INVALID_ID(pigen_syntax_expr_id);
	*at = keyword + 1;
	if (descriptor->parameter == PIGEN_TRANSFER_PARAMETER_NONE)
	{
		if (token_is(parser, *at, "["))
			return fail(parser, *at,
				"transfer type does not accept an argument");
		return 1;
	}
	if (descriptor->parameter == PIGEN_TRANSFER_PARAMETER_DEPTH)
	{
		size_t close;
		if (!token_is(parser, *at, "["))
			return fail(parser, keyword,
				"transfer type requires a depth argument");
		close = matching_bracket(parser, *at, limit);
		if (close == limit)
			return fail(parser, *at, "unterminated transfer argument");
		if (close == *at + 1)
			return fail(parser, *at, "transfer depth requires an expression");
		if (!pigen_parse_expression(parser->expanded, *at + 1, close,
			&parser->tree->expressions, &parser->tree->types,
			&occurrence->argument, parser->error)) return 0;
		*at = close + 1;
		if (token_is(parser, *at, "["))
			return fail(parser, *at,
				"transfer type accepts exactly one argument");
		return 1;
	}
	return fail(parser, keyword, "unsupported transfer parameter form");
}

static int append_declarators(syntax_parser *parser,
	pigen_syntax_id declaration, size_t start, size_t limit)
{
	size_t at = start;

	while (at < limit)
	{
		if (!identifier(parser, at))
			return fail(parser, at, "expected signal name");
		if (add_signal_declarator(parser, declaration, at, limit,
			&at).index == PIGEN_INVALID_ID) return 0;
		if (at == limit) return 1;
		if (!token_is(parser, at, ","))
			return fail(parser, at,
				"expected `,` or declaration terminator after signal name");
		at++;
	}
	return fail(parser, at, "declaration requires a signal name");
}

static pigen_syntax_id add_declaration_node(syntax_parser *parser,
	pigen_syntax_direction direction, pigen_syntax_type_id data_type)
{
	pigen_syntax_node node = {0};

	node.kind = PIGEN_SYNTAX_SIGNAL_DECLARATION;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.signal_declaration.direction = direction;
	node.as.signal_declaration.data_type = data_type;
	node.as.signal_declaration.transfer_type.argument =
		INVALID_ID(pigen_syntax_expr_id);
	return add_node(parser, node);
}

static declaration_parse_result parse_systemverilog_static_declaration(
	syntax_parser *parser, pigen_syntax_id scope, size_t start, size_t limit,
	pigen_syntax_id *declaration)
{
	size_t at = start;
	size_t name;
	size_t parsed_after;
	pigen_syntax_direction direction = parse_direction(parser, &at);
	pigen_syntax_type_id data_type;
	pigen_transfer_type prefix_transfer_type;
	size_t prefix_transfer_at = at;
	int has_prefix_transfer = systemverilog_prefix_transfer_type_at(parser, at,
		&prefix_transfer_type);
	int has_static_prefix = has_prefix_transfer || token_is(parser, at, "logic") ||
		token_is(parser, at, "bit") || token_is(parser, at, "byte") ||
		token_is(parser, at, "integer") ||
		(parser->combinational_body && token_is(parser,at,"int") && !token_is(parser,at+1,"[")) ||
		token_is(parser, at, "shortint") ||
		token_is(parser, at, "longint") ||
		token_is(parser, at, "time") || token_is(parser, at, "signed") ||
		token_is(parser, at, "unsigned") || token_is(parser, at, "[") ||
		(direction != PIGEN_DIRECTION_INTERNAL &&
			declarator_candidate(parser, at, limit));
	pigen_syntax_id id;

	if (!has_static_prefix) return DECLARATION_NOT_RECOGNIZED;
	if (has_prefix_transfer)
	{
		at++;
		if (declarator_candidate(parser, at, limit))
		{
			name = at;
			if (!pigen_parse_type_prefix(parser->expanded, at, at,
				&parser->tree->expressions, &parser->tree->types, &data_type,
				&parsed_after, parser->error))
				return DECLARATION_NOT_RECOGNIZED;
		}
		else
		{
			if (!pigen_parse_type_prefix(parser->expanded, at, limit,
				&parser->tree->expressions, &parser->tree->types, &data_type,
				&name, parser->error)) return DECLARATION_NOT_RECOGNIZED;
		}
	}
	else
	{
		if (direction != PIGEN_DIRECTION_INTERNAL &&
			declarator_candidate(parser, at, limit))
		{
			name = at;
			if (!pigen_parse_type_prefix(parser->expanded, at, at,
				&parser->tree->expressions, &parser->tree->types, &data_type,
				&parsed_after, parser->error))
				return DECLARATION_NOT_RECOGNIZED;
		}
		else if (!pigen_parse_type_prefix(parser->expanded, at, limit,
			&parser->tree->expressions, &parser->tree->types, &data_type, &name,
			parser->error)) return DECLARATION_NOT_RECOGNIZED;
		if (transfer_type_at(parser, name, &prefix_transfer_type) &&
			declaration_type_ownership(parser, scope, data_type) ==
				PIGEN_SYNTAX_TYPE_PIGEN)
			return DECLARATION_NOT_RECOGNIZED;
	}
	if (!identifier(parser, name)) return DECLARATION_NOT_RECOGNIZED;
	id = add_declaration_node(parser, direction, data_type);
	if (has_prefix_transfer)
	{
		parser->tree->nodes[id.index].as.signal_declaration.has_transfer_type = 1;
		parser->tree->nodes[id.index].as.signal_declaration.transfer_type.
			transfer_type = prefix_transfer_type;
		parser->tree->nodes[id.index].as.signal_declaration.transfer_type.location =
			range_location(parser, prefix_transfer_at, prefix_transfer_at + 1);
	}
	if (!append_declarators(parser, id, name, limit))
		return DECLARATION_NOT_RECOGNIZED;
	*declaration = id;
	return DECLARATION_RECOGNIZED;
}

static declaration_parse_result parse_data_first_declaration(
	syntax_parser *parser, pigen_syntax_id scope, size_t start, size_t limit,
	int ansi,
	pigen_syntax_id *declaration, int *decisive)
{
	size_t at = start;
	size_t type_start;
	pigen_syntax_direction direction = parse_direction(parser, &at);
	pigen_syntax_type_id data_type;
	pigen_syntax_transfer_type_occurrence transfer = {0};
	pigen_syntax_protocol_occurrence protocol = {0};
	pigen_transfer_type transfer_type;
	const pigen_transfer_type_descriptor *descriptor;
	pigen_syntax_type_ownership ownership;
	pigen_syntax_id id;
	int has_transfer_type = 0;

	*decisive = 0;
	type_start = at;
	pigen_protocol_family alias=protocol_alias_at(parser,scope,type_start);
	if (!pigen_parse_type_prefix(parser->expanded, type_start,alias!=PIGEN_PROTOCOL_NONE ? protocol_alias_after(parser,type_start) : limit,
		&parser->tree->expressions, &parser->tree->types, &data_type, &at,
		parser->error)) return DECLARATION_NOT_RECOGNIZED;
	ownership = declaration_type_ownership(parser, scope, data_type);
	*decisive = ownership == PIGEN_SYNTAX_TYPE_PIGEN ||
		ownership == PIGEN_SYNTAX_TYPE_STRUCTURED;
	if(alias!=PIGEN_PROTOCOL_NONE)
	{
		*decisive=1;at=type_start;
		if(!parse_protocol_occurrence(parser,&at,limit,&protocol)) return DECLARATION_INVALID;
		protocol.family=alias;
		if(protocol_at(parser,at)!=PIGEN_PROTOCOL_NONE)
		{fail(parser,at,"native AXI alias already includes its protocol");return DECLARATION_INVALID;}
		if(ansi && direction==PIGEN_DIRECTION_INTERNAL)
		{fail(parser,start,"ANSI AXI endpoint requires `input` or `output`");return DECLARATION_INVALID;}
	}
	else if (protocol_at(parser, at) != PIGEN_PROTOCOL_NONE)
	{
		*decisive = 1;
		if (ansi && direction == PIGEN_DIRECTION_INTERNAL)
		{
			fail(parser, start, "ANSI AXI endpoint requires `input` or `output`");
			return DECLARATION_INVALID;
		}
		if (!parse_protocol_occurrence(parser, &at, limit, &protocol))
			return DECLARATION_INVALID;
	}
	if (transfer_type_at(parser, at, &transfer_type))
	{
		descriptor = pigen_transfer_type_descriptor_get(transfer_type);
		*decisive = 1;
		has_transfer_type = 1;
		if (ansi && direction == PIGEN_DIRECTION_INTERNAL && descriptor &&
			!descriptor->is_static)
		{
			fail(parser, start,
				"ANSI dynamic signal port requires `input` or `output`");
			return DECLARATION_INVALID;
		}
		if (!parse_transfer_type_occurrence(parser, &at, limit, &transfer))
			return DECLARATION_INVALID;
	}
	if (!identifier(parser, at))
	{
		if (*decisive)
		{
			fail(parser, at, "declaration requires a signal name");
			return DECLARATION_INVALID;
		}
		return DECLARATION_NOT_RECOGNIZED;
	}
	if (!*decisive) return DECLARATION_NOT_RECOGNIZED;
	id = add_declaration_node(parser, direction, data_type);
	parser->tree->nodes[id.index].as.signal_declaration.protocol = protocol;
	if (has_transfer_type)
	{
		parser->tree->nodes[id.index].as.signal_declaration.has_transfer_type = 1;
		parser->tree->nodes[id.index].as.signal_declaration.transfer_type = transfer;
	}
	if (!append_declarators(parser, id, at, limit))
		return *decisive ? DECLARATION_INVALID : DECLARATION_NOT_RECOGNIZED;
	*declaration = id;
	return DECLARATION_RECOGNIZED;
}

static void commit_declaration(syntax_parser *parser, pigen_syntax_id module,
	size_t start, size_t declaration_after, syntax_cursor *opaque_cursor,
	pigen_syntax_id declaration)
{
	const pigen_expanded_token *decl_tok;
	const pigen_expanded_token *start_tok;
	size_t opaque_start;
	size_t opaque_end;

	parser->tree->nodes[declaration.index].location = range_location(parser,
		start, declaration_after);
	/* Emit the OPAQUE span between the previous item's end (byte cursor) and
	 * this declaration's start, using byte-extent so whitespace is covered. */
	decl_tok = pigen_expanded_token_get(parser->expanded,
		(pigen_token_id){(uint32_t)(declaration_after - 1)});
	start_tok = pigen_expanded_token_get(parser->expanded,
		(pigen_token_id){(uint32_t)start});
	opaque_start = parser->opaque_byte_cursor;
	opaque_end = start_tok ?
		pigen_origin_expansion_span(parser->expanded,
			start_tok->origin).start : parser->opaque_byte_cursor;
	if (start_tok && decl_tok)
		parser->opaque_byte_cursor = pigen_origin_expansion_span(
			parser->expanded, decl_tok->origin).end;
	add_opaque_bytes(parser, module, opaque_start, opaque_end);
	add_child(parser, module, declaration);
	*opaque_cursor = declaration_after;
}

static int decisive_typedef_without_terminator(syntax_parser *parser,
	pigen_syntax_id parent, size_t start, size_t boundary)
{
	syntax_checkpoint checkpoint = save_checkpoint(parser);
	pigen_syntax_type_id type;
	pigen_syntax_type_ownership ownership;
	size_t name;
	int decisive = 0;

	if (pigen_parse_type_prefix(parser->expanded, start + 1, boundary,
		&parser->tree->expressions, &parser->tree->types, &type, &name,
		parser->error))
	{
		ownership = declaration_type_ownership(parser, parent, type);
		decisive = (ownership == PIGEN_SYNTAX_TYPE_PIGEN ||
			ownership == PIGEN_SYNTAX_TYPE_STRUCTURED) &&
			identifier(parser, name) && name + 1 == boundary;
	}
	restore_checkpoint(parser, checkpoint);
	return decisive;
}

static declaration_parse_result parse_typedef(syntax_parser *parser,
	pigen_syntax_id parent,
	size_t start, size_t semicolon, syntax_cursor *opaque_cursor)
{
	syntax_checkpoint checkpoint = save_checkpoint(parser);
	pigen_syntax_type_id type;
	pigen_syntax_type_ownership ownership;
	pigen_syntax_location declaration = range_location(parser, start,
		semicolon + 1);
	pigen_syntax_node node = {0};
	pigen_syntax_id id;
	size_t name;
	pigen_syntax_protocol_occurrence protocol={0};
	pigen_protocol_family alias=protocol_alias_at(parser,parent,start+1);
	if (!pigen_parse_type_prefix(parser->expanded, start + 1,alias!=PIGEN_PROTOCOL_NONE ? protocol_alias_after(parser,start+1) : semicolon,
		&parser->tree->expressions, &parser->tree->types, &type, &name,
		parser->error)) return DECLARATION_INVALID;
	if(alias!=PIGEN_PROTOCOL_NONE)
	{
		name=start+1;
		if(!parse_protocol_occurrence(parser,&name,semicolon,&protocol)) return DECLARATION_INVALID;
		protocol.family=alias;
	}
	else if(protocol_at(parser,name)!=PIGEN_PROTOCOL_NONE && !parse_protocol_occurrence(parser,&name,semicolon,&protocol))
		return DECLARATION_INVALID;
	ownership = protocol.family!=PIGEN_PROTOCOL_NONE ? PIGEN_SYNTAX_TYPE_STRUCTURED : declaration_type_ownership(parser, parent, type);
	if (ownership == PIGEN_SYNTAX_TYPE_UNOWNED)
	{
		restore_checkpoint(parser, checkpoint);
		if (declarator_candidate(parser, name, semicolon))
			add_typedef_name_fact(parser, (pigen_token_id){(uint32_t)name},
				parent, TYPEDEF_NAME_OPAQUE_SHADOW);
		return DECLARATION_NOT_RECOGNIZED;
	}
	if (!identifier(parser, name))
	{
		fail(parser, name, "typedef requires a name");
		return DECLARATION_INVALID;
	}
	if (name + 1 != semicolon)
	{
		if (ownership == PIGEN_SYNTAX_TYPE_SYSTEMVERILOG)
		{
			int introduced_name = declarator_candidate(parser, name, semicolon);

			restore_checkpoint(parser, checkpoint);
			if (introduced_name)
				add_typedef_name_fact(parser,
					(pigen_token_id){(uint32_t)name}, parent,
					TYPEDEF_NAME_OPAQUE_SHADOW);
			return DECLARATION_NOT_RECOGNIZED;
		}
		fail(parser, name + 1, "typedef permits exactly one name");
		return DECLARATION_INVALID;
	}
	add_opaque(parser, parent, *opaque_cursor, start);
	node.kind = PIGEN_SYNTAX_TYPEDEF;
	node.location = declaration;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.type_definition.name = (pigen_token_id){(uint32_t)name};
	node.as.type_definition.type = type;
	node.as.type_definition.protocol=protocol;
	id = add_node(parser, node);
	add_child(parser, parent, id);
	add_typedef_name_fact(parser, node.as.type_definition.name, parent,
		protocol.family!=PIGEN_PROTOCOL_NONE ? TYPEDEF_NAME_PROTOCOL : TYPEDEF_NAME_ELIGIBLE);
	parser->typedef_name_facts[parser->typedef_name_fact_count-1].family=protocol.family;
	advance_opaque_byte_cursor(parser, semicolon + 1);
	*opaque_cursor = semicolon + 1;
	return DECLARATION_RECOGNIZED;
}

static int block_opener(const syntax_parser *parser, size_t at)
{
	return token_is(parser, at, "begin") || token_is(parser, at, "case") ||
		token_is(parser, at, "casex") || token_is(parser, at, "casez") ||
		token_is(parser, at, "function") || token_is(parser, at, "task") ||
		token_is(parser, at, "generate") || token_is(parser, at, "class");
}

static int block_closer(const syntax_parser *parser, size_t at)
{
	return token_is(parser, at, "end") || token_is(parser, at, "endcase") ||
		token_is(parser, at, "endfunction") || token_is(parser, at, "endtask") ||
		token_is(parser, at, "endgenerate") || token_is(parser, at, "endclass");
}

static size_t matching_parenthesis(const syntax_parser *parser, size_t open,
	size_t limit)
{
	size_t at;
	size_t depth = 0;

	for (at = open; at < limit; at++)
	{
		if (token_is(parser, at, "(")) depth++;
		else if (token_is(parser, at, ")") && !--depth) return at;
	}
	return limit;
}

static size_t matching_begin_end(const syntax_parser *parser, size_t begin,
	size_t limit)
{
	size_t at;
	size_t depth = 0;

	for (at = begin; at < limit; at++)
	{
		if (token_is(parser, at, "begin")) depth++;
		else if ((token_is(parser, at, "end") || token_is(parser, at, "endfabric") || token_is(parser, at, "endpipeline")) && !--depth) return at;
	}
	return limit;
}

static size_t ansi_item_end(const syntax_parser *parser, size_t start,
	size_t after)
{
	size_t at;
	size_t parens = 0;
	size_t brackets = 0;
	size_t braces = 0;

	for (at = start; at < after; at++)
	{
		if (token_is(parser, at, "(")) parens++;
		else if (token_is(parser, at, ")")) parens--;
		else if (token_is(parser, at, "[")) brackets++;
		else if (token_is(parser, at, "]")) brackets--;
		else if (token_is(parser, at, "{")) braces++;
		else if (token_is(parser, at, "}")) braces--;
		else if (!parens && !brackets && !braces && token_is(parser, at, ","))
			break;
	}
	return at;
}

static size_t top_level_token(const syntax_parser *parser, size_t start,
	size_t after, const char *wanted)
{
	size_t at;
	size_t parens = 0;
	size_t brackets = 0;
	size_t braces = 0;

	for (at = start; at < after; at++)
	{
		if (!parens && !brackets && !braces && token_is(parser, at, wanted))
			return at;
		if (token_is(parser, at, "(")) parens++;
		else if (token_is(parser, at, ")") && parens) parens--;
		else if (token_is(parser, at, "[")) brackets++;
		else if (token_is(parser, at, "]") && brackets) brackets--;
		else if (token_is(parser, at, "{")) braces++;
		else if (token_is(parser, at, "}") && braces) braces--;
	}
	return after;
}

static int simple_parameter_shape(const syntax_parser *parser, size_t start,
	size_t after, int has_keyword, size_t *name, size_t *equals)
{
	size_t at = start + (has_keyword ? 1 : 0);
	size_t equal = top_level_token(parser, at, after, "=");

	if (equal == after || equal + 1 == after || at + 1 != equal ||
		!identifier(parser, at))
		return 0;
	*name = at;
	*equals = equal;
	return 1;
}

static int parse_parameter(syntax_parser *parser, pigen_syntax_id module,
	size_t start, size_t after, int has_keyword, int is_local,
	syntax_cursor *opaque_cursor)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id id;
	size_t name;
	size_t equals;

	{
		size_t at = start + (has_keyword ? 1 : 0);
		size_t equal = top_level_token(parser, at, after, "=");
		pigen_syntax_type_id typed = INVALID_ID(pigen_syntax_type_id);
		size_t typed_name = 0;

		if (equal != after &&
			pigen_parse_type_prefix(parser->expanded, at, equal,
				&parser->tree->expressions, &parser->tree->types, &typed,
				&typed_name, parser->error) &&
			identifier(parser, typed_name) && typed_name > at &&
			typed_name + 1 == equal)
		{
			name = typed_name;
			equals = equal;
			node.as.parameter.has_type = 1;
			node.as.parameter.data_type = typed;
		}
		else if (!simple_parameter_shape(parser, start, after, has_keyword,
			&name, &equals))
			return 0;
	}
	add_opaque(parser, module, *opaque_cursor, start);
	node.kind = PIGEN_SYNTAX_PARAMETER;
	node.location = range_location(parser, start, after);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.parameter.name = (pigen_token_id){(uint32_t)name};
	node.as.parameter.is_local = is_local;
	if (!pigen_parse_expression(parser->expanded, equals + 1, after,
		&parser->tree->expressions, &parser->tree->types, &node.as.parameter.value, parser->error))
		return 0;
	id = add_node(parser, node);
	add_child(parser, module, id);
	const pigen_syntax_node *owner=pigen_syntax_get(parser->tree,module);
	size_t cursor_after=after;
	if(owner && owner->kind==PIGEN_SYNTAX_PACKAGE && (token_is(parser,after,",") || token_is(parser,after,";"))) cursor_after++;
	advance_opaque_byte_cursor(parser, cursor_after);
	*opaque_cursor = cursor_after;
	return 1;
}

static int parse_parameter_items(syntax_parser *parser,
	pigen_syntax_id module, size_t first, size_t after,
	syntax_cursor *opaque_cursor)
{
	size_t at = first;
	int continuation = 0;
	int continuation_is_local = 0;

	while (at < after)
	{
		size_t item_end = ansi_item_end(parser, at, after);
		int has_keyword = token_is(parser, at, "parameter") ||
			token_is(parser, at, "localparam");
		int is_local = has_keyword ? token_is(parser, at, "localparam") :
			continuation_is_local;
		size_t ignored_name;
		size_t ignored_equals;
		int has_equal = top_level_token(parser, at, item_end, "=") !=
			item_end;
		int shaped = ((has_keyword && has_equal) ||
			(continuation && simple_parameter_shape(parser, at, item_end,
				has_keyword, &ignored_name, &ignored_equals)));

		if (shaped)
		{
			if (!parse_parameter(parser, module, at, item_end, has_keyword,
				is_local, opaque_cursor)) return 0;
			continuation = 1;
			continuation_is_local = is_local;
		}
		else
		{
			continuation = 0;
			continuation_is_local = 0;
		}
		at = item_end < after ? item_end + 1 : after;
	}
	return 1;
}

/* A procedural constant declaration shares its explicit type across comma
 * declarators. Each value still has its own source-order semantic owner. */
static int parse_procedural_parameters(syntax_parser *parser,
	pigen_syntax_id parent, size_t first, size_t end, syntax_cursor *cursor)
{
	size_t at = first;
	int has_type = 0;
	pigen_syntax_type_id data_type = INVALID_ID(pigen_syntax_type_id);
	int is_local = token_is(parser, first, "localparam");
	while (at < end)
	{
		size_t next = top_level_token(parser, at, end, ",");
		if (!parse_parameter(parser, parent, at, next, at == first,
			is_local, cursor)) return 0;
		pigen_syntax_id id = parser->tree->nodes[parent.index].last_child;
		pigen_syntax_node *node = &parser->tree->nodes[id.index];
		if (at == first)
		{
			has_type = node->as.parameter.has_type;
			data_type = node->as.parameter.data_type;
		}
		else
		{
			/* A new type requires a new declaration, not another declarator. */
			if (node->as.parameter.has_type) return 0;
			node->as.parameter.has_type = has_type;
			node->as.parameter.data_type = data_type;
		}
		if (next == end) return 1;
		at = next + 1;
	}
	return 0;
}

static int parse_ansi_ports(syntax_parser *parser,
	pigen_syntax_id module, size_t first, size_t after,
	syntax_cursor *opaque_cursor)
{
	size_t at = first;

	while (at < after)
	{
		size_t item_end = ansi_item_end(parser, at, after);
		size_t declaration_after = item_end;
		size_t continuation = item_end;
		size_t type_at = at;
		int has_direction = token_is(parser, at, "input") ||
			token_is(parser, at, "output") || token_is(parser, at, "inout");
		syntax_checkpoint checkpoint;
		declaration_parse_result result;
		pigen_syntax_id declaration = INVALID_SYNTAX;
		int decisive = 0;

		if (has_direction) type_at++;
		if (leading_transfer_first_declaration(parser, module, type_at, item_end))
			return fail(parser, type_at,
				"data type must precede the transfer type");
		while (continuation < after)
		{
			size_t next = continuation + 1;
			size_t next_end = ansi_item_end(parser, next, after);
			if (!declarator_candidate(parser, next, next_end)) break;
			declaration_after = next_end;
			continuation = next_end;
		}

		checkpoint = save_checkpoint(parser);
		result = parse_systemverilog_static_declaration(parser, module, at, item_end,
			&declaration);
		if (result == DECLARATION_RECOGNIZED)
		{
			continuation = item_end;
			while (continuation < declaration_after)
			{
				size_t next = continuation + 1;
				size_t next_end = ansi_item_end(parser, next, after);
				if (!append_declarators(parser, declaration, next, next_end))
					break;
				continuation = next_end;
			}
			if (continuation == declaration_after)
			{
				commit_declaration(parser, module, at, declaration_after,
					opaque_cursor, declaration);
				item_end = declaration_after;
				at = item_end < after ? item_end + 1 : after;
				continue;
			}
		}
		restore_checkpoint(parser, checkpoint);

		checkpoint = save_checkpoint(parser);
		result = parse_data_first_declaration(parser, module, at, item_end, 1,
			&declaration, &decisive);
		if (result == DECLARATION_INVALID) return 0;
		if (result == DECLARATION_RECOGNIZED)
		{
			continuation = item_end;
			while (continuation < declaration_after)
			{
				size_t next = continuation + 1;
				size_t next_end = ansi_item_end(parser, next, after);
				if (!append_declarators(parser, declaration, next, next_end))
				{
					if (decisive) return 0;
					break;
				}
				continuation = next_end;
			}
			if (continuation == declaration_after)
			{
				commit_declaration(parser, module, at, declaration_after,
					opaque_cursor, declaration);
				item_end = declaration_after;
				at = item_end < after ? item_end + 1 : after;
				continue;
			}
		}
		restore_checkpoint(parser, checkpoint);
		at = item_end < after ? item_end + 1 : after;
	}
	return 1;
}

static const char *opaque_unit_closer(const syntax_parser *parser, size_t at)
{
	if (token_is(parser, at, "package")) return "endpackage";
	if (token_is(parser, at, "interface")) return "endinterface";
	if (token_is(parser, at, "program")) return "endprogram";
	if (token_is(parser, at, "class")) return "endclass";
	if (token_is(parser, at, "checker")) return "endchecker";
	if (token_is(parser, at, "primitive")) return "endprimitive";
	if (token_is(parser, at, "config")) return "endconfig";
	return NULL;
}

static int parse_pipeline_reset(syntax_parser *parser,
	pigen_syntax_id parent, size_t start, size_t limit, size_t *after,
	size_t *assignment_count)
{
	const char *text;
	size_t length = 0;
	const pigen_expanded_token *token = token_at(parser, start);

	(void)parent;
	(void)limit;
	text = token ? pigen_expanded_token_text(parser->expanded, token, &length) :
		NULL;
	if (!text || !((length == 14 && !memcmp(text, "pipeline_reset", 14)) ||
		(length == 10 && !memcmp(text, "pipe_reset", 10))))
		return 0;
	if (!token_is(parser, start + 1, "(") || !identifier(parser, start + 2) ||
		!token_is(parser, start + 3, ")") || !token_is(parser, start + 4, ";"))
		return 0;
	{
		pigen_syntax_node node = {0};

		node.kind = PIGEN_SYNTAX_ACTION;
		node.location = range_location(parser, start, start + 5);
		node.parent = INVALID_SYNTAX;
		node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
		node.as.action.action = 4;
		node.as.action.target = (pigen_token_id){(uint32_t)(start + 2)};
		add_child(parser, parent, add_node(parser, node));
	}
	*after = start + 5;
	(*assignment_count)++;
	return 1;
}

static int parse_import(syntax_parser *parser,pigen_syntax_id scope,size_t first,
 size_t limit,syntax_cursor *cursor,size_t *after);

static int parse_procedural_statement(syntax_parser *parser,
	pigen_syntax_id parent, size_t start, size_t limit, size_t *after_statement,
	size_t *assignment_count);

static int parse_pipeline(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t limit, size_t *after_statement,
	size_t *assignment_count);

static int parse_fsm(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t limit, size_t *after_statement);

static int parse_procedural_block(syntax_parser *parser,
	pigen_syntax_id parent, size_t start, size_t limit, size_t *after_statement,
	size_t *assignment_count)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id block;
	size_t at = start + 1;

	if (!token_is(parser, start, "begin"))
		return 0;
	if (token_is(parser,at,":"))
	{
		if (!parser->combinational_body) return 0;
		if (!identifier(parser,at+1)) return fail(parser,at,"procedural block requires a name after colon");
		node.as.procedural_block.has_name=1;
		node.as.procedural_block.name=(pigen_token_id){(uint32_t)(at+1)};
		at+=2;
	}
	node.kind = PIGEN_SYNTAX_PROCEDURAL_BLOCK;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	block = add_node(parser, node);
	add_child(parser, parent, block);
	while (at < limit && !token_is(parser, at, "end"))
	{
		if (!parse_procedural_statement(parser, block, at, limit, &at,
			assignment_count))
		{
			if (parser->fsm_states)
				parser->fsm_body_failed = 1;
			return 0;
		}
	}
	if (at == limit || !token_is(parser, at, "end")) return 0;
	*after_statement = at + 1;
	if (token_is(parser,at+1,":"))
	{
		const pigen_syntax_node *owner=&parser->tree->nodes[block.index];
		if (!owner->as.procedural_block.has_name || !identifier(parser,at+2))
			return fail(parser,at+1,"end label requires a named procedural block");
		if (!token_text_equal(parser,owner->as.procedural_block.name.index,at+2))
			return fail(parser,at+2,"procedural end label must match its block name");
		*after_statement=at+3;
	}
	parser->tree->nodes[block.index].location = range_location(parser, start,
		*after_statement);
	return 1;
}

static int parse_if_statement(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t limit, size_t *after_statement,
	size_t *assignment_count)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id statement;
	size_t close;
	size_t at;
	size_t keyword = start;

	if (token_is(parser,start,"unique") || token_is(parser,start,"unique0") || token_is(parser,start,"priority"))
	{
		node.as.if_statement.qualifier=token_is(parser,start,"unique") ? PIGEN_CASE_UNIQUE :
			token_is(parser,start,"unique0") ? PIGEN_CASE_UNIQUE0 : PIGEN_CASE_PRIORITY;
		keyword++;
	}
	if (!token_is(parser, keyword, "if") || !token_is(parser, keyword + 1, "("))
		return 0;
	close = matching_parenthesis(parser, keyword + 1, limit);
	if (close == limit || close == keyword + 2) return 0;
	node.kind = PIGEN_SYNTAX_IF_STATEMENT;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	if (!pigen_parse_expression(parser->expanded, keyword + 2, close,
		&parser->tree->expressions, &parser->tree->types, &node.as.if_statement.condition,
		parser->error)) return 0;
	statement = add_node(parser, node);
	add_child(parser, parent, statement);
	if (node.as.if_statement.qualifier!=PIGEN_CASE_UNQUALIFIED) (*assignment_count)++;
	at = close + 1;
	if (!parse_procedural_statement(parser, statement, at, limit, &at,
		assignment_count))
	{
		if (parser->fsm_states)
			parser->fsm_body_failed = 1;
		return 0;
	}
	if (token_is(parser, at, "else"))
	{
		parser->tree->nodes[statement.index].as.if_statement.has_else = 1;
		parser->tree->nodes[statement.index].as.if_statement.else_if = token_is(parser,at+1,"if");
		if (!parse_procedural_statement(parser, statement, at + 1, limit, &at,
			assignment_count))
		{
			if (parser->fsm_states)
				parser->fsm_body_failed = 1;
			return 0;
		}
	}
	*after_statement = at;
	parser->tree->nodes[statement.index].location = range_location(parser, start,
		at);
	return 1;
}

/* The transfer-event control `when (destination <= source) statement`: the
 * body runs only on the cycle the transfer fires. Split its assignment before
 * parsing either expression so low-precedence RHS operators stay in the value. */
static int parse_when_statement(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t limit, size_t *after_statement,
	size_t *assignment_count)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id statement;
	size_t close;
	size_t separator;
	size_t at;

	if (!token_is(parser, start, "when") || !token_is(parser, start + 1, "("))
		return 0;
	close = matching_parenthesis(parser, start + 1, limit);
	if (close == limit || close == start + 2) return 0;
	separator = top_level_token(parser, start + 2, close, "<=");
	if (separator == start + 2 || separator == close || separator + 1 == close)
		return fail(parser, start,
			"when requires a transfer event, e.g. `when (destination <= source)`");
	node.kind = PIGEN_SYNTAX_WHEN_STATEMENT;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	if (!pigen_parse_expression(parser->expanded, start + 2, separator,
		&parser->tree->expressions, &parser->tree->types,
		&node.as.when_statement.destination, parser->error) ||
		!pigen_parse_expression(parser->expanded, separator + 1, close,
			&parser->tree->expressions, &parser->tree->types,
			&node.as.when_statement.value, parser->error)) return 0;
	statement = add_node(parser, node);
	add_child(parser, parent, statement);
	at = close + 1;
	if (!parse_procedural_statement(parser, statement, at, limit, &at,
		assignment_count))
	{
		if (parser->fsm_states)
			parser->fsm_body_failed = 1;
		return 0;
	}
	/* The event itself is a transfer even when its body has no writes. */
	(*assignment_count)++;
	*after_statement = at;
	parser->tree->nodes[statement.index].location = range_location(parser, start,
		at);
	return 1;
}

static int parse_nonblocking_assignment(syntax_parser *parser,
	pigen_syntax_id parent, size_t start, size_t limit, size_t *after_statement,
	size_t *assignment_count)
{
	const size_t semicolon = top_level_token(parser, start, limit, ";");
	const size_t separator = top_level_token(parser, start, semicolon, "<=");
	pigen_syntax_node node = {0};

	if (semicolon == limit || separator == start || separator == semicolon ||
		separator + 1 == semicolon)
	{
		if (parser->fsm_states)
			return fail(parser, start,
				"fsm state body statement must be an ordinary statement");
		return 0;
	}
	node.kind = PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT;
	node.location = range_location(parser, start, semicolon + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	if (!pigen_parse_expression(parser->expanded, start, separator,
		&parser->tree->expressions, &parser->tree->types,
		&node.as.nonblocking_assignment.destination, parser->error) ||
		!pigen_parse_expression(parser->expanded, separator + 1, semicolon,
			&parser->tree->expressions, &parser->tree->types,
			&node.as.nonblocking_assignment.value, parser->error)) return 0;
	add_child(parser, parent, add_node(parser, node));
	(*assignment_count)++;
	*after_statement = semicolon + 1;
	return 1;
}

static int pipeline_scope_push(pipeline_scope_stack *stack)
{
	if (stack->depth >= MAX_PIPELINE_SCOPES) return 0;
	stack->scopes[stack->depth] = (pipeline_scope){0};
	stack->depth++;
	return 1;
}

static void pipeline_scope_pop(pipeline_scope_stack *stack)
{
	pipeline_scope *top = &stack->scopes[--stack->depth];
	free(top->fields);
	*top = (pipeline_scope){0};
}

static int pipeline_scope_add(pipeline_scope *scope, pigen_token_id name,
	pipeline_scope_kind kind)
{
	if (scope->count == scope->capacity)
	{
		scope->capacity = scope->capacity ? scope->capacity * 2 : 4;
		scope->fields = pigen_resize(scope->fields,
			scope->capacity * sizeof(*scope->fields));
	}
	scope->fields[scope->count].name = name;
	scope->fields[scope->count].kind = kind;
	scope->fields[scope->count].index = scope->count;
	scope->count++;
	return 1;
}

static int pipeline_scope_contains(const syntax_parser *parser,
	const pipeline_scope *scope, pigen_token_id name)
{
	size_t i;
	for (i = 0; i < scope->count; i++)
		if (tokens_equal(parser, scope->fields[i].name, name)) return 1;
	return 0;
}

static int parse_pipeline_field(syntax_parser *parser,
	pigen_syntax_id scope_id, size_t start, size_t semicolon)
{
	pigen_syntax_type_id data_type;
	pigen_transfer_type transfer_type;
	pigen_syntax_direction direction;
	pigen_syntax_type_ownership ownership;
	pigen_syntax_node node = {0};
	pigen_syntax_id declaration;
	size_t at = start;
	size_t name;
	size_t transfer_at = 0;
	int has_explicit_transfer = 0;
	pigen_token_id name_token;

	direction = parse_direction(parser, &at);
	if (transfer_type_at(parser, at, &transfer_type))
	{
		if (transfer_type != PIGEN_TRANSFER_TYPE_BUF)
		{
			fail(parser, at, "pipeline field transfer type must be buf");
			return 0;
		}
		transfer_at = at;
		has_explicit_transfer = 1;
		at++;
	}
	if (!pigen_parse_type_prefix(parser->expanded, at, semicolon,
		&parser->tree->expressions, &parser->tree->types, &data_type, &name,
		parser->error))
	{
		fail(parser, has_explicit_transfer ? at : start,
			"pipeline field declaration requires a data type");
		return 0;
	}
	if (!has_explicit_transfer && identifier(parser, name) &&
		token_is(parser, name, "buf") && identifier(parser, name + 1))
	{
		transfer_at = name;
		has_explicit_transfer = 1;
		name++;
	}
	if (!identifier(parser, name))
	{
		fail(parser, name, "pipeline field declaration requires a field name");
		return 0;
	}
	if (name + 1 != semicolon)
	{
		fail(parser, name + 1,
			"pipeline field declaration declares exactly one field");
		return 0;
	}
	ownership = declaration_type_ownership(parser, scope_id, data_type);
	if (ownership == PIGEN_SYNTAX_TYPE_UNOWNED)
	{
		fail(parser, at, "pipeline field declaration requires a data type");
		return 0;
	}
	name_token = (pigen_token_id){(uint32_t)name};
	if (parser->pipeline_scopes.depth)
	{
		const pipeline_scope *pipeline_scope =
			&parser->pipeline_scopes.scopes[0];
		size_t d;
		if (pipeline_scope_contains(parser, pipeline_scope, name_token))
		{
			fail(parser, name, "duplicate pipeline field name");
			return 0;
		}
		for (d = parser->pipeline_scopes.depth; d-- > 1;)
		{
			if (pipeline_scope_contains(parser, &parser->pipeline_scopes
					.scopes[d], name_token))
			{
				fail(parser, name,
					"pipeline field shadows a stage-local name");
				return 0;
			}
		}
	}
	node.kind = PIGEN_SYNTAX_SIGNAL_DECLARATION;
	node.location = range_location(parser, start, semicolon + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.signal_declaration.direction = direction;
	node.as.signal_declaration.data_type = data_type;
	node.as.signal_declaration.has_transfer_type = 1;
	node.as.signal_declaration.transfer_type.transfer_type =
		PIGEN_TRANSFER_TYPE_BUF;
	node.as.signal_declaration.transfer_type.argument =
		INVALID_ID(pigen_syntax_expr_id);
	if (has_explicit_transfer)
		node.as.signal_declaration.transfer_type.location =
			range_location(parser, transfer_at, transfer_at + 1);
	declaration = add_node(parser, node);
	if (add_signal_declarator(parser, declaration, name, semicolon,
		&at).index == PIGEN_INVALID_ID) return 0;
	add_child(parser, scope_id, declaration);
	if (parser->pipeline_scopes.depth)
	{
		pipeline_scope *top = &parser->pipeline_scopes
			.scopes[parser->pipeline_scopes.depth - 1];
		pipeline_scope_add(top, name_token, PIPELINE_SCOPE_PIPELINE);
	}
	return 1;
}

static int parse_pipeline_stage_item(syntax_parser *parser,
	pigen_syntax_id stage, size_t start, size_t limit, size_t *after_item);

static int parse_pipeline_stage(syntax_parser *parser, pigen_syntax_id pipeline,
	size_t start, size_t limit, size_t *after_stage)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id stage;
	size_t at = start + 1;
	size_t item_after;
	int saw_item = 0;
	int has_name = 0;

	if (!token_is(parser, start, "stage")) return 0;
	/* Compact stage form: `stage: statement;` (no begin/end). */
	if (token_is(parser, at, ":"))
	{
		size_t item_after;

		node.kind = PIGEN_SYNTAX_STAGE;
		node.as.stage.has_name = 0;
		node.parent = INVALID_SYNTAX;
		node.first_child = node.last_child = node.next_sibling =
			INVALID_SYNTAX;
		stage = add_node(parser, node);
		add_child(parser, pipeline, stage);
		if (!parse_pipeline_stage_item(parser, stage, at + 1, limit,
			&item_after))
			return 0;
		parser->tree->nodes[stage.index].location = range_location(parser,
			start, item_after);
		*after_stage = item_after;
		return 1;
	}
	if (at >= limit)
		return fail(parser, start, "pipeline stage requires `begin`");
	if (identifier(parser, at) && !token_is(parser, at, "begin"))
	{
		size_t begin = at + 1;
		while (begin < limit && (token_is(parser, begin, " ") ||
			token_is(parser, begin, "\t"))) begin++;
		if (begin >= limit || !token_is(parser, begin, "begin"))
			return fail(parser, at, "named pipeline stage requires `begin`");
		node.as.stage.name = (pigen_token_id){(uint32_t)at};
		has_name = 1;
		at = begin;
	}
	if (!token_is(parser, at, "begin"))
		return fail(parser, at, "pipeline stage requires `begin`");
	node.kind = PIGEN_SYNTAX_STAGE;
	node.as.stage.has_name = has_name;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	stage = add_node(parser, node);
	add_child(parser, pipeline, stage);
	if (!pipeline_scope_push(&parser->pipeline_scopes))
		return fail(parser, at, "too many pipeline scopes");
	at++;
	while (at < limit && !token_is(parser, at, "end"))
	{
		if (!parse_pipeline_stage_item(parser, stage, at, limit, &item_after))
		{
			pipeline_scope_pop(&parser->pipeline_scopes);
			return 0;
		}
		saw_item = 1;
		at = item_after;
	}
	if (!saw_item)
	{
		pipeline_scope_pop(&parser->pipeline_scopes);
		return fail(parser, start, "pipeline stage requires at least one item");
	}
	pipeline_scope_pop(&parser->pipeline_scopes);
	if (at == limit || !token_is(parser, at, "end"))
		return fail(parser, at, "pipeline stage requires `end`");
	node.location = range_location(parser, start, at + 1);
	parser->tree->nodes[stage.index].location = node.location;
	*after_stage = at + 1;
	return 1;
}

static int parse_pipeline_stage_item(syntax_parser *parser,
	pigen_syntax_id stage, size_t start, size_t limit, size_t *after_item)
{
	size_t semicolon = top_level_token(parser, start, limit, ";");
	size_t separator;
	pigen_syntax_type_id data_type;
	pigen_syntax_direction direction;
	pigen_syntax_type_ownership ownership;
	pigen_syntax_node node = {0};
	pigen_syntax_id declaration;
	size_t at = start;
	size_t name;
	int is_declaration;

	if (semicolon == limit)
		return fail(parser, start, "unterminated pipeline stage item");
	separator = top_level_token(parser, start, semicolon, "<=");
	if (separator > start && separator < semicolon)
	{
		size_t value_start = separator + 1;
		pigen_syntax_expr_id destination, value;

		if (value_start == semicolon)
			return fail(parser, separator,
				"pipeline stage assignment requires a value");
		if (!pigen_parse_expression(parser->expanded, start, separator,
			&parser->tree->expressions, &parser->tree->types, &destination,
			parser->error) ||
			!pigen_parse_expression(parser->expanded, value_start, semicolon,
				&parser->tree->expressions, &parser->tree->types, &value,
				parser->error)) return 0;
		node.kind = PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT;
		node.location = range_location(parser, start, semicolon + 1);
		node.parent = INVALID_SYNTAX;
		node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
		node.as.nonblocking_assignment.destination = destination;
		node.as.nonblocking_assignment.value = value;
		add_child(parser, stage, add_node(parser, node));
		*after_item = semicolon + 1;
		return 1;
	}
	if (token_is(parser, start, "yield"))
		return fail(parser, start,
			"yield is only after the final stage");
	at = start;
	direction = parse_direction(parser, &at);
	if (!pigen_parse_type_prefix(parser->expanded, at, semicolon,
		&parser->tree->expressions, &parser->tree->types, &data_type, &name,
		parser->error))
		return fail(parser, start, "unrecognized pipeline stage item");
	pigen_syntax_transfer_type_occurrence transfer={0};
	transfer.transfer_type=PIGEN_TRANSFER_TYPE_WIRE;
	transfer.argument=INVALID_ID(pigen_syntax_expr_id);
	pigen_transfer_type written;
	if(transfer_type_at(parser,name,&written))
	{
		if(!pigen_transfer_type_descriptor_get(written)->is_static)
			return fail(parser,name,"pipeline stage locals require a static transfer type");
		if(!parse_transfer_type_occurrence(parser,&name,semicolon,&transfer)) return 0;
	}
	if (!identifier(parser, name))
		return fail(parser, start, "unrecognized pipeline stage item");
	ownership = declaration_type_ownership(parser, stage, data_type);
	is_declaration = (ownership == PIGEN_SYNTAX_TYPE_PIGEN ||
		ownership == PIGEN_SYNTAX_TYPE_STRUCTURED);
	if (!is_declaration)
		return fail(parser, start, "unrecognized pipeline stage item");
	node.kind = PIGEN_SYNTAX_SIGNAL_DECLARATION;
	node.location = range_location(parser, start, semicolon + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.signal_declaration.direction = direction;
	node.as.signal_declaration.data_type = data_type;
	node.as.signal_declaration.has_transfer_type = 1;
	node.as.signal_declaration.transfer_type = transfer;
	declaration = add_node(parser, node);
	if (add_signal_declarator(parser, declaration, name, semicolon,
		&at).index == PIGEN_INVALID_ID) return 0;
	add_child(parser, stage, declaration);
	{
		pigen_token_id name_token = (pigen_token_id){(uint32_t)name};
		pipeline_scope *stage_scope = &parser->pipeline_scopes
			.scopes[parser->pipeline_scopes.depth - 1];
		size_t d;
		if (pipeline_scope_contains(parser, stage_scope, name_token))
		{
			fail(parser, name, "duplicate stage-local name");
			return 0;
		}
		for (d = parser->pipeline_scopes.depth - 1; d-- > 0;)
		{
			if (pipeline_scope_contains(parser, &parser->pipeline_scopes
					.scopes[d], name_token))
			{
				fail(parser, name,
					"stage-local name shadows a pipeline field");
				return 0;
			}
		}
		pipeline_scope_add(stage_scope, name_token, PIPELINE_SCOPE_STAGE);
	}
	*after_item = semicolon + 1;
	return 1;
}

static int parse_pipeline(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t limit, size_t *after_statement,
	size_t *assignment_count)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id pipeline;
	size_t endpipeline;
	size_t name, at;
	int saw_stage = 0;
	int saw_yield = 0;

	if (!token_is(parser, start, "pipeline")) return 0;
	parser->pipeline_failed = 1;
	name = start + 1;
	if (name >= limit || !identifier(parser, name))
		return fail(parser, start, "pipeline requires a name");
	at = name + 1;
	while (at < limit && (token_is(parser, at, " ") || token_is(parser, at, "\t")))
		at++;
	if (at >= limit || !token_is(parser, at, "begin"))
		return fail(parser, at, "pipeline requires `begin`");
	endpipeline = top_level_token(parser, at, limit, "endpipeline");
	if (endpipeline == limit)
		return fail(parser, at, "pipeline requires `endpipeline`");
	if (pipeline_scope_push(&parser->pipeline_scopes))
	{
		syntax_checkpoint checkpoint = save_checkpoint(parser);
		size_t body_start = at + 1;
		size_t body_at = body_start;
		int ok = 1;

		node.kind = PIGEN_SYNTAX_PIPELINE;
		node.location = range_location(parser, start, endpipeline + 1);
		node.parent = INVALID_SYNTAX;
		node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
		node.as.pipeline.name = (pigen_token_id){(uint32_t)name};
		node.as.pipeline.yield = INVALID_ID(pigen_syntax_expr_id);
		pipeline = add_node(parser, node);
		add_child(parser, parent, pipeline);
		while (body_at < endpipeline)
		{
			size_t item_end;
			if (token_is(parser, body_at, "stage"))
			{
				if (!parse_pipeline_stage(parser, pipeline, body_at, endpipeline,
					&item_end)) { ok = 0; break; }
				saw_stage = 1;
				body_at = item_end;
				continue;
			}
			if (token_is(parser, body_at, "yield"))
			{
				size_t semicolon = top_level_token(parser, body_at, endpipeline, ";");
				size_t value_start = body_at + 1;
				pigen_syntax_expr_id value;

				if (saw_yield)
				{
					fail(parser, body_at, "pipeline permits exactly one yield");
					ok = 0; break;
				}
				if (!saw_stage)
				{
					fail(parser, body_at, "pipeline yield must follow the stages");
					ok = 0; break;
				}
				if (semicolon == endpipeline || value_start == semicolon)
				{
					fail(parser, body_at, "pipeline yield requires an expression");
					ok = 0; break;
				}
				if (!pigen_parse_expression(parser->expanded, value_start,
					semicolon, &parser->tree->expressions,
					&parser->tree->types, &value, parser->error))
				{
					ok = 0; break;
				}
				parser->tree->nodes[pipeline.index].as.pipeline.yield = value;
				saw_yield = 1;
				body_at = semicolon + 1;
				continue;
			}
			if (token_is(parser, body_at, "export"))
			{
				fail(parser, body_at,
					"export is reserved but not yet supported");
				ok = 0;
				break;
			}
			{
				size_t semicolon = top_level_token(parser, body_at, endpipeline, ";");

				if (semicolon == endpipeline)
				{
					fail(parser, body_at, "unterminated pipeline declaration");
					ok = 0; break;
				}
				if (top_level_token(parser, body_at, semicolon, "<=") > body_at &&
					top_level_token(parser, body_at, semicolon, "<=") < semicolon)
				{
					fail(parser, body_at, "pipeline field declaration cannot assign");
					ok = 0; break;
				}
				if (!parse_pipeline_field(parser, pipeline, body_at, semicolon))
				{ ok = 0; break; }
				body_at = semicolon + 1;
				continue;
			}
		}
		if (ok)
		{
			if (!saw_stage)
			{
				fail(parser, start, "pipeline requires at least one stage");
				ok = 0;
			}
			else if (!saw_yield)
			{
				fail(parser, endpipeline, "pipeline requires a final yield");
				ok = 0;
			}
		}
		if (ok)
		{
			pipeline_scope_pop(&parser->pipeline_scopes);
			parser->pipeline_failed = 0;
			*after_statement = endpipeline + 1;
			*assignment_count += 1;
			return 1;
		}
		{
			pigen_syntax_error preserved = {0};
			int has_error = parser->error && parser->error->message;
			if (has_error) preserved = *parser->error;
			restore_checkpoint(parser, checkpoint);
			if (has_error && parser->error) *parser->error = preserved;
			parser->pipeline_failed = 1;
		}
		pipeline_scope_pop(&parser->pipeline_scopes);
		return 0;
	}
	return 0;
}

static int fsm_state_names_add(fsm_state_names *states, pigen_token_id name)
{
	if (states->count == states->capacity)
	{
		states->capacity = states->capacity ? states->capacity * 2 : 4;
		states->names = pigen_resize(states->names,
			states->capacity * sizeof(*states->names));
	}
	states->names[states->count++] = name;
	return 1;
}

static int fsm_state_names_contains(const syntax_parser *parser,
	const fsm_state_names *states, pigen_token_id name)
{
	size_t i;
	for (i = 0; i < states->count; i++)
		if (tokens_equal(parser, states->names[i], name)) return 1;
	return 0;
}

static int parse_goto_statement(syntax_parser *parser, pigen_syntax_id parent,
	const fsm_state_names *declared, size_t start, size_t limit,
	size_t *after_statement)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id target;
	size_t semicolon;

	if (!token_is(parser, start, "goto")) return 0;
	semicolon = top_level_token(parser, start + 1, limit, ";");
	if (semicolon == limit)
		return fail(parser, start, "goto requires a state name");
	if (semicolon != start + 2 || !identifier(parser, start + 1))
		return fail(parser, start + 1, "goto requires a state name");
	if (!fsm_state_names_contains(parser, declared,
		(pigen_token_id){(uint32_t)(start + 1)}))
		return fail(parser, start + 1, "goto references an undeclared state");
	node.kind = PIGEN_SYNTAX_GOTO;
	node.location = range_location(parser, start, semicolon + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.goto_statement.target = (pigen_token_id){(uint32_t)(start + 1)};
	target = add_node(parser, node);
	add_child(parser, parent, target);
	*after_statement = semicolon + 1;
	return 1;
}

static size_t fsm_state_header(const syntax_parser *parser, size_t start,
	size_t limit, size_t *name, size_t *body_start)
{
	if (!token_is(parser, start, "state")) return 0;
	*name = start + 1;
	if (*name >= limit || !identifier(parser, *name)) return 0;
	if (*name + 1 >= limit || !token_is(parser, *name + 1, ":")) return 0;
	*body_start = *name + 2;
	return 1;
}

static size_t fsm_skip_statement(const syntax_parser *parser, size_t start,
	size_t limit)
{
	if (token_is(parser,start,"unique") || token_is(parser,start,"unique0") || token_is(parser,start,"priority"))
		return fsm_skip_statement(parser,start+1,limit);
	if (token_is(parser, start, "begin"))
		return matching_begin_end(parser, start, limit) + 1;
	if (token_is(parser, start, "if"))
	{
		size_t close = matching_parenthesis(parser, start + 1, limit);
		size_t at;
		if (close == limit || close == start + 2) return limit;
		at = fsm_skip_statement(parser, close + 1, limit);
		if (at == limit) return limit;
		if (token_is(parser, at, "else"))
			at = fsm_skip_statement(parser, at + 1, limit);
		return at;
	}
	if (token_is(parser,start,"case") || token_is(parser,start,"casez") || token_is(parser,start,"casex"))
	{
		size_t depth=0;
		for(size_t at=start;at<limit;at++)
		{
			if(token_is(parser,at,"case") || token_is(parser,at,"casez") || token_is(parser,at,"casex")) depth++;
			else if(token_is(parser,at,"endcase") && !--depth) return at+1;
		}
		return limit;
	}
	if (token_is(parser,start,"forever")) return fsm_skip_statement(parser,start+1,limit);
	if (token_is(parser,start,"do"))
	{
		size_t at=fsm_skip_statement(parser,start+1,limit);
		if (!token_is(parser,at,"while") || !token_is(parser,at+1,"(")) return limit;
		size_t close=matching_parenthesis(parser,at+1,limit);
		return close==limit || !token_is(parser,close+1,";") ? limit : close+2;
	}
	if (token_is(parser,start,"while") || token_is(parser,start,"repeat") || token_is(parser,start,"for") || token_is(parser,start,"foreach"))
	{
		size_t close=matching_parenthesis(parser,start+1,limit);
		return close==limit ? limit : fsm_skip_statement(parser,close+1,limit);
	}
	if (token_is(parser, start, "when"))
	{
		size_t close = matching_parenthesis(parser, start + 1, limit);
		size_t at;
		if (close == limit || close == start + 2) return limit;
		at = fsm_skip_statement(parser, close + 1, limit);
		return at;
	}
	{
		size_t semicolon = top_level_token(parser, start, limit, ";");
		return semicolon == limit ? limit : semicolon + 1;
	}
}

static int parse_fsm_state(syntax_parser *parser, pigen_syntax_id fsm,
	size_t start, size_t limit, size_t *after_state)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id state;
	size_t name = start + 1;
	size_t at;
	size_t assignment_count = 0;

	if (!fsm_state_header(parser, start, limit, &name, &at))
		return fail(parser, start, "fsm state requires a name");
	node.kind = PIGEN_SYNTAX_FSM_STATE;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.fsm_state.name = (pigen_token_id){(uint32_t)name};
	state = add_node(parser, node);
	add_child(parser, fsm, state);
	if (!parse_procedural_statement(parser, state, at, limit, &at,
		&assignment_count))
	{
		if (!(parser->error && parser->error->message))
			parser->fsm_body_failed = 1;
		return 0;
	}
	parser->tree->nodes[state.index].location = range_location(parser, start,
		at);
	*after_state = at;
	return 1;
}

static int parse_fsm(syntax_parser *parser, pigen_syntax_id parent,
	size_t start, size_t limit, size_t *after_statement)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id fsm;
	fsm_state_names declared = {0};
	syntax_checkpoint checkpoint;
	size_t name = start + 1;
	size_t clock_at;
	size_t at;
	size_t body_start;
	size_t body_end;
	int has_name = 0;
	int saw_state = 0;
	int ok = 0;

	if (!token_is(parser, start, "fsm")) return 0;
	/* The FSM name is optional: `fsm sender @(posedge clk)` or the anonymous
	 * `fsm @(posedge clk)`. */
	if (name < limit && identifier(parser, name))
	{
		has_name = 1;
		at = name + 1;
	}
	else
		at = start + 1;
	if (at >= limit || !token_is(parser, at, "@"))
		return fail(parser, at, "fsm requires an event control");
	at++;
	if (at >= limit || !token_is(parser, at, "("))
		return fail(parser, at, "fsm event control must be one posedge");
	{
		size_t close = matching_parenthesis(parser, at, limit);
		size_t scan;
		if (close == limit)
			return fail(parser, at, "fsm event control must be one posedge");
		scan = at + 1;
		if (!token_is(parser, scan, "posedge"))
			return fail(parser, scan, "fsm event control must be one posedge");
		scan++;
		if (scan >= close || !identifier(parser, scan))
			return fail(parser, scan < close ? scan : close,
				"fsm event control must be one posedge");
		clock_at = scan;
		scan++;
		if (scan != close)
			return fail(parser, scan < close ? scan : close,
				"fsm event control must be one posedge");
		at = close;
	}
	at++;
	/* The reset/initial header is superseded: reset is ordinary level-zero
	 * code and the initial state is an ordinary state named by a goto
	 * (how/to/write/fsm/states.md). Diagnose the old header at its original
	 * span rather than failing on the generic body check. */
	if (at < limit && token_is(parser, at, "reset"))
		return fail(parser, at,
			"fsm reset is ordinary code: use `if (reset) goto state;`");
	if (at < limit && token_is(parser, at, "initial"))
		return fail(parser, at,
			"fsm initial state is ordinary code: use `goto state;`");
	if (at >= limit || !token_is(parser, at, "begin"))
		return fail(parser, at, "fsm requires `begin`");
	body_start = at + 1;
	body_end = matching_begin_end(parser, at, limit);
	if (body_end == limit)
		return fail(parser, at, "fsm requires `end`");
	checkpoint = save_checkpoint(parser);
	node.kind = PIGEN_SYNTAX_FSM;
	node.location = range_location(parser, start, body_end + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.fsm.name = has_name ?
		(pigen_token_id){(uint32_t)name} : INVALID_ID(pigen_token_id);
	node.as.fsm.has_name = has_name;
	node.as.fsm.clock = INVALID_ID(pigen_syntax_expr_id);
	fsm = add_node(parser, node);
	add_child(parser, parent, fsm);
	if (!pigen_parse_expression(parser->expanded, clock_at, clock_at + 1,
		&parser->tree->expressions, &parser->tree->types,
		&node.as.fsm.clock, parser->error))
		goto done;
	parser->fsm_states = &declared;
	parser->fsm_body_failed = 0;
	/* Declare every state name before parsing the bodies, so a goto may
	 * forward-reference a later state. Level-zero ordinary statements are
	 * skipped as whole statements. */
	{
		size_t scan = body_start;
		while (scan < body_end)
		{
			size_t sname, sbody;
			if (!fsm_state_header(parser, scan, body_end, &sname, &sbody))
			{
				size_t next = fsm_skip_statement(parser, scan, body_end);
				if (next == scan || next >= body_end) break;
				scan = next;
				continue;
			}
			if (fsm_state_names_contains(parser, &declared,
				(pigen_token_id){(uint32_t)sname}))
			{
				fail(parser, sname, "fsm state declared more than once");
				goto done;
			}
			fsm_state_names_add(&declared, (pigen_token_id){(uint32_t)sname});
			scan = fsm_skip_statement(parser, sbody, body_end);
			if (scan == body_end) break;
		}
	}
	/* The body is an ordered sequence of level-zero ordinary statements and
	 * `state identifier : statement` items. */
	at = body_start;
	while (at < body_end)
	{
		size_t item_after;
		if (token_is(parser, at, "state") && identifier(parser, at + 1))
		{
			if (!parse_fsm_state(parser, fsm, at, body_end, &item_after))
				goto done;
			saw_state = 1;
		}
		else
		{
			size_t assignment_count = 0;
			if (!parse_procedural_statement(parser, fsm, at, body_end,
				&item_after, &assignment_count))
			{
				if (!(parser->error && parser->error->message))
					parser->fsm_body_failed = 1;
				goto done;
			}
		}
		at = item_after;
	}
	if (!saw_state)
	{
		fail(parser, start, "fsm requires at least one state");
		goto done;
	}
	ok = 1;
done:
	parser->fsm_states = NULL;
	if (!ok && !(parser->error && parser->error->message) &&
		parser->fsm_body_failed)
	{
		fail(parser, start,
			"fsm body statement must be an ordinary statement");
		parser->fsm_body_failed = 0;
	}
	if (ok)
	{
		parser->tree->nodes[fsm.index].as.fsm.clock = node.as.fsm.clock;
		*after_statement = body_end + 1;
	}
	else
	{
		pigen_syntax_error preserved = {0};
		int has_error = parser->error && parser->error->message;
		if (has_error) preserved = *parser->error;
		restore_checkpoint(parser, checkpoint);
		if (has_error && parser->error) *parser->error = preserved;
	}
	free(declared.names);
	return ok ? 1 : 0;
}

static int parse_validity_action(syntax_parser *parser,
	pigen_syntax_id parent, size_t start, size_t limit, size_t *after,
	size_t *assignment_count)
{
	pigen_syntax_node node = {0};
	int action;
	size_t length = 0;
	const char *text = pigen_expanded_token_text(parser->expanded,
		token_at(parser, start), &length);

	(void)limit;
	if (!text) return 0;
	if (length == 8 && !memcmp(text, "validate", 8)) action = 1;
	else if (length == 10 && !memcmp(text, "invalidate", 10)) action = 2;
	else if (length == 5 && !memcmp(text, "flush", 5)) action = 3;
	else return 0;
	if (!token_is(parser, start + 1, "(") || !identifier(parser, start + 2) ||
		!token_is(parser, start + 3, ")") || !token_is(parser, start + 4, ";"))
		return 0;
	node.kind = PIGEN_SYNTAX_ACTION;
	node.location = range_location(parser, start, start + 5);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.action.action = action;
	node.as.action.target = (pigen_token_id){(uint32_t)(start + 2)};
	add_child(parser, parent, add_node(parser, node));
	(*assignment_count)++;
	*after = start + 5;
	return 1;
}

static int compound_assignment_operator(syntax_parser *parser, size_t at, pigen_syntax_operator *operator)
{
	static const struct { const char *text; pigen_syntax_operator operator; } operations[] = {
		{"+=",PIGEN_SYNTAX_OP_ADD}, {"-=",PIGEN_SYNTAX_OP_SUBTRACT},
		{"*=",PIGEN_SYNTAX_OP_MULTIPLY}, {"/=",PIGEN_SYNTAX_OP_DIVIDE},
		{"%=",PIGEN_SYNTAX_OP_MODULO}, {"<<=",PIGEN_SYNTAX_OP_SHIFT_LEFT},
		{"<<<=",PIGEN_SYNTAX_OP_ARITH_SHIFT_LEFT},
		{">>=",PIGEN_SYNTAX_OP_SHIFT_RIGHT}, {">>>=",PIGEN_SYNTAX_OP_ARITH_SHIFT_RIGHT},
		{"&=",PIGEN_SYNTAX_OP_BITWISE_AND}, {"|=",PIGEN_SYNTAX_OP_BITWISE_OR},
		{"^=",PIGEN_SYNTAX_OP_BITWISE_XOR}
	};
	for (size_t i=0;i<sizeof operations/sizeof *operations;i++)
		if (token_is(parser,at,operations[i].text)) { *operator=operations[i].operator; return 1; }
	return 0;
}

static int parse_combinational_assignment(syntax_parser *parser,pigen_syntax_id parent,
	size_t start,size_t end,size_t *after_statement,size_t *assignment_count)
{
	int result=0;
	size_t equal=top_level_token(parser,start,end,"=");
	pigen_syntax_node node={0};
	node.parent=node.first_child=node.last_child=node.next_sibling=INVALID_SYNTAX;
	if (end==start)
	{
		node.kind=PIGEN_SYNTAX_PROCEDURAL_BLOCK;
		node.location=range_location(parser,start,end+1);
		add_child(parser,parent,add_node(parser,node));*after_statement=end+1;result=1;
	}
	else
	{
		size_t left=start,right=end;
		pigen_syntax_operator operation=PIGEN_SYNTAX_OP_ADD;
		/* Compound operators are one token. Only a top-level operator
		 * can separate an assignment's lvalue from its RHS. */
		if (equal==end)
		{
			size_t grouping=0;
			for (size_t at=start;at<end;at++)
			{
				if (token_is(parser,at,"(") || token_is(parser,at,"[") || token_is(parser,at,"{")) grouping++;
				else if (token_is(parser,at,")") || token_is(parser,at,"]") || token_is(parser,at,"}")) { if (grouping) grouping--; }
				else if (!grouping && compound_assignment_operator(parser,at,&operation))
				{ equal=at;node.as.nonblocking_assignment.compound=1;break; }
			}
		}
		node.as.nonblocking_assignment.compound_operator=operation;
		if (equal==end && end>start+1)
		{
			size_t at=token_is(parser,start,"++") || token_is(parser,start,"--") ? start : end-1;
			if (token_is(parser,at,"++") || token_is(parser,at,"--"))
			{
				node.as.nonblocking_assignment.increment=token_is(parser,at,"++") ? 1 : -1;
				if (at==start) left++; else right--;
			}
		}
		else right=equal;
		if ((node.as.nonblocking_assignment.increment && left<right) ||
			(equal>start && equal<end && equal+1<end))
		{
			node.kind=PIGEN_SYNTAX_BLOCKING_ASSIGNMENT;
			node.location=range_location(parser,start,end+1);
			node.as.nonblocking_assignment.value=INVALID_ID(pigen_syntax_expr_id);
			result=pigen_parse_expression(parser->expanded,left,right,&parser->tree->expressions,
				&parser->tree->types,&node.as.nonblocking_assignment.destination,parser->error) &&
				(node.as.nonblocking_assignment.increment ||
				pigen_parse_expression(parser->expanded,equal+1,end,&parser->tree->expressions,
					&parser->tree->types,&node.as.nonblocking_assignment.value,parser->error));
			if (result) { add_child(parser,parent,add_node(parser,node));(*assignment_count)++;*after_statement=end+1; }
		}
	}
	return result;
}

static void mark_procedural_int_types(syntax_parser *parser,size_t first)
{
	for (size_t i=first;i<parser->tree->types.node_count;i++)
	{
		pigen_syntax_type *type=&parser->tree->types.nodes[i];
		if (!type->argument_count && type->base.index!=PIGEN_INVALID_ID && token_is(parser,type->base.index,"int"))
			type->is_systemverilog_int=1;
	}
}

static declaration_parse_result parse_combinational_declaration(syntax_parser *parser,
	pigen_syntax_id parent,size_t start,size_t limit,size_t *after)
{
	const pigen_syntax_node *owner=pigen_syntax_get(parser->tree,parent);
	if (!owner || owner->kind!=PIGEN_SYNTAX_PROCEDURAL_BLOCK) return DECLARATION_NOT_RECOGNIZED;
	size_t end=top_level_token(parser,start,limit,";");
	if (end==limit) return DECLARATION_NOT_RECOGNIZED;
	int automatic=token_is(parser,start,"automatic");
	size_t first=(automatic || token_is(parser,start,"static")) ? start+1 : start;
	if (token_is(parser,first,"typedef") || token_is(parser,first,"localparam") || token_is(parser,first,"parameter"))
	{
		if (automatic) {fail(parser,start,"automatic lifetime requires a variable declaration");return DECLARATION_INVALID;}
		size_t saved=parser->opaque_byte_cursor;
		const pigen_expanded_token *token=token_at(parser,first);
		parser->opaque_byte_cursor=pigen_origin_expansion_span(parser->expanded,token->origin).start;
		syntax_cursor cursor=first;size_t before=parser->tree->node_count;
		int ok=token_is(parser,first,"typedef") ?
			parse_typedef(parser,parent,first,end,&cursor)==DECLARATION_RECOGNIZED :
			parse_procedural_parameters(parser,parent,first,end,&cursor);
		parser->opaque_byte_cursor=saved;
		int declaration=0;
		for (size_t i=before;i<parser->tree->node_count;i++)
			if (parser->tree->nodes[i].kind==PIGEN_SYNTAX_TYPEDEF || parser->tree->nodes[i].kind==PIGEN_SYNTAX_PARAMETER) declaration=1;
		if (!ok || !declaration)
		{
			fail(parser,start,"unsupported procedural constant or typedef declaration");return DECLARATION_INVALID;
		}
		*after=end+1;return DECLARATION_RECOGNIZED;
	}
	syntax_checkpoint checkpoint=save_checkpoint(parser);
	pigen_syntax_id id=INVALID_SYNTAX;
	declaration_parse_result result=parse_systemverilog_static_declaration(parser,parent,first,end,&id);
	if (result!=DECLARATION_RECOGNIZED)
	{
		restore_checkpoint(parser,checkpoint);
		/* A selected assignment can look like a parameterized type. Require
		 * a declarator after the type before claiming declaration syntax. */
		pigen_syntax_type_id candidate; size_t name;
		int typed=pigen_parse_type_prefix(parser->expanded,first,end,
			&parser->tree->expressions,&parser->tree->types,&candidate,&name,parser->error);
		restore_checkpoint(parser,checkpoint);
		if (!typed || !identifier(parser,name)) return DECLARATION_NOT_RECOGNIZED;
		int decisive=0;
		result=parse_data_first_declaration(parser,parent,first,end,0,&id,&decisive);
		if (result!=DECLARATION_RECOGNIZED)
		{
			if (result==DECLARATION_INVALID) return result;
			restore_checkpoint(parser,checkpoint);return DECLARATION_NOT_RECOGNIZED;
		}
	}
	pigen_syntax_node *node=&parser->tree->nodes[id.index];
	node->as.signal_declaration.is_automatic=automatic;
	if (node->as.signal_declaration.direction!=PIGEN_DIRECTION_INTERNAL || node->as.signal_declaration.protocol.family!=PIGEN_PROTOCOL_NONE)
	{
		fail(parser,start,"procedural locals must be static variables, not ports or AXI endpoints");return DECLARATION_INVALID;
	}
	if (node->as.signal_declaration.has_transfer_type &&
		!pigen_transfer_type_descriptor_get(node->as.signal_declaration.transfer_type.transfer_type)->is_static)
	{
		fail(parser,start,"procedural locals require a static transfer type");return DECLARATION_INVALID;
	}
	if (node->as.signal_declaration.has_transfer_type &&
		node->as.signal_declaration.transfer_type.transfer_type==PIGEN_TRANSFER_TYPE_WIRE)
	{
		fail(parser,start,"procedural locals require variables, not wire nets");return DECLARATION_INVALID;
	}
	if (!node->as.signal_declaration.has_transfer_type)
	{
		node->as.signal_declaration.has_transfer_type=1;
		node->as.signal_declaration.transfer_type.transfer_type=PIGEN_TRANSFER_TYPE_LOGIC;
		node->as.signal_declaration.transfer_type.argument=INVALID_ID(pigen_syntax_expr_id);
	}
 for(pigen_syntax_id child=node->first_child;child.index!=PIGEN_INVALID_ID;)
 {
  const pigen_syntax_node *declarator=pigen_syntax_get(parser->tree,child);
  if(declarator->as.signal_declarator.has_initializer && !automatic && !token_is(parser,start,"static"))
  {fail(parser,start,"procedural declaration initialization requires explicit static or automatic lifetime");return DECLARATION_INVALID;}
  child=declarator->next_sibling;
 }
	node->location=range_location(parser,start,end+1);add_child(parser,parent,id);
	*after=end+1;return DECLARATION_RECOGNIZED;
}

static size_t generate_case_label_end(syntax_parser *parser,size_t first,size_t limit);

/* Header lists use the same typed assignments as ordinary statements. */
static int parse_combinational_for_list(syntax_parser *parser,pigen_syntax_id parent,
	size_t first,size_t limit,size_t *assignment_count,int declarations)
{
	if (first==limit) return 1;
	size_t name=first;
	pigen_syntax_id declaration=INVALID_SYNTAX;
	if (declarations)
	{
		syntax_checkpoint checkpoint=save_checkpoint(parser);
		pigen_syntax_type_id type;
		if (pigen_parse_type_prefix(parser->expanded,first,limit,&parser->tree->expressions,
			&parser->tree->types,&type,&name,parser->error) && identifier(parser,name) && token_is(parser,name+1,"="))
		{
			declaration=add_declaration_node(parser,PIGEN_DIRECTION_INTERNAL,type);
			parser->tree->nodes[declaration.index].as.signal_declaration.has_transfer_type=1;
			parser->tree->nodes[declaration.index].as.signal_declaration.transfer_type.transfer_type=PIGEN_TRANSFER_TYPE_LOGIC;
			parser->tree->nodes[declaration.index].location=range_location(parser,first,limit);
			add_child(parser,parent,declaration);
		}
		else {restore_checkpoint(parser,checkpoint);name=first;}
	}
	for(size_t at=name;at<limit;)
	{
		size_t end=top_level_token(parser,at,limit,","),after;
		if (declaration.index!=PIGEN_INVALID_ID)
		{
			if (!identifier(parser,at) || !token_is(parser,at+1,"=")) return fail(parser,at,"for variable requires an initializer");
			size_t parsed;
			if (add_signal_declarator(parser,declaration,at,at+1,&parsed).index==PIGEN_INVALID_ID) return 0;
		}
		if (at==end || !parse_combinational_assignment(parser,parent,at,end,&after,assignment_count))
			return fail(parser,at,"for header requires blocking assignments or updates");
		if (end==limit) return 1;
		at=end+1;
		if (at==limit) return fail(parser,end,"for header cannot end with a comma");
	}
	return 1;
}

static int parse_combinational_for(syntax_parser *parser,pigen_syntax_id parent,
	size_t first,size_t limit,size_t *after,size_t *assignment_count)
{
	if (!token_is(parser,first+1,"(")) return fail(parser,first,"for requires a parenthesized header");
	size_t close=matching_parenthesis(parser,first+1,limit);
	if (close==limit) return fail(parser,first,"unterminated for header");
	size_t semi1=top_level_token(parser,first+2,close,";"),semi2=top_level_token(parser,semi1+1,close,";");
	if (semi1==close || semi2==close || top_level_token(parser,semi2+1,close,";")!=close)
		return fail(parser,first,"for header requires two semicolons");
	pigen_syntax_node node={0};node.kind=PIGEN_SYNTAX_PROCEDURAL_FOR;
	node.parent=node.first_child=node.last_child=node.next_sibling=INVALID_SYNTAX;
	node.as.procedural_loop.control=INVALID_ID(pigen_syntax_expr_id);
	if (semi1+1!=semi2 && !pigen_parse_expression(parser->expanded,semi1+1,semi2,
		&parser->tree->expressions,&parser->tree->types,&node.as.procedural_loop.control,parser->error)) return 0;
	pigen_syntax_id loop=add_node(parser,node);add_child(parser,parent,loop);
	for (int step=0;step<2;step++)
	{
		pigen_syntax_node list={0};list.kind=PIGEN_SYNTAX_PROCEDURAL_BLOCK;
		list.parent=list.first_child=list.last_child=list.next_sibling=INVALID_SYNTAX;
		list.location=range_location(parser,step ? semi2+1 : first+2,step ? close : semi1);
		pigen_syntax_id id=add_node(parser,list);add_child(parser,loop,id);
		if (!parse_combinational_for_list(parser,id,step ? semi2+1 : first+2,step ? close : semi1,assignment_count,!step)) return 0;
	}
	if (!parse_procedural_statement(parser,loop,close+1,limit,after,assignment_count)) return 0;
	parser->tree->nodes[loop.index].location=range_location(parser,first,*after);return 1;
}

/* The final bracket list belongs to foreach, not to the target expression. */
static int parse_procedural_foreach(syntax_parser *parser,pigen_syntax_id parent,
	size_t first,size_t limit,size_t *after,size_t *assignment_count)
{
	if (!token_is(parser,first+1,"(")) return fail(parser,first,"foreach requires a parenthesized header");
	size_t close=matching_parenthesis(parser,first+1,limit),open=close;
	if (close==limit || !token_is(parser,close-1,"]")) return fail(parser,first,"foreach requires an array and index list");
	size_t nesting=0;
	for(size_t i=close-1;i>first+1;i--)
	{
		if(token_is(parser,i,"]")) nesting++;
		else if(token_is(parser,i,"[") && !--nesting) {open=i;break;}
	}
	if(open==close || open==first+2) return fail(parser,first,"foreach requires an array target");
	pigen_syntax_node node={0};node.kind=PIGEN_SYNTAX_PROCEDURAL_FOREACH;
	node.parent=node.first_child=node.last_child=node.next_sibling=INVALID_SYNTAX;
	if(!pigen_parse_expression(parser->expanded,first+2,open,&parser->tree->expressions,&parser->tree->types,
		&node.as.procedural_loop.control,parser->error)) return 0;
	pigen_syntax_id loop=add_node(parser,node);add_child(parser,parent,loop);
	if(!parser->combinational_body) (*assignment_count)++;
	for(size_t at=open+1;;)
	{
		size_t end=top_level_token(parser,at,close-1,",");
		if(end!=at && (end!=at+1 || !identifier(parser,at))) return fail(parser,at,"foreach index must be an identifier or omitted");
		pigen_syntax_node index={0};index.kind=PIGEN_SYNTAX_FOREACH_INDEX;
		index.parent=index.first_child=index.last_child=index.next_sibling=INVALID_SYNTAX;
		index.as.foreach_index.name=end==at ? INVALID_ID(pigen_token_id) : (pigen_token_id){(uint32_t)at};
		index.location=range_location(parser,at,end==at ? at+1 : end);
		add_child(parser,loop,add_node(parser,index));
		if(end==close-1) break;
		at=end+1;
	}
	if(!parse_procedural_statement(parser,loop,close+1,limit,after,assignment_count)) return 0;
	parser->tree->nodes[loop.index].location=range_location(parser,first,*after);return 1;
}

static int parse_combinational_trailing_loop(syntax_parser *parser,pigen_syntax_id parent,
	size_t first,size_t limit,size_t *after,size_t *assignment_count)
{
	int trailing=token_is(parser,first,"do");
	pigen_syntax_node node={0};node.kind=trailing ? PIGEN_SYNTAX_PROCEDURAL_DO_WHILE : PIGEN_SYNTAX_PROCEDURAL_FOREVER;
	node.parent=node.first_child=node.last_child=node.next_sibling=INVALID_SYNTAX;
	node.as.procedural_loop.control=INVALID_ID(pigen_syntax_expr_id);
	pigen_syntax_id loop=add_node(parser,node);add_child(parser,parent,loop);
	size_t at;
	if (!parse_procedural_statement(parser,loop,first+1,limit,&at,assignment_count)) return 0;
	if (trailing)
	{
		if (!token_is(parser,at,"while") || !token_is(parser,at+1,"(")) return fail(parser,at,"do body requires trailing while control");
		size_t close=matching_parenthesis(parser,at+1,limit);
		if (close==limit || !token_is(parser,close+1,";")) return fail(parser,at,"do/while requires a parenthesized control and semicolon");
		pigen_syntax_expr_id control;
		if (!pigen_parse_expression(parser->expanded,at+2,close,&parser->tree->expressions,&parser->tree->types,&control,parser->error)) return 0;
		parser->tree->nodes[loop.index].as.procedural_loop.control=control;at=close+2;
	}
	*after=at;parser->tree->nodes[loop.index].location=range_location(parser,first,at);return 1;
}

static int parse_combinational_loop(syntax_parser *parser,pigen_syntax_id parent,
	size_t first,size_t limit,size_t *after,size_t *assignment_count)
{
	if (!token_is(parser,first+1,"(")) return fail(parser,first,"procedural loop requires parenthesized control");
	size_t close=matching_parenthesis(parser,first+1,limit);
	if (close==limit) return fail(parser,first,"unterminated procedural loop control");
	pigen_syntax_node node={0};
	node.kind=token_is(parser,first,"while") ? PIGEN_SYNTAX_PROCEDURAL_WHILE : PIGEN_SYNTAX_PROCEDURAL_REPEAT;
	node.parent=node.first_child=node.last_child=node.next_sibling=INVALID_SYNTAX;
	if (!pigen_parse_expression(parser->expanded,first+2,close,&parser->tree->expressions,&parser->tree->types,
		&node.as.procedural_loop.control,parser->error)) return 0;
	pigen_syntax_id loop=add_node(parser,node);add_child(parser,parent,loop);
	if (!parse_procedural_statement(parser,loop,close+1,limit,after,assignment_count)) return 0;
	parser->tree->nodes[loop.index].location=range_location(parser,first,*after);return 1;
}

static int parse_procedural_case(syntax_parser *parser,pigen_syntax_id parent,
	size_t first,size_t limit,size_t *after,size_t *assignment_count)
{
	size_t keyword=first;
	pigen_case_qualifier qualifier=PIGEN_CASE_UNQUALIFIED;
	if (token_is(parser,first,"unique")) qualifier=PIGEN_CASE_UNIQUE;
	else if (token_is(parser,first,"unique0")) qualifier=PIGEN_CASE_UNIQUE0;
	else if (token_is(parser,first,"priority")) qualifier=PIGEN_CASE_PRIORITY;
	if (qualifier!=PIGEN_CASE_UNQUALIFIED) keyword++;
	if (!token_is(parser,keyword,"case") && !token_is(parser,keyword,"casez") && !token_is(parser,keyword,"casex"))
		return fail(parser,first,"typed procedural qualifier requires case, casez or casex");
	if (!token_is(parser,keyword+1,"(")) return fail(parser,first,"procedural case requires a parenthesized selector");
	size_t close=matching_parenthesis(parser,keyword+1,limit);
	if (close==limit) return fail(parser,first,"unterminated procedural case selector");
	pigen_syntax_node node={0};node.kind=PIGEN_SYNTAX_PROCEDURAL_CASE;
	node.parent=node.first_child=node.last_child=node.next_sibling=INVALID_SYNTAX;
	node.as.procedural_case.qualifier=qualifier;
	node.as.procedural_case.wildcard=token_is(parser,keyword,"casez") ? 1 : token_is(parser,keyword,"casex") ? 2 : 0;
	if (!pigen_parse_expression(parser->expanded,keyword+2,close,&parser->tree->expressions,&parser->tree->types,
		&node.as.procedural_case.selector,parser->error)) return 0;
	pigen_syntax_id selection=add_node(parser,node);add_child(parser,parent,selection);
	if (qualifier!=PIGEN_CASE_UNQUALIFIED) (*assignment_count)++;
	size_t at=close+1;int fallback=0;
	while (at<limit && !token_is(parser,at,"endcase"))
	{
		size_t start=at;
		pigen_syntax_node item={0};item.kind=PIGEN_SYNTAX_PROCEDURAL_CASE_ITEM;
		item.parent=item.first_child=item.last_child=item.next_sibling=INVALID_SYNTAX;
		pigen_syntax_id id=add_node(parser,item);add_child(parser,selection,id);
		if (token_is(parser,at,"default"))
		{
			if (fallback++) return fail(parser,at,"procedural case cannot contain multiple defaults");
			parser->tree->nodes[id.index].as.generate_case_item.is_default=1;
			at++;if (token_is(parser,at,":")) at++;
		}
		else for (;;)
		{
			size_t end=generate_case_label_end(parser,at,limit);
			if (end==limit || end==at) return fail(parser,at,"procedural case requires labels followed by a colon");
			pigen_syntax_node label={0};label.kind=PIGEN_SYNTAX_PROCEDURAL_CASE_LABEL;
			label.parent=label.first_child=label.last_child=label.next_sibling=INVALID_SYNTAX;
			label.location=range_location(parser,at,end);
			if (!pigen_parse_expression(parser->expanded,at,end,&parser->tree->expressions,&parser->tree->types,
				&label.as.generate_case_label.value,parser->error)) return 0;
			add_child(parser,id,add_node(parser,label));at=end+1;
			if (token_is(parser,end,":")) break;
		}
		if (!parse_procedural_statement(parser,id,at,limit,&at,assignment_count)) return 0;
		parser->tree->nodes[id.index].location=range_location(parser,start,at);
	}
	if (at==limit) return fail(parser,first,"procedural case requires endcase");
	*after=at+1;parser->tree->nodes[selection.index].location=range_location(parser,first,*after);return 1;
}

static int parse_procedural_statement(syntax_parser *parser,
	pigen_syntax_id parent, size_t start, size_t limit, size_t *after_statement,
	size_t *assignment_count)
{
    if(token_is(parser,start,"import"))
    {
        syntax_cursor cursor=start;
        const pigen_syntax_node *owner=pigen_syntax_get(parser->tree,parent);
        size_t saved=parser->opaque_byte_cursor;
        const pigen_expanded_token *token=token_at(parser,start);
        parser->opaque_byte_cursor=pigen_origin_expansion_span(parser->expanded,token->origin).start;
        int result=owner && owner->kind==PIGEN_SYNTAX_PROCEDURAL_BLOCK ?
            parse_import(parser,parent,start,limit,&cursor,after_statement) :
            fail(parser,start,"procedural import requires a lexical block");
        parser->opaque_byte_cursor=saved;
        return result;
    }
 if(token_is(parser,start,"break") || token_is(parser,start,"continue"))
 {
  if(!token_is(parser,start+1,";")) return fail(parser,start,"loop control requires a semicolon");
  pigen_syntax_node node={0};node.kind=token_is(parser,start,"break") ? PIGEN_SYNTAX_PROCEDURAL_BREAK : PIGEN_SYNTAX_PROCEDURAL_CONTINUE;
  node.parent=node.first_child=node.last_child=node.next_sibling=INVALID_SYNTAX;
  node.location=range_location(parser,start,start+2);
  add_child(parser,parent,add_node(parser,node));*after_statement=start+2;
  if(!parser->combinational_body) (*assignment_count)++;
  return 1;
 }
	if (parser->combinational_body)
	{
		if (parser->combinational_depth++ >= 128)
			return fail(parser, start, "combinational statement nesting exceeds limit");
		size_t type_start=parser->tree->types.node_count;
		declaration_parse_result declaration=parse_combinational_declaration(parser,parent,start,limit,after_statement);
		if (declaration!=DECLARATION_NOT_RECOGNIZED)
		{
			if (declaration==DECLARATION_RECOGNIZED) mark_procedural_int_types(parser,type_start);
			parser->combinational_depth--;return declaration==DECLARATION_RECOGNIZED;
		}
		int result = 0;
		if (token_is(parser,start,"begin"))
			result=parse_procedural_block(parser,parent,start,limit,after_statement,assignment_count);
		else if (token_is(parser,start,"if") ||
			((token_is(parser,start,"unique") || token_is(parser,start,"unique0") || token_is(parser,start,"priority")) && token_is(parser,start+1,"if")))
			result=parse_if_statement(parser,parent,start,limit,after_statement,assignment_count);
		else if (token_is(parser,start,"do") || token_is(parser,start,"forever"))
			result=parse_combinational_trailing_loop(parser,parent,start,limit,after_statement,assignment_count);
		else if (token_is(parser,start,"foreach"))
			result=parse_procedural_foreach(parser,parent,start,limit,after_statement,assignment_count);
		else if (token_is(parser,start,"for"))
			result=parse_combinational_for(parser,parent,start,limit,after_statement,assignment_count);
		else if (token_is(parser,start,"while") || token_is(parser,start,"repeat"))
			result=parse_combinational_loop(parser,parent,start,limit,after_statement,assignment_count);
		else if (token_is(parser,start,"case") || token_is(parser,start,"casez") || token_is(parser,start,"casex") ||
			token_is(parser,start,"unique") || token_is(parser,start,"unique0") || token_is(parser,start,"priority"))
			result=parse_procedural_case(parser,parent,start,limit,after_statement,assignment_count);
		else
		{
			size_t end=top_level_token(parser,start,limit,";");
			if (end<limit) result=parse_combinational_assignment(parser,parent,start,end,after_statement,assignment_count);
		}
		if (result) mark_procedural_int_types(parser,type_start);
		parser->combinational_depth--;
		return result;
	}

	if (token_is(parser,start,"foreach"))
		return parse_procedural_foreach(parser,parent,start,limit,after_statement,assignment_count);
	if (token_is(parser,start,";"))
	{
		pigen_syntax_node node={0};node.kind=PIGEN_SYNTAX_PROCEDURAL_BLOCK;
		node.parent=node.first_child=node.last_child=node.next_sibling=INVALID_SYNTAX;
		node.location=range_location(parser,start,start+1);
		add_child(parser,parent,add_node(parser,node));*after_statement=start+1;return 1;
	}
	if (token_is(parser,start,"case") || token_is(parser,start,"casez") || token_is(parser,start,"casex") ||
		((token_is(parser,start,"unique") || token_is(parser,start,"unique0") || token_is(parser,start,"priority")) && !token_is(parser,start+1,"if")))
		return parse_procedural_case(parser,parent,start,limit,after_statement,assignment_count);
	if (parse_pipeline_reset(parser, parent, start, limit, after_statement,
		assignment_count))
		return 1;
	if (parse_validity_action(parser, parent, start, limit, after_statement,
		assignment_count))
		return 1;
	if (token_is(parser, start, "begin"))
		return parse_procedural_block(parser, parent, start, limit,
			after_statement, assignment_count);
	if (token_is(parser, start, "if") ||
		((token_is(parser,start,"unique") || token_is(parser,start,"unique0") || token_is(parser,start,"priority")) && token_is(parser,start+1,"if")))
		return parse_if_statement(parser, parent, start, limit, after_statement,
			assignment_count);
	if (token_is(parser, start, "when"))
	{
		int parsed = parse_when_statement(parser, parent, start, limit,
			after_statement, assignment_count);
		if (!parsed)
		{
			parser->when_failed = 1;
			if (!parser->error || !parser->error->message)
				return fail(parser, start, "malformed when transfer event or body");
		}
		return parsed;
	}
	if (token_is(parser, start, "pipeline"))
		return parse_pipeline(parser, parent, start, limit, after_statement,
			assignment_count);
	if (parser->fsm_states && token_is(parser, start, "goto"))
		return parse_goto_statement(parser, parent, parser->fsm_states, start,
			limit, after_statement);
	return parse_nonblocking_assignment(parser, parent, start, limit,
		after_statement, assignment_count);
}

static int parse_clocked_process(syntax_parser *parser,
	pigen_syntax_id module, size_t start, size_t limit,
	syntax_cursor *opaque_cursor, size_t *after_process)
{
	pigen_syntax_expr_arena *expressions = &parser->tree->expressions;
	syntax_checkpoint checkpoint = save_checkpoint(parser);
	size_t event_open = start + 2;
	size_t event_close;
	size_t process_after;
	pigen_syntax_expr_id clock;
	size_t assignment_count = 0;
	pigen_syntax_node node = {0};
	pigen_syntax_id process;

	if ((!token_is(parser, start, "always") &&
		!token_is(parser, start, "always_ff")) ||
		!token_is(parser, start + 1, "@") ||
		!token_is(parser, event_open, "(")) return 0;
	event_close = matching_parenthesis(parser, event_open, limit);
	if (event_close == limit || !token_is(parser, event_open + 1, "posedge") ||
		event_open + 3 != event_close || !identifier(parser, event_open + 2))
		return 0;
	if (!pigen_parse_expression(parser->expanded, event_open + 2, event_close,
		expressions, &parser->tree->types, &clock, parser->error))
	{
		restore_checkpoint(parser, checkpoint);
		return 0;
	}
	node.kind = PIGEN_SYNTAX_CLOCKED_PROCESS;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.clocked_process.edge = PIGEN_EDGE_POSEDGE;
	node.as.clocked_process.clock = clock;
	process = add_node(parser, node);
	if (!parse_procedural_statement(parser, process, event_close + 1, limit,
		&process_after, &assignment_count) || !assignment_count)
	{
		pigen_syntax_error preserved = {0};
		int structured_failure = parser->pipeline_failed || parser->when_failed;
		if (structured_failure && parser->error) preserved = *parser->error;
		restore_checkpoint(parser, checkpoint);
		if (structured_failure && parser->error) *parser->error = preserved;
		return 0;
	}
	parser->tree->nodes[process.index].location = range_location(parser, start,
		process_after);
	if (node_has_function_call(parser, process, 0))
	{
		if (node_has_structured_construct(parser, process, 0))
			return fail(parser, start,
				"function call in a structured clocked process is not supported");
		restore_checkpoint(parser, checkpoint);
		return 0;
	}
	add_opaque(parser, module, *opaque_cursor, start);
	add_child(parser, module, process);
	advance_opaque_byte_cursor(parser, process_after);
	*opaque_cursor = process_after;
	*after_process = process_after;
	return 1;
}

static int looks_like_plain_instance(const syntax_parser *parser, size_t at,
	size_t limit)
{
	size_t after_name, close;

	if (at + 1 >= limit || !identifier(parser, at) ||
		!identifier(parser, at + 1)) return 0;
	after_name = at + 2;
	if (token_is(parser, after_name, "#"))
	{
		if (!token_is(parser, after_name + 1, "(")) return 0;
		close = matching_parenthesis(parser, after_name + 1, limit);
		if (close == limit) return 0;
		after_name = close + 1;
	}
	if (!token_is(parser, after_name, "(")) return 0;
	close = matching_parenthesis(parser, after_name, limit);
	return close < limit && token_is(parser, close + 1, ";");
}

static int looks_like_parameterized_instance(const syntax_parser *parser,
	size_t at, size_t limit)
{
	size_t close, after_name;

	if (at + 4 >= limit || !identifier(parser, at) ||
		!token_is(parser, at + 1, "#") || !token_is(parser, at + 2, "("))
		return 0;
	close = matching_parenthesis(parser, at + 2, limit);
	if (close == limit) return 0;
	after_name = close + 1;
	if (!identifier(parser, after_name) ||
		!token_is(parser, after_name + 1, "(")) return 0;
	close = matching_parenthesis(parser, after_name + 1, limit);
	return close < limit && token_is(parser, close + 1, ";");
}

static int parse_instance_connections(syntax_parser *parser,
	pigen_syntax_id instance, size_t open, size_t close, pigen_syntax_kind kind)
{
	for (size_t item = open + 1; item < close; )
	{
		pigen_syntax_node connection = {0};
		pigen_token_id port = INVALID_ID(pigen_token_id);
		pigen_syntax_expr_id value = INVALID_ID(pigen_syntax_expr_id);
		size_t value_close;

		if (token_is(parser, item, "."))
		{
			if (!identifier(parser, item + 1) ||
				!token_is(parser, item + 2, "("))
				return fail(parser, item,
					"named instance connection requires .port(expression)");
			port = (pigen_token_id){(uint32_t)(item + 1)};
			value_close = matching_parenthesis(parser, item + 2, close);
			if (value_close == close)
				return fail(parser, item + 2,
					"unterminated instance connection");
			if (value_close > item + 3 &&
				!pigen_parse_expression(parser->expanded, item + 3,
					value_close, &parser->tree->expressions,
					&parser->tree->types, &value, parser->error))
				return 0;
		}
		else
		{
			size_t next_comma = top_level_token(parser, item, close, ",");
			value_close = next_comma - 1;
			if (next_comma > item &&
				!pigen_parse_expression(parser->expanded, item, next_comma,
					&parser->tree->expressions, &parser->tree->types,
					&value, parser->error))
				return 0;
		}
		connection.kind = kind;
		connection.location = range_location(parser, item, value_close + 1);
		connection.parent = INVALID_SYNTAX;
		connection.first_child = connection.last_child =
			connection.next_sibling = INVALID_SYNTAX;
		connection.as.connection.port = port;
		connection.as.connection.value = value;
		add_child(parser, instance, add_node(parser, connection));
		item = value_close + 1;
		if (item < close)
		{
			if (!token_is(parser, item, ","))
				return fail(parser, item, "instance connections require `,`");
			item++;
		}
	}
	return 1;
}

static int parse_structural_instance(syntax_parser *parser,
	pigen_syntax_id module, size_t start, size_t limit, size_t *after)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id instance;
	size_t at = start + 2, name = start + 1, close, semicolon, opaque_start, opaque_end;
	size_t parameter_open = limit, parameter_close = limit;
	const pigen_expanded_token *start_tok = token_at(parser, start);
	const pigen_expanded_token *last_tok;

	if (token_is(parser, start + 1, "#"))
	{
		parameter_open = start + 2;
		parameter_close = matching_parenthesis(parser, parameter_open, limit);
		if (parameter_close == limit) return 0;
		name = parameter_close + 1;
		at = name + 1;
	}
	if (token_is(parser, at, "#"))
	{
		parameter_open = at + 1;
		parameter_close = matching_parenthesis(parser, parameter_open, limit);
		if (parameter_close == limit) return 0;
		at = parameter_close + 1;
	}
	if (!token_is(parser, at, "(")) return 0;
	close = matching_parenthesis(parser, at, limit);
	if (close == limit || !token_is(parser, close + 1, ";")) return 0;
	semicolon = close + 1;
	opaque_start = parser->opaque_byte_cursor;
	opaque_end = start_tok ? pigen_origin_expansion_span(parser->expanded,
		start_tok->origin).start : opaque_start;
	add_opaque_bytes(parser, module, opaque_start, opaque_end);
	node.kind = PIGEN_SYNTAX_INSTANCE;
	node.location = range_location(parser, start, semicolon + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.instance.module = (pigen_token_id){(uint32_t)start};
	node.as.instance.name = (pigen_token_id){(uint32_t)name};
	node.as.instance.has_parameters = parameter_open != limit;
	instance = add_node(parser, node);
	add_child(parser, module, instance);
	if (parameter_open != limit &&
		!parse_instance_connections(parser, instance, parameter_open, parameter_close,
			PIGEN_SYNTAX_PARAMETER_CONNECTION)) return 0;
	if (!parse_instance_connections(parser, instance, at, close,
		PIGEN_SYNTAX_CONNECTION)) return 0;
	last_tok = token_at(parser, semicolon);
	if (last_tok)
		parser->opaque_byte_cursor = pigen_origin_expansion_span(
			parser->expanded, last_tok->origin).end;
	*after = semicolon + 1;
	return 1;
}

static int retained_body_end(syntax_parser *parser, size_t body, size_t limit,
	size_t *end_out)
{
	size_t end;

	if (token_is(parser, body, "begin"))
	{
		size_t depth = 0;

		for (end = body; end < limit; end++)
		{
			if (token_is(parser, end, "begin")) depth++;
			else if (token_is(parser, end, "end") && !--depth) break;
		}
		if (end == limit) return 0;
	}
	else
	{
		end = top_level_token(parser, body, limit, ";");
		if (end == limit) return 0;
	}
	*end_out = end;
	return 1;
}

static int parse_retained_unknown_item(syntax_parser *parser,
	pigen_syntax_id module, size_t start, size_t limit, size_t *after_item)
{
	static const char *const pigen_words[] = {
		"buf", "port", "fifo", "skid", "pipeline", "fsm", "transfer",
		"validate", "invalidate", "flush", "peek", "pipeline_reset",
		"ready", "valid", "accepts", "when",
	};
	size_t end, body;
	pigen_syntax_node node = {0};
	size_t opaque_start, opaque_end;
	const pigen_expanded_token *start_tok = token_at(parser, start);
	const pigen_expanded_token *last_tok;

	if (token_is(parser, start, "generate"))
	{
		size_t depth = 0;

		for (end = start; end < limit; end++)
		{
			if (token_is(parser, end, "generate")) depth++;
			else if (token_is(parser, end, "endgenerate") && !--depth) break;
		}
		if (end == limit) return 0;
	}
	else if (token_is(parser, start, "case") || token_is(parser, start, "casex") ||
		token_is(parser, start, "casez"))
	{
		size_t depth = 0;
		size_t open = start + 1;

		if (!token_is(parser, open, "(")) return 0;
		open = matching_parenthesis(parser, open, limit);
		if (open == limit) return 0;
		for (end = open + 1; end < limit; end++)
		{
			if (token_is(parser, end, "case") || token_is(parser, end, "casex") ||
				token_is(parser, end, "casez")) depth++;
			else if (token_is(parser, end, "endcase") && !--depth) break;
		}
		if (end == limit) return 0;
	}
	else if (token_is(parser, start, "if") || token_is(parser, start, "for") ||
		token_is(parser, start, "while"))
	{
		size_t open = start + 1;
		size_t body_end;

		if (!token_is(parser, open, "(")) return 0;
		open = matching_parenthesis(parser, open, limit);
		if (open == limit) return 0;
		body = open + 1;
		if (!retained_body_end(parser, body, limit, &body_end)) return 0;
		if (token_is(parser, start, "if") && token_is(parser, body_end + 1, "else"))
		{
			size_t else_body = body_end + 2;

			if (!retained_body_end(parser, else_body, limit, &body_end))
				return 0;
		}
		end = body_end;
	}
	else if (token_is(parser, start, "initial") || token_is(parser, start, "final") ||
		token_is(parser, start, "always_comb") || token_is(parser, start, "always_latch"))
	{
		body = start + 1;
		if (!retained_body_end(parser, body, limit, &end)) return 0;
	}
	else
	{
		end = top_level_token(parser, start, limit, ";");
		if (end == limit) return 0;
	}
	for (size_t at = start; at <= end; at++)
	{
		if (protocol_at(parser, at) != PIGEN_PROTOCOL_NONE)
			return fail(parser, at, "AXI declaration requires a supported module item");
		for (size_t k = 0; k < sizeof pigen_words / sizeof *pigen_words; k++)
			if (token_is(parser, at, pigen_words[k]))
				return fail(parser, at,
					"structured construct in an unsupported module item is not supported");
	}
	opaque_start = parser->opaque_byte_cursor;
	opaque_end = start_tok ? pigen_origin_expansion_span(parser->expanded,
		start_tok->origin).start : opaque_start;
	add_opaque_bytes(parser, module, opaque_start, opaque_end);
	node.kind = PIGEN_SYNTAX_RETAINED;
	node.location = range_location(parser, start, end + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	add_child(parser, module, add_node(parser, node));
	last_tok = token_at(parser, end);
	if (last_tok)
		parser->opaque_byte_cursor = pigen_origin_expansion_span(
			parser->expanded, last_tok->origin).end;
	*after_item = end + 1;
	return 1;
}

static void add_retained_range(syntax_parser *parser, pigen_syntax_id module,
	size_t start, size_t after)
{
	pigen_syntax_node node = {0};

	node.kind = PIGEN_SYNTAX_RETAINED;
	node.location = range_location(parser, start, after);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	add_child(parser, module, add_node(parser, node));
}

static int parse_import(syntax_parser *parser,pigen_syntax_id scope,size_t first,size_t limit,syntax_cursor *cursor,size_t *after)
{
 int exporting=token_is(parser,first,"export");
 size_t end=top_level_token(parser,first,limit,";");
 if(end==limit) return fail(parser,first,"package import/export requires a semicolon");
 add_opaque(parser,scope,*cursor,first);
 for(size_t at=first+1;at<end;)
 {
  if((!identifier(parser,at) && !(exporting && token_is(parser,at,"*") && token_is(parser,at+2,"*"))) || !token_is(parser,at+1,"::") || (!identifier(parser,at+2) && !token_is(parser,at+2,"*")))
   return fail(parser,at,"package import requires package::name or package::*");
  pigen_syntax_node node={0};node.kind=exporting ? PIGEN_SYNTAX_EXPORT : PIGEN_SYNTAX_IMPORT;
  node.parent=node.first_child=node.last_child=node.next_sibling=INVALID_SYNTAX;
  node.as.package_import.package=(pigen_token_id){(uint32_t)at};
  node.as.package_import.member=(pigen_token_id){(uint32_t)(at+2)};
  node.as.package_import.wildcard=token_is(parser,at+2,"*");
  node.as.package_import.is_structured=structured_package_name(parser,node.as.package_import.package,at).index!=PIGEN_INVALID_ID;
  node.location=range_location(parser,at,at+3);add_child(parser,scope,add_node(parser,node));
  if(exporting && !node.as.package_import.wildcard)
  {
   pigen_syntax_id source=structured_package_name(parser,node.as.package_import.package,at);
   const typedef_name_fact *found=typedef_name_fact_in_scope(parser,node.as.package_import.member,source);
   if(found && !typedef_name_fact_in_scope(parser,node.as.package_import.member,scope))
   {
    typedef_name_fact fact=*found;
    add_typedef_name_fact(parser,node.as.package_import.member,scope,fact.kind);
    parser->typedef_name_facts[parser->typedef_name_fact_count-1].family=fact.family;
   }
  }
  at+=3;if(at<end) {if(!token_is(parser,at,",")) return fail(parser,at,"package imports require comma-separated entries");at++;if(at==end) return fail(parser,at,"package import cannot end with a comma");}
 }
 if(first+1==end) return fail(parser,first,"package import requires an entry");
 *after=end+1;*cursor=*after;advance_opaque_byte_cursor(parser,*after);return 1;
}

static int inquiry_requires_structure(syntax_parser *parser,pigen_syntax_id module,
 size_t first,size_t last,size_t module_limit);

static int parse_retained_module_item(syntax_parser *parser,
	pigen_syntax_id module, size_t start, size_t limit, size_t *after_item)
{
	size_t semicolon = top_level_token(parser, start, limit, ";");
	pigen_syntax_node node = {0};
	int structured_assignment = 0;
	size_t opaque_start;
	size_t opaque_end;
	const pigen_expanded_token *start_tok = token_at(parser, start);
	const pigen_expanded_token *last_tok;

	if (semicolon == limit) return 0;
	if (token_is(parser, start, "assign") &&
		top_level_token(parser, start + 1, semicolon, "=") == semicolon)
		for (size_t at = start + 1; at < semicolon; at++)
			if (token_is(parser, at, "<="))
				return fail(parser, at,
					"transfers are allowed only in clocked always blocks");
	opaque_start = parser->opaque_byte_cursor;
	opaque_end = start_tok ? pigen_origin_expansion_span(parser->expanded,
		start_tok->origin).start : opaque_start;
	add_opaque_bytes(parser, module, opaque_start, opaque_end);
	node.kind = PIGEN_SYNTAX_RETAINED;
	if (token_is(parser, start, "assign"))
	{
		int intrinsic = inquiry_requires_structure(parser,module,start+1,semicolon,limit);
		for (size_t at = start + 1; at + 1 < semicolon; at++)
			if (token_is(parser, at + 1, "(") &&
				(token_is(parser, at, "ready") || token_is(parser, at, "valid") ||
				 token_is(parser, at, "accepts") || token_is(parser, at, "peek")))
				intrinsic = 1;
		for(size_t at=start+1;at+1<semicolon;at++)
		{
			size_t after_alias=protocol_alias_after(parser,at);
			if(protocol_alias_construct_at(parser,module,at,semicolon) &&
				token_is(parser,after_alias,"'") && token_is(parser,after_alias+1,"(")) intrinsic=1;
			if((token_is(parser,at,"uint") || token_is(parser,at,"int") || token_is(parser,at,"bit")) && token_is(parser,at+1,"["))
			{
				size_t close=matching_bracket(parser,at+1,semicolon);
				if(close+2<semicolon && token_is(parser,close+1,"'") && token_is(parser,close+2,"(")) intrinsic=1;
			}
		}
		if (intrinsic || parser->generate_depth)
		{
			structured_assignment = 1;
			if (start + 1 == semicolon)
				return fail(parser, start, "continuous assignment requires a binding");
			for (size_t first = start + 1; first < semicolon;)
			{
				size_t end = top_level_token(parser, first, semicolon, ",");
				size_t equal = top_level_token(parser, first, end, "=");
				if (equal == end ||
				!pigen_parse_expression(parser->expanded, first, equal,
					&parser->tree->expressions, &parser->tree->types,
					&node.as.nonblocking_assignment.destination, parser->error) ||
				!pigen_parse_expression(parser->expanded, equal + 1, end,
					&parser->tree->expressions, &parser->tree->types,
					&node.as.nonblocking_assignment.value, parser->error))
					return fail(parser, first, "continuous assignment requires a destination and expression per binding");
				node.kind = PIGEN_SYNTAX_CONTINUOUS_ASSIGNMENT;
				/* Each binding is a semantic assignment, but the enclosing
				 * lexical item still spans the complete assign statement. */
				node.location = range_location(parser, start, semicolon + 1);
				node.parent = INVALID_SYNTAX;
				node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
				add_child(parser, module, add_node(parser, node));
				if (end == semicolon) break;
				first = end + 1;
				if (first == semicolon)
					return fail(parser, end, "continuous assignment requires a binding after comma");
			}
		}
	}
	node.location = range_location(parser, start, semicolon + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	if (!structured_assignment) add_child(parser, module, add_node(parser, node));
	last_tok = token_at(parser, semicolon);
	if (last_tok)
		parser->opaque_byte_cursor = pigen_origin_expansion_span(
			parser->expanded, last_tok->origin).end;
	*after_item = semicolon + 1;
	return 1;
}

static int retained_procedural_end(syntax_parser *parser, size_t start,
	size_t limit, size_t *end_out)
{
	size_t event_close=matching_parenthesis(parser,start+2,limit);
	if(event_close==limit) return 0;
	size_t body=event_close+1,after=fsm_skip_statement(parser,body,limit);
	if(after<=body || after>limit) return 0;
	*end_out=after-1;return 1;
}

static int retained_process_has_structured(syntax_parser *parser,
	size_t start, size_t end)
{
	size_t at;

	for (at = start; at <= end; at++)
		if (token_is(parser, at, "pipeline") ||
			token_is(parser, at, "when") ||
			token_is(parser, at, "fsm") ||
			token_is(parser, at, "validate") ||
			token_is(parser, at, "invalidate") ||
			token_is(parser, at, "flush") ||
			token_is(parser, at, "pipeline_reset") ||
			token_is(parser, at, "pipe_reset"))
			return 1;
	return 0;
}

static int parse_retained_procedural(syntax_parser *parser,
	pigen_syntax_id module, size_t start, size_t limit, size_t *after_item)
{
	size_t end;
	pigen_syntax_node node = {0};
	size_t opaque_start, opaque_end;
	const pigen_expanded_token *start_tok = token_at(parser, start);
	const pigen_expanded_token *last_tok;

	if (!retained_procedural_end(parser, start, limit, &end)) return 0;

	opaque_start = parser->opaque_byte_cursor;
	opaque_end = start_tok ? pigen_origin_expansion_span(parser->expanded,
		start_tok->origin).start : opaque_start;
	add_opaque_bytes(parser, module, opaque_start, opaque_end);
	node.kind = PIGEN_SYNTAX_RETAINED;
	node.location = range_location(parser, start, end + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	add_child(parser, module, add_node(parser, node));
	last_tok = token_at(parser, end);
	if (last_tok)
		parser->opaque_byte_cursor = pigen_origin_expansion_span(
			parser->expanded, last_tok->origin).end;
	*after_item = end + 1;
	return 1;
}

static void remember_function_name(syntax_parser *parser, size_t start,
	size_t end)
{
	size_t at;

	for (at = start; at + 1 < end; at++)
		if (token_is(parser, at + 1, "(") && identifier(parser, at))
		{
			if (parser->function_name_count ==
				parser->function_name_capacity)
			{
				parser->function_name_capacity =
					parser->function_name_capacity ?
					parser->function_name_capacity * 2 : 8;
				parser->function_names = pigen_resize(
					parser->function_names,
					parser->function_name_capacity *
					sizeof(*parser->function_names));
			}
			parser->function_names[parser->function_name_count++] =
				(pigen_token_id){(uint32_t)at};
			return;
		}
}

static int token_is_function_name(syntax_parser *parser, pigen_token_id token)
{
	size_t i;

	for (i = 0; i < parser->function_name_count; i++)
		if (tokens_equal(parser, parser->function_names[i], token)) return 1;
	return 0;
}

static int expression_has_function_call(syntax_parser *parser,
	pigen_syntax_expr_id expression, size_t depth)
{
	const pigen_syntax_expr *known = pigen_syntax_expr_get(
		&parser->tree->expressions, expression);
	const pigen_syntax_expr_id *children;
	size_t i;

	if (!known || depth > 64) return 0;
	switch (known->kind)
	{
		case PIGEN_SYNTAX_EXPR_CALL:
		{
			const pigen_syntax_expr *callee = pigen_syntax_expr_get(
				&parser->tree->expressions, known->as.call.callee);

			if (callee && callee->kind == PIGEN_SYNTAX_EXPR_NAME &&
				token_is_function_name(parser, callee->as.atom.token))
				return 1;
			/* Runtime calls not owned by typed expression resolution belong
			 * to retained SV even when no preceding local definition named
			 * them (qualified/imported/legacy/extern/system calls). */
			static const char *const typed_calls[]={"valid","ready","accepts","peek","$bits","$left","$right","$low","$high",
				"$increment","$size","$dimensions","$unpacked_dimensions","$signed","$unsigned","$clog2"};
			int typed=0;
			if(callee && callee->kind==PIGEN_SYNTAX_EXPR_NAME)
				for(size_t k=0;k<sizeof typed_calls/sizeof *typed_calls;k++)
					if(token_is(parser,callee->as.atom.token.index,typed_calls[k])) typed=1;
			if(!typed) return 1;
			children = pigen_syntax_expr_children(&parser->tree->expressions,
				known->as.call.first_argument, known->as.call.argument_count);
			for (i = 0; children && i < known->as.call.argument_count; i++)
				if (expression_has_function_call(parser, children[i], depth + 1))
					return 1;
			return 0;
		}
		case PIGEN_SYNTAX_EXPR_GROUP:
			return expression_has_function_call(parser,
				known->as.group.operand, depth + 1);
		case PIGEN_SYNTAX_EXPR_UNARY:
			return expression_has_function_call(parser,
				known->as.unary.operand, depth + 1);
		case PIGEN_SYNTAX_EXPR_BINARY:
			return expression_has_function_call(parser,
				known->as.binary.left, depth + 1) ||
				expression_has_function_call(parser,
					known->as.binary.right, depth + 1);
		case PIGEN_SYNTAX_EXPR_CONDITIONAL:
			return expression_has_function_call(parser,
				known->as.conditional.condition, depth + 1) ||
				expression_has_function_call(parser,
					known->as.conditional.when_true, depth + 1) ||
				expression_has_function_call(parser,
					known->as.conditional.when_false, depth + 1);
		case PIGEN_SYNTAX_EXPR_INDEX:
			return expression_has_function_call(parser, known->as.index.base,
				depth + 1) || expression_has_function_call(parser,
					known->as.index.index, depth + 1);
		case PIGEN_SYNTAX_EXPR_SELECT:
			return expression_has_function_call(parser, known->as.select.base,
				depth + 1) || expression_has_function_call(parser,
					known->as.select.left, depth + 1) ||
				expression_has_function_call(parser, known->as.select.right,
					depth + 1);
		case PIGEN_SYNTAX_EXPR_MEMBER:
			return expression_has_function_call(parser, known->as.member.base,
				depth + 1);
		case PIGEN_SYNTAX_EXPR_CAST:
			return expression_has_function_call(parser, known->as.cast.value,
				depth + 1);
		case PIGEN_SYNTAX_EXPR_CONCATENATION:
			children = pigen_syntax_expr_children(&parser->tree->expressions,
				known->as.sequence.first_child,
				known->as.sequence.child_count);
			for (i = 0; children && i < known->as.sequence.child_count; i++)
				if (expression_has_function_call(parser, children[i], depth + 1))
					return 1;
			return 0;
		case PIGEN_SYNTAX_EXPR_REPLICATION:
		case PIGEN_SYNTAX_EXPR_ARRAY_PATTERN_REPLICATION:
			if(expression_has_function_call(parser,known->as.replication.count,depth+1)) return 1;
			children=pigen_syntax_expr_children(&parser->tree->expressions,
				known->as.replication.first_child,known->as.replication.child_count);
			for(i=0;children && i<known->as.replication.child_count;i++)
				if(expression_has_function_call(parser,children[i],depth+1)) return 1;
			return 0;
		default:
			return 0;
	}
}

static int node_has_structured_construct(syntax_parser *parser,
	pigen_syntax_id node_id, size_t depth)
{
	const pigen_syntax_node *node = pigen_syntax_get(parser->tree, node_id);
	pigen_syntax_id child;

	if (!node || depth > 64) return 0;
	if (node->kind == PIGEN_SYNTAX_PIPELINE ||
		node->kind == PIGEN_SYNTAX_FSM ||
		node->kind == PIGEN_SYNTAX_ACTION)
		return 1;
	for (child = node->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *known = pigen_syntax_get(parser->tree, child);

		if (!known) break;
		if (node_has_structured_construct(parser, child, depth + 1)) return 1;
		child = known->next_sibling;
	}
	return 0;
}

static int node_has_function_call(syntax_parser *parser,
	pigen_syntax_id node_id, size_t depth)
{
	const pigen_syntax_node *node = pigen_syntax_get(parser->tree, node_id);
	pigen_syntax_id child;

	if (!node || depth > 64) return 0;
	if (node->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT &&
		expression_has_function_call(parser,
			node->as.nonblocking_assignment.value, 0))
		return 1;
	if (node->kind == PIGEN_SYNTAX_IF_STATEMENT &&
		expression_has_function_call(parser, node->as.if_statement.condition, 0))
		return 1;
	if (node->kind==PIGEN_SYNTAX_PROCEDURAL_CASE &&
		expression_has_function_call(parser,node->as.procedural_case.selector,0)) return 1;
	if (node->kind==PIGEN_SYNTAX_PROCEDURAL_CASE_LABEL &&
		expression_has_function_call(parser,node->as.generate_case_label.value,0)) return 1;
	if (node->kind == PIGEN_SYNTAX_WHEN_STATEMENT &&
		(expression_has_function_call(parser, node->as.when_statement.destination, 0) ||
		 expression_has_function_call(parser, node->as.when_statement.value, 0)))
		return 1;
	for (child = node->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *known = pigen_syntax_get(parser->tree, child);

		if (!known) break;
		if (node_has_function_call(parser, child, depth + 1)) return 1;
		child = known->next_sibling;
	}
	return 0;
}

static int parse_retained_block_item(syntax_parser *parser,
	pigen_syntax_id module, size_t start, size_t limit, const char *closer,
	size_t *after_item)
{
	size_t end = start;
	pigen_syntax_node node = {0};
	size_t opaque_start;
	size_t opaque_end;
	const pigen_expanded_token *start_tok = token_at(parser, start);
	const pigen_expanded_token *last_tok;

	while (end < limit && !token_is(parser, end, closer)) end++;
	if (end == limit) return 0;
	opaque_start = parser->opaque_byte_cursor;
	opaque_end = start_tok ? pigen_origin_expansion_span(parser->expanded,
		start_tok->origin).start : opaque_start;
	add_opaque_bytes(parser, module, opaque_start, opaque_end);
	node.kind = PIGEN_SYNTAX_RETAINED;
	node.location = range_location(parser, start, end + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	add_child(parser, module, add_node(parser, node));
	last_tok = token_at(parser, end);
	if (last_tok)
		parser->opaque_byte_cursor = pigen_origin_expansion_span(
			parser->expanded, last_tok->origin).end;
	if (closer && !strcmp(closer, "endfunction"))
		remember_function_name(parser, start, end);
	*after_item = end + 1;
	return 1;
}

static int parse_fabric_endpoint(syntax_parser *parser, size_t *at, size_t limit,
	pigen_token_id *instance, pigen_token_id *port, pigen_syntax_expr_id *expression)
{
	size_t first = *at;
	if (!identifier(parser, *at)) return fail(parser, *at, "fabric endpoint requires a native endpoint or instance.port");
	*instance = (pigen_token_id){(uint32_t)(*at)++};
	*port = (pigen_token_id){PIGEN_INVALID_ID};
	int suffixes=0;
	while(token_is(parser,*at,".") || token_is(parser,*at,"["))
	{
		if(token_is(parser,*at,"."))
		{
			if(!identifier(parser,*at+1)) return fail(parser,*at,"fabric endpoint requires a member name");
			if(!suffixes) *port=(pigen_token_id){(uint32_t)(*at+1)};
			else *port=(pigen_token_id){PIGEN_INVALID_ID};
			*at+=2;
		}
		else
		{
			size_t close=matching_bracket(parser,*at,limit);
			if(close>=limit) return fail(parser,*at,"fabric endpoint index requires closing bracket");
			*at=close+1;
		}
		suffixes++;
	}
	return pigen_parse_expression(parser->expanded, first, *at, &parser->tree->expressions,
		&parser->tree->types, expression, parser->error);
}

/* Recognize the endpoint prefix without speculating about a packed data type:
 * an indexed endpoint such as s[0] must remain a connection. */
static int fabric_connection_at(const syntax_parser *parser,size_t at,size_t limit)
{
	if(!identifier(parser,at)) return 0;
	at++;
	while(at<limit && (token_is(parser,at,".") || token_is(parser,at,"[")))
	{
		if(token_is(parser,at,"."))
		{
			if(!identifier(parser,at+1)) return 0;
			at+=2;
		}
		else
		{
			size_t close=matching_bracket(parser,at,limit);
			if(close==limit) return 0;
			at=close+1;
		}
	}
	return token_is(parser,at,">") || token_is(parser,at,"->") || token_is(parser,at,"-->") || token_is(parser,at,"-");
}

static int parse_fabric_block(syntax_parser *parser, pigen_syntax_id module,
	size_t start, size_t limit, size_t body_start, size_t *after_fabric)
{
	pigen_syntax_node node = {0};
	pigen_syntax_id fabric;
	size_t at = start + 1, endfabric;
	size_t opaque_start, opaque_end;
	const pigen_expanded_token *start_tok = token_at(parser, start);
	const pigen_expanded_token *last_tok;

	node.kind = PIGEN_SYNTAX_FABRIC;
	node.as.fabric.name = (pigen_token_id){PIGEN_INVALID_ID};
	if (!token_is(parser, at, "begin"))
	{
		if (!identifier(parser, at)) return fail(parser, at, "fabric requires a name or `begin`");
		node.as.fabric.name = (pigen_token_id){(uint32_t)at++};
	}
	if (body_start) at = body_start;
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	if (at >= limit || !token_is(parser, at, "begin")) return fail(parser, at, "fabric requires `begin`");
	at++;
	opaque_start = parser->opaque_byte_cursor;
	opaque_end = start_tok ? pigen_origin_expansion_span(parser->expanded, start_tok->origin).start : opaque_start;
	add_opaque_bytes(parser, module, opaque_start, opaque_end);
	fabric = add_node(parser, node);
	add_child(parser, module, fabric);
	while (at < limit && !token_is(parser, at, "endfabric"))
	{
		/* A typed standalone fabric owns a module scope. Reuse declaration
		 * parsing there so local storage and native aliases keep their usual
		 * type/binding contracts; connections remain children of the fabric. */
		if(body_start && !fabric_connection_at(parser,at,limit))
		{
			size_t semicolon=top_level_token(parser,at,limit,";"),cursor=at;
			if(semicolon<limit)
			{
				if(token_is(parser,at,"import"))
				{if(!parse_import(parser,module,at,limit,&cursor,&at)) return 0;continue;}
				if(token_is(parser,at,"typedef"))
				{
					if(parse_typedef(parser,module,at,semicolon,&cursor)!=DECLARATION_RECOGNIZED)
						return fail(parser,at,"typed fabric requires a supported typedef declaration");
					at=semicolon+1;continue;
				}
				syntax_checkpoint checkpoint=save_checkpoint(parser);
				pigen_syntax_id declaration=INVALID_SYNTAX;int decisive=0;
				declaration_parse_result result=parse_data_first_declaration(parser,module,at,semicolon,0,&declaration,&decisive);
				if(result==DECLARATION_INVALID) return 0;
				if(result==DECLARATION_RECOGNIZED)
				{
					commit_declaration(parser,module,at,semicolon+1,&cursor,declaration);
					at=semicolon+1;continue;
				}
				restore_checkpoint(parser,checkpoint);
			}
		}
		pigen_syntax_node edge = {0};
		pigen_token_id left_instance, left_port, right_instance, right_port;
		int direct = 1, tier = 0;
		size_t arrow;
		arrow = at;
		if (!parse_fabric_endpoint(parser, &arrow, limit, &left_instance, &left_port,
			&edge.as.fabric_connection.left_expression)) return 0;
		if (token_is(parser, arrow, ">"))
			arrow++;
		else if (token_is(parser, arrow, "->"))
		{
			direct = 0;
			tier = 1;
			arrow++;
		}
		else if (token_is(parser, arrow, "-->"))
		{
			direct = 0;
			tier = 2;
			arrow++;
		}
		else
		{
			direct = 0;
			while (token_is(parser, arrow, "-")) { tier++; arrow++; }
			if (!token_is(parser, arrow, ">"))
				return fail(parser, arrow,
					"fabric connection requires `>` or routed `->`");
			tier++;
			arrow++;
		}
		if (!parse_fabric_endpoint(parser, &arrow, limit, &right_instance, &right_port,
			&edge.as.fabric_connection.right_expression)) return 0;
		if(token_is(parser,arrow,"at"))
		{
			if(!token_is(parser,arrow+1,"[")) return fail(parser,arrow,"fabric address window requires [base:limit]");
			size_t first=arrow+2,close=matching_bracket(parser,arrow+1,limit),colon=close;
			size_t parens=0,brackets=0,braces=0,questions=0;
			if(close>=limit) return fail(parser,arrow,"fabric address window requires closing bracket");
			for(size_t i=first;i<close;i++)
			{
				if(!parens && !brackets && !braces)
				{
					if(token_is(parser,i,"?")) questions++;
					if(token_is(parser,i,":")) {if(questions) questions--;else {colon=i;break;}}
				}
				if(token_is(parser,i,"(")) parens++;else if(token_is(parser,i,")") && parens) parens--;
				else if(token_is(parser,i,"[")) brackets++;else if(token_is(parser,i,"]") && brackets) brackets--;
				else if(token_is(parser,i,"{")) braces++;else if(token_is(parser,i,"}") && braces) braces--;
			}
			if(colon==first || colon+1>=close) return fail(parser,arrow,"fabric address window requires base:limit");
			if(!pigen_parse_expression(parser->expanded,first,colon,&parser->tree->expressions,&parser->tree->types,
				&edge.as.fabric_connection.window_base,parser->error) ||
				!pigen_parse_expression(parser->expanded,colon+1,close,&parser->tree->expressions,&parser->tree->types,
				&edge.as.fabric_connection.window_limit,parser->error)) return 0;
			edge.as.fabric_connection.has_window=1;arrow=close+1;
		}
		if (!token_is(parser, arrow, ";")) return fail(parser, arrow, "fabric connection must end with semicolon");
		edge.kind = PIGEN_SYNTAX_FABRIC_CONNECTION;
		edge.location = range_location(parser, at, arrow + 1);
		edge.parent = INVALID_SYNTAX;
		edge.first_child = edge.last_child = edge.next_sibling =
			INVALID_SYNTAX;
		edge.as.fabric_connection.left_instance = left_instance;
		edge.as.fabric_connection.left_port = left_port;
		edge.as.fabric_connection.right_instance = right_instance;
		edge.as.fabric_connection.right_port = right_port;
		edge.as.fabric_connection.direct = direct;
		edge.as.fabric_connection.tier = tier;
		add_child(parser, fabric, add_node(parser, edge));
		at = arrow + 1;
	}
	if (at >= limit || !token_is(parser, at, "endfabric"))
		return fail(parser, at, "fabric requires `endfabric`");
	endfabric = at;
	node.location = range_location(parser, start, endfabric + 1);
	parser->tree->nodes[fabric.index].location = node.location;
	last_tok = token_at(parser, endfabric);
	if (last_tok)
		parser->opaque_byte_cursor = pigen_origin_expansion_span(
			parser->expanded, last_tok->origin).end;
	*after_fabric = endfabric + 1;
	return 1;
}

static int parse_module_items(syntax_parser *parser, pigen_syntax_id module,
	size_t first, size_t after, syntax_cursor *opaque_cursor);

static pigen_syntax_id generate_node(syntax_parser *parser, pigen_syntax_id parent,
	pigen_syntax_kind kind, size_t first, size_t after)
{
	pigen_syntax_node node = {0};
	node.kind = kind; node.location = range_location(parser, first, after);
	node.parent = node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	pigen_syntax_id id = add_node(parser, node);
	add_child(parser, parent, id);
	return id;
}

static int parse_generate_item(syntax_parser *parser, pigen_syntax_id parent,
	size_t first, size_t limit, size_t *after);

static int parse_generate_body(syntax_parser *parser, pigen_syntax_id parent,
	size_t first, size_t limit, size_t *after)
{
	if (token_is(parser, first, "if") || token_is(parser, first, "for") || token_is(parser, first, "case"))
		return parse_generate_item(parser, parent, first, limit, after);
	pigen_token_id name = INVALID_ID(pigen_token_id);
	size_t body = first, end, last;
	if (token_is(parser, first, "begin"))
	{
		end = matching_begin_end(parser, first, limit);
		if (end == limit) return fail(parser, first, "generate block requires `end`");
		body++;
		if (token_is(parser, body, ":"))
		{
			if (!identifier(parser, body+1)) return fail(parser, body, "generate block requires a name after colon");
			name = (pigen_token_id){(uint32_t)(body+1)}; body += 2;
		}
		last = end+1;
		if (token_is(parser, last, ":"))
		{
			if (!identifier(parser, last+1) || name.index == PIGEN_INVALID_ID ||
				!tokens_equal(parser, name, (pigen_token_id){(uint32_t)(last+1)}))
				return fail(parser, last, "generate end label must match its block name");
			last += 2;
		}
	}
	else
	{
		if (token_is(parser, first, "always") || token_is(parser, first, "always_ff") ||
			token_is(parser, first, "initial"))
		{
			if (!retained_procedural_end(parser, first, limit, &end))
				return fail(parser, first, "unterminated generate body");
		}
		else if (token_is(parser, first, "fabric"))
		{
			for (end=first; end<limit && !token_is(parser,end,"endfabric"); end++) {}
		}
		else end = top_level_token(parser, first, limit, ";");
		if (end >= limit) return fail(parser, first, "generate body requires one module item");
		last = end+1; end = last;
		if(token_is(parser,first,";")) body=last;
	}
	pigen_syntax_id block = generate_node(parser, parent, PIGEN_SYNTAX_GENERATE_BLOCK, first, last);
	parser->tree->nodes[block.index].as.generate_block.name = name;
	advance_opaque_byte_cursor(parser, body);
	syntax_cursor cursor = body;
	if (!parse_module_items(parser, block, body, end, &cursor)) return 0;
	advance_opaque_byte_cursor(parser, last);
	*after = last;
	return 1;
}


/* Commas/colons inside grouped expressions or ternaries are not case-item
 * separators. Keep every label as syntax so semantic sizing sees the full set. */
static size_t generate_case_label_end(syntax_parser *parser,size_t first,size_t limit)
{
	size_t grouping=0,conditional=0;
	for(size_t at=first;at<limit;at++)
	{
		if(token_is(parser,at,"(") || token_is(parser,at,"[") || token_is(parser,at,"{")) grouping++;
		else if(token_is(parser,at,")") || token_is(parser,at,"]") || token_is(parser,at,"}")) {if(grouping) grouping--;}
		else if(!grouping)
		{
			if(token_is(parser,at,"?")) conditional++;
			else if(token_is(parser,at,":")) {if(conditional) conditional--;else return at;}
			else if(token_is(parser,at,",") && !conditional) return at;
			else if(token_is(parser,at,"endcase")) return limit;
		}
	}
	return limit;
}

static int parse_generate_case(syntax_parser *parser,pigen_syntax_id parent,
	size_t first,size_t limit,size_t *after)
{
	if(!token_is(parser,first+1,"(")) return fail(parser,first,"case generate requires a parenthesized selector");
	size_t close=matching_parenthesis(parser,first+1,limit);
	if(close==limit) return fail(parser,first,"unterminated case generate selector");
	pigen_syntax_id selection=generate_node(parser,parent,PIGEN_SYNTAX_GENERATE_CASE,first,close+1);
	if(!pigen_parse_expression(parser->expanded,first+2,close,&parser->tree->expressions,&parser->tree->types,
		&parser->tree->nodes[selection.index].as.generate_case.selector,parser->error)) return 0;
	size_t at=close+1,items=0;int has_default=0;
	advance_opaque_byte_cursor(parser,at);
	while(at<limit && !token_is(parser,at,"endcase"))
	{
		size_t item_first=at;
		pigen_syntax_id item=generate_node(parser,selection,PIGEN_SYNTAX_GENERATE_CASE_ITEM,at,at+1);
		if(token_is(parser,at,"default"))
		{
			if(has_default) return fail(parser,at,"case generate cannot contain multiple defaults");
			has_default=1;parser->tree->nodes[item.index].as.generate_case_item.is_default=1;
			at++;if(token_is(parser,at,":")) at++;
		}
		else for(;;)
		{
			size_t end=generate_case_label_end(parser,at,limit);
			if(end==limit || end==at) return fail(parser,at,"case generate requires constant labels followed by a colon");
			pigen_syntax_id label=generate_node(parser,item,PIGEN_SYNTAX_GENERATE_CASE_LABEL,at,end);
			if(!pigen_parse_expression(parser->expanded,at,end,&parser->tree->expressions,&parser->tree->types,
				&parser->tree->nodes[label.index].as.generate_case_label.value,parser->error)) return 0;
			at=end+1;
			if(token_is(parser,end,":")) break;
		}
		advance_opaque_byte_cursor(parser,at);
		if(!parse_generate_body(parser,item,at,limit,&at)) return 0;
		parser->tree->nodes[item.index].location=range_location(parser,item_first,at);items++;
	}
	if(!items || at==limit) return fail(parser,first,"case generate requires items and endcase");
	*after=at+1;parser->tree->nodes[selection.index].location=range_location(parser,first,*after);
	advance_opaque_byte_cursor(parser,*after);return 1;
}

static int parse_generate_item(syntax_parser *parser, pigen_syntax_id parent,
	size_t first, size_t limit, size_t *after)
{
	if (parser->generate_depth >= 128) return fail(parser, first, "generate nesting exceeds parser limit");
	parser->generate_depth++;
	int ok = 0;
	if(token_is(parser,first,"case")) {ok=parse_generate_case(parser,parent,first,limit,after);goto done;}
	if (!token_is(parser, first, "if") && !token_is(parser, first, "for"))
	{ ok = parse_generate_body(parser, parent, first, limit, after); goto done; }
	int is_for = token_is(parser, first, "for");
	if (!token_is(parser, first+1, "(")) { fail(parser,first,"generate control requires parentheses"); goto done; }
	size_t close = matching_parenthesis(parser, first+1, limit);
	if (close == limit) { fail(parser,first,"unterminated generate control"); goto done; }
	pigen_syntax_id id = generate_node(parser, parent,
		is_for ? PIGEN_SYNTAX_GENERATE_FOR : PIGEN_SYNTAX_GENERATE_IF, first, close+1);
	if (is_for)
	{
		size_t at = first+2;
		int declares = token_is(parser,at,"genvar");
		if (declares) at++;
		if (!identifier(parser,at) || !token_is(parser,at+1,"="))
		{ fail(parser,at,"generate loop requires iterator initialization"); goto done; }
		pigen_token_id iterator = {(uint32_t)at};
		size_t semi = top_level_token(parser,at+2,close,";");
		size_t semi2 = semi<close ? top_level_token(parser,semi+1,close,";") : close;
		if (semi == close || semi2 == close) { fail(parser,at,"generate loop requires initialization, condition and step"); goto done; }
		pigen_syntax_expr_id initial, condition, next = INVALID_ID(pigen_syntax_expr_id);
		if (!pigen_parse_expression(parser->expanded,at+2,semi,&parser->tree->expressions,&parser->tree->types,&initial,parser->error) ||
			!pigen_parse_expression(parser->expanded,semi+1,semi2,&parser->tree->expressions,&parser->tree->types,&condition,parser->error)) goto done;
		at = semi2+1;
		int increment = 0, compound = 0;
		pigen_syntax_operator step_operator = PIGEN_SYNTAX_OP_ADD;
		if (at+2 == close && tokens_equal(parser,iterator,(pigen_token_id){(uint32_t)at}) &&
			(token_is(parser,at+1,"++") || token_is(parser,at+1,"--")))
			increment = token_is(parser,at+1,"++") ? 1 : -1;
		else if (at+2 == close && tokens_equal(parser,iterator,(pigen_token_id){(uint32_t)(at+1)}) &&
			(token_is(parser,at,"++") || token_is(parser,at,"--")))
			increment = token_is(parser,at,"++") ? 1 : -1;
		else if (at+2<close && tokens_equal(parser,iterator,(pigen_token_id){(uint32_t)at}) &&
			(token_is(parser,at+1,"=") || (compound=compound_assignment_operator(parser,at+1,&step_operator))))
		{
			if (!pigen_parse_expression(parser->expanded,at+2,close,&parser->tree->expressions,&parser->tree->types,&next,parser->error)) goto done;
		}
		else { fail(parser,at,"generate loop step must update its iterator"); goto done; }
		parser->tree->nodes[id.index].as.generate_for.iterator = iterator;
		parser->tree->nodes[id.index].as.generate_for.declares_iterator = declares;
		parser->tree->nodes[id.index].as.generate_for.initial = initial;
		parser->tree->nodes[id.index].as.generate_for.condition = condition;
		parser->tree->nodes[id.index].as.generate_for.next_value = next;
		parser->tree->nodes[id.index].as.generate_for.increment = increment;
		parser->tree->nodes[id.index].as.generate_for.compound_step = compound;
		parser->tree->nodes[id.index].as.generate_for.compound_operator = step_operator;
	}
	else if (!pigen_parse_expression(parser->expanded,first+2,close,&parser->tree->expressions,
		&parser->tree->types,&parser->tree->nodes[id.index].as.generate_if.condition,parser->error)) goto done;
	advance_opaque_byte_cursor(parser,close+1);
	if (!parse_generate_body(parser,id,close+1,limit,after)) goto done;
	if (!is_for && token_is(parser,*after,"else"))
	{
		parser->tree->nodes[id.index].as.generate_if.has_else = 1;
		advance_opaque_byte_cursor(parser,*after+1);
		if (!parse_generate_body(parser,id,*after+1,limit,after)) goto done;
	}
	parser->tree->nodes[id.index].location = range_location(parser,first,*after);
	ok = 1;
done:
	parser->generate_depth--;
	if (!ok && (!parser->error || !parser->error->message))
		fail(parser,first,"invalid generate control or body");
	return ok;
}

static size_t generate_region_end(syntax_parser *parser, size_t first, size_t limit)
{
	size_t depth = 0;
	for (size_t at=first;at<limit;at++)
	{
		if (token_is(parser,at,"generate")) depth++;
		else if (token_is(parser,at,"endgenerate") && !--depth) return at;
	}
	return limit;
}

/* Native child interfaces must be elaborated even when every data port is
 * omitted. Inspect source tokens here, before any generate region is retained. */
static int generate_has_native_child(syntax_parser *parser,size_t first,size_t last)
{
	for(size_t at=first;at<last;at++)
	{
		if(!looks_like_plain_instance(parser,at,last) && !looks_like_parameterized_instance(parser,at,last)) continue;
		for(size_t module=0;module+1<parser->expanded->token_count;module++)
		{
			int typed_fabric=token_is(parser,module,"fabric") &&
				(token_is(parser,module+2,"(") || (token_is(parser,module+2,"#") && token_is(parser,module+3,"(")));
			if((!token_is(parser,module,"module") && !typed_fabric) || !tokens_equal(parser,
				(pigen_token_id){(uint32_t)at},(pigen_token_id){(uint32_t)(module+1)})) continue;
			for(size_t item=module+2;item<parser->expanded->token_count && !token_is(parser,item,typed_fabric ? "endfabric" : "endmodule");item++)
				if(protocol_at(parser,item)!=PIGEN_PROTOCOL_NONE || protocol_alias_construct_at(parser,(pigen_syntax_id){0},item,parser->expanded->token_count)) return 1;
		}
	}
	return 0;
}

/* Recognize explicit numerical type syntax without claiming ordinary packed
 * bit ranges or an indexed value named uint. Member names are not types. */
static int numerical_construct_at(const syntax_parser *parser,size_t at,size_t last)
{
	int numerical=token_is(parser,at,"uint") || token_is(parser,at,"int");
	if ((!numerical && !token_is(parser,at,"bit")) || !token_is(parser,at+1,"[") ||
		(at && (token_is(parser,at-1,".") || token_is(parser,at-1,"::")))) return 0;
	size_t next=at+1;
	int count=numerical;
	while (next<last && token_is(parser,next,"["))
	{
		size_t close=matching_bracket(parser,next,last);
		if (close==last) return 1;
		if (top_level_token(parser,next+1,close,":")==close) count=1;
		next=close+1;
	}
	if (next+1<last && token_is(parser,next,"'") && token_is(parser,next+1,"(")) return 1;
	return count && next<last && identifier(parser,next) &&
		!token_is(parser,next,"inside") && !token_is(parser,next,"dist") &&
		!token_is(parser,next,"with") && !token_is(parser,next,"matches") && !token_is(parser,next,"iff");
}

static int generate_requires_structure(syntax_parser *parser, pigen_syntax_id module,
	size_t first, size_t last)
{
	if (process_has_protocol_reference(parser,module,first,last) || generate_has_native_child(parser,first,last) ||
		(first<last && retained_process_has_structured(parser,first,last-1))) return 1;
	for (size_t at=first;at<last;at++)
		if (protocol_alias_construct_at(parser,module,at,last) || numerical_construct_at(parser,at,last) ||
			module_dynamic_name(parser,module,(pigen_token_id){(uint32_t)at},0) ||
			protocol_at(parser,at) != PIGEN_PROTOCOL_NONE || token_is(parser,at,"fabric") ||
			token_is(parser,at,"buf") || token_is(parser,at,"fifo") || token_is(parser,at,"skid") ||
			token_is(parser,at,"pipeline") || token_is(parser,at,"fsm") || token_is(parser,at,"when")) return 1;
	return 0;
}

/* Native foreach headers also select typed lowering when their root is a
 * child instance or a named generated namespace. Forward instances are checked
 * against their module interface before ordinary text retention. */
static int native_foreach_named_target(syntax_parser *parser,pigen_syntax_id scope,pigen_token_id name,size_t depth)
{
	const pigen_syntax_node *owner=pigen_syntax_get(parser->tree,scope);
	if(!owner || depth>128) return 0;
	for(pigen_syntax_id id=owner->first_child;id.index!=PIGEN_INVALID_ID;)
	{
		const pigen_syntax_node *node=pigen_syntax_get(parser->tree,id);
		if(node->kind==PIGEN_SYNTAX_INSTANCE && tokens_equal(parser,node->as.instance.name,name))
			return generate_has_native_child(parser,node->location.extent.first.index,node->location.extent.after.index);
		if(node->kind==PIGEN_SYNTAX_GENERATE_BLOCK && node->as.generate_block.name.index!=PIGEN_INVALID_ID)
		{
			if(tokens_equal(parser,node->as.generate_block.name,name))
				return generate_requires_structure(parser,scope,node->location.extent.first.index,node->location.extent.after.index);
		}
		else if(node->kind==PIGEN_SYNTAX_GENERATE_REGION || node->kind==PIGEN_SYNTAX_GENERATE_BLOCK ||
			node->kind==PIGEN_SYNTAX_GENERATE_FOR || node->kind==PIGEN_SYNTAX_GENERATE_IF ||
			node->kind==PIGEN_SYNTAX_GENERATE_CASE || node->kind==PIGEN_SYNTAX_GENERATE_CASE_ITEM)
			if(native_foreach_named_target(parser,id,name,depth+1)) return 1;
		id=node->next_sibling;
	}
	return 0;
}

static int native_foreach_forward_instance(syntax_parser *parser,pigen_syntax_id module,pigen_token_id name,size_t limit)
{
	const pigen_syntax_node *owner=pigen_syntax_get(parser->tree,module);
	if(!owner || owner->kind!=PIGEN_SYNTAX_MODULE) return 0;
	for(size_t at=owner->as.module.name.index+1;at<limit;at++)
	{
		if(!looks_like_plain_instance(parser,at,limit) && !looks_like_parameterized_instance(parser,at,limit)) continue;
		size_t instance=at+1;
		if(token_is(parser,instance,"#"))
		{size_t close=matching_parenthesis(parser,instance+1,limit);if(close==limit) continue;instance=close+1;}
		if(tokens_equal(parser,(pigen_token_id){(uint32_t)instance},name) && generate_has_native_child(parser,at,top_level_token(parser,at,limit,";")+1)) return 1;
	}
	return 0;
}

static int inquiry_requires_structure(syntax_parser *parser,pigen_syntax_id module,
 size_t first,size_t last,size_t module_limit)
{
 const char *inquiries[]={"$bits","$left","$right","$low","$high","$size","$increment","$dimensions","$unpacked_dimensions"};
 for(size_t t=first;t<last;t++)
 {
  if(!token_is(parser,t+1,"(")) continue;
  int inquiry=0;
  for(size_t q=0;q<sizeof inquiries/sizeof *inquiries;q++)
   if(token_is(parser,t,inquiries[q])) inquiry=1;
  if(!inquiry) continue;
  size_t close=matching_parenthesis(parser,t+1,last);
  if(t+2<close && protocol_alias_at(parser,module,t+2)!=PIGEN_PROTOCOL_NONE) return 1;
  /* Share foreach's named/forward native reference classifier;
   * buffered operands also require typed read semantics. */
  for(size_t u=t+2;u<close;u++)
   if(identifier(parser,u) && (module_dynamic_name(parser,module,(pigen_token_id){(uint32_t)u},0) ||
      native_foreach_named_target(parser,module,(pigen_token_id){(uint32_t)u},0) ||
      native_foreach_forward_instance(parser,module,(pigen_token_id){(uint32_t)u},module_limit))) return 1;
 }
 return 0;
}

/* Bound one implicit generate item before deciding whether it needs the
 * structured path. Ordinary SV items retain their preservation route. */
static int implicit_generate_extent(syntax_parser *parser,size_t first,size_t limit,
	size_t depth,size_t *after)
{
	if(first>=limit || depth>128) return 0;
	if(token_is(parser,first,"if") || token_is(parser,first,"for"))
	{
		if(!token_is(parser,first+1,"(")) return 0;
		size_t close=matching_parenthesis(parser,first+1,limit);
		if(close==limit || !implicit_generate_extent(parser,close+1,limit,depth+1,after)) return 0;
		if(token_is(parser,first,"if") && token_is(parser,*after,"else"))
			return implicit_generate_extent(parser,*after+1,limit,depth+1,after);
		return 1;
	}
	if(token_is(parser,first,"case") || token_is(parser,first,"casez") || token_is(parser,first,"casex"))
	{
		size_t nesting=0;
		for(size_t at=first;at<limit;at++)
		{
			if(token_is(parser,at,"case") || token_is(parser,at,"casez") || token_is(parser,at,"casex")) nesting++;
			else if(token_is(parser,at,"endcase") && !--nesting) {*after=at+1;return 1;}
		}
		return 0;
	}
	if(token_is(parser,first,"begin"))
	{
		size_t end=matching_begin_end(parser,first,limit);
		if(end==limit) return 0;
		*after=end+1;
		if(token_is(parser,*after,":"))
		{
			if(!identifier(parser,*after+1)) return 0;
			*after+=2;
		}
		return 1;
	}
	if(token_is(parser,first,"fabric"))
	{
		for(size_t end=first;end<limit;end++)
			if(token_is(parser,end,"endfabric")) {*after=end+1;return 1;}
		return 0;
	}
	if((token_is(parser,first,"always") || token_is(parser,first,"always_ff")) && token_is(parser,first+1,"@"))
	{
		if(!token_is(parser,first+2,"(")) return 0;
		size_t close=matching_parenthesis(parser,first+2,limit);
		return close<limit && implicit_generate_extent(parser,close+1,limit,depth+1,after);
	}
	size_t end=top_level_token(parser,first,limit,";");
	if(end==limit) return 0;
	*after=end+1;return 1;
}

static int parse_module_items(syntax_parser *parser, pigen_syntax_id module,
	size_t first, size_t after, syntax_cursor *opaque_cursor)
{
	size_t at = first;
	size_t depth = 0;
	size_t brace_depth = 0;
	int item_start = 1;

	while (at < after)
	{
		if(item_start && token_is(parser,at,"genvar"))
		{
			size_t end=top_level_token(parser,at+1,after,";");
			if(end==after) return fail(parser,at,"genvar declaration requires a semicolon");
			if(at+1==end) return fail(parser,at,"genvar declaration requires an identifier");
			add_opaque(parser,module,*opaque_cursor,at);
			for(size_t name=at+1;name<end;name+=2)
			{
				if(!identifier(parser,name) || (name+1<end && !token_is(parser,name+1,",")) ||
					(name+1<end && name+2==end)) return fail(parser,name,"genvar declaration requires comma-separated identifiers");
				pigen_syntax_node node={0};node.kind=PIGEN_SYNTAX_GENVAR;
				node.location=range_location(parser,name,name+1);
				node.parent=node.first_child=node.last_child=node.next_sibling=INVALID_SYNTAX;
				node.as.genvar.name=(pigen_token_id){(uint32_t)name};
				add_child(parser,module,add_node(parser,node));
			}
			advance_opaque_byte_cursor(parser,end+1);*opaque_cursor=end+1;
			at=end+1;item_start=1;continue;
		}
		if (item_start && token_is(parser,at,"generate"))
		{
			if (parser->generate_depth) return fail(parser,at,"generate regions cannot be nested");
			size_t end = generate_region_end(parser,at,after);
			if (end == after) return fail(parser,at,"generate region requires `endgenerate`");
			if (generate_requires_structure(parser,module,at,end))
			{
				add_opaque_bytes(parser,module,parser->opaque_byte_cursor,
					pigen_origin_expansion_span(parser->expanded,token_at(parser,at)->origin).start);
				pigen_syntax_id region = generate_node(parser,module,PIGEN_SYNTAX_GENERATE_REGION,at,end+1);
				advance_opaque_byte_cursor(parser,at+1);
				parser->generate_depth++;
				syntax_cursor cursor = at+1;
				int ok = parse_module_items(parser,region,at+1,end,&cursor);
				parser->generate_depth--;
				if (!ok) return 0;
				advance_opaque_byte_cursor(parser,end+1);
				at=end+1; item_start=1; continue;
			}
		}
		if(item_start && !parser->generate_depth &&
			(token_is(parser,at,"casez") || token_is(parser,at,"casex")))
		{
			size_t next=after;
			int bounded=implicit_generate_extent(parser,at,after,0,&next);
			if(generate_requires_structure(parser,module,at,bounded ? next : after))
				return fail(parser,at,"native generate selection requires case, not casez or casex");
		}
		if(item_start && !parser->generate_depth &&
			(token_is(parser,at,"if") || token_is(parser,at,"for") || token_is(parser,at,"case") || token_is(parser,at,"begin")))
		{
			size_t next=after;
			int bounded=implicit_generate_extent(parser,at,after,0,&next);
			if(generate_requires_structure(parser,module,at,bounded ? next : after))
			{
				if(!bounded) return fail(parser,at,"implicit generate item requires a complete control and body");
				add_opaque_bytes(parser,module,parser->opaque_byte_cursor,
					pigen_origin_expansion_span(parser->expanded,token_at(parser,at)->origin).start);
				pigen_syntax_id region=generate_node(parser,module,PIGEN_SYNTAX_GENERATE_REGION,at,next);
				advance_opaque_byte_cursor(parser,at);
				size_t parsed=at;
				if(!parse_generate_item(parser,region,at,next,&parsed)) return 0;
				if(parsed!=next) return fail(parser,parsed,"implicit generate item has an unexpected trailing token");
				advance_opaque_byte_cursor(parser,next);*opaque_cursor=next;
				at=next;item_start=1;continue;
			}
			if(bounded)
			{
				add_opaque_bytes(parser,module,parser->opaque_byte_cursor,
					pigen_origin_expansion_span(parser->expanded,token_at(parser,at)->origin).start);
				pigen_syntax_node retained={0};retained.kind=PIGEN_SYNTAX_RETAINED;
				retained.location=range_location(parser,at,next);
				retained.parent=retained.first_child=retained.last_child=retained.next_sibling=INVALID_SYNTAX;
				add_child(parser,module,add_node(parser,retained));
				advance_opaque_byte_cursor(parser,next);*opaque_cursor=next;
				at=next;item_start=1;continue;
			}
		}
		if (item_start && parser->generate_depth &&
			(token_is(parser,at,"if") || token_is(parser,at,"for") || token_is(parser,at,"case") || token_is(parser,at,"begin")))
		{
			size_t next;
			if (!parse_generate_item(parser,module,at,after,&next)) return 0;
			at=next; item_start=1; continue;
		}
		if (item_start && token_is(parser,at,"always_comb"))
		{
			size_t extent=fsm_skip_statement(parser,at+1,after);
			int structured=parser->generate_depth != 0 || inquiry_requires_structure(parser,module,at+1,extent,after);
			for (size_t t=at+1;!structured && t<extent;t++)
			{
				if(token_is(parser,t,"foreach") && token_is(parser,t+1,"("))
				{
					size_t close=matching_parenthesis(parser,t+1,extent);
					if(close<extent && (process_has_protocol_reference(parser,module,t,close+1) ||
						native_foreach_named_target(parser,module,(pigen_token_id){(uint32_t)(t+2)},0) ||
						native_foreach_forward_instance(parser,module,(pigen_token_id){(uint32_t)(t+2)},after))) structured=1;
				}
				if (token_is(parser,t+1,"(") && (token_is(parser,t,"peek") || token_is(parser,t,"ready") || token_is(parser,t,"valid") || token_is(parser,t,"accepts"))) structured=1;
				if (native_import_at(parser,t,extent) || protocol_alias_construct_at(parser,module,t,extent) || numerical_construct_at(parser,t,extent)) structured=1;
			}
			if (structured)
			{
				pigen_syntax_node node={0};node.kind=PIGEN_SYNTAX_COMBINATIONAL_PROCESS;
				node.parent=node.first_child=node.last_child=node.next_sibling=INVALID_SYNTAX;
				pigen_syntax_id process=add_node(parser,node);size_t count=0,next=at;
				parser->combinational_body=1;
				int ok=parse_procedural_statement(parser,process,at+1,after,&next,&count);
				parser->combinational_body=0;
				if (!ok) { if (parser->error && parser->error->message) return 0; return fail(parser,at,"scoped always_comb requires supported blocking assignments, blocks and if statements"); }
				parser->tree->nodes[process.index].location=range_location(parser,at,next);
				add_opaque(parser,module,*opaque_cursor,at);add_child(parser,module,process);
				advance_opaque_byte_cursor(parser,next);*opaque_cursor=next;at=next;item_start=1;continue;
			}
		}
		int retained_keyword = token_is(parser, at, "initial") ||
			token_is(parser, at, "final") ||
			token_is(parser, at, "generate") ||
			token_is(parser, at, "genvar") ||
			token_is(parser, at, "always_comb") ||
			token_is(parser, at, "always_latch");

		if (item_start && !retained_keyword &&
			looks_like_parameterized_instance(parser, at, after))
		{
			size_t retained_after;
			const pigen_syntax_node *owner = pigen_syntax_get(parser->tree, module);
			if (owner && (owner->kind != PIGEN_SYNTAX_MODULE ||
				generate_has_native_child(parser,at,top_level_token(parser,at,after,";")+1)))
			{
				if (!parse_structural_instance(parser, module, at, after, &retained_after)) return 0;
				at = retained_after;
				item_start = 1;
				continue;
			}

			if (!remember_parameterized_instance(parser, at, at)) return 0;
			if (!parse_retained_module_item(parser, module, at, after,
				&retained_after)) return 0;
			at = retained_after;
			item_start = 1;
			continue;
		}
		if (item_start && !retained_keyword &&
			looks_like_plain_instance(parser, at, after))
		{
			size_t instance_after;

			if (!parse_structural_instance(parser, module, at, after,
				&instance_after)) return 0;
			at = instance_after;
			item_start = 1;
			continue;
		}
		if (item_start && token_is(parser, at, "assign"))
		{
			size_t retained_after;

			if (!parse_retained_module_item(parser, module, at, after,
				&retained_after)) return 0;
			at = retained_after;
			item_start = 1;
			continue;
		}
		if (item_start && token_is(parser, at, "fabric"))
		{
			size_t fabric_after;
			if (!parse_fabric_block(parser, module, at, after, 0,
				&fabric_after)) return 0;
			at = fabric_after;
			item_start = 1;
			continue;
		}
		if (item_start && (token_is(parser, at, "always") ||
			token_is(parser, at, "always_ff")))
		{
			size_t process_after;
			if (parse_clocked_process(parser, module, at, after, opaque_cursor,
				&process_after))
			{
				at = process_after;
				item_start = 1;
				continue;
			}
			/* Speculative failures restore their diagnostic checkpoint. A
			 * remaining error is committed and must survive retention probes. */
			if (parser->pipeline_failed || parser->when_failed ||
				(parser->error && parser->error->message)) return 0;
			/* Retained native reads are bound semantically. Unsupported
			 * transfers still fail below, and retained writes fail binding. */
			{
				size_t process_end;
				if(retained_procedural_end(parser,at,after,&process_end) &&
					process_has_protocol_reference(parser,module,at,process_end) &&
					process_has_buffered_transfer(parser,module,at,process_end) &&
					(!token_is(parser,at+3,"posedge") || !identifier(parser,at+4) || !token_is(parser,at+5,")")))
					return fail(parser,at,"AXI process requires supported statements on its bound rising-edge clock");
			}
			if (token_is(parser, at, "always_ff"))
			{
				size_t retained_after;

				if (!token_is(parser, at + 1, "@") ||
					!token_is(parser, at + 2, "("))
					return fail(parser, at + 2,
						"exactly one posedge event control");
				{
					size_t process_end;

					if (!retained_procedural_end(parser, at, after,
						&process_end)) return 0;
					if (retained_process_has_structured(parser, at,
						process_end) ||
						process_has_buffered_transfer(parser, module, at,
							process_end))
						return fail(parser, at + 2,
							"exactly one posedge event control");
				}
				if (!parse_retained_procedural(parser, module, at, after,
					&retained_after)) return 0;
				at = retained_after;
				item_start = 1;
				continue;
			}
			/* A non-clocked `always` block is ordinary SV retained as-is.
			 * A failed edge-clocked block must roll back, not become opaque. */
			if (token_is(parser, at, "always") &&
				token_is(parser, at + 1, "@") &&
				token_is(parser, at + 2, "(") &&
				(token_is(parser, at + 3, "posedge") ||
				 token_is(parser, at + 3, "negedge") ||
				 token_is(parser, at + 3, "edge")))
			{
				size_t process_end;
				size_t retained_after;

				if (!retained_procedural_end(parser, at, after,
					&process_end)) return 0;
				if (!retained_process_has_structured(parser, at, process_end) &&
					!process_has_buffered_transfer(parser, module, at,
						process_end))
				{
					if (!parse_retained_procedural(parser, module, at, after,
						&retained_after)) return 0;
					at = retained_after;
					item_start = 1;
					continue;
				}
				return fail(parser, at,
					"unsupported statement in a clocked process with buffered or structured transfers");
			}
			if (token_is(parser, at, "always") &&
				!(token_is(parser, at + 2, "(") &&
					(token_is(parser, at + 3, "posedge") ||
					 token_is(parser, at + 3, "negedge") ||
					 token_is(parser, at + 3, "edge"))))
			{
				size_t retained_after;

				if (parse_retained_module_item(parser, module, at, after,
					&retained_after))
				{
					at = retained_after;
					item_start = 1;
					continue;
				}
			}
			if (token_is(parser, at, "always") &&
				token_is(parser, at + 2, "(") &&
				(token_is(parser, at + 3, "posedge") ||
				 token_is(parser, at + 3, "negedge") ||
				 token_is(parser, at + 3, "edge")) &&
				matching_parenthesis(parser, at + 2, after) > at + 5)
			{
				size_t process_end;
				size_t retained_after;

				if (!retained_procedural_end(parser, at, after,
					&process_end)) return 0;
				if (retained_process_has_structured(parser, at, process_end) ||
					process_has_buffered_transfer(parser, module, at,
						process_end))
					return fail(parser, at + 2,
						"structured construct in an unsupported clocked "
						"process is not supported");
				if (!parse_retained_procedural(parser, module, at, after,
					&retained_after)) return 0;
				at = retained_after;
				item_start = 1;
				continue;
			}
		}
		if (item_start && token_is(parser, at, "fsm"))
		{
			size_t fsm_after;
			if (parse_fsm(parser, module, at, after, &fsm_after))
			{
				advance_opaque_byte_cursor(parser, fsm_after);
				at = fsm_after;
				item_start = 1;
				continue;
			}
			return 0;
		}
		if (item_start && (token_is(parser, at, "parameter") ||
			token_is(parser, at, "localparam")))
		{
			size_t semicolon = at;
			while (semicolon < after && !token_is(parser, semicolon, ";")) semicolon++;
			if (semicolon == after)
				return fail(parser, at, "unterminated parameter declaration");
			if (!parse_parameter_items(parser, module, at, semicolon,
				opaque_cursor)) return 0;
			advance_opaque_byte_cursor(parser, semicolon + 1);
			*opaque_cursor = semicolon + 1;
			at = semicolon + 1;
			item_start = 1;
			continue;
		}
		if (item_start && (token_is(parser, at, "function") ||
			token_is(parser, at, "task")))
		{
			size_t retained_after;
			const char *closer = token_is(parser, at, "function") ?
				"endfunction" : "endtask";

			if (!parse_retained_block_item(parser, module, at, after, closer,
				&retained_after)) return 0;
			at = retained_after;
			item_start = 1;
			continue;
		}
		if(item_start && token_is(parser,at,"import"))
		{
			if(!parse_import(parser,module,at,after,opaque_cursor,&at)) return 0;
			item_start=1;continue;
		}
		if (item_start && token_is(parser, at, "typedef"))
		{
			size_t semicolon = top_level_token(parser, at, after, ";");
			declaration_parse_result result;
			if (semicolon == after)
			{
				if (decisive_typedef_without_terminator(parser, module, at, after))
					return fail(parser, after,
						"typedef declaration requires `;`");
				return fail(parser, at, "unterminated typedef declaration");
			}
			result = parse_typedef(parser, module, at, semicolon, opaque_cursor);
			if (result == DECLARATION_INVALID) return 0;
			/* Owned DATA/native typedefs emit from their parsed declaration.
			 * Unowned SV retains its original item. */
			if (result != DECLARATION_RECOGNIZED)
				add_retained_range(parser, module, at, semicolon + 1);
			at = semicolon + 1;
			item_start = 1;
			continue;
		}
		if (item_start)
		{
			size_t semicolon = top_level_token(parser, at, after, ";");
			size_t type_at = at;
			int has_direction = token_is(parser, at, "input") ||
				token_is(parser, at, "output") || token_is(parser, at, "inout");
			syntax_checkpoint checkpoint;
			declaration_parse_result result;
			pigen_syntax_id declaration = INVALID_SYNTAX;
			int decisive = 0;

			if (has_direction) type_at++;
			if (semicolon < after)
			{
				if (leading_transfer_first_declaration(parser, module, type_at,
					semicolon))
					return fail(parser, type_at,
						"data type must precede the transfer type");
				checkpoint = save_checkpoint(parser);
				result = parse_systemverilog_static_declaration(parser, module, at,
					semicolon, &declaration);
				if (result == DECLARATION_RECOGNIZED)
				{
					commit_declaration(parser, module, at, semicolon + 1,
						opaque_cursor, declaration);
					at = semicolon + 1;
					item_start = 1;
					continue;
				}
				restore_checkpoint(parser, checkpoint);

				checkpoint = save_checkpoint(parser);
				result = parse_data_first_declaration(parser, module, at, semicolon,
					0,
					&declaration, &decisive);
				if (result == DECLARATION_INVALID) return 0;
				if (result == DECLARATION_RECOGNIZED)
				{
					commit_declaration(parser, module, at, semicolon + 1,
						opaque_cursor, declaration);
					at = semicolon + 1;
					item_start = 1;
					continue;
				}
				restore_checkpoint(parser, checkpoint);
			}
			else
			{
				if (leading_transfer_first_declaration(parser, module, type_at,
					after))
					return fail(parser, type_at,
						"data type must precede the transfer type");
				checkpoint = save_checkpoint(parser);
				result = parse_data_first_declaration(parser, module, at, after, 0,
					&declaration, &decisive);
				if (result == DECLARATION_INVALID) return 0;
				if (result == DECLARATION_RECOGNIZED && decisive)
				{
					restore_checkpoint(parser, checkpoint);
					return fail(parser, after,
						"signal declaration requires `;`");
				}
				restore_checkpoint(parser, checkpoint);
			}
		}
		if (item_start && !token_is(parser, at, "always") &&
			!token_is(parser, at, "always_ff"))
		{
			size_t retained_after;

			if (parse_retained_unknown_item(parser, module, at, after,
				&retained_after))
			{
				at = retained_after;
				item_start = 1;
				continue;
			}
		}
		if (token_is(parser, at, "{")) brace_depth++;
		else if (token_is(parser, at, "}") && brace_depth) brace_depth--;
		else if (block_opener(parser, at)) depth++;
		else if (block_closer(parser, at))
		{
			if (depth) depth--;
			if (!depth) item_start = 1;
		}
		else if (!depth && !brace_depth && token_is(parser, at, ";"))
			item_start = 1;
		else if (item_start) item_start = 0;
		at++;
	}
	return 1;
}

static int module_has_bare_int(const syntax_parser *parser, size_t start)
{
	size_t header_end, at;
	size_t paren_depth = 0;

	for (header_end = start + 2;
		header_end < parser->expanded->token_count; header_end++)
	{
		if (token_is(parser, header_end, "(")) paren_depth++;
		else if (token_is(parser, header_end, ")") && paren_depth) paren_depth--;
		else if (!paren_depth && token_is(parser, header_end, ";")) break;
	}
	if (header_end == parser->expanded->token_count) return 0;
	for (at = start; at + 1 < header_end; at++)
		if (token_is(parser, at, "int") && !token_is(parser, at + 1, "["))
			return 1;
	return 0;
}

static int module_has_unpacked_struct(const syntax_parser *parser, size_t start)
{
	size_t header_end, endmodule, at;
	size_t paren_depth = 0;

	for (header_end = start + 2;
		header_end < parser->expanded->token_count; header_end++)
	{
		if (token_is(parser, header_end, "(")) paren_depth++;
		else if (token_is(parser, header_end, ")") && paren_depth) paren_depth--;
		else if (!paren_depth && token_is(parser, header_end, ";")) break;
	}
	if (header_end == parser->expanded->token_count) return 0;
	for (endmodule = header_end + 1;
		endmodule < parser->expanded->token_count; endmodule++)
		if (token_is(parser, endmodule, "endmodule")) break;
	if (endmodule == parser->expanded->token_count) return 0;
	for (at = start + 1; at < endmodule; at++)
		if (token_is(parser, at, "struct") &&
			!token_is(parser, at + 1, "packed")) return 1;
	return 0;
}

static int module_has_union(const syntax_parser *parser, size_t start)
{
	size_t header_end, endmodule, at;
	size_t paren_depth = 0;

	for (header_end = start + 2;
		header_end < parser->expanded->token_count; header_end++)
	{
		if (token_is(parser, header_end, "(")) paren_depth++;
		else if (token_is(parser, header_end, ")") && paren_depth) paren_depth--;
		else if (!paren_depth && token_is(parser, header_end, ";")) break;
	}
	if (header_end == parser->expanded->token_count) return 0;
	for (endmodule = header_end + 1;
		endmodule < parser->expanded->token_count; endmodule++)
		if (token_is(parser, endmodule, "endmodule")) break;
	if (endmodule == parser->expanded->token_count) return 0;
	for (at = start; at < endmodule; at++)
		if (token_is(parser, at, "union")) return 1;
	return 0;
}

static int module_has_enum(const syntax_parser *parser, size_t start)
{
	size_t header_end, endmodule, at;
	size_t paren_depth = 0;

	for (header_end = start + 2;
		header_end < parser->expanded->token_count; header_end++)
	{
		if (token_is(parser, header_end, "(")) paren_depth++;
		else if (token_is(parser, header_end, ")") && paren_depth) paren_depth--;
		else if (!paren_depth && token_is(parser, header_end, ";")) break;
	}
	if (header_end == parser->expanded->token_count) return 0;
	for (endmodule = header_end + 1;
		endmodule < parser->expanded->token_count; endmodule++)
		if (token_is(parser, endmodule, "endmodule")) break;
	if (endmodule == parser->expanded->token_count) return 0;
	for (at = start; at < endmodule; at++)
		if (token_is(parser, at, "enum")) return 1;
	return 0;
}

static int module_header_has_directionless_port(const syntax_parser *parser,
	size_t start)
{
	size_t header_end, at, close, item;
	size_t paren_depth = 0;

	for (header_end = start + 2;
		header_end < parser->expanded->token_count; header_end++)
	{
		if (token_is(parser, header_end, "(")) paren_depth++;
		else if (token_is(parser, header_end, ")") && paren_depth) paren_depth--;
		else if (!paren_depth && token_is(parser, header_end, ";")) break;
	}
	if (header_end == parser->expanded->token_count) return 0;
	at=start+2;
	if (token_is(parser,at,"#") && token_is(parser,at+1,"("))
	{
		at=matching_parenthesis(parser,at+1,header_end);
		if (at==header_end) return 0;
		at++;
	}
	for (; at < header_end; at++)
		if (token_is(parser, at, "(")) break;
	if (at == header_end) return 0;
	close = matching_parenthesis(parser, at, header_end);
	if (close == header_end) return 0;
	int directed=0;
	for (item = at + 1; item < close; )
	{
		size_t next = item;

		size_t depth = 0;
		while (next < close)
		{
			if (token_is(parser, next, "(") || token_is(parser, next, "[") || token_is(parser, next, "{")) depth++;
			else if (token_is(parser, next, ")") || token_is(parser, next, "]") || token_is(parser, next, "}"))
			{
				if (depth) depth--;
			}
			else if (!depth && token_is(parser, next, ",")) break;
			next++;
		}
		if (item<next)
		{
			if (token_is(parser,item,"input") || token_is(parser,item,"output") || token_is(parser,item,"inout")) directed=1;
			else if (!directed || !declarator_candidate(parser,item,next)) return 1;
		}
		item = next + 1;
	}
	return 0;
}

static int parameterized_module(const syntax_parser *parser, size_t start)
{
	return token_is(parser, start, "module") &&
		identifier(parser, start + 1) && token_is(parser, start + 2, "#");
}

static int parameterized_module_has_pigen(const syntax_parser *parser,
	size_t start)
{
	static const char *const pigen_words[] = {
		"buf", "port", "fifo", "skid", "pipeline", "fsm", "transfer",
		"validate", "invalidate", "flush", "peek", "pipeline_reset",
		"ready", "valid", "accepts", "when",
	};
	size_t header_end, endmodule, at;
	size_t paren_depth = 0;

	for (header_end = start + 2;
		header_end < parser->expanded->token_count; header_end++)
	{
		if (token_is(parser, header_end, "(")) paren_depth++;
		else if (token_is(parser, header_end, ")") && paren_depth) paren_depth--;
		else if (!paren_depth && token_is(parser, header_end, ";")) break;
	}
	if (header_end == parser->expanded->token_count) return 0;
	for (endmodule = header_end + 1;
		endmodule < parser->expanded->token_count; endmodule++)
		if (token_is(parser, endmodule, "endmodule")) break;
	if (endmodule == parser->expanded->token_count) return 0;
	for (at = start; at < endmodule; at++)
	{
		if (protocol_at(parser, at) != PIGEN_PROTOCOL_NONE || protocol_alias_construct_at(parser,(pigen_syntax_id){0},at,endmodule)) return 1;
		if(native_import_at(parser,at,endmodule)) return 1;
		if (numerical_construct_at(parser,at,endmodule)) return 1;
		for (size_t k = 0; k < sizeof pigen_words / sizeof *pigen_words; k++)
			if (token_is(parser, at, pigen_words[k]))
			{
				if ((!strcmp(pigen_words[k], "ready") || !strcmp(pigen_words[k], "valid") ||
					!strcmp(pigen_words[k], "accepts") || !strcmp(pigen_words[k], "when")) &&
					!token_is(parser, at + 1, "(")) continue;
				return 1;
			}
	}
	return 0;
}

static int parse_parameterized_module_opaque(syntax_parser *parser,
	pigen_syntax_id root, size_t start, size_t *after_module)
{
	size_t header_end, endmodule;
	pigen_syntax_node node = {0};
	size_t paren_depth = 0;

	for (header_end = start + 2;
		header_end < parser->expanded->token_count; header_end++)
	{
		if (token_is(parser, header_end, "(")) paren_depth++;
		else if (token_is(parser, header_end, ")") && paren_depth) paren_depth--;
		else if (!paren_depth && token_is(parser, header_end, ";")) break;
	}
	if (header_end == parser->expanded->token_count)
		return fail(parser, start, "unterminated module header");
	for (endmodule = header_end + 1;
		endmodule < parser->expanded->token_count; endmodule++)
		if (token_is(parser, endmodule, "endmodule")) break;
	if (endmodule == parser->expanded->token_count)
		return fail(parser, start, "module requires `endmodule`");
	node.kind = PIGEN_SYNTAX_RETAINED;
	node.location = range_location(parser, start, endmodule + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	add_child(parser, root, add_node(parser, node));
	advance_opaque_byte_cursor(parser, endmodule + 1);
	*after_module = endmodule + 1;
	return 1;
}

/* Name templates follow source scope, not selected semantic occurrences. Retained
 * generate items take part without being converted into structured syntax. */
typedef struct {
	pigen_syntax_id module;
	size_t parent, ordinal;
	pigen_token_id *names;
	size_t name_count;
} generate_external_namespace;

/* Retained escaped identifiers may span several lexer tokens. Original
 * expansion adjacency supplies their whitespace terminator without changing
 * the authored-name grammar of structured native scopes. */
static size_t external_identifier_after(syntax_parser *parser,size_t first,size_t limit)
{
	if(!token_is(parser,first,"\\")) return first+1;
	pigen_source_span previous=pigen_origin_expansion_span(parser->expanded,token_at(parser,first)->origin);
	size_t at=first+1;
	for(;at<limit;at++)
	{
		pigen_source_span next=pigen_origin_expansion_span(parser->expanded,token_at(parser,at)->origin);
		if(next.source.index!=previous.source.index || next.start!=previous.end) break;
		previous=next;
	}
	return at;
}

static char *external_identifier_text(syntax_parser *parser,pigen_token_id token)
{
	size_t end=external_identifier_after(parser,token.index,parser->expanded->token_count);
	size_t first=token.index+token_is(parser,token.index,"\\");
	pigen_string name={0};
	for(size_t at=first;at<end;at++)
	{
		size_t length;const char *text=pigen_expanded_token_text(parser->expanded,token_at(parser,at),&length);
		pigen_append_range(&name,text,length);
	}
	return name.data;
}

static void external_reserve_name(generate_external_namespace *space,size_t token)
{
	space->names=pigen_resize(space->names,(space->name_count+1)*sizeof(*space->names));
	space->names[space->name_count++]=(pigen_token_id){(uint32_t)token};
}

static void external_declaration_names(syntax_parser *parser,generate_external_namespace *space,
	size_t first,size_t after)
{
	if(token_is(parser,first,"assign") || token_is(parser,first,"defparam") ||
		token_is(parser,first,"import") || token_is(parser,first,"export")) return;
	size_t grouping=0;int initializer=0;
	for(size_t at=first;at<after;at++)
	{
		if(!grouping)
		{
			if(token_is(parser,at,",")) initializer=0;
			if(token_is(parser,at,"=")) initializer=1;
			if(token_is(parser,at,"\\"))
			{
				size_t end=external_identifier_after(parser,at,after);
				if(!initializer && (token_is(parser,end,"=") || token_is(parser,end,";") ||
					token_is(parser,end,",") || token_is(parser,end,"[") || token_is(parser,end,"(")))
					external_reserve_name(space,at);
				at=end-1;continue;
			}
			if(!initializer && identifier(parser,at) &&
				(token_is(parser,at+1,"=") || token_is(parser,at+1,";") ||
				 token_is(parser,at+1,",") || token_is(parser,at+1,"[") || token_is(parser,at+1,"(")))
				external_reserve_name(space,at);
		}
		if(token_is(parser,at,"(") || token_is(parser,at,"[") || token_is(parser,at,"{")) grouping++;
		else if((token_is(parser,at,")") || token_is(parser,at,"]") || token_is(parser,at,"}")) && grouping) grouping--;
	}
}

static int external_name_is_reserved(syntax_parser *parser,const generate_external_namespace *space,
	const char *name)
{
	for(size_t i=0;i<space->name_count;i++)
	{
		char *text=external_identifier_text(parser,space->names[i]);
		int equal=text && !strcmp(text,name);free(text);
		if(equal) return 1;
	}
	return 0;
}

static void finish_external_namespace(syntax_parser *parser,generate_external_namespace *space,
	size_t first_record)
{
	for(size_t i=first_record;i<parser->tree->generate_name_count;i++)
	{
		pigen_syntax_generate_name *record=&parser->tree->generate_names[i];
		if(record->parent!=space->parent || record->module.index!=space->module.index) continue;
		if(record->explicit_name.index!=PIGEN_INVALID_ID)
		{
			record->external_name=external_identifier_text(parser,record->explicit_name);
		}
		else
		{
			pigen_string name={0};pigen_append_format(&name,"genblk%zu",record->ordinal);
			while(external_name_is_reserved(parser,space,name.data))
			{
				pigen_string next={0};pigen_append(&next,"genblk0");pigen_append(&next,name.data+6);
				free(name.data);name=next;
			}
			record->external_name=name.data;
		}
	}
	free(space->names);space->names=NULL;
}

static size_t external_plain_item_end(syntax_parser *parser,size_t first,size_t limit,
	const size_t *item_ends)
{
	if(item_ends[first]>first && item_ends[first]<=limit) return item_ends[first];
	if(token_is(parser,first,"function") || token_is(parser,first,"task"))
	{
		const char *closer=token_is(parser,first,"function") ? "endfunction" : "endtask";
		for(size_t at=first;at<limit;at++) if(token_is(parser,at,closer)) return at+1;
	}
	if(token_is(parser,first,"always") || token_is(parser,first,"always_ff") ||
		token_is(parser,first,"always_comb") || token_is(parser,first,"always_latch") ||
		token_is(parser,first,"initial") || token_is(parser,first,"final"))
	{
		size_t after;if(retained_procedural_end(parser,first,limit,&after)) return after;
	}
	size_t after;if(implicit_generate_extent(parser,first,limit,0,&after)) return after;
	return limit;
}


static int external_is_process(syntax_parser *parser,size_t at)
{
	return token_is(parser,at,"always") || token_is(parser,at,"always_ff") ||
		token_is(parser,at,"always_comb") || token_is(parser,at,"always_latch") ||
		token_is(parser,at,"initial") || token_is(parser,at,"final");
}

static void external_procedural_names(syntax_parser *parser,generate_external_namespace *space,
	size_t first,size_t after)
{
	for(size_t at=first;at<after;at++)
		if(token_is(parser,at,"begin") && token_is(parser,at+1,":") && (identifier(parser,at+2) || token_is(parser,at+2,"\\")))
		{
			external_reserve_name(space,at+2);
			/* A named procedural block owns its nested declarations. */
			size_t end=matching_begin_end(parser,at,after);
			if(end==after) break;
			at=end;
		}
}

static int external_generate_items(syntax_parser *parser,generate_external_namespace *space,
	size_t first,size_t limit,const size_t *item_ends,size_t depth);
static int external_generate_control(syntax_parser *parser,generate_external_namespace *space,
	size_t first,size_t limit,size_t ordinal,size_t control,const size_t *item_ends,size_t depth,size_t *after);

static int external_generate_body(syntax_parser *parser,generate_external_namespace *space,
	size_t first,size_t limit,size_t ordinal,size_t control,int loop,const size_t *item_ends,size_t depth,size_t *after)
{
	/* Only directly nested conditionals suppress an intermediate scope. */
	if(!loop && (token_is(parser,first,"if") || token_is(parser,first,"case") ||
		token_is(parser,first,"casez") || token_is(parser,first,"casex")))
		return external_generate_control(parser,space,first,limit,ordinal,control,item_ends,depth+1,after);
	size_t body=first,end;
	pigen_token_id name=INVALID_ID(pigen_token_id);
	if(token_is(parser,first,"begin"))
	{
		end=matching_begin_end(parser,first,limit);if(end==limit) return 0;
		body=first+1;
		if(token_is(parser,body,":")) {name=(pigen_token_id){(uint32_t)(body+1)};body=external_identifier_after(parser,body+1,limit);external_reserve_name(space,name.index);}
		*after=end+1;if(token_is(parser,*after,":")) *after+=2;
	}
	else
	{
		if(!implicit_generate_extent(parser,first,limit,depth,after)) return 0;
		end=*after;
	}
	pigen_syntax_tree *tree=parser->tree;
	if(tree->generate_name_count==tree->generate_name_capacity)
	{
		tree->generate_name_capacity=tree->generate_name_capacity ? tree->generate_name_capacity*2 : 16;
		tree->generate_names=pigen_resize(tree->generate_names,tree->generate_name_capacity*sizeof(*tree->generate_names));
	}
	size_t index=tree->generate_name_count++;
	tree->generate_names[index]=(pigen_syntax_generate_name){.module=space->module,.parent=space->parent,
		.control={(uint32_t)control},.block={(uint32_t)first},.explicit_name=name,.ordinal=ordinal};
	generate_external_namespace nested={.module=space->module,.parent=index};
	/* The loop's implicit localparam is an explicit declaration in its body. */
	if(loop)
	{
		size_t iterator=control+2;if(token_is(parser,iterator,"genvar")) iterator++;
		external_reserve_name(&nested,iterator);
	}
	int ok=external_generate_items(parser,&nested,body,end,item_ends,depth+1);
	finish_external_namespace(parser,&nested,index+1);return ok;
}

static int external_generate_control(syntax_parser *parser,generate_external_namespace *space,
	size_t first,size_t limit,size_t ordinal,size_t control,const size_t *item_ends,size_t depth,size_t *after)
{
	if(depth>128 || first>=limit) return 0;
	if(token_is(parser,first,"begin"))
		return external_generate_body(parser,space,first,limit,ordinal,control,0,item_ends,depth,after);
	if(!token_is(parser,first+1,"(")) return 0;
	size_t close=matching_parenthesis(parser,first+1,limit);if(close==limit) return 0;
	if(token_is(parser,first,"if") || token_is(parser,first,"for"))
	{
		int loop=token_is(parser,first,"for");
		if(!external_generate_body(parser,space,close+1,limit,ordinal,control,loop,item_ends,depth,after)) return 0;
		if(!loop && token_is(parser,*after,"else"))
			return external_generate_body(parser,space,*after+1,limit,ordinal,control,0,item_ends,depth,after);
		return 1;
	}
	size_t at=close+1;
	while(at<limit && !token_is(parser,at,"endcase"))
	{
		if(token_is(parser,at,"default")) {at++;if(token_is(parser,at,":")) at++;}
		else for(;;)
		{
			size_t separator=generate_case_label_end(parser,at,limit);if(separator==limit) return 0;
			at=separator+1;if(token_is(parser,separator,":")) break;
		}
		if(!external_generate_body(parser,space,at,limit,ordinal,control,0,item_ends,depth,&at)) return 0;
	}
	if(at==limit) return 0;
	*after=at+1;return 1;
}

static int external_generate_items(syntax_parser *parser,generate_external_namespace *space,
	size_t first,size_t limit,const size_t *item_ends,size_t depth)
{
	if(depth>128) return 0;
	for(size_t at=first;at<limit;)
	{
		size_t after;
		if(token_is(parser,at,"generate"))
		{
			size_t end=generate_region_end(parser,at,limit);if(end==limit) return 0;
			if(!external_generate_items(parser,space,at+1,end,item_ends,depth+1)) return 0;
			after=end+1;
		}
		else if(token_is(parser,at,"if") || token_is(parser,at,"for") || token_is(parser,at,"case") ||
			token_is(parser,at,"casez") || token_is(parser,at,"casex") || token_is(parser,at,"begin"))
		{
			if(!external_generate_control(parser,space,at,limit,++space->ordinal,at,item_ends,depth,&after)) return 0;
		}
		else
		{
			after=external_plain_item_end(parser,at,limit,item_ends);
			if(after<=at) return 0;
			if(external_is_process(parser,at)) external_procedural_names(parser,space,at,after);
			else if(!token_is(parser,at,"fsm") && !token_is(parser,at,"pipeline") && !token_is(parser,at,"fabric"))
			{
				size_t names_end=(token_is(parser,at,"function") || token_is(parser,at,"task")) ? top_level_token(parser,at,after,";")+1 : after;
				external_declaration_names(parser,space,at,names_end);
			}
		}
		at=after;
	}
	return 1;
}

static int assign_external_generate_names(syntax_parser *parser,pigen_syntax_id module,size_t first,size_t limit)
{
	/* One extent map keeps procedural expressions opaque to this lexical pass. */
	size_t *ends=pigen_resize(NULL,parser->expanded->token_count*sizeof(*ends));
	memset(ends,0,parser->expanded->token_count*sizeof(*ends));
	for(size_t i=0;i<parser->tree->node_count;i++)
	{
		const pigen_syntax_node *node=&parser->tree->nodes[i];
		if(node->kind==PIGEN_SYNTAX_OPAQUE || node->kind==PIGEN_SYNTAX_MODULE ||
			node->kind==PIGEN_SYNTAX_COMPILATION_UNIT ||
			(node->kind>=PIGEN_SYNTAX_GENERATE_REGION && node->kind<=PIGEN_SYNTAX_GENERATE_FOR)) continue;
		size_t start=node->location.extent.first.index,after=node->location.extent.after.index;
		if(start<parser->expanded->token_count && after>ends[start]) ends[start]=after;
	}
	generate_external_namespace space={.module=module,.parent=SIZE_MAX};
	/* Header parameters/ports already have structured declaration identities. */
	for(pigen_syntax_id child=parser->tree->nodes[module.index].first_child;child.index!=PIGEN_INVALID_ID;)
	{
		const pigen_syntax_node *node=&parser->tree->nodes[child.index];
		if(node->kind==PIGEN_SYNTAX_PARAMETER) external_reserve_name(&space,node->as.parameter.name.index);
		if(node->kind==PIGEN_SYNTAX_SIGNAL_DECLARATION)
			for(pigen_syntax_id d=node->first_child;d.index!=PIGEN_INVALID_ID;d=parser->tree->nodes[d.index].next_sibling)
				external_reserve_name(&space,parser->tree->nodes[d.index].as.signal_declarator.name.index);
		child=node->next_sibling;
	}
	size_t start=parser->tree->generate_name_count;
	int ok=external_generate_items(parser,&space,first,limit,ends,0);
	finish_external_namespace(parser,&space,start);free(ends);
	return ok || fail(parser,first,"cannot determine external generate scope names");
}

static int parse_module(syntax_parser *parser, pigen_syntax_id root,
	size_t start, size_t *after_module)
{
	int typed_fabric = token_is(parser, start, "fabric");
	int is_external = token_is(parser, start, "extern");
	size_t name = start + 1 + is_external;
	size_t header_end;
	size_t endmodule;
	size_t port_open = 0;
	size_t port_close = 0;
	size_t header_at;
	syntax_cursor opaque_cursor;
	size_t paren_depth = 0;
	pigen_syntax_node node = {0};
	pigen_syntax_id module;

	if (!identifier(parser, name))
		return fail(parser, name, "module requires a name");
	for (header_end = name + 1;
		header_end < parser->expanded->token_count; header_end++)
	{
		if(!paren_depth && token_is(parser,header_end,"import"))
		{size_t semi=top_level_token(parser,header_end,parser->expanded->token_count,";");if(semi==parser->expanded->token_count) break;header_end=semi;continue;}
		if (token_is(parser, header_end, "(")) paren_depth++;
		else if (token_is(parser, header_end, ")") && paren_depth) paren_depth--;
		else if (!paren_depth && token_is(parser, header_end, typed_fabric ? "begin" : ";")) break;
	}
	if (header_end == parser->expanded->token_count)
		return fail(parser, start, "unterminated module header");
	for (endmodule = is_external ? header_end : header_end + 1;
		endmodule < parser->expanded->token_count; endmodule++)
		if (is_external || token_is(parser, endmodule, typed_fabric ? "endfabric" : "endmodule")) break;
	if (endmodule == parser->expanded->token_count)
		return fail(parser, start, typed_fabric ? "typed fabric requires `endfabric`" : "module requires `endmodule`");
	for (header_at = name + 1; header_at < endmodule; header_at++)
		if (token_is(parser, header_at, "uint") &&
			!token_is(parser, header_at + 1, "["))
			return fail(parser, header_at,
				"Pigen uint requires a width argument");
	node.kind = PIGEN_SYNTAX_MODULE;
	node.location = range_location(parser, start, endmodule + 1);
	node.parent = INVALID_SYNTAX;
	node.first_child = node.last_child = node.next_sibling = INVALID_SYNTAX;
	node.as.module.name = (pigen_token_id){(uint32_t)name};
	node.as.module.is_external = is_external;
	module = add_node(parser, node);
	add_child(parser, root, module);
	opaque_cursor = start;
	header_at = name + 1;
	while(token_is(parser,header_at,"import"))
	 if(!parse_import(parser,module,header_at,header_end,&opaque_cursor,&header_at)) return 0;
	if (token_is(parser, header_at, "#"))
	{
		size_t parameter_close;
		if (!remember_parameterized_module(parser, name)) return 0;
		if (!token_is(parser, header_at + 1, "("))
			return fail(parser, header_at, "module parameter list requires `(`");
		parameter_close = matching_parenthesis(parser, header_at + 1, header_end);
		if (parameter_close == header_end)
			return fail(parser, header_at + 1,
				"unterminated module parameter list");
		if (!parse_parameter_items(parser, module, header_at + 2,
			parameter_close, &opaque_cursor)) return 0;
		header_at = parameter_close + 1;
	}
	if (token_is(parser, header_at, "("))
	{
		port_open = header_at;
		port_close = matching_parenthesis(parser, port_open, header_end);
		if (port_close == header_end)
			return fail(parser, port_open, "unterminated module port list");
		if (!parse_ansi_ports(parser, module, port_open + 1,
			port_close, &opaque_cursor)) return 0;
	}
	if (typed_fabric)
	{
		size_t after;
		if (!port_open) return fail(parser, start, "typed fabric requires a port list");
		if (!parse_fabric_block(parser, module, start, endmodule + 1, header_end, &after)) return 0;
	}
	else if (!is_external && !parse_module_items(parser, module, header_end + 1, endmodule,
		&opaque_cursor)) return 0;
	{
		size_t module_end = pigen_origin_expansion_span(parser->expanded,
			parser->expanded->tokens[endmodule].origin).end;

		add_opaque_bytes(parser, module, parser->opaque_byte_cursor,
			module_end);
		advance_opaque_byte_cursor(parser, endmodule + 1);
	}
	if(!typed_fabric && !is_external && !assign_external_generate_names(parser,module,header_end+1,endmodule)) return 0;
	*after_module = endmodule + 1;
	return 1;
}

/* Diagnose only source forms whose retained spelling would otherwise refer
 * to a removed native typedef. Normal declarations remain source ordered. */
static int future_protocol_alias(const syntax_parser *parser,pigen_syntax_id scope,size_t at)
{
 if(token_is(parser,at+1,"::"))
 {
  for(pigen_syntax_id id=parser->tree->nodes[0].first_child;id.index!=PIGEN_INVALID_ID;)
  {
   const pigen_syntax_node *package=pigen_syntax_get(parser->tree,id);
   if(package->kind==PIGEN_SYNTAX_PACKAGE && package->location.extent.first.index>at &&
      tokens_equal(parser,package->as.module.name,(pigen_token_id){(uint32_t)at}))
   {
    const typedef_name_fact *fact=typedef_name_fact_in_scope(parser,(pigen_token_id){(uint32_t)(at+2)},id);
    return fact && fact->kind==TYPEDEF_NAME_PROTOCOL;
   }
   id=package->next_sibling;
  }
  return 0;
 }
 pigen_token_id name={(uint32_t)at};
 for(size_t depth=0;scope.index!=PIGEN_INVALID_ID && depth<=parser->tree->node_count;depth++)
 {
  const pigen_syntax_node *owner=pigen_syntax_get(parser->tree,scope);
  if(!owner) break;
  const typedef_name_fact *future=NULL;
  for(size_t i=0;i<parser->typedef_name_fact_count;i++)
  {
   const typedef_name_fact *fact=&parser->typedef_name_facts[i];
   if(fact->scope.index!=scope.index || !tokens_equal(parser,fact->name,name)) continue;
   if(fact->name.index<at) return 0;
   if(!future || fact->name.index<future->name.index) future=fact;
  }
  if(protocol_scope_value_shadows(parser,owner,name,at)) return 0;
  if(future) return future->kind==TYPEDEF_NAME_PROTOCOL;
  scope=owner->parent;
 }
 return 0;
}

static int check_retained_native_forward_uses(syntax_parser *parser)
{
 /* Unowned typedef aliases remain source text and have no type node. */
 for(size_t i=0;i<parser->typedef_name_fact_count;i++)
 {
  const typedef_name_fact *fact=&parser->typedef_name_facts[i];
  if(fact->kind!=TYPEDEF_NAME_OPAQUE_SHADOW) continue;
  size_t first=fact->name.index;
  while(first && !token_is(parser,first,"typedef") && !token_is(parser,first-1,";")) first--;
  if(token_is(parser,first,"typedef") && future_protocol_alias(parser,fact->scope,first+1))
   return fail(parser,first+1,"native AXI typedef must be declared before use");
 }
 for(size_t i=0;i<parser->tree->node_count;i++)
 {
  const pigen_syntax_node *node=&parser->tree->nodes[i];
  if(node->kind==PIGEN_SYNTAX_IMPORT && !node->as.package_import.is_structured)
  {
   for(pigen_syntax_id id=parser->tree->nodes[0].first_child;id.index!=PIGEN_INVALID_ID;)
   {
    const pigen_syntax_node *package=pigen_syntax_get(parser->tree,id);
    if(package->kind==PIGEN_SYNTAX_PACKAGE && tokens_equal(parser,package->as.module.name,node->as.package_import.package))
     return fail(parser,node->as.package_import.package.index,"structured package must be declared before import");
    id=package->next_sibling;
   }
  }
  if(node->kind==PIGEN_SYNTAX_MODULE)
  {
   size_t at=node->as.module.name.index+1,limit=node->location.extent.after.index;
   while(token_is(parser,at,"import"))
   {size_t end=top_level_token(parser,at,limit,";");at=end<limit ? end+1 : limit;}
   if(token_is(parser,at,"#"))
   {size_t close=matching_parenthesis(parser,at+1,limit);at=close<limit ? close+1 : limit;}
   if(token_is(parser,at,"("))
   {
    size_t close=matching_parenthesis(parser,at,limit);
    for(size_t item=at+1;item<close;)
    {
     size_t end=ansi_item_end(parser,item,close),type=item;
     if(token_is(parser,type,"input") || token_is(parser,type,"output") || token_is(parser,type,"inout")) type++;
     if(token_is(parser,type,"wire") || token_is(parser,type,"tri") || token_is(parser,type,"reg") || token_is(parser,type,"var")) type++;
     if(type+1<end && (identifier(parser,type+1) || token_is(parser,type+1,"[") || token_is(parser,type+1,"::")) &&
        identifier(parser,type) && future_protocol_alias(parser,(pigen_syntax_id){(uint32_t)i},type))
      return fail(parser,type,"native AXI typedef must be declared before use");
     item=end<close ? end+1 : close;
    }
   }
  }
  if(node->kind!=PIGEN_SYNTAX_RETAINED) continue;
  size_t first=node->location.extent.first.index,last=node->location.extent.after.index;
  /* A leading unresolved type followed by a declarator is a declaration,
   * whereas a module instantiation has a connection list after its name. */
  size_t type=first;
  if(token_is(parser,type,"wire") || token_is(parser,type,"tri") || token_is(parser,type,"reg") || token_is(parser,type,"var")) type++;
  size_t after_name=type+2;
  while(after_name<last && token_is(parser,after_name,"["))
  {
   size_t close=matching_bracket(parser,after_name,last);
   if(close==last) break;
   after_name=close+1;
  }
  if(identifier(parser,type) && identifier(parser,type+1) &&
     !token_is(parser,after_name,"(") && !token_is(parser,after_name,"#") &&
     future_protocol_alias(parser,node->parent,type))
   return fail(parser,type,"native AXI typedef must be declared before use");
  /* Retained procedural bodies have their own unmodeled lexical scopes. */
  if(!token_is(parser,first,"assign")) continue;
  for(size_t t=first;t<last;t++)
  {
   size_t name=t;
   if(token_is(parser,t,"$bits") || token_is(parser,t,"$left") || token_is(parser,t,"$right") ||
      token_is(parser,t,"$low") || token_is(parser,t,"$high") || token_is(parser,t,"$size") ||
      token_is(parser,t,"$increment") || token_is(parser,t,"$dimensions") || token_is(parser,t,"$unpacked_dimensions"))
   {if(!token_is(parser,t+1,"(")) continue;name=t+2;}
   else
   {
    size_t after_alias=protocol_alias_after(parser,t);
    if(!identifier(parser,t) || (t && token_is(parser,t-1,"::")) ||
       !token_is(parser,after_alias,"'") || !token_is(parser,after_alias+1,"(")) continue;
   }
   if(identifier(parser,name) && future_protocol_alias(parser,node->parent,name))
    return fail(parser,name,"native AXI typedef must be declared before use");
  }
 }
 return 1;
}

/* Demand ordinary DATA/configuration packages only from typed contexts. This
 * keeps unrelated SV packages and their procedural consumers on preservation
 * paths while following transitive package dependencies before parsing uses. */
static size_t package_subroutine_after(const syntax_parser *parser,size_t first,size_t limit);

static int package_context_is_typed(const syntax_parser *parser,size_t first,size_t after)
{
 static const char *const markers[]={"buf","port","fifo","skid","pipeline","fsm","transfer",
  "validate","invalidate","flush","pipeline_reset"};
 int package=token_is(parser,first,"package");
 for(size_t at=first;at<after;at++)
 {
  if(package && (token_is(parser,at,"function") || token_is(parser,at,"task")))
  {
   size_t next=package_subroutine_after(parser,at,after);
   if(next==SIZE_MAX) return 0;
   for(size_t token=at;token<next;token++)
    if(numerical_construct_at(parser,token,next) || protocol_alias_construct_at(parser,(pigen_syntax_id){0},token,next)) return 1;
   at=next-1;continue;
  }
  if(protocol_at(parser,at)!=PIGEN_PROTOCOL_NONE || numerical_construct_at(parser,at,after) ||
   protocol_alias_construct_at(parser,(pigen_syntax_id){0},at,after) || native_import_at(parser,at,after)) return 1;
  for(size_t i=0;i<sizeof(markers)/sizeof(*markers);i++) if(token_is(parser,at,markers[i]))
  {
   size_t next=at+1;
   if(token_is(parser,next,"["))
   {size_t close=matching_bracket(parser,next,after);next=close<after ? close+1 : after;}
   if(identifier(parser,next) || (i>=4 && (token_is(parser,next,"@") || token_is(parser,next,"(") || token_is(parser,next,"begin")))) return 1;
  }
  if(!package && (token_is(parser,at,"peek") || token_is(parser,at,"valid") || token_is(parser,at,"ready") ||
      token_is(parser,at,"accepts") || token_is(parser,at,"when")) && token_is(parser,at+1,"(")) return 1;
 }
 return 0;
}

static int ordinary_package_needed(const syntax_parser *parser,pigen_token_id wanted,size_t depth)
{
 if(depth>64) return 0;
 size_t limit=parser->expanded->token_count;
 int global_reference=0,typed=0;
 for(size_t at=0;at<limit;)
 {
  int package=token_is(parser,at,"package");
  int module=token_is(parser,at,"module");
  int fabric=token_is(parser,at,"fabric") &&
   (token_is(parser,at+2,"(") || token_is(parser,at+2,"#"));
  if(package || module || fabric)
  {
   const char *closing=package ? "endpackage" : module ? "endmodule" : "endfabric";
   size_t after=at+1;
   while(after<limit && !token_is(parser,after,closing)) after++;
   size_t name=at+1;
   if(package && (token_is(parser,name,"automatic") || token_is(parser,name,"static"))) name++;
   int context=package_context_is_typed(parser,at,after),reference=0;
   typed|=context;
   for(size_t use=name+1;use+1<after;use++)
    if(tokens_equal(parser,wanted,(pigen_token_id){(uint32_t)use}) && token_is(parser,use+1,"::")) {reference=1;break;}
   if(reference && !(package && tokens_equal(parser,wanted,(pigen_token_id){(uint32_t)name})) &&
    (context || (package && ordinary_package_needed(parser,(pigen_token_id){(uint32_t)name},depth+1)))) return 1;
   at=after<limit ? after+1 : limit;
  }
  else
  {
   if(tokens_equal(parser,wanted,(pigen_token_id){(uint32_t)at}) && token_is(parser,at+1,"::")) global_reference=1;
   if(protocol_at(parser,at)!=PIGEN_PROTOCOL_NONE || numerical_construct_at(parser,at,limit) || native_import_at(parser,at,limit)) typed=1;
   if(token_is(parser,at,"import"))
   {
    size_t end=top_level_token(parser,at,limit,";");
    for(size_t use=at+1;use+1<end;use++)
     if(tokens_equal(parser,wanted,(pigen_token_id){(uint32_t)use}) && token_is(parser,use+1,"::")) global_reference=1;
   }
   at++;
  }
 }
 return global_reference && typed;
}

static size_t package_subroutine_after(const syntax_parser *parser,size_t first,size_t limit)
{
 const char *closers[128];size_t depth=0;
 for(size_t at=first;at<limit;at++)
 {
  if(token_is(parser,at,"function") || token_is(parser,at,"task"))
  {
   if(depth==sizeof(closers)/sizeof(*closers)) return SIZE_MAX;
   closers[depth++]=token_is(parser,at,"function") ? "endfunction" : "endtask";
  }
  else if(token_is(parser,at,"endfunction") || token_is(parser,at,"endtask"))
  {
   if(!depth || !token_is(parser,at,closers[depth-1])) return SIZE_MAX;
   if(!--depth)
   {
    size_t after=at+1;
    if(token_is(parser,after,":"))
    {if(!identifier(parser,after+1)) return SIZE_MAX;after+=2;}
    if(token_is(parser,after,";")) after++;
    return after;
   }
  }
 }
 return SIZE_MAX;
}

static pigen_token_id package_subroutine_name(const syntax_parser *parser,size_t first,size_t after)
{
 size_t end=top_level_token(parser,first,after,";");
 pigen_token_id name={PIGEN_INVALID_ID};
 for(size_t at=first+1;at<end;at++)
 {
  if(token_is(parser,at,"["))
  {size_t close=matching_bracket(parser,at,end);if(close==end) return (pigen_token_id){PIGEN_INVALID_ID};at=close;continue;}
  if(token_is(parser,at,"(") && at>first+1)
  {
   if(token_is(parser,at-1,"type"))
   {size_t close=matching_parenthesis(parser,at,end);if(close==end) return (pigen_token_id){PIGEN_INVALID_ID};at=close;continue;}
   return identifier(parser,at-1) && !token_is(parser,at-2,"::") ?
    (pigen_token_id){(uint32_t)(at-1)} : (pigen_token_id){PIGEN_INVALID_ID};
  }
  if(identifier(parser,at)) name=(pigen_token_id){(uint32_t)at};
 }
 return name;
}

static size_t retained_scope_add(syntax_parser *parser,size_t parent,size_t first,size_t after)
{
 pigen_syntax_tree *tree=parser->tree;
 if(tree->retained_scope_count==tree->retained_scope_capacity)
 {
  tree->retained_scope_capacity=tree->retained_scope_capacity ? tree->retained_scope_capacity*2 : 16;
  tree->retained_scopes=pigen_resize(tree->retained_scopes,tree->retained_scope_capacity*sizeof(*tree->retained_scopes));
 }
 size_t result=tree->retained_scope_count++;
 tree->retained_scopes[result]=(pigen_syntax_retained_scope){parent,range_location(parser,first,after)};
 return result;
}

static void retained_name_add(syntax_parser *parser,size_t scope,pigen_syntax_retained_name_kind kind,
 size_t name,size_t package,int wildcard)
{
 pigen_syntax_tree *tree=parser->tree;
 if(tree->retained_name_count==tree->retained_name_capacity)
 {
  tree->retained_name_capacity=tree->retained_name_capacity ? tree->retained_name_capacity*2 : 32;
  tree->retained_names=pigen_resize(tree->retained_names,tree->retained_name_capacity*sizeof(*tree->retained_names));
 }
 tree->retained_names[tree->retained_name_count++]=(pigen_syntax_retained_name){.kind=kind,.scope=scope,
  .name={(uint32_t)name},.package={package==SIZE_MAX ? PIGEN_INVALID_ID : (uint32_t)package},.wildcard=wildcard,
  .primary={PIGEN_INVALID_ID},.argument_name={PIGEN_INVALID_ID},.argument_value={PIGEN_INVALID_ID}};
}

/* Call actuals are syntax facts, independent of namespace activation. Keep
 * qualified/member callees and named/empty actuals without evaluating them. */
static void retained_arguments(syntax_parser *parser,size_t scope,size_t first,size_t callee_after,size_t limit)
{
 if(!token_is(parser,callee_after,"(")) return;
 size_t close=matching_parenthesis(parser,callee_after,limit);
 if(close>=limit) return;
 pigen_syntax_expr_id callee={PIGEN_INVALID_ID};pigen_syntax_error error={0};
 if(!pigen_parse_expression(parser->expanded,first,callee_after,&parser->tree->expressions,
  &parser->tree->types,&callee,&error)) return;
 size_t index=0;
 for(size_t at=callee_after+1;at<close;index++)
 {
  size_t end=top_level_token(parser,at,close,","),value=at,after=end;
  pigen_token_id named={PIGEN_INVALID_ID};
  if(token_is(parser,at,".") && identifier(parser,at+1) && token_is(parser,at+2,"(") &&
   matching_parenthesis(parser,at+2,end)==end-1)
  {named=(pigen_token_id){(uint32_t)(at+1)};value=at+3;after=end-1;}
  retained_name_add(parser,scope,PIGEN_RETAINED_ARGUMENT,value,SIZE_MAX,0);
  pigen_syntax_retained_name *actual=&parser->tree->retained_names[parser->tree->retained_name_count-1];
  actual->primary=callee;actual->first=value;actual->after=after;actual->argument_index=index;actual->argument_name=named;
  pigen_syntax_expr_id expression={PIGEN_INVALID_ID};pigen_syntax_error actual_error={0};
  if(value<after && pigen_parse_expression(parser->expanded,value,after,&parser->tree->expressions,
   &parser->tree->types,&expression,&actual_error)) actual->argument_value=expression;
  at=end<close ? end+1 : close;
 }
}

/* Expressions contribute lexical references, not values. Qualified members,
 * object fields, named arguments and assignment-pattern keys are not imports. */
static void retained_references(syntax_parser *parser,size_t scope,size_t first,size_t after)
{
 pigen_syntax_tree *tree=parser->tree;
 size_t braces=0;
 for(size_t at=first;at<after;at++)
 {
  if(token_is(parser,at,"{")) braces++;
  else if(token_is(parser,at,"}") && braces) braces--;
  if((identifier(parser,at) && !(at>first && (token_is(parser,at-1,".") || token_is(parser,at-1,"::")))) ||
   token_is(parser,at,"("))
  {
   size_t callee_after=at+1;
   if(token_is(parser,at,"("))
   {size_t close=matching_parenthesis(parser,at,after);callee_after=close<after ? close+1 : after;}
   else
    while(callee_after<after)
    {
     if((token_is(parser,callee_after,".") || token_is(parser,callee_after,"::")) && identifier(parser,callee_after+1)) callee_after+=2;
     else if(token_is(parser,callee_after,"["))
     {size_t close=matching_bracket(parser,callee_after,after);if(close>=after) break;callee_after=close+1;}
     else break;
    }
   retained_arguments(parser,scope,at,callee_after,after);
  }
  if(!identifier(parser,at) || (at>first && (token_is(parser,at-1,".") || token_is(parser,at-1,"::"))) ||
   token_is(parser,at+1,"::") || (braces && token_is(parser,at+1,":"))) continue;
  retained_name_add(parser,scope,token_is(parser,at+1,"(") ? PIGEN_RETAINED_CALL : PIGEN_RETAINED_REFERENCE,at,SIZE_MAX,0);
  size_t end=at+1;
  while(end<after)
  {
   if(token_is(parser,end,".") && identifier(parser,end+1)) end+=2;
   else if(token_is(parser,end,"["))
   {size_t close=matching_bracket(parser,end,after);if(close>=after) break;end=close+1;}
   else break;
  }
  if(tree->retained_names[tree->retained_name_count-1].kind==PIGEN_RETAINED_REFERENCE)
  {
   pigen_syntax_expr_id primary={PIGEN_INVALID_ID};pigen_syntax_error error={0};
   if(pigen_parse_expression(parser->expanded,at,end,&tree->expressions,&tree->types,&primary,&error))
    tree->retained_names[tree->retained_name_count-1].primary=primary;
  }
  /* Expression-local mutations are writes too, including helper arguments.
   * <= is deliberately excluded here: inside an expression it compares;
   * statement-level nonblocking destinations are marked by the caller. */
  static const char *const mutations[]={"=","+=","-=","*=","/=","%=","&=","|=","^=","<<=",">>=","<<<=",">>>=","++","--"};
  size_t left=at,right=end;
  while(left>first && token_is(parser,left-1,"(") && token_is(parser,right,")") &&
   matching_parenthesis(parser,left-1,after)==right)
  {
   /* Do not cross a call/cast boundary: incrementing a call result is not
    * incrementing its argument. Plain grouping preserves the lvalue. */
   if(left>first+1 && (identifier(parser,left-2) || token_is(parser,left-2,"'"))) break;
   left--;right++;
  }
  for(size_t i=0;i<sizeof mutations/sizeof *mutations;i++)
   if(token_is(parser,right,mutations[i])) tree->retained_names[tree->retained_name_count-1].written=1;
  if(left>first && (token_is(parser,left-1,"++") || token_is(parser,left-1,"--")))
   tree->retained_names[tree->retained_name_count-1].written=1;
 }
}

static int retained_word_in(const syntax_parser *parser,size_t at,const char *const *words,size_t count)
{
 for(size_t i=0;i<count;i++) if(token_is(parser,at,words[i])) return 1;
 return 0;
}

/* Recognize declaration boundaries without interpreting DATA or initializers.
 * A lone ANSI formal inherits its predecessor's type; ordinary statements do
 * not. All dimensions/defaults remain references at their authored positions. */
static int retained_declaration(syntax_parser *parser,size_t scope,size_t first,size_t after,int formal)
{
 static const char *const statements[]={"return","disable","break","continue","wait","assert","assume","cover",
  "force","release","assign","deassign","expect"};
 static const char *const qualifiers[]={"input","output","inout","ref","const","var","static","automatic",
  "localparam","parameter","typedef","rand","randc"};
 static const char *const types[]={"bit","logic","reg","wire","tri","byte","shortint","int","longint",
  "integer","time","real","shortreal","realtime","string","chandle","event","void"};
 size_t at=first;int implicit=formal;
 if(retained_word_in(parser,first,statements,sizeof statements/sizeof *statements)) return 0;
 while(at<after && retained_word_in(parser,at,qualifiers,sizeof qualifiers/sizeof *qualifiers)) {implicit=1;at++;}
 if(at==after) return 0;
 size_t type=at,name=SIZE_MAX,prefix_after=first;
 int builtin=retained_word_in(parser,at,types,sizeof types/sizeof *types);
 if(token_is(parser,at,"struct") || token_is(parser,at,"union") || token_is(parser,at,"enum"))
 {
  size_t open=at+1;
  while(open<after && !token_is(parser,open,"{")) open++;
  if(open==after) return 0;
  size_t close=open+1,braces=1;
  for(;close<after;close++)
  {if(token_is(parser,close,"{")) braces++;else if(token_is(parser,close,"}") && !--braces) break;}
  if(close==after) return 0;
  retained_references(parser,scope,first,open);
  if(token_is(parser,type,"enum"))
  {
   for(size_t item=open+1;item<close;)
   {
    size_t end=top_level_token(parser,item,close,",");
    if(identifier(parser,item)) retained_name_add(parser,scope,PIGEN_RETAINED_DECLARATION,item,SIZE_MAX,0);
    retained_references(parser,scope,item+1,end);item=end<close ? end+1 : close;
   }
  }
  else
  {
   size_t members=retained_scope_add(parser,scope,open,close+1);
   for(size_t item=open+1;item<close;)
   {
    size_t end=top_level_token(parser,item,close,";");
    if(!retained_declaration(parser,members,item,end,0)) retained_references(parser,members,item,end);
    item=end<close ? end+1 : close;
   }
  }
  at=close+1;prefix_after=at;
 }
 else if(builtin) at++;
 else if(identifier(parser,at))
 {
  size_t candidate=at+1;
  while(candidate+1<after && token_is(parser,candidate,"::") && identifier(parser,candidate+1)) candidate+=2;
  if(candidate==at+1 && implicit && (candidate==after || token_is(parser,candidate,",") || token_is(parser,candidate,"=")))
   name=at;
  else at=candidate;
 }
 else if(!token_is(parser,at,"[") && !token_is(parser,at,"signed") && !token_is(parser,at,"unsigned")) return 0;
 if(name==SIZE_MAX)
 {
  if(token_is(parser,at,"signed") || token_is(parser,at,"unsigned")) at++;
  while(at<after && token_is(parser,at,"["))
  {size_t close=matching_bracket(parser,at,after);if(close==after) return 0;at=close+1;}
  if(!identifier(parser,at))
  {
   /* Untyped old-style formals can carry unpacked dimensions. */
   if(implicit && identifier(parser,type) && !builtin) name=type;
   else return 0;
  }
  else name=at;
 }
 if(name>=after || (name+1<after && !token_is(parser,name+1,"[") &&
  !token_is(parser,name+1,",") && !token_is(parser,name+1,"="))) return 0;
 retained_references(parser,scope,prefix_after,name);
 for(size_t item=name;item<after;)
 {
  if(!identifier(parser,item)) return 0;
  retained_name_add(parser,scope,PIGEN_RETAINED_DECLARATION,item,SIZE_MAX,0);
  pigen_syntax_retained_name *declaration=&parser->tree->retained_names[parser->tree->retained_name_count-1];
  int mode=formal ? 1 : 0,explicit=0,constant=0,reference=0;
  for(size_t t=first;t<name;t++)
  {
   if(token_is(parser,t,"input")) {mode=1;explicit=1;}
   if(token_is(parser,t,"output") || token_is(parser,t,"inout")) {mode=2;explicit=1;}
   if(token_is(parser,t,"ref")) {mode=2;explicit=1;reference=1;}
   if(token_is(parser,t,"const")) constant=1;
  }
  declaration->formal_mode=reference && constant ? 1 : mode;declaration->formal_explicit=explicit;
  size_t comma=top_level_token(parser,item+1,after,",");
  retained_references(parser,scope,item+1,comma);
  item=comma<after ? comma+1 : after;
 }
 return 1;
}

static size_t retained_namespace_statement(syntax_parser *parser,size_t scope,size_t first,size_t limit,size_t depth)
{
 if(depth>128 || first>=limit) return limit;
 if(token_is(parser,first,"unique") || token_is(parser,first,"unique0") || token_is(parser,first,"priority"))
  return retained_namespace_statement(parser,scope,first+1,limit,depth+1);
 if(identifier(parser,first) && !token_is(parser,first,"begin") && !token_is(parser,first,"fork") && token_is(parser,first+1,":"))
 {
  retained_name_add(parser,scope,PIGEN_RETAINED_DECLARATION,first,SIZE_MAX,0);
  return retained_namespace_statement(parser,scope,first+2,limit,depth+1);
 }
 if(token_is(parser,first,"@") || token_is(parser,first,"#"))
 {
  size_t after=first+2;
  if(token_is(parser,first+1,"("))
  {size_t close=matching_parenthesis(parser,first+1,limit);if(close==limit) return limit;
   retained_references(parser,scope,first+2,close);after=close+1;}
  else retained_references(parser,scope,first+1,after);
  return retained_namespace_statement(parser,scope,after,limit,depth+1);
 }
 if(token_is(parser,first,"fork"))
 {
  size_t end=first+1,nesting=1;
  for(;end<limit;end++)
  {
   if(token_is(parser,end,"fork") && !token_is(parser,end-1,"disable") && !token_is(parser,end-1,"wait")) nesting++;
   else if((token_is(parser,end,"join") || token_is(parser,end,"join_any") || token_is(parser,end,"join_none")) && !--nesting) break;
  }
  if(end==limit) return limit;
  size_t nested=retained_scope_add(parser,scope,first,end+1),at=first+1;
  if(token_is(parser,at,":") && identifier(parser,at+1))
  {retained_name_add(parser,scope,PIGEN_RETAINED_DECLARATION,at+1,SIZE_MAX,0);at+=2;}
  while(at<end)
  {size_t next=retained_namespace_statement(parser,nested,at,end,depth+1);if(next<=at) break;at=next;}
  return token_is(parser,end+1,":") && identifier(parser,end+2) ? end+3 : end+1;
 }
 if(token_is(parser,first,"begin"))
 {
  size_t end=matching_begin_end(parser,first,limit),at=first+1;
  if(end==limit) return limit;
  size_t nested=retained_scope_add(parser,scope,first,end+1);
  if(token_is(parser,at,":") && identifier(parser,at+1))
  {retained_name_add(parser,scope,PIGEN_RETAINED_DECLARATION,at+1,SIZE_MAX,0);at+=2;}
  while(at<end)
  {size_t next=retained_namespace_statement(parser,nested,at,end,depth+1);if(next<=at) break;at=next;}
  return token_is(parser,end+1,":") && identifier(parser,end+2) ? end+3 : end+1;
 }
 if(token_is(parser,first,"import"))
 {
  size_t end=top_level_token(parser,first+1,limit,";");
  for(size_t at=first+1;at+2<end;)
  {
   if(!identifier(parser,at) || !token_is(parser,at+1,"::")) break;
   retained_name_add(parser,scope,PIGEN_RETAINED_IMPORT,at+2,at,token_is(parser,at+2,"*"));
   at+=3;if(token_is(parser,at,",")) at++;else break;
  }
  return end<limit ? end+1 : limit;
 }
 if(token_is(parser,first,"if") || token_is(parser,first,"while") || token_is(parser,first,"repeat") ||
  token_is(parser,first,"for") || token_is(parser,first,"foreach") || token_is(parser,first,"wait") ||
  token_is(parser,first,"assert") || token_is(parser,first,"assume") || token_is(parser,first,"cover"))
 {
  size_t close=matching_parenthesis(parser,first+1,limit);
  if(close==limit) return limit;
  size_t body_scope=scope;
  if(token_is(parser,first,"for") || token_is(parser,first,"foreach"))
   body_scope=retained_scope_add(parser,scope,first,limit);
  if(token_is(parser,first,"for"))
  {
   size_t end=top_level_token(parser,first+2,close,";");
   if(!retained_declaration(parser,body_scope,first+2,end,0)) retained_references(parser,body_scope,first+2,end);
   retained_references(parser,body_scope,end<close ? end+1 : close,close);
  }
  else if(token_is(parser,first,"foreach"))
  {
   size_t bracket=top_level_token(parser,first+2,close,"[");
   /* top_level_token skips bracket groups, so find the final iterator group. */
   for(size_t at=first+2;at<close;at++) if(token_is(parser,at,"[")) bracket=at;
   retained_references(parser,body_scope,first+2,bracket);
   for(size_t at=bracket+1;bracket<close && at<close;at++)
    if(identifier(parser,at)) retained_name_add(parser,body_scope,PIGEN_RETAINED_DECLARATION,at,SIZE_MAX,0);
  }
  else retained_references(parser,scope,first+2,close);
  size_t after=retained_namespace_statement(parser,body_scope,close+1,limit,depth+1);
  if(body_scope!=scope) parser->tree->retained_scopes[body_scope].location=range_location(parser,first,after);
  if((token_is(parser,first,"if") || token_is(parser,first,"assert") || token_is(parser,first,"assume") ||
   token_is(parser,first,"cover")) && token_is(parser,after,"else"))
   after=retained_namespace_statement(parser,scope,after+1,limit,depth+1);
  return after;
 }
 if(token_is(parser,first,"case") || token_is(parser,first,"casez") || token_is(parser,first,"casex"))
 {
  size_t close=matching_parenthesis(parser,first+1,limit);if(close==limit) return limit;
  retained_references(parser,scope,first+2,close);
  size_t at=close+1;
  while(at<limit && !token_is(parser,at,"endcase"))
  {
   size_t label=at;
   if(token_is(parser,at,"default")) {at++;if(token_is(parser,at,":")) at++;}
   else
   {
    do {size_t end=generate_case_label_end(parser,at,limit);if(end==limit) return limit;
     retained_references(parser,scope,at,end);at=end+1;} while(token_is(parser,at-1,","));
   }
   size_t next=retained_namespace_statement(parser,scope,at,limit,depth+1);
   if(next<=label) return limit;
   at=next;
  }
  return at<limit ? at+1 : limit;
 }
 if(token_is(parser,first,"forever")) return retained_namespace_statement(parser,scope,first+1,limit,depth+1);
 if(token_is(parser,first,"do"))
 {
  size_t after=retained_namespace_statement(parser,scope,first+1,limit,depth+1);
  if(token_is(parser,after,"while") && token_is(parser,after+1,"("))
  {size_t close=matching_parenthesis(parser,after+1,limit);retained_references(parser,scope,after+2,close);return close<limit ? close+2 : limit;}
  return after;
 }
 size_t end=top_level_token(parser,first,limit,";");
 size_t block=top_level_token(parser,first,end,"begin");
 if(block<end)
 {retained_references(parser,scope,first,block);return retained_namespace_statement(parser,scope,block,limit,depth+1);}
 if(end==first+1 && identifier(parser,first))
  retained_name_add(parser,scope,PIGEN_RETAINED_BARE_CALL,first,SIZE_MAX,0);
 else if(!retained_declaration(parser,scope,first,end,0))
 {
  size_t names=parser->tree->retained_name_count;
  retained_references(parser,scope,first,end);
  static const char *const writes[]={"=","<=","+=","-=","*=","/=","%=","&=","|=","^=","<<=",">>=","<<<=",">>>=","++","--"};
  size_t write=end;
  for(size_t i=0;i<sizeof writes/sizeof *writes;i++)
  {size_t at=top_level_token(parser,first,end,writes[i]);if(at<write) write=at;}
  for(size_t i=names;i<parser->tree->retained_name_count;i++)
   if((parser->tree->retained_names[i].name.index<write || write==first) && write<end)
    parser->tree->retained_names[i].written=1;
 }
 return end<limit ? end+1 : limit;
}

static void retained_signature(syntax_parser *parser,pigen_syntax_id id,pigen_token_id name,size_t header)
{
 pigen_syntax_tree *tree=parser->tree;pigen_syntax_node *node=&tree->nodes[id.index];
 size_t first=node->as.retained_subroutine.first_name,count=node->as.retained_subroutine.name_count;
 size_t root=node->as.retained_subroutine.first_scope,formals=0;int known=1;
 int ansi=token_is(parser,name.index+1,"(");
 if(ansi)
 {
  size_t close=matching_parenthesis(parser,name.index+1,header);int mode=1;
  if(close>=header) known=0;
  for(size_t at=name.index+2;known && at<close;)
  {
   size_t end=top_level_token(parser,at,close,","),found=SIZE_MAX;
   for(size_t i=first;i<first+count;i++)
   {
    pigen_syntax_retained_name *decl=&tree->retained_names[i];
    if(decl->kind==PIGEN_RETAINED_DECLARATION && decl->scope==root && decl->name.index>=at && decl->name.index<end)
    {if(found!=SIZE_MAX) {known=0;break;}found=i;}
   }
   if(found==SIZE_MAX) {known=0;break;}
   pigen_syntax_retained_name *decl=&tree->retained_names[found];
   if(decl->formal_explicit) mode=decl->formal_mode;
   decl->formal_mode=mode;decl->formal_index=++formals;
   at=end<close ? end+1 : close;
  }
 }
 /* Old-style declarations refine a header's names, or establish argument
  * order when the routine has no parenthesized header. */
 for(size_t i=first;known && i<first+count;i++)
 {
  pigen_syntax_retained_name *decl=&tree->retained_names[i];
  if(decl->kind!=PIGEN_RETAINED_DECLARATION || decl->scope!=root || !decl->formal_explicit || decl->name.index<=header) continue;
  if(!ansi) {decl->formal_index=++formals;continue;}
  size_t j=first;
  for(;j<first+count;j++)
  {
   pigen_syntax_retained_name *formal=&tree->retained_names[j];
   if(formal->formal_index && formal->name.index<header && tokens_equal(parser,formal->name,decl->name))
   {formal->formal_mode=decl->formal_mode;decl->formal_index=formal->formal_index;break;}
  }
  if(j==first+count) known=0;
 }
 node->as.retained_subroutine.has_name=1;node->as.retained_subroutine.name=name;
 node->as.retained_subroutine.formal_count=formals;node->as.retained_subroutine.signature_known=known;
}

static void retained_subroutine_namespace(syntax_parser *parser,pigen_syntax_id id,size_t first,size_t after,pigen_token_id name)
{
 pigen_syntax_tree *tree=parser->tree;
 size_t first_scope=tree->retained_scope_count,first_name=tree->retained_name_count;
 size_t scope=retained_scope_add(parser,SIZE_MAX,first,after);
 size_t header=top_level_token(parser,first,after,";");
 if(!retained_declaration(parser,scope,first+1,name.index+1,1))
 {
  retained_references(parser,scope,first+1,name.index);
  retained_name_add(parser,scope,PIGEN_RETAINED_DECLARATION,name.index,SIZE_MAX,0);
 }
 if(token_is(parser,name.index+1,"("))
 {
  size_t close=matching_parenthesis(parser,name.index+1,header);
  for(size_t at=name.index+2;at<close;)
  {
   size_t end=top_level_token(parser,at,close,",");
   if(!retained_declaration(parser,scope,at,end,1)) retained_references(parser,scope,at,end);
   at=end<close ? end+1 : close;
  }
 }
 size_t body_end=after;
 while(body_end>header && !token_is(parser,body_end,"endfunction") && !token_is(parser,body_end,"endtask")) body_end--;
 for(size_t at=header+1;at<body_end;)
 {size_t next=retained_namespace_statement(parser,scope,at,body_end,0);if(next<=at) break;at=next;}
 tree->nodes[id.index].as.retained_subroutine.first_scope=first_scope;
 tree->nodes[id.index].as.retained_subroutine.scope_count=tree->retained_scope_count-first_scope;
 tree->nodes[id.index].as.retained_subroutine.first_name=first_name;
 tree->nodes[id.index].as.retained_subroutine.name_count=tree->retained_name_count-first_name;
 retained_signature(parser,id,name,header);
}

static void retained_module_namespaces(syntax_parser *parser)
{
 pigen_syntax_tree *tree=parser->tree;
 for(size_t i=0;i<tree->node_count;i++)
 {
  const pigen_syntax_node *node=&tree->nodes[i];
  if(node->kind!=PIGEN_SYNTAX_RETAINED || node->as.retained_subroutine.scope_count) continue;
  size_t first=node->location.extent.first.index,after=node->location.extent.after.index;
  if(token_is(parser,first,"function") || token_is(parser,first,"task"))
  {
   pigen_token_id name=package_subroutine_name(parser,first,after);
   if(name.index!=PIGEN_INVALID_ID) retained_subroutine_namespace(parser,(pigen_syntax_id){(uint32_t)i},first,after,name);
   continue;
  }
  if(!token_is(parser,first,"assign") && !token_is(parser,first,"always") &&
     !token_is(parser,first,"always_ff") && !token_is(parser,first,"always_comb") &&
     !token_is(parser,first,"always_latch") && !token_is(parser,first,"initial") && !token_is(parser,first,"final")) continue;
  size_t scopes=tree->retained_scope_count,names=tree->retained_name_count;
  size_t scope=retained_scope_add(parser,SIZE_MAX,first,after);
  size_t body=token_is(parser,first,"assign") ? first : first+1;
  retained_namespace_statement(parser,scope,body,after,0);
  tree->nodes[i].as.retained_subroutine.first_scope=scopes;
  tree->nodes[i].as.retained_subroutine.scope_count=tree->retained_scope_count-scopes;
  tree->nodes[i].as.retained_subroutine.first_name=names;
  tree->nodes[i].as.retained_subroutine.name_count=tree->retained_name_count-names;
 }
}

static int ordinary_package_items_supported(const syntax_parser *parser,size_t first,size_t end)
{
 size_t name=first+1;
 if(token_is(parser,name,"automatic") || token_is(parser,name,"static")) name++;
 if(!identifier(parser,name) || !token_is(parser,name+1,";")) return 0;
 for(size_t at=name+2;at<end;)
 {
  if(token_is(parser,at,"function") || token_is(parser,at,"task"))
  {
   size_t after=package_subroutine_after(parser,at,end);
   if(after==SIZE_MAX || package_subroutine_name(parser,at,after).index==PIGEN_INVALID_ID) return 0;
   at=after;continue;
  }
  if(!token_is(parser,at,"typedef") && !token_is(parser,at,"parameter") && !token_is(parser,at,"localparam") &&
   !token_is(parser,at,"import") && !token_is(parser,at,"export")) return 0;
  size_t semicolon=top_level_token(parser,at,end,";");
  if(semicolon==end) return 0;
  /* Enum/class/union semantic types have their own remaining integration. */
  if(token_is(parser,at,"typedef") && (token_is(parser,at+1,"enum") || token_is(parser,at+1,"class") ||
   token_is(parser,at+1,"union"))) return 0;
  at=semicolon+1;
 }
 return 1;
}

static int structured_package_at(const syntax_parser *parser,size_t first,size_t *end)
{
 size_t limit=parser->expanded->token_count;
 for(*end=first+1;*end<limit;(*end)++) if(token_is(parser,*end,"endpackage")) break;
 if(*end==limit) return 0;
 for(size_t at=first+1;at<*end;at++)
  if(protocol_at(parser,at)!=PIGEN_PROTOCOL_NONE || protocol_alias_construct_at(parser,(pigen_syntax_id){0},at,*end) ||
   native_import_at(parser,at,*end)) return 1;
 size_t name=first+1;
 if(token_is(parser,name,"automatic") || token_is(parser,name,"static")) name++;
 return ordinary_package_items_supported(parser,first,*end) &&
  (package_context_is_typed(parser,first,*end) || ordinary_package_needed(parser,(pigen_token_id){(uint32_t)name},0));
}

static void exported_package_facts(syntax_parser *parser,pigen_syntax_id package)
{
 const pigen_syntax_node *owner=pigen_syntax_get(parser->tree,package);
 for(pigen_syntax_id id=owner->first_child;id.index!=PIGEN_INVALID_ID;)
 {
  const pigen_syntax_node *item=pigen_syntax_get(parser->tree,id);
  if(item->kind==PIGEN_SYNTAX_EXPORT)
  {
   for(pigen_syntax_id entry=owner->first_child;entry.index!=PIGEN_INVALID_ID;)
   {
    const pigen_syntax_node *import=pigen_syntax_get(parser->tree,entry);
    if(import->kind==PIGEN_SYNTAX_IMPORT && import->as.package_import.is_structured &&
     (token_is(parser,item->as.package_import.package.index,"*") ||
      tokens_equal(parser,item->as.package_import.package,import->as.package_import.package)))
    {
     pigen_syntax_id source=structured_package_name(parser,import->as.package_import.package,owner->location.extent.after.index);
     size_t count=parser->typedef_name_fact_count;
     for(size_t i=0;i<count;i++)
     {
      typedef_name_fact fact=parser->typedef_name_facts[i];
      if(fact.scope.index!=source.index ||
       (!item->as.package_import.wildcard && !tokens_equal(parser,item->as.package_import.member,fact.name)) ||
       (!import->as.package_import.wildcard && !tokens_equal(parser,import->as.package_import.member,fact.name)) ||
       typedef_name_fact_in_scope(parser,fact.name,package)) continue;
      add_typedef_name_fact(parser,fact.name,package,fact.kind);
      parser->typedef_name_facts[parser->typedef_name_fact_count-1].family=fact.family;
     }
    }
    entry=import->next_sibling;
   }
  }
  id=item->next_sibling;
 }
}

static int parse_structured_package(syntax_parser *parser,pigen_syntax_id root,size_t first,size_t end,size_t *after)
{
 size_t name=first+1;
 if(token_is(parser,name,"automatic") || token_is(parser,name,"static")) name++;
 if(!identifier(parser,name) || !token_is(parser,name+1,";")) return fail(parser,name,"package requires a name and semicolon");
 pigen_syntax_node node={0};node.kind=PIGEN_SYNTAX_PACKAGE;node.as.module.name=(pigen_token_id){(uint32_t)name};
 node.parent=node.first_child=node.last_child=node.next_sibling=INVALID_SYNTAX;
 size_t finish=end+1;if(token_is(parser,finish,":")) finish+=2;
 node.location=range_location(parser,first,finish);
 pigen_syntax_id package=add_node(parser,node);add_child(parser,root,package);
 syntax_cursor cursor=first;
 for(size_t at=name+2;at<end;)
 {
  if(token_is(parser,at,"function") || token_is(parser,at,"task"))
  {
   size_t next=package_subroutine_after(parser,at,end);
   if(next==SIZE_MAX) return fail(parser,at,"unterminated package subroutine");
   pigen_token_id declared=package_subroutine_name(parser,at,next);
   if(declared.index==PIGEN_INVALID_ID) return fail(parser,at,"package subroutine requires an unqualified name");
   for(size_t token=at;token<next;token++)
   {
    int native=0;
    if(protocol_at(parser,token)!=PIGEN_PROTOCOL_NONE)
    {
     size_t following=token+1;
     while(following<next && token_is(parser,following,"["))
     {size_t close=matching_bracket(parser,following,next);following=close<next ? close+1 : next;}
     native=identifier(parser,following);
    }
    if(native || numerical_construct_at(parser,token,next) || protocol_alias_construct_at(parser,package,token,next))
     return fail(parser,token,"retained package subroutine requires ordinary SystemVerilog constructs");
   }
   add_opaque(parser,package,cursor,at);add_retained_range(parser,package,at,next);
   const pigen_syntax_node *owner=pigen_syntax_get(parser->tree,package);
   parser->tree->nodes[owner->last_child.index].as.retained_subroutine.has_name=1;
   parser->tree->nodes[owner->last_child.index].as.retained_subroutine.name=declared;
   retained_subroutine_namespace(parser,owner->last_child,at,next,declared);
   cursor=next;advance_opaque_byte_cursor(parser,next);at=next;continue;
  }
  size_t semicolon=top_level_token(parser,at,end,";");
  if(semicolon==end) return fail(parser,at,"unsupported native package item");
  if(token_is(parser,at,"typedef"))
  {
   declaration_parse_result result=parse_typedef(parser,package,at,semicolon,&cursor);
   if(result==DECLARATION_INVALID) return 0;
   const pigen_syntax_node *owner=pigen_syntax_get(parser->tree,package);
   const pigen_syntax_node *definition=pigen_syntax_get(parser->tree,owner->last_child);
   if(result!=DECLARATION_RECOGNIZED)
   {
    add_opaque(parser,package,cursor,at);add_retained_range(parser,package,at,semicolon+1);
    cursor=semicolon+1;advance_opaque_byte_cursor(parser,cursor);
   }
   (void)definition;
  }
  else if(token_is(parser,at,"import") || token_is(parser,at,"export"))
  {if(!parse_import(parser,package,at,end,&cursor,&at)) return 0;continue;}
  else if(token_is(parser,at,"parameter") || token_is(parser,at,"localparam"))
  {
   if(!parse_parameter_items(parser,package,at,semicolon,&cursor)) return fail(parser,at,"unsupported package parameter");
  }
  else return fail(parser,at,"unsupported structured package item");
  at=semicolon+1;
 }
 exported_package_facts(parser,package);
 add_opaque(parser,package,cursor,finish);advance_opaque_byte_cursor(parser,finish);*after=finish;return 1;
}

int pigen_parse_syntax(const pigen_expanded_source *source,
	pigen_syntax_tree *tree, pigen_syntax_error *error)
{
	syntax_parser parser = {0};
	pigen_syntax_node root_node = {0};
	pigen_syntax_id root;
	size_t at = 0;
	syntax_cursor opaque_cursor;

	memset(tree, 0, sizeof(*tree));
	if (error) *error = (pigen_syntax_error){
		INVALID_ID(pigen_origin_id), invalid_source_span(), NULL};
	parser.expanded = source;
	parser.tree = tree;
	parser.error = error;
	parser.pipeline_failed = 0;
	parser.fsm_states = NULL;
	parser.source_length = 0;
	parser.opaque_byte_cursor = 0;
	if (!source || !pigen_source_get(source->sources, source->root_source) ||
		!source->token_count) return 0;
	parser.source_length = pigen_source_get(source->sources,
		source->root_source)->length;
	opaque_cursor = 0;
	tree->expanded = source;
	root_node.kind = PIGEN_SYNTAX_COMPILATION_UNIT;
	root_node.location = range_location(&parser, 0, source->token_count - 1);
	root_node.parent = root_node.first_child = root_node.last_child =
		root_node.next_sibling = INVALID_SYNTAX;
	root = add_node(&parser, root_node);
	while (token_at(&parser, at)->kind != PIGEN_TOKEN_EOF)
	{
		size_t package_end;
		if(token_is(&parser,at,"package") && structured_package_at(&parser,at,&package_end))
		{
			add_opaque(&parser,root,opaque_cursor,at);
			/* The package's first child owns its wrapper bytes. Root filler
			 * already owns any intervening ordinary packages. */
			parser.opaque_byte_cursor=pigen_origin_expansion_span(source,token_at(&parser,at)->origin).start;
			if(!parse_structured_package(&parser,root,at,package_end,&at)) {free_parser_state(&parser);return 0;}
			opaque_cursor=at;continue;
		}
		const char *closer = opaque_unit_closer(&parser, at);
		if (closer)
		{
			while (token_at(&parser, at)->kind != PIGEN_TOKEN_EOF &&
				!token_is(&parser, at, closer)) at++;
			if (token_at(&parser, at)->kind != PIGEN_TOKEN_EOF) at++;
			continue;
		}
		/* Global ordinary routines need the same syntax-owned signature facts
		 * as module routines. Keep their bodies retained rather than letting
		 * the root scanner interpret body-local declarations as unit items. */
		if ((token_is(&parser,at,"function") || token_is(&parser,at,"task")) &&
			!(at && token_is(&parser,at-1,"extern")))
		{
			size_t next=package_subroutine_after(&parser,at,source->token_count);
			if(next==SIZE_MAX)
			{fail(&parser,at,"unterminated compilation-unit subroutine");free_parser_state(&parser);return 0;}
			add_opaque(&parser,root,opaque_cursor,at);
			add_retained_range(&parser,root,at,next);
			advance_opaque_byte_cursor(&parser,next);
			at=opaque_cursor=next;continue;
		}
		if(token_is(&parser,at,"import"))
		{
			if(!parse_import(&parser,root,at,source->token_count,&opaque_cursor,&at)) {free_parser_state(&parser);return 0;}
			continue;
		}
		if (token_is(&parser, at, "typedef"))
		{
			size_t semicolon = top_level_token(&parser, at,
				source->token_count, ";");
			declaration_parse_result result;
			if (semicolon == source->token_count ||
				token_at(&parser, semicolon)->kind == PIGEN_TOKEN_EOF)
			{
				size_t boundary = source->token_count - 1;
				int decisive = decisive_typedef_without_terminator(&parser, root,
					at, boundary);

				if (decisive)
					fail(&parser, boundary,
						"typedef declaration requires `;`");
				free_parser_state(&parser);
				return 0;
			}
			result = parse_typedef(&parser, root, at, semicolon, &opaque_cursor);
			if (result == DECLARATION_INVALID)
			{
				free_parser_state(&parser);
				return 0;
			}
			at = semicolon + 1;
			continue;
		}
		if (!token_is(&parser, at, "module") && !token_is(&parser, at, "fabric") &&
			!(token_is(&parser, at, "extern") && token_is(&parser, at + 1, "module"))) { at++; continue; }
		add_opaque(&parser, root, opaque_cursor, at);
		if (!token_is(&parser, at, "extern") && !token_is(&parser, at, "fabric") && (parameterized_module(&parser, at) ||
			module_header_has_directionless_port(&parser, at) ||
			module_has_enum(&parser, at) || module_has_union(&parser, at) ||
			module_has_unpacked_struct(&parser, at) ||
			module_has_bare_int(&parser, at)) &&
			!parameterized_module_has_pigen(&parser, at))
		{
			if (!parse_parameterized_module_opaque(&parser, root, at, &at))
			{
				free_parser_state(&parser);
				return 0;
			}
		}
		else if (!parse_module(&parser, root, at, &at))
		{
			free_parser_state(&parser);
			return 0;
		}
		opaque_cursor = at;
	}
	/* Trailing bytes after the last design unit belong to the last module's
	 * nested scope when that is the most recent top-level item, so a module's
	 * owner-managed layout covers the complete remaining source. */
	{
		pigen_syntax_id owner = root;
		pigen_syntax_node *last = parser.tree->nodes[0].last_child.index ==
			PIGEN_INVALID_ID ? NULL :
			&parser.tree->nodes[parser.tree->nodes[0].last_child.index];

		(void)last;
		add_opaque_bytes(&parser, owner, parser.opaque_byte_cursor,
			parser.source_length);
	}
	for (size_t i = 0; i < parser.pending_parameterized_instance_count; i++)
		for (size_t j = 0; j < parser.parameterized_module_count; j++)
			if (token_text_equal(&parser,
				parser.pending_parameterized_instances[i].module_token,
				parser.parameterized_module_names[j].index))
			{
				fail(&parser,
					parser.pending_parameterized_instances[i].instance_token,
					"parameterized Pigen child instances are not supported");
				free_parser_state(&parser);
				return 0;
			}
	retained_module_namespaces(&parser);
	if(!check_retained_native_forward_uses(&parser))
	{free_parser_state(&parser);return 0;}
	free_parser_state(&parser);
	return 1;
}

const pigen_syntax_node *pigen_syntax_get(const pigen_syntax_tree *tree,
	pigen_syntax_id node)
{
	if (node.index == PIGEN_INVALID_ID || node.index >= tree->node_count)
		return NULL;
	return &tree->nodes[node.index];
}

const pigen_syntax_shape_dimension *pigen_syntax_declarator_shape_dimensions(
	const pigen_syntax_tree *tree, const pigen_syntax_node *declarator)
{
	const pigen_syntax_signal_declarator *known;

	if (!tree || !declarator) return NULL;
	if (declarator->kind != PIGEN_SYNTAX_SIGNAL_DECLARATOR)
		return NULL;
	known = &declarator->as.signal_declarator;
	if (!known->dimension_count ||
		known->first_shape_dimension + known->dimension_count >
			tree->shape_dimension_count) return NULL;
	return tree->shape_dimensions + known->first_shape_dimension;
}

void pigen_free_syntax_tree(pigen_syntax_tree *tree)
{
	free(tree->retained_scopes);
	free(tree->retained_names);
	for(size_t i=0;i<tree->generate_name_count;i++) free(tree->generate_names[i].external_name);
	free(tree->generate_names);
	free(tree->nodes);
	free(tree->shape_dimensions);
	pigen_free_syntax_expr_arena(&tree->expressions);
	pigen_free_syntax_type_arena(&tree->types);
	*tree = (pigen_syntax_tree){0};
}
