/* Elastic-pipeline discovery and parent-module lowering. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "pigen/pipeline.h"
#include "pigen/lexer.h"
#include "pigen/util.h"

typedef struct { char *name; char *type; } pipe_name;
typedef struct { char *text; char *name; char *type; int expression; } pipe_part;
typedef struct { char *name; char *type; char kind; } transport_symbol;
typedef struct {
	char *label;
	pipe_part *inputs; size_t input_count;
	pipe_part *outputs; size_t output_count;
	pipe_part *ingress; size_t ingress_count;
	pipe_name *locals; size_t local_count;
	char *ingress_name;
	char *body;
} pipe_stage;
typedef struct {
	char *name;
	pipe_part output;
	pipe_name *names; size_t name_count;
	transport_symbol *transports; size_t transport_count;
	pipe_stage *stages; size_t stage_count;
	size_t source_start, source_end;
	char *output_buffer, *reset_marker, *reset_guard, *yield_text;
	size_t assignment_offset, render_offset;
	int module_has_reset;
} pipeline_model;

struct pigen_pipelines { pipeline_model *items; size_t count; };

static const pipe_part *ingress_named(const pipe_stage *stage, const char *name);
static void append_ingress_reference(pigen_string *out, const pipe_stage *stage,
	const pipe_part *part, const char *packet);

typedef struct { const char *source; pigen_tokens tokens; size_t at; } parser;

static const pigen_token *cur(parser *p) { return &p->tokens.items[p->at]; }
static int tok(parser *p, size_t at, const char *text)
{ return at < p->tokens.count && pigen_token_is(p->source, &p->tokens.items[at], text); }
static void fail_at(parser *p, size_t at, const char *message)
{ pigen_set_diagnostic_position(p->source + p->tokens.items[at].span.start); pigen_fail(message); }
static void expect(parser *p, const char *text, const char *message)
{ if (!tok(p, p->at, text)) fail_at(p, p->at, message); p->at++; }

static char *range_copy(parser *p, size_t first, size_t last)
{
	if (first >= last) return pigen_copy_range("", 0);
	return pigen_copy_range(p->source + p->tokens.items[first].span.start,
		p->tokens.items[last - 1].span.end - p->tokens.items[first].span.start);
}

static char *identifier(parser *p, const char *message)
{
	size_t at;
	if (cur(p)->kind != PIGEN_TOKEN_IDENTIFIER) fail_at(p, p->at, message);
	at = p->at++;
	return range_copy(p, at, at + 1);
}

static pipe_name *find_name(pipeline_model *pipe, const char *name)
{
	for (size_t i = 0; i < pipe->name_count; i++) if (!strcmp(pipe->names[i].name, name)) return &pipe->names[i];
	return NULL;
}

static const transport_symbol *find_transport(const pipeline_model *pipe, const char *name)
{
	for (size_t i = 0; i < pipe->transport_count; i++)
		if (!strcmp(pipe->transports[i].name, name)) return &pipe->transports[i];
	return NULL;
}

static pipe_name *find_local(pipe_stage *stage, const char *name)
{
	for (size_t i = 0; i < stage->local_count; i++)
		if (!strcmp(stage->locals[i].name, name)) return &stage->locals[i];
	return NULL;
}


static void declare_name(pipeline_model *pipe, const char *name, const char *type)
{
	pipe_name *known = find_name(pipe, name);
	if (known)
	{
		if (strcmp(known->type, type)) pigen_fail("incompatible pipeline-local type declaration");
		return;
	}
	pipe->names = pigen_resize(pipe->names, (pipe->name_count + 1) * sizeof(*pipe->names));
	pipe->names[pipe->name_count++] = (pipe_name){pigen_copy_range(name, strlen(name)), pigen_copy_range(type, strlen(type))};
}

static void warn_pipeline_shadow(parser *p, const pipeline_model *pipe,
	const char *name, size_t name_at)
{
	if (!find_transport(pipe, name)) return;
	pigen_set_diagnostic_position(p->source + p->tokens.items[name_at].span.start);
	pigen_warn("pipeline-local declaration shadows a module-local name");
}

static size_t matching(parser *p, size_t at, const char *open, const char *close)
{
	int depth = 0;
	for (; at < p->tokens.count; at++)
	{
		if (tok(p, at, open)) depth++;
		else if (tok(p, at, close) && --depth == 0) return at;
	}
	pigen_fail("unterminated pipeline delimiter");
	return at;
}

static void add_part(pipe_part **parts, size_t *count, const char *text,
	const char *name, const char *type, int expression)
{
	*parts = pigen_resize(*parts, (*count + 1) * sizeof(**parts));
	(*parts)[(*count)++] = (pipe_part){pigen_copy_range(text, strlen(text)),
		name ? pigen_copy_range(name, strlen(name)) : NULL,
		type ? pigen_copy_range(type, strlen(type)) : NULL, expression};
}

static char transport_kind(parser *p, size_t at)
{
	if (tok(p, at, "buf")) return 'b';
	if (tok(p, at, "fifo")) return 'f';
	if (tok(p, at, "skid")) return 's';
	if (tok(p, at, "port")) return 'h';
	if (tok(p, at, "wire")) return 'w';
	if (tok(p, at, "reg")) return 'r';
	if (tok(p, at, "logic")) return 'l';
	if (tok(p, at, "localparam") || tok(p, at, "parameter")) return 'c';
	return 0;
}

static void add_transport_symbol(pipeline_model *pipe, const char *name,
	const char *type, char kind)
{
	if (find_transport(pipe, name)) return;
	pipe->transports = pigen_resize(pipe->transports,
		(pipe->transport_count + 1) * sizeof(*pipe->transports));
	pipe->transports[pipe->transport_count++] = (transport_symbol){
		pigen_copy_range(name, strlen(name)), pigen_copy_range(type, strlen(type)), kind};
}

/* Collect transport declarations from the enclosing source before the
 * pipeline is replaced.  Stage input discovery then resolves identifiers
 * against actual module transports rather than guessing from spelling. */
static void collect_transport_symbols(parser *p, pipeline_model *pipe)
{
	int begin_depth = 0;
	size_t first = 0, last = p->tokens.count;
	int in_module = 0;
	for (size_t at = 0; at < p->tokens.count &&
		p->tokens.items[at].span.start < pipe->source_start; at++)
	{
		if (tok(p, at, "module")) { first = at; in_module = 1; }
		else if (tok(p, at, "endmodule")) in_module = 0;
	}
	if (!in_module) return;
	for (last = first + 1; last < p->tokens.count && !tok(p, last, "endmodule"); last++) ;
	for (size_t at = first; at < last; at++)
	{
		char kind = transport_kind(p, at);
		size_t delimiter, name_at;
		char *type;
		int parens = 0, brackets = 0, braces = 0;
		if (tok(p, at, "begin")) { begin_depth++; continue; }
		if (tok(p, at, "end")) { if (begin_depth) begin_depth--; continue; }
		/* Module-scope declarations and ANSI ports live outside procedural
		 * begin/end scopes.  In particular, do not mistake stage-local wires
		 * or unrelated procedural locals for enclosing module names. */
		if (begin_depth) continue;
		if (!kind) continue;
		if (kind == 'l' || kind == 'r' || kind == 'w')
		{
			int is_typedef = 0;
			for (size_t before = at; before > 0 && !tok(p, before - 1, ";"); before--)
				if (tok(p, before - 1, "typedef")) { is_typedef = 1; break; }
			if (is_typedef) continue;
		}
		for (delimiter = at + 1; delimiter < p->tokens.count; delimiter++)
		{
			if (tok(p, delimiter, "(")) parens++;
			else if (tok(p, delimiter, ")")) { if (!parens) break; parens--; }
			else if (tok(p, delimiter, "[")) brackets++;
			else if (tok(p, delimiter, "]")) brackets--;
			else if (tok(p, delimiter, "{")) braces++;
			else if (tok(p, delimiter, "}")) braces--;
			else if (!parens && !brackets && !braces &&
				(tok(p, delimiter, ",") || tok(p, delimiter, ";") ||
				 tok(p, delimiter, ")") || tok(p, delimiter, "="))) break;
		}
		if (delimiter == p->tokens.count) continue;
		name_at = delimiter;
		while (name_at > at + 1 && p->tokens.items[name_at - 1].kind != PIGEN_TOKEN_IDENTIFIER)
			name_at--;
		if (name_at == at + 1) continue;
		name_at--;
		type = pigen_copy_range(p->source + p->tokens.items[at + 1].span.start,
			p->tokens.items[name_at].span.start - p->tokens.items[at + 1].span.start);
		{
			char *trim = type + strlen(type);
			while (trim > type && (trim[-1] == ' ' || trim[-1] == '\t' || trim[-1] == '\n' || trim[-1] == '\r')) *--trim = 0;
		}
		if (!*type && kind == 'c')
		{
			free(type);
			type = pigen_copy_range("integer", 7);
		}
		if (*type)
		{
			char *name = range_copy(p, name_at, name_at + 1);
			add_transport_symbol(pipe, name, type, kind);
			free(name);
		}
		/* A module-body declaration may share its payload type across a list
		 * of bare names. Stop as soon as the next item is another port or a
		 * typed declaration. */
		while (tok(p, delimiter, ",") && delimiter + 1 < p->tokens.count &&
			p->tokens.items[delimiter + 1].kind == PIGEN_TOKEN_IDENTIFIER &&
			(delimiter + 2 >= p->tokens.count || tok(p, delimiter + 2, ",") || tok(p, delimiter + 2, ";")))
		{
			char *name = range_copy(p, delimiter + 1, delimiter + 2);
			add_transport_symbol(pipe, name, type, kind);
			free(name);
			delimiter += 2;
		}
		free(type);
	}
}

static void add_stage_local(parser *body, pipeline_model *pipe, pipe_stage *stage,
	size_t first, size_t equals)
{
	size_t name_at = equals;
	char *name, *type;
	while (name_at > first + 1 && body->tokens.items[name_at - 1].kind != PIGEN_TOKEN_IDENTIFIER)
		name_at--;
	if (name_at == first + 1) fail_at(body, first, "stage-local wire requires a type and name");
	name_at--;
	name = range_copy(body, name_at, name_at + 1);
	type = range_copy(body, first + 1, name_at);
	if (find_name(pipe, name) || find_transport(pipe, name))
	{
		pigen_set_diagnostic_position(body->source + body->tokens.items[name_at].span.start);
		pigen_warn("stage-local declaration shadows a less-local name");
	}
	stage->locals = pigen_resize(stage->locals, (stage->local_count + 1) * sizeof(*stage->locals));
	stage->locals[stage->local_count++] = (pipe_name){name, type};
}

static void collect_stage_scope(parser *body, pipeline_model *pipe, pipe_stage *stage)
{
	size_t statement = 0;
	int parens = 0, brackets = 0, braces = 0;
	for (size_t at = 0; body->tokens.items[at].kind != PIGEN_TOKEN_EOF; at++)
	{
		if (tok(body, at, "(")) parens++;
		else if (tok(body, at, ")")) parens--;
		else if (tok(body, at, "[")) brackets++;
		else if (tok(body, at, "]")) brackets--;
		else if (tok(body, at, "{")) braces++;
		else if (tok(body, at, "}")) braces--;
		if (!parens && !brackets && !braces && tok(body, at, ";"))
		{
			if (statement < at && tok(body, statement, "wire"))
			{
				size_t equals = statement + 1;
				while (equals < at && !tok(body, equals, "=")) equals++;
				if (equals == at) fail_at(body, statement, "stage-local wire requires an initializer");
				add_stage_local(body, pipe, stage, statement, equals);
			}
			statement = at + 1;
		}
	}
	statement = 0; parens = brackets = braces = 0;
	for (size_t at = 0; body->tokens.items[at].kind != PIGEN_TOKEN_EOF; at++)
	{
		if (tok(body, at, "(")) parens++;
		else if (tok(body, at, ")")) parens--;
		else if (tok(body, at, "[")) brackets++;
		else if (tok(body, at, "]")) brackets--;
		else if (tok(body, at, "{")) braces++;
		else if (tok(body, at, "}")) braces--;
		if (!parens && !brackets && !braces && tok(body, at, ";"))
		{
			if (statement + 1 < at &&
				body->tokens.items[statement].kind == PIGEN_TOKEN_IDENTIFIER &&
				tok(body, statement + 1, "<="))
			{
				char *destination = range_copy(body, statement, statement + 1);
				if (find_local(stage, destination))
					fail_at(body, statement, "wire is not a transfer destination");
				free(destination);
			}
			statement = at + 1;
		}
	}
	for (size_t at = 0; body->tokens.items[at].kind != PIGEN_TOKEN_EOF; at++)
		if (body->tokens.items[at].kind == PIGEN_TOKEN_IDENTIFIER)
		{
			char *name = range_copy(body, at, at + 1);
			const transport_symbol *symbol = find_transport(pipe, name);
			int known = 0;
			for (size_t i = 0; i < stage->ingress_count; i++)
				if (!strcmp(stage->ingress[i].name, name)) known = 1;
			if (symbol && !known && !find_local(stage, name) && !find_name(pipe, name))
				add_part(&stage->ingress, &stage->ingress_count, name, name, symbol->type, 0);
			free(name);
		}
}

static void parse_declaration(parser *p, pipeline_model *pipe)
{
	size_t first = p->at, semi = p->at;
	size_t item = first;
	char *type = NULL;
	for (; semi < p->tokens.count && !tok(p, semi, ";"); semi++) ;
	if (semi == first || semi == p->tokens.count) fail_at(p, p->at, "pipeline type declaration requires `;`");
	for (size_t at = first; at <= semi; at++)
	{
		if (at == semi || tok(p, at, ","))
		{
			if (at == item) fail_at(p, at, "empty pipeline type declaration");
			if (!type)
			{
				if (p->tokens.items[at - 1].kind != PIGEN_TOKEN_IDENTIFIER) fail_at(p, at - 1, "pipeline declaration requires a name");
				type = range_copy(p, item, at - 1);
				{
					char *name = range_copy(p, at - 1, at);
					warn_pipeline_shadow(p, pipe, name, at - 1);
					declare_name(pipe, name, type); free(name);
				}
			}
			else
			{
				if (at - item != 1 || p->tokens.items[item].kind != PIGEN_TOKEN_IDENTIFIER)
					fail_at(p, item, "pipeline declaration continuation requires only a name");
				{
					char *name = range_copy(p, item, at);
					warn_pipeline_shadow(p, pipe, name, item);
					declare_name(pipe, name, type); free(name);
				}
			}
			item = at + 1;
		}
	}
	free(type); p->at = semi + 1;
}

static pipe_stage parse_stage(parser *p, pipeline_model *pipe)
{
	pipe_stage stage = {0};
	size_t body_start, body_end, close;
	expect(p, "stage", "expected `stage`");
	if (!tok(p, p->at, "begin")) stage.label = identifier(p, "expected stage name or `begin`");
	expect(p, "begin", "expected `begin` after stage");
	body_start = p->tokens.items[p->at - 1].span.end;
	close = matching(p, p->at - 1, "begin", "end");
	body_end = p->tokens.items[close].span.start;
	stage.body = pigen_copy_range(p->source + body_start, body_end - body_start);
	/* A stage owns declarations and transforms; `yield` belongs to the enclosing
	 * pipeline grammar and is invalid in this scope. */
	{
		parser body = {p->source + body_start, {0}, 0};
		pigen_lex_source(body.source, body_end - body_start, &body.tokens);
		collect_stage_scope(&body, pipe, &stage);
		for (; body.tokens.items[body.at].kind != PIGEN_TOKEN_EOF; body.at++)
			if (tok(&body, body.at, "yield"))
				fail_at(&body, body.at, "`yield` is permitted only after the final stage");
		pigen_free_tokens(&body.tokens);
	}
	p->at = close + 1;
	return stage;
}

/* A pipeline-level yield is emitted at module scope, where its field names do
 * not exist. Infer its packed type from the final pipeline scope instead of
 * leaving `$bits(field)` to create an accidental implicit module net. */
static char *yield_type(const pipeline_model *pipe, const char *expression)
{
	pigen_tokens tokens = {0};
	pigen_string widths = {0}, type = {0};
	size_t first, last;
	pigen_lex_source(expression, strlen(expression), &tokens);
	first = 0;
	while (tokens.items[first].kind != PIGEN_TOKEN_EOF &&
		(tokens.items[first].kind != PIGEN_TOKEN_IDENTIFIER)) first++;
	last = first + 1;
	if (tokens.items[first].kind == PIGEN_TOKEN_EOF)
		pigen_fail("pipeline `yield` requires an expression");
	if (tokens.items[first].kind == PIGEN_TOKEN_IDENTIFIER &&
		tokens.items[last].kind == PIGEN_TOKEN_EOF)
	{
		char *name = range_copy((parser *)&(parser){expression, tokens, first}, first, first + 1);
		pipe_name *field = find_name((pipeline_model *)pipe, name);
		free(name);
		if (!field) pigen_fail("pipeline `yield` requires pipeline-local fields");
		pigen_free_tokens(&tokens);
		return pigen_copy_range(field->type, strlen(field->type));
	}
	/* The useful nontrivial form is `{field_a, field_b, ...}`. */
	if (!pigen_token_is(expression, &tokens.items[0], "{"))
		pigen_fail("pipeline `yield` expression type is not inferable");
	for (size_t at = 1; tokens.items[at].kind != PIGEN_TOKEN_EOF; at++)
		if (tokens.items[at].kind == PIGEN_TOKEN_IDENTIFIER)
		{
			char *name = range_copy((parser *)&(parser){expression, tokens, at}, at, at + 1);
			pipe_name *field = find_name((pipeline_model *)pipe, name);
			free(name);
			if (!field) pigen_fail("pipeline `yield` concatenation requires pipeline-local fields");
			if (widths.length) pigen_append(&widths, " + ");
			pigen_append(&widths, "$bits("); pigen_append(&widths, field->type); pigen_append(&widths, ")");
		}
	if (!widths.length) pigen_fail("pipeline `yield` concatenation is empty");
	pigen_append(&type, "logic [("); pigen_append(&type, widths.data);
	pigen_append(&type, ")-1:0]");
	free(widths.data);
	pigen_free_tokens(&tokens);
	return type.data;
}

static void resolve_pipeline(pipeline_model *pipe)
{
	if (!pipe->stage_count) pigen_fail("pipeline requires at least one stage");
	if (!pipe->name_count) pigen_fail("procedural pipeline requires at least one pipeline-local declaration");
	for (size_t stage = 0; stage < pipe->stage_count; stage++)
		for (size_t i = 0; i < pipe->name_count; i++)
		{
			add_part(&pipe->stages[stage].inputs, &pipe->stages[stage].input_count,
				pipe->names[i].name, pipe->names[i].name, pipe->names[i].type, 0);
			add_part(&pipe->stages[stage].outputs, &pipe->stages[stage].output_count,
				pipe->names[i].name, pipe->names[i].name, pipe->names[i].type, 0);
		}
	if (!pipe->yield_text)
		pigen_fail("pipeline requires a final `yield expression;`");
	{
		char *type;
		pipe_stage *last = &pipe->stages[pipe->stage_count - 1];
		type = yield_type(pipe, pipe->yield_text);
		last->output_count = 0;
		add_part(&last->outputs, &last->output_count, pipe->yield_text, NULL, type, 1);
		pipe->output = (pipe_part){pigen_copy_range(pipe->yield_text, strlen(pipe->yield_text)),
			NULL, pigen_copy_range(type, strlen(type)), 1};
		free(type);
	}
}

static pipeline_model parse_pipeline(parser *p)
{
	pipeline_model pipe = {0};
	expect(p, "pipeline", "expected `pipeline`");
	pipe.source_start = p->tokens.items[p->at - 1].span.start;
	pipe.name = identifier(p, "expected pipeline name");
	expect(p, "begin", "procedural pipeline syntax is `pipeline name begin ... endpipeline`");
	collect_transport_symbols(p, &pipe);
	while (!tok(p, p->at, "endpipeline"))
	{
		if (cur(p)->kind == PIGEN_TOKEN_EOF) fail_at(p, p->at, "unterminated pipeline");
		if (tok(p, p->at, "stage"))
		{
			if (pipe.yield_text)
				fail_at(p, p->at, "`yield` must follow the final stage");
			pipe.stages = pigen_resize(pipe.stages, (pipe.stage_count + 1) * sizeof(*pipe.stages));
			pipe.stages[pipe.stage_count++] = parse_stage(p, &pipe);
		}
		else if (tok(p, p->at, "yield"))
		{
			size_t first = ++p->at, semi = first;
			while (semi < p->tokens.count && !tok(p, semi, ";")) semi++;
			if (semi == first || semi == p->tokens.count)
				fail_at(p, first, "pipeline `yield` requires an expression followed by `;`");
			if (pipe.yield_text) fail_at(p, first, "pipeline permits exactly one `yield`");
			pipe.yield_text = range_copy(p, first, semi);
			p->at = semi + 1;
		}
		else if (tok(p, p->at, "export"))
			fail_at(p, p->at, "`export` is reserved but not yet supported");
		else
			parse_declaration(p, &pipe);
	}
	pipe.source_end = p->tokens.items[p->at].span.end;
	p->at++;
	resolve_pipeline(&pipe);
	for (size_t i = 0; i < pipe.stage_count; i++) if (pipe.stages[i].ingress_count)
	{
		pigen_string name = {0};
		pigen_append_format(&name, "__pigen_pipe_%s_s%zu_in", pipe.name, i);
		pipe.stages[i].ingress_name = name.data;
	}
	pipe.output_buffer = pigen_copy_range(pipe.name, strlen(pipe.name));
	pipe.reset_marker = pigen_copy_range("__pigen_pipe_reset_", 19);
	pipe.reset_marker = pigen_resize(pipe.reset_marker, strlen(pipe.reset_marker) + strlen(pipe.name) + 1);
	strcat(pipe.reset_marker, pipe.name);
	return pipe;
}


static pipeline_model *pipeline_named(pigen_pipelines *pipes, const char *name)
{
	for (size_t i = 0; i < pipes->count; i++)
		if (!strcmp(pipes->items[i].name, name)) return &pipes->items[i];
	return NULL;
}

static char *rewrite_names(const char *source, size_t length, pigen_pipelines *pipes,
	const pigen_procedural_ast *ast, size_t *output_length)
{
	pigen_tokens tokens = {0};
	pigen_string out = {0};
	size_t copied = 0;
	pigen_lex_source(source, length, &tokens);
	for (size_t at = 0; at < tokens.count && tokens.items[at].kind != PIGEN_TOKEN_EOF; at++)
	{
		char *word;
		if (tokens.items[at].kind != PIGEN_TOKEN_IDENTIFIER) continue;
		word = range_copy((parser *)&(parser){source, tokens, at}, at, at + 1);
		if ((!strcmp(word, "pipe_reset") || !strcmp(word, "pipeline_reset")) && at + 4 < tokens.count &&
			tokens.items[at + 2].kind == PIGEN_TOKEN_IDENTIFIER &&
			pigen_token_is(source, &tokens.items[at + 1], "(") &&
			pigen_token_is(source, &tokens.items[at + 3], ")") &&
			pigen_token_is(source, &tokens.items[at + 4], ";"))
		{
			char *name = range_copy((parser *)&(parser){source, tokens, at + 2}, at + 2, at + 3);
			pipeline_model *pipe = pipeline_named(pipes, name);
			free(name);
			if (pipe)
			{
				const char *guard = pigen_procedural_guard_for(ast, source + tokens.items[at].span.start);
				if (pipe->reset_guard)
				{
					pigen_string combined = {0};
					pigen_append_format(&combined, "(%s) || (%s)", pipe->reset_guard, guard);
					free(pipe->reset_guard);
					pipe->reset_guard = combined.data;
				}
				else pipe->reset_guard = pigen_copy_range(guard, strlen(guard));
				pigen_append_range(&out, source + copied, tokens.items[at].span.start - copied);
				pigen_append(&out, "begin end");
				copied = tokens.items[at + 4].span.end;
				at += 4;
				free(word); continue;
			}
		}
		/* Pipeline output is the pipeline name itself; no yielded member is
		 * rewritten into a hidden projection. */
		free(word);
	}
	pigen_append_range(&out, source + copied, length - copied);
	pigen_free_tokens(&tokens);
	*output_length = out.length;
	return out.data ? out.data : pigen_copy_range(source, length);
}

char *pigen_prepare_pipeline_models(const char *source, size_t length,
	size_t *output_length, pigen_pipelines **pipelines)
{
	parser p = {source, {0}, 0};
	pigen_pipelines *result = calloc(1, sizeof(*result));
	pigen_string out = {0};
	size_t copied = 0;
	if (!result) pigen_fail("out of memory");
	pigen_lex_source(source, length, &p.tokens);
	while (p.tokens.items[p.at].kind != PIGEN_TOKEN_EOF)
	{
		if (!tok(&p, p.at, "pipeline") || p.at + 2 >= p.tokens.count ||
			p.tokens.items[p.at + 1].kind != PIGEN_TOKEN_IDENTIFIER || !tok(&p, p.at + 2, "begin"))
		{ p.at++; continue; }
		{
			pipeline_model pipe = parse_pipeline(&p);
			for (size_t i = 0; i < result->count; i++)
				if (!strcmp(result->items[i].name, pipe.name)) pigen_fail("duplicate pipeline name");
			pigen_append_range(&out, source + copied, pipe.source_start - copied);
			for (size_t stage = 0; stage < pipe.stage_count; stage++)
				if (pipe.stages[stage].ingress_count)
				{
					pigen_append(&out, pipe.stages[stage].ingress_name);
					pigen_append(&out, " <= {");
					for (size_t input = 0; input < pipe.stages[stage].ingress_count; input++)
					{
						if (input) pigen_append(&out, ", ");
						pigen_append(&out, pipe.stages[stage].ingress[input].text);
					}
					pigen_append(&out, "};");
				}
			/* The empty statement is a real procedural-AST anchor for a pipeline
			 * whose stages read only always-valid degenerate values. */
			pigen_append_format(&out, "/*__PIGEN_PIPELINE_SITE_%s__*/;", pipe.name);
			copied = pipe.source_end;
			result->items = pigen_resize(result->items, (result->count + 1) * sizeof(*result->items));
			result->items[result->count++] = pipe;
		}
	}
	pigen_append_range(&out, source + copied, length - copied);
	pigen_free_tokens(&p.tokens);
	{
		for (size_t i = 0; i < result->count; i++)
		{
			char marker[256], *at = NULL;
			for (size_t stage = 0; stage < result->items[i].stage_count && !at; stage++)
				if (result->items[i].stages[stage].ingress_count)
					at = strstr(out.data, result->items[i].stages[stage].ingress_name);
			snprintf(marker, sizeof(marker), "/*__PIGEN_PIPELINE_SITE_%s__*/", result->items[i].name);
			if (!at && (at = strstr(out.data, marker))) at = strchr(at, ';');
			if (!at) pigen_fail("internal pipeline placement failure");
			result->items[i].assignment_offset = (size_t)(at - out.data);
		}
		pigen_procedural_ast reset_ast = {0};
		char *prepared;
		pigen_parse_procedural_ast(out.data, out.data + out.length, &reset_ast);
		prepared = rewrite_names(out.data, out.length, result, &reset_ast, output_length);
		pigen_free_procedural_ast(&reset_ast);
		free(out.data);
		for (size_t i = 0; i < result->count; i++)
		{
			char marker[256], *at = NULL;
			for (size_t stage = 0; stage < result->items[i].stage_count && !at; stage++)
				if (result->items[i].stages[stage].ingress_count)
					at = strstr(prepared, result->items[i].stages[stage].ingress_name);
			snprintf(marker, sizeof(marker), "/*__PIGEN_PIPELINE_SITE_%s__*/", result->items[i].name);
			if (!at && (at = strstr(prepared, marker))) at = strchr(at, ';');
			if (!at) pigen_fail("internal pipeline placement failure");
			result->items[i].assignment_offset = (size_t)(at - prepared);
		}
		{
			pigen_tokens prepared_tokens = {0};
			pigen_string marked = {0};
			size_t marked_copied = 0;
			pigen_lex_source(prepared, *output_length, &prepared_tokens);
			for (size_t i = 0; i < result->count; i++)
			{
				size_t render = (size_t)-1;
				for (size_t at = 0; at < prepared_tokens.count; at++)
					if (prepared_tokens.items[at].span.start >= result->items[i].assignment_offset) break;
					else if (pigen_token_is(prepared, &prepared_tokens.items[at], "always") ||
						pigen_token_is(prepared, &prepared_tokens.items[at], "always_ff"))
						render = prepared_tokens.items[at].span.start;
				if (render == (size_t)-1) pigen_fail("pipeline must appear inside a clocked always block");
				result->items[i].render_offset = render;
				if (render < marked_copied) pigen_fail("internal pipeline placement order failure");
				pigen_append_range(&marked, prepared + marked_copied, render - marked_copied);
				pigen_append(&marked, "/*__PIGEN_PIPELINE_RTL_"); pigen_append(&marked, result->items[i].name); pigen_append(&marked, "__*/\n");
				marked_copied = render;
			}
			pigen_append_range(&marked, prepared + marked_copied, *output_length - marked_copied);
			pigen_free_tokens(&prepared_tokens);
			free(prepared);
			prepared = marked.data;
			*output_length = marked.length;
			for (size_t i = 0; i < result->count; i++)
			{
				char site[256], *at = NULL;
				for (size_t stage = 0; stage < result->items[i].stage_count && !at; stage++)
					if (result->items[i].stages[stage].ingress_count)
						at = strstr(prepared, result->items[i].stages[stage].ingress_name);
				snprintf(site, sizeof(site), "/*__PIGEN_PIPELINE_SITE_%s__*/", result->items[i].name);
				if (!at && (at = strstr(prepared, site))) at = strchr(at, ';');
				if (!at) pigen_fail("internal pipeline placement failure");
				result->items[i].assignment_offset = (size_t)(at - prepared);
			}
		}
		*pipelines = result;
		return prepared;
	}
}

static void append_width(pigen_string *out, const pipe_part *parts, size_t count)
{
	for (size_t i = 0; i < count; i++)
	{
		if (i) pigen_append(out, " + ");
		pigen_append(out, "$bits("); pigen_append(out, parts[i].type); pigen_append(out, ")");
	}
}

static void append_local_unpack(pigen_string *out, const char *packet,
	const pipe_part *parts, size_t count)
{
	for (size_t i = 0; i < count; i++)
	{
		pigen_append(out, "\t\t"); pigen_append(out, parts[i].type); pigen_append(out, " ");
		pigen_append(out, parts[i].name); pigen_append(out, "__pigen_in;\n");
		pigen_append(out, "\t\t"); pigen_append(out, parts[i].type); pigen_append(out, " ");
		pigen_append(out, parts[i].name); pigen_append(out, ";\n");
	}
	for (size_t i = 0; i < count; i++)
	{
		size_t offset_count = count - i - 1;
		pigen_append(out, "\t\t");
		pigen_append(out, parts[i].name); pigen_append(out, "__pigen_in = "); pigen_append(out, packet); pigen_append(out, "[");
		if (!offset_count) pigen_append(out, "0");
		else for (size_t j = i + 1; j < count; j++)
		{
			if (j != i + 1) pigen_append(out, " + ");
			pigen_append(out, "$bits("); pigen_append(out, parts[j].type); pigen_append(out, ")");
		}
		pigen_append(out, " +: $bits("); pigen_append(out, parts[i].type); pigen_append(out, ")];\n");
		pigen_append(out, "\t\t"); pigen_append(out, parts[i].name); pigen_append(out, " = ");
		pigen_append(out, parts[i].name); pigen_append(out, "__pigen_in;\n");
	}
}

static void append_local_declarations(pigen_string *out, const pipe_part *parts, size_t count)
{
	for (size_t i = 0; i < count; i++)
	{
		pigen_append(out, "\t\t"); pigen_append(out, parts[i].type);
		pigen_append(out, " "); pigen_append(out, parts[i].name); pigen_append(out, ";\n");
	}
}

static int part_named(const pipe_part *parts, size_t count, const char *name)
{
	for (size_t i = 0; i < count; i++) if (parts[i].name && !strcmp(parts[i].name, name)) return 1;
	return 0;
}

static int body_declares(const char *body, const char *type, const char *name)
{
	const char *at;
	if (!body) return 0;
	for (at = strstr(body, type); at; at = strstr(at + 1, type))
	{
		const char *after = at + strlen(type);
		while (*after == ' ' || *after == '\t' || *after == '\n' || *after == '\r') after++;
		if (!strncmp(after, name, strlen(name)) &&
			!pigen_is_identifier_char((unsigned char)after[strlen(name)])) return 1;
	}
	return 0;
}

/* Pigen permits a simple typed local declaration anywhere in a stage body.
 * Lower it to an SV declaration at the start of the generated nested scope,
 * while leaving an initializer as an assignment at its original program point. */

/* Stage assignments to pipeline fields are packet-transform expressions. They
 * are deliberately lowered to blocking comb assignments; all state is in the
 * generated elastic registers, never in procedural execution order. */
static void append_procedural_stage_body(pigen_string *out, const pipeline_model *pipe,
	const pipe_stage *stage, const char *body, const char *ingress_packet,
	pigen_string *declarations)
{
	pigen_tokens tokens = {0};
	size_t copied = 0, statement = 0;
	int parens = 0, brackets = 0, braces = 0, blocks = 0;
	pigen_lex_source(body, strlen(body), &tokens);
	for (size_t at = 0; at < tokens.count && tokens.items[at].kind != PIGEN_TOKEN_EOF; at++)
	{
		if (pigen_token_is(body, &tokens.items[at], "(")) parens++;
		else if (pigen_token_is(body, &tokens.items[at], ")")) parens--;
		else if (pigen_token_is(body, &tokens.items[at], "[")) brackets++;
		else if (pigen_token_is(body, &tokens.items[at], "]")) brackets--;
		else if (pigen_token_is(body, &tokens.items[at], "{")) braces++;
		else if (pigen_token_is(body, &tokens.items[at], "}")) braces--;
		else if (pigen_token_is(body, &tokens.items[at], "begin")) blocks++;
		else if (pigen_token_is(body, &tokens.items[at], "end")) blocks--;
		if (!pigen_token_is(body, &tokens.items[at], ";") || parens || brackets || braces || blocks) continue;
		if (statement < at && pigen_token_is(body, &tokens.items[statement], "yield"))
		{
			pigen_append_range(out, body + copied, tokens.items[statement].span.start - copied);
			copied = tokens.items[at].span.end;
		}
		else if (statement + 2 < at && tokens.items[statement].kind == PIGEN_TOKEN_IDENTIFIER &&
			pigen_token_is(body, &tokens.items[statement + 1], "<="))
		{
			char *lhs = range_copy((parser *)&(parser){body, tokens, statement}, statement, statement + 1);
			if (!find_local((pipe_stage *)stage, lhs) && find_name((pipeline_model *)pipe, lhs))
			{
				pigen_append_range(out, body + copied, tokens.items[statement + 1].span.start - copied);
				pigen_append(out, "=");
				copied = tokens.items[statement + 1].span.end;
				/* Every RHS field is read from the input packet.  Do not let an
				 * earlier transfer in this same stage become visible here. */
				for (size_t item = statement + 2; item < at; item++)
					if (tokens.items[item].kind == PIGEN_TOKEN_IDENTIFIER)
					{
						char *name = range_copy((parser *)&(parser){body, tokens, item}, item, item + 1);
						const pipe_part *ingress = ingress_named(stage, name);
						if (ingress || (!find_local((pipe_stage *)stage, name) &&
							find_name((pipeline_model *)pipe, name)))
						{
							pigen_append_range(out, body + copied, tokens.items[item].span.start - copied);
							if (ingress) append_ingress_reference(out, stage, ingress, ingress_packet);
							else { pigen_append(out, name); pigen_append(out, "__pigen_in"); }
							copied = tokens.items[item].span.end;
						}
						free(name);
					}
			}
			free(lhs);
		}
		/* Stage-local `wire` is a combinational local, not an SV net driven
		 * from an always_comb block. */
		else if (statement < at && pigen_token_is(body, &tokens.items[statement], "wire"))
		{
			size_t equals = statement + 1;
			while (equals < at && !pigen_token_is(body, &tokens.items[equals], "=")) equals++;
			if (equals == at || equals == statement + 1)
				pigen_fail("stage-local wire requires an initializer");
			pigen_append_range(out, body + copied, tokens.items[statement].span.start - copied);
			pigen_append_range(declarations, body + tokens.items[statement + 1].span.start,
				tokens.items[equals].span.start - tokens.items[statement + 1].span.start);
			pigen_append(declarations, ";\n");
			pigen_append_range(out, body + tokens.items[equals - 1].span.start,
				tokens.items[equals - 1].span.end - tokens.items[equals - 1].span.start);
			pigen_append(out, " =");
			copied = tokens.items[equals].span.end;
			for (size_t item = equals + 1; item < at; item++)
				if (tokens.items[item].kind == PIGEN_TOKEN_IDENTIFIER)
				{
					char *name = range_copy((parser *)&(parser){body, tokens, item}, item, item + 1);
					const pipe_part *ingress = ingress_named(stage, name);
					if (ingress || (!find_local((pipe_stage *)stage, name) &&
						find_name((pipeline_model *)pipe, name)))
					{
						pigen_append_range(out, body + copied, tokens.items[item].span.start - copied);
						if (ingress) append_ingress_reference(out, stage, ingress, ingress_packet);
						else { pigen_append(out, name); pigen_append(out, "__pigen_in"); }
						copied = tokens.items[item].span.end;
					}
					free(name);
				}
			pigen_append_range(out, body + copied, tokens.items[at].span.end - copied);
			copied = tokens.items[at].span.end;
		}
		statement = at + 1;
	}
	pigen_append_range(out, body + copied, strlen(body) - copied);
	pigen_free_tokens(&tokens);
	(void)declarations;
}

static const pipe_part *ingress_named(const pipe_stage *stage, const char *name)
{
	for (size_t i = 0; i < stage->ingress_count; i++)
		if (!strcmp(stage->ingress[i].name, name)) return &stage->ingress[i];
	return NULL;
}

static void append_ingress_reference(pigen_string *out, const pipe_stage *stage,
	const pipe_part *part, const char *packet)
{
	size_t index = (size_t)(part - stage->ingress);
	int is_signed = !strncmp(part->type, "signed ", 7) || strstr(part->type, " signed ");
	int is_builtin = strchr(part->type, '[') || !strcmp(part->type, "logic") ||
		!strcmp(part->type, "bit") || !strcmp(part->type, "reg") ||
		!strcmp(part->type, "integer") || !strcmp(part->type, "int");
	/* The part-select already has exactly the declared width.  It only needs
	 * an explicit signed interpretation for signed packed declarations; named
	 * typedefs retain their type through an ordinary SV cast. */
	if (is_signed) pigen_append(out, "$signed(");
	else if (!is_builtin) { pigen_append(out, part->type); pigen_append(out, "'("); }
	pigen_append(out, packet); pigen_append(out, "[");
	if (index + 1 == stage->ingress_count) pigen_append(out, "0");
	else for (size_t i = index + 1; i < stage->ingress_count; i++)
	{
		if (i != index + 1) pigen_append(out, " + ");
		pigen_append(out, "$bits("); pigen_append(out, stage->ingress[i].text); pigen_append(out, ")");
	}
	pigen_append(out, " +: $bits("); pigen_append(out, part->text); pigen_append(out, ")]");
	if (is_signed || !is_builtin) pigen_append(out, ")");
}

static void append_stage_expression(pigen_string *out, const pipeline_model *pipe,
	const pipe_stage *stage,
	const char *body, const pigen_tokens *tokens, size_t first, size_t last,
	const char *packet, int permit_pipeline_fields)
{
	size_t copied = tokens->items[first].span.start;
	for (size_t at = first; at < last; at++) if (tokens->items[at].kind == PIGEN_TOKEN_IDENTIFIER)
	{
		char *name = range_copy((parser *)&(parser){body, *tokens, at}, at, at + 1);
		const pipe_part *ingress = ingress_named(stage, name);
		pipe_name *field = find_local((pipe_stage *)stage, name) ? NULL :
			find_name((pipeline_model *)pipe, name);
		if (ingress || field)
		{
			if (field && !permit_pipeline_fields)
				pigen_fail("first-stage expression cannot read a pipeline-local value");
			pigen_append_range(out, body + copied, tokens->items[at].span.start - copied);
			if (ingress) append_ingress_reference(out, stage, ingress, packet);
			else { pigen_append(out, name); pigen_append(out, "__pigen_in"); }
			copied = tokens->items[at].span.end;
		}
		free(name);
	}
	pigen_append_range(out, body + copied, tokens->items[last - 1].span.end - copied);
}

static void append_first_stage_body_new(pigen_string *out, const pipeline_model *pipe,
	const pipe_stage *stage, const char *body, const char *packet, pigen_string *declarations)
{
	pigen_tokens tokens = {0};
	size_t statement = 0;
	int parens = 0, brackets = 0, braces = 0;
	pigen_lex_source(body, strlen(body), &tokens);
	for (size_t at = 0; tokens.items[at].kind != PIGEN_TOKEN_EOF; at++)
	{
		if (pigen_token_is(body, &tokens.items[at], "(")) parens++;
		else if (pigen_token_is(body, &tokens.items[at], ")")) parens--;
		else if (pigen_token_is(body, &tokens.items[at], "[")) brackets++;
		else if (pigen_token_is(body, &tokens.items[at], "]")) brackets--;
		else if (pigen_token_is(body, &tokens.items[at], "{")) braces++;
		else if (pigen_token_is(body, &tokens.items[at], "}")) braces--;
		if (!pigen_token_is(body, &tokens.items[at], ";") || parens || brackets || braces) continue;
		{
			size_t op = statement;
			while (op < at && !pigen_token_is(body, &tokens.items[op], "<=")) op++;
			if (statement < at && pigen_token_is(body, &tokens.items[statement], "wire"))
			{
				size_t equals = statement + 1;
				while (equals < at && !pigen_token_is(body, &tokens.items[equals], "=")) equals++;
				if (equals == at || equals == statement + 1) pigen_fail("stage-local wire requires an initializer");
				pigen_append_range(declarations, body + tokens.items[statement + 1].span.start,
					tokens.items[equals].span.start - tokens.items[statement + 1].span.start);
				pigen_append(declarations, ";\n");
				pigen_append_range(out, body + tokens.items[equals - 1].span.start,
					tokens.items[equals - 1].span.end - tokens.items[equals - 1].span.start);
				pigen_append(out, " = ");
				append_stage_expression(out, pipe, stage, body, &tokens, equals + 1, at,
					packet, 0);
				pigen_append(out, ";\n");
			}
			else if (op < at)
			{
				pigen_append_range(out, body + tokens.items[statement].span.start,
					tokens.items[op].span.start - tokens.items[statement].span.start);
				pigen_append(out, "= ");
				append_stage_expression(out, pipe, stage, body, &tokens, op + 1, at,
					packet, 0);
				pigen_append(out, ";\n");
			}
			else pigen_append_range(out, body + tokens.items[statement].span.start,
				tokens.items[at].span.end - tokens.items[statement].span.start);
		}
		statement = at + 1;
	}
	pigen_free_tokens(&tokens);
}

static int header_has_reset(const char *source, size_t module_start, size_t header_end)
{
	const char *at = source + module_start;
	const char *end = source + header_end;
	for (; at + 5 <= end; at++)
		if (!memcmp(at, "reset", 5) && (at == source + module_start || !pigen_is_identifier_char((unsigned char)at[-1])) &&
			(at + 5 == end || !pigen_is_identifier_char((unsigned char)at[5]))) return 1;
	return 0;
}

static void append_pipeline_rtl(pigen_string *out, pipeline_model *pipe,
	const char *source, const pigen_procedural_ast *ast)
{
	const char *guard = pigen_procedural_guard_for(ast, source + pipe->assignment_offset);
	const char *domain = pigen_procedural_domain_for(ast, source + pipe->assignment_offset);
	const char *reset = pipe->reset_guard ? pipe->reset_guard :
		(pipe->module_has_reset ? "reset" : "1'b0");

	if (!pigen_procedural_statement_for(ast, source + pipe->assignment_offset))
		pigen_fail("pipeline must appear inside a clocked always block");
	if (!domain || !*domain) pigen_fail("pipeline requires a clock event");
	pigen_append(out, "\n\t/* Pigen pipeline: "); pigen_append(out, pipe->name); pigen_append(out, " */\n");
	pigen_append(out, "\t/*__PIGEN_PIPELINE_BEGIN__*/\n");
	pigen_append(out, "\t(* pigen_internal = 1 *) logic "); pigen_append(out, pipe->reset_marker); pigen_append(out, ";\n");
	for (size_t i = 0; i < pipe->stage_count; i++)
	{
		pipe_stage *stage = &pipe->stages[i];
		pigen_append(out, "\t(* pigen_internal = 1 *) logic ");
		pigen_append_format(out, "%s__s%zu_valid, %s__s%zu_ready, %s__s%zu_slot_ready;\n",
			pipe->name, i, pipe->name, i, pipe->name, i);
		pigen_append(out, "\t(* pigen_internal = 1 *) logic [("); append_width(out, stage->outputs, stage->output_count);
		pigen_append_format(out, ")-1:0] %s__s%zu_packet, %s__s%zu_comb;\n", pipe->name, i, pipe->name, i);
		if (i + 1 < pipe->stage_count)
		{
			pigen_append(out, "\tinitial if ($bits("); pigen_append_format(out, "%s__s%zu_packet", pipe->name, i);
			pigen_append(out, ") != ("); append_width(out, pipe->stages[i + 1].inputs, pipe->stages[i + 1].input_count);
			pigen_append(out, ")) $fatal(1, \"Pigen pipeline stage packing width mismatch\");\n");
		}
	}
	for (size_t i = 0; i < pipe->stage_count; i++)
		if (pipe->stages[i].ingress_count)
		{
			pigen_append(out, "\tassign "); pigen_append(out, pipe->stages[i].ingress_name);
			pigen_append(out, "__pigen_in_ready = ");
			pigen_append_format(out, "%s__s%zu_slot_ready", pipe->name, i);
			if (i) pigen_append_format(out, " && %s__s%zu_valid", pipe->name, i - 1);
			pigen_append(out, ";\n");
		}
	pigen_append(out, "\tassign "); pigen_append(out, pipe->output_buffer); pigen_append(out, "__pigen_in_valid = (");
	pigen_append(out, guard && *guard ? guard : "1'b1"); pigen_append(out, ") && ");
	pigen_append_format(out, "%s__s%zu_valid;\n", pipe->name, pipe->stage_count - 1);
	pigen_append(out, "\tassign "); pigen_append(out, pipe->output_buffer); pigen_append(out, "__pigen_packet_in = ");
	pigen_append_format(out, "%s__s%zu_packet", pipe->name, pipe->stage_count - 1);
	pigen_append(out, ";\n");
	for (size_t i = pipe->stage_count; i-- > 0; )
	{
		pigen_append(out, "\tassign "); pigen_append_format(out, "%s__s%zu_slot_ready = (", pipe->name, i);
		pigen_append(out, guard && *guard ? guard : "1'b1"); pigen_append(out, ") && (~");
		pigen_append_format(out, "%s__s%zu_valid || ", pipe->name, i);
		if (i + 1 == pipe->stage_count) { pigen_append(out, pipe->output_buffer); pigen_append(out, "__pigen_in_ready"); }
		else pigen_append_format(out, "%s__s%zu_ready", pipe->name, i + 1);
		pigen_append(out, ");\n");
		pigen_append(out, "\tassign "); pigen_append_format(out, "%s__s%zu_ready = %s__s%zu_slot_ready",
			pipe->name, i, pipe->name, i);
		if (i && pipe->stages[i].ingress_count)
		{
			pigen_append(out, " && "); pigen_append(out, pipe->stages[i].ingress_name);
			pigen_append(out, "__pigen_in_valid");
		}
		pigen_append(out, ";\n");
	}
	for (size_t i = 0; i < pipe->stage_count; i++)
	{
		pipe_stage *stage = &pipe->stages[i];
		/* always_comb executes once at time zero as well as on dependency
		 * changes.  That matters for stages fed only by constants/localparams. */
		pigen_append(out, "\talways_comb begin\n");
		for (size_t j = 0; j < stage->output_count; j++)
			if (!stage->outputs[j].expression && stage->outputs[j].name &&
				!part_named(stage->inputs, stage->input_count, stage->outputs[j].name) &&
				!body_declares(stage->body, stage->outputs[j].type, stage->outputs[j].name))
			{
				pigen_append(out, "\t\t"); pigen_append(out, stage->outputs[j].type);
				pigen_append(out, " "); pigen_append(out, stage->outputs[j].name); pigen_append(out, ";\n");
			}
		if (i) { char name[128]; snprintf(name, sizeof(name), "%s__s%zu_packet", pipe->name, i - 1); append_local_unpack(out, name, stage->inputs, stage->input_count); }
		else append_local_declarations(out, stage->inputs, stage->input_count);
		/* A stage body has its own lexical scope. Besides matching the language
		 * model, this permits ordinary local declarations after the generated
		 * unpack assignments without violating SV's declaration-before-statement
		 * rule in the enclosing always_comb block. */
		if (stage->body) {
			pigen_string body = {0}, declarations = {0};
			char ingress_packet[256] = {0};
			if (stage->ingress_count)
				snprintf(ingress_packet, sizeof(ingress_packet), "%s__pigen_packet_in", stage->ingress_name);
			if (!i) {
				append_first_stage_body_new(&body, pipe, stage, stage->body,
					ingress_packet, &declarations);
			}
			else append_procedural_stage_body(&body, pipe, stage, stage->body,
				ingress_packet, &declarations);
			pigen_append(out, "\t\tbegin\n"); pigen_append(out, declarations.data ? declarations.data : "");
			pigen_append(out, body.data ? body.data : ""); pigen_append(out, "\n\t\t");
			free(body.data); free(declarations.data);
		}
		else pigen_append(out, "\n\t\t");
		pigen_append_format(out, "%s__s%zu_comb = {", pipe->name, i);
		for (size_t j = 0; j < stage->output_count; j++) { if (j) pigen_append(out, ", "); pigen_append(out, stage->outputs[j].text); }
		pigen_append(out, "};\n");
		if (stage->body) pigen_append(out, "\t\tend\n");
		pigen_append(out, "\tend\n");
		pigen_append(out, "\talways_ff @("); pigen_append(out, domain); pigen_append(out, ") begin\n\t\tif (");
		pigen_append(out, reset);
		pigen_append_format(out, ") begin %s__s%zu_valid <= 1'b0; %s__s%zu_packet <= '0; end\n", pipe->name, i, pipe->name, i);
		pigen_append(out, "\t\telse if ("); pigen_append(out, guard && *guard ? guard : "1'b1"); pigen_append(out, ") begin\n\t\t\tif (");
		pigen_append_format(out, "%s__s%zu_slot_ready) begin\n", pipe->name, i);
		pigen_append(out, "\t\t\t\tif (");
		if (!i) pigen_append(out, stage->ingress_count ? stage->ingress_name : "1'b1");
		else pigen_append_format(out, "%s__s%zu_valid", pipe->name, i - 1);
		if (!i && stage->ingress_count) pigen_append(out, "__pigen_in_valid");
		else if (i && stage->ingress_count)
		{
			pigen_append(out, " && "); pigen_append(out, stage->ingress_name);
			pigen_append(out, "__pigen_in_valid");
		}
		pigen_append_format(out, ") begin %s__s%zu_packet <= %s__s%zu_comb; %s__s%zu_valid <= 1'b1; end\n", pipe->name, i, pipe->name, i, pipe->name, i);
		pigen_append_format(out, "\t\t\t\telse %s__s%zu_valid <= 1'b0;\n", pipe->name, i);
		pigen_append(out, "\t\t\tend\n\t\tend\n\tend\n");
	}
	pigen_append(out, "\t/*__PIGEN_PIPELINE_END__*/\n");
}

static void append_pipeline_declarations(pigen_string *out, const pipeline_model *pipe)
{
	pigen_append(out, "\n\t/* Pigen pipeline endpoints: "); pigen_append(out, pipe->name); pigen_append(out, " */\n");
	for (size_t stage = 0; stage < pipe->stage_count; stage++)
		if (pipe->stages[stage].ingress_count)
		{
			pigen_append(out, "\tingress [(");
			for (size_t input = 0; input < pipe->stages[stage].ingress_count; input++)
			{
				if (input) pigen_append(out, " + ");
				pigen_append(out, "$bits(");
				pigen_append(out, pipe->stages[stage].ingress[input].text);
				pigen_append(out, ")");
			}
			pigen_append(out, ")-1:0] ");
			pigen_append(out, pipe->stages[stage].ingress_name);
			pigen_append(out, ";\n");
		}
	pigen_append(out, "\tbuf ");
	if (!strncmp(pipe->output.type, "logic ", 6))
		pigen_append(out, pipe->output.type + 6);
	else pigen_append(out, pipe->output.type);
	pigen_append(out, " "); pigen_append(out, pipe->output_buffer); pigen_append(out, ";\n");
}

char *pigen_finish_pipeline_models(const char *source, size_t length,
	const pigen_procedural_ast *ast, pigen_pipelines *pipelines,
	size_t *output_length)
{
	pigen_tokens tokens = {0};
	pigen_string out = {0};
	size_t copied = 0;
	pigen_lex_source(source, length, &tokens);
	for (size_t at = 0; at < tokens.count; at++) if (pigen_token_is(source, &tokens.items[at], "module"))
	{
		size_t header_end = at;
		size_t module_end;
		int parens = 0;
		for (; header_end < tokens.count; header_end++)
		{
			if (pigen_token_is(source, &tokens.items[header_end], "(")) parens++;
			else if (pigen_token_is(source, &tokens.items[header_end], ")")) parens--;
			else if (!parens && pigen_token_is(source, &tokens.items[header_end], ";")) break;
		}
		if (header_end == tokens.count) pigen_fail("unterminated module header");
		for (module_end = header_end + 1; module_end < tokens.count &&
			!pigen_token_is(source, &tokens.items[module_end], "endmodule"); module_end++) ;
		if (module_end == tokens.count) pigen_fail("unterminated module");
		for (size_t i = 0; i < pipelines->count; i++)
			if (pipelines->items[i].assignment_offset > tokens.items[at].span.start &&
				pipelines->items[i].assignment_offset < tokens.items[module_end].span.start)
			{
				pipelines->items[i].module_has_reset = header_has_reset(source,
					tokens.items[at].span.start, tokens.items[header_end].span.end);
			}
	}
	pigen_append_range(&out, source + copied, length - copied);
	pigen_free_tokens(&tokens);
	{
		pigen_string expanded = {0};
		const char *scan = out.data ? out.data : source;
		for (size_t i = 0; i < pipelines->count; i++)
		{
			char marker[256];
			const char *found;
			snprintf(marker, sizeof(marker), "/*__PIGEN_PIPELINE_RTL_%s__*/", pipelines->items[i].name);
			found = strstr(scan, marker);
			if (!found) pigen_fail("internal pipeline render placement failure");
			pigen_append_range(&expanded, scan, (size_t)(found - scan));
			append_pipeline_declarations(&expanded, &pipelines->items[i]);
			append_pipeline_rtl(&expanded, &pipelines->items[i], source, ast);
			scan = found + strlen(marker);
		}
		pigen_append(&expanded, scan);
		/* Placement anchors belong only to the compiler's intermediate source;
		 * do not leak their comments or empty statements into emitted RTL. */
		for (size_t i = 0; i < pipelines->count; i++)
		{
			char marker[288], *found;
			snprintf(marker, sizeof(marker), "/*__PIGEN_PIPELINE_SITE_%s__*/;", pipelines->items[i].name);
			while ((found = strstr(expanded.data, marker)))
			{
				size_t marker_length = strlen(marker);
				memmove(found, found + marker_length,
					expanded.length - (size_t)(found - expanded.data) - marker_length + 1);
				expanded.length -= marker_length;
			}
		}
		free(out.data);
		*output_length = expanded.length;
		return expanded.data ? expanded.data : pigen_copy_range(source, length);
	}
}

void pigen_free_pipeline_models(pigen_pipelines *pipes)
{
	if (!pipes) return;
	for (size_t i = 0; i < pipes->count; i++)
	{
		pipeline_model *p = &pipes->items[i];
		free(p->name); free(p->output_buffer); free(p->reset_marker); free(p->reset_guard);
		free(p->yield_text); free(p->output.text); free(p->output.name); free(p->output.type);
		for (size_t j = 0; j < p->name_count; j++) { free(p->names[j].name); free(p->names[j].type); }
		free(p->names);
		for (size_t j = 0; j < p->transport_count; j++) { free(p->transports[j].name); free(p->transports[j].type); }
		free(p->transports);
		for (size_t j = 0; j < p->stage_count; j++)
		{
			pipe_stage *s = &p->stages[j]; free(s->label); free(s->body); free(s->ingress_name);
			for (size_t k = 0; k < s->input_count; k++) { free(s->inputs[k].text); free(s->inputs[k].name); free(s->inputs[k].type); }
			for (size_t k = 0; k < s->output_count; k++) { free(s->outputs[k].text); free(s->outputs[k].name); free(s->outputs[k].type); }
			for (size_t k = 0; k < s->ingress_count; k++) { free(s->ingress[k].text); free(s->ingress[k].name); free(s->ingress[k].type); }
			for (size_t k = 0; k < s->local_count; k++) { free(s->locals[k].name); free(s->locals[k].type); }
			free(s->inputs); free(s->outputs); free(s->ingress); free(s->locals);
		}
		free(p->stages);
	}
	free(pipes->items); free(pipes);
}
