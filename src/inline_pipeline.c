/* Inline elastic-pipeline discovery and parent-module lowering. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "pigen/inline_pipeline.h"
#include "pigen/lexer.h"
#include "pigen/util.h"

typedef struct { char *name; char *type; } pipe_name;
typedef struct { char *text; char *name; char *type; int expression; } pipe_part;
typedef struct {
	char *label;
	pipe_part *inputs; size_t input_count;
	pipe_part *outputs; size_t output_count;
	char *body;
} pipe_stage;
typedef struct {
	char *name;
	char *input_text;
	pipe_part *header_inputs; size_t header_input_count;
	pipe_part *header_outputs; size_t header_output_count;
	pipe_name *names; size_t name_count;
	pipe_stage *stages; size_t stage_count;
	size_t source_start, source_end;
	char *input_buffer, *output_buffer, *reset_marker, *reset_guard;
	size_t assignment_offset, render_offset;
	int module_has_reset;
} inline_pipeline;

struct pigen_inline_pipelines { inline_pipeline *items; size_t count; };

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

static pipe_name *find_name(inline_pipeline *pipe, const char *name)
{
	for (size_t i = 0; i < pipe->name_count; i++) if (!strcmp(pipe->names[i].name, name)) return &pipe->names[i];
	return NULL;
}

static int type_starts(inline_pipeline *pipe, const char *text)
{
	size_t length = strlen(text);
	/* In a packed pipeline list, two adjacent identifiers are declaration
	 * syntax unless the first is an already-known value. This permits ordinary
	 * typedef names such as `sample value`, not only the `_t` convention. */
	if (find_name(pipe, text)) return 0;
	return !strcmp(text, "logic") || !strcmp(text, "wire") || !strcmp(text, "reg") ||
		(length > 2 && !strcmp(text + length - 2, "_t")) || length != 0;
}

static void declare_name(inline_pipeline *pipe, const char *name, const char *type)
{
	pipe_name *known = find_name(pipe, name);
	if (known)
	{
		if (strcmp(known->type, type)) pigen_fail("incompatible inline pipeline type declaration");
		return;
	}
	pipe->names = pigen_resize(pipe->names, (pipe->name_count + 1) * sizeof(*pipe->names));
	pipe->names[pipe->name_count++] = (pipe_name){pigen_copy_range(name, strlen(name)), pigen_copy_range(type, strlen(type))};
}

static size_t matching(parser *p, size_t at, const char *open, const char *close)
{
	int depth = 0;
	for (; at < p->tokens.count; at++)
	{
		if (tok(p, at, open)) depth++;
		else if (tok(p, at, close) && --depth == 0) return at;
	}
	pigen_fail("unterminated inline pipeline delimiter");
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

static void parse_part(parser *p, inline_pipeline *pipe, size_t first, size_t last,
	pipe_part **parts, size_t *count, int allow_unknown)
{
	char *text;
	char *first_word;
	char *last_word;
	char *type;
	if (first == last) fail_at(p, first, "empty packed pipeline item");
	text = range_copy(p, first, last);
	first_word = range_copy(p, first, first + 1);
	last_word = p->tokens.items[last - 1].kind == PIGEN_TOKEN_IDENTIFIER ? range_copy(p, last - 1, last) : NULL;
	if (last_word && last - first >= 2 && type_starts(pipe, first_word))
	{
		type = range_copy(p, first, last - 1);
		declare_name(pipe, last_word, type);
		/* A declaration in a packed list contributes its declared wire, not
		 * the declaration syntax itself, to the packet expression. */
		add_part(parts, count, last_word, last_word, type, 0);
		free(type);
	}
	else if (last - first == 1 && last_word)
	{
		pipe_name *known = find_name(pipe, last_word);
		if (!known && !allow_unknown) pigen_fail("inline pipeline name requires a type declaration");
		add_part(parts, count, text, last_word, known ? known->type : NULL, 0);
	}
	else
		add_part(parts, count, text, NULL, NULL, 1);
	free(text); free(first_word); free(last_word);
}

static void parse_packed(parser *p, inline_pipeline *pipe, pipe_part **parts, size_t *count,
	int allow_unknown)
{
	size_t first;
	size_t at;
	int parens = 0, brackets = 0, braces = 0;
	expect(p, "{", "expected `{` for packed pipeline expression");
	first = p->at;
	for (at = p->at; at < p->tokens.count; at++)
	{
		if (tok(p, at, "(") ) parens++;
		else if (tok(p, at, ")")) parens--;
		else if (tok(p, at, "[")) brackets++;
		else if (tok(p, at, "]")) brackets--;
		if (tok(p, at, "{")) braces++;
		else if (tok(p, at, "}"))
		{
			if (!parens && !brackets && !braces)
			{
				if (first != at) parse_part(p, pipe, first, at, parts, count, allow_unknown);
				else if (!*count) fail_at(p, at, "empty packed pipeline expression");
				p->at = at + 1; return;
			}
			braces--;
		}
		else if (!parens && !brackets && !braces && tok(p, at, ","))
		{
			parse_part(p, pipe, first, at, parts, count, allow_unknown); first = at + 1;
		}
	}
	pigen_fail("unterminated packed pipeline expression");
}

static void parse_declaration(parser *p, inline_pipeline *pipe)
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
					char *name = range_copy(p, at - 1, at); declare_name(pipe, name, type); free(name);
				}
			}
			else
			{
				if (at - item != 1 || p->tokens.items[item].kind != PIGEN_TOKEN_IDENTIFIER)
					fail_at(p, item, "pipeline declaration continuation requires only a name");
				{
					char *name = range_copy(p, item, at); declare_name(pipe, name, type); free(name);
				}
			}
			item = at + 1;
		}
	}
	free(type); p->at = semi + 1;
}

static pipe_stage parse_stage(parser *p, inline_pipeline *pipe)
{
	pipe_stage stage = {0};
	size_t body_start, body_end, close;
	expect(p, "stage", "expected `stage`");
	if (!tok(p, p->at, "{")) stage.label = identifier(p, "expected stage name or `{`");
	parse_packed(p, pipe, &stage.inputs, &stage.input_count, 0);
	expect(p, "yields", "expected `yields`");
	parse_packed(p, pipe, &stage.outputs, &stage.output_count, 0);
	if (tok(p, p->at, ";")) { p->at++; return stage; }
	expect(p, "begin", "expected `;` or `begin` after yielded packing");
	body_start = p->tokens.items[p->at - 1].span.end;
	close = matching(p, p->at - 1, "begin", "end");
	body_end = p->tokens.items[close].span.start;
	stage.body = pigen_copy_range(p->source + body_start, body_end - body_start);
	p->at = close + 1;
	return stage;
}

static void resolve_pipeline(inline_pipeline *pipe)
{
	if (!pipe->stage_count) pigen_fail("inline pipeline requires at least one stage");
	for (size_t i = 0; i < pipe->header_output_count; i++)
		if (!pipe->header_outputs[i].type && pipe->header_outputs[i].name)
		{
			pipe_name *known = find_name(pipe, pipe->header_outputs[i].name);
			if (known) pipe->header_outputs[i].type = pigen_copy_range(known->type, strlen(known->type));
		}
	for (size_t stage = 0; stage < pipe->stage_count; stage++)
	{
		pipe_stage *current = &pipe->stages[stage];
		pipe_part *next = stage + 1 < pipe->stage_count ? pipe->stages[stage + 1].inputs : pipe->header_outputs;
		size_t next_count = stage + 1 < pipe->stage_count ? pipe->stages[stage + 1].input_count : pipe->header_output_count;
		for (size_t i = 0; i < current->output_count; i++)
		{
			if (current->outputs[i].expression)
			{
				if (current->output_count != next_count || !next[i].type)
					pigen_fail("repartitioned pipeline expression output requires an explicit typed name");
				current->outputs[i].type = pigen_copy_range(next[i].type, strlen(next[i].type));
			}
			if (!current->outputs[i].type) pigen_fail("untyped inline pipeline output");
		}
	}
	for (size_t i = 0; i < pipe->header_output_count; i++)
		if (!pipe->header_outputs[i].type) pigen_fail("pipeline output requires a type declaration");
}

static inline_pipeline parse_pipeline(parser *p)
{
	inline_pipeline pipe = {0};
	size_t header_start;
	expect(p, "pipeline", "expected `pipeline`");
	pipe.source_start = p->tokens.items[p->at - 1].span.start;
	pipe.name = identifier(p, "expected inline pipeline name");
	header_start = p->at;
	/* Header inputs are expressions and therefore need no prior type lookup. */
	{
		size_t close = matching(p, p->at, "{", "}");
		pipe.input_text = range_copy(p, header_start, close + 1);
		p->at = close + 1;
	}
	expect(p, "yields", "expected `yields` after inline pipeline input packing");
	parse_packed(p, &pipe, &pipe.header_outputs, &pipe.header_output_count, 1);
	expect(p, "begin", "expected `begin` after inline pipeline header");
	while (!tok(p, p->at, "endpipeline"))
	{
		if (cur(p)->kind == PIGEN_TOKEN_EOF) fail_at(p, p->at, "unterminated inline pipeline");
		if (tok(p, p->at, "stage"))
		{
			pipe.stages = pigen_resize(pipe.stages, (pipe.stage_count + 1) * sizeof(*pipe.stages));
			pipe.stages[pipe.stage_count++] = parse_stage(p, &pipe);
		}
		else
			parse_declaration(p, &pipe);
	}
	pipe.source_end = p->tokens.items[p->at].span.end;
	p->at++;
	resolve_pipeline(&pipe);
	pipe.input_buffer = pigen_copy_range("__pigen_pipe_", 13);
	pipe.input_buffer = pigen_resize(pipe.input_buffer, strlen(pipe.input_buffer) + strlen(pipe.name) + 4);
	strcat(pipe.input_buffer, pipe.name); strcat(pipe.input_buffer, "_in");
	pipe.output_buffer = pigen_copy_range("__pigen_pipe_", 13);
	pipe.output_buffer = pigen_resize(pipe.output_buffer, strlen(pipe.output_buffer) + strlen(pipe.name) + 5);
	strcat(pipe.output_buffer, pipe.name); strcat(pipe.output_buffer, "_out");
	pipe.reset_marker = pigen_copy_range("__pigen_pipe_reset_", 19);
	pipe.reset_marker = pigen_resize(pipe.reset_marker, strlen(pipe.reset_marker) + strlen(pipe.name) + 1);
	strcat(pipe.reset_marker, pipe.name);
	return pipe;
}

static void append_projection(pigen_string *out, const inline_pipeline *pipe, size_t member)
{
	size_t offset = pipe->header_output_count - member - 1;
	pigen_append(out, pipe->output_buffer);
	pigen_append(out, "[");
	if (!offset) pigen_append(out, "0");
	else
	{
		for (size_t i = member + 1; i < pipe->header_output_count; i++)
		{
			if (i != member + 1) pigen_append(out, " + ");
			pigen_append(out, "$bits("); pigen_append(out, pipe->header_outputs[i].type); pigen_append(out, ")");
		}
	}
	pigen_append(out, " +: $bits("); pigen_append(out, pipe->header_outputs[member].type); pigen_append(out, ")]");
}

static inline_pipeline *pipeline_named(pigen_inline_pipelines *pipes, const char *name)
{
	for (size_t i = 0; i < pipes->count; i++) if (!strcmp(pipes->items[i].name, name)) return &pipes->items[i];
	return NULL;
}

static int output_projection(pigen_inline_pipelines *pipes, const char *name,
	inline_pipeline **owner, size_t *member)
{
	for (size_t i = 0; i < pipes->count; i++)
		for (size_t j = 0; j < pipes->items[i].header_output_count; j++)
			if (pipes->items[i].header_outputs[j].name && !strcmp(pipes->items[i].header_outputs[j].name, name))
			{
				*owner = &pipes->items[i]; *member = j; return 1;
			}
	return 0;
}

static char *rewrite_names(const char *source, size_t length, pigen_inline_pipelines *pipes,
	const pigen_procedural_ast *ast, size_t *output_length)
{
	pigen_tokens tokens = {0};
	pigen_string out = {0};
	size_t copied = 0;
	pigen_lex_source(source, length, &tokens);
	for (size_t at = 0; at < tokens.count && tokens.items[at].kind != PIGEN_TOKEN_EOF; at++)
	{
		inline_pipeline *owner;
		size_t member;
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
			inline_pipeline *pipe = pipeline_named(pipes, name);
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
		if (output_projection(pipes, word, &owner, &member) &&
			tokens.items[at].span.start > owner->assignment_offset)
		{
			pigen_append_range(&out, source + copied, tokens.items[at].span.start - copied);
			append_projection(&out, owner, member);
			copied = tokens.items[at].span.end;
		}
		free(word);
	}
	pigen_append_range(&out, source + copied, length - copied);
	pigen_free_tokens(&tokens);
	*output_length = out.length;
	return out.data ? out.data : pigen_copy_range(source, length);
}

char *pigen_prepare_inline_pipelines(const char *source, size_t length,
	size_t *output_length, pigen_inline_pipelines **pipelines)
{
	parser p = {source, {0}, 0};
	pigen_inline_pipelines *result = calloc(1, sizeof(*result));
	pigen_string out = {0};
	size_t copied = 0;
	if (!result) pigen_fail("out of memory");
	pigen_lex_source(source, length, &p.tokens);
	while (p.tokens.items[p.at].kind != PIGEN_TOKEN_EOF)
	{
		/* Top-level pipeline blocks and ordinary identifiers named `pipeline`
		 * remain existing SystemVerilog/Pigen syntax.  The inline form is
		 * unambiguously `pipeline name { ... } yields ...`. */
		if (!tok(&p, p.at, "pipeline") || p.at + 2 >= p.tokens.count ||
			p.tokens.items[p.at + 1].kind != PIGEN_TOKEN_IDENTIFIER || !tok(&p, p.at + 2, "{"))
		{ p.at++; continue; }
		{
			inline_pipeline pipe = parse_pipeline(&p);
			for (size_t i = 0; i < result->count; i++)
				if (!strcmp(result->items[i].name, pipe.name)) pigen_fail("duplicate inline pipeline name");
			pigen_append_range(&out, source + copied, pipe.source_start - copied);
			pigen_append(&out, pipe.input_buffer); pigen_append(&out, " <= ");
			pigen_append(&out, pipe.input_text); pigen_append(&out, ";");
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
			char *at = strstr(out.data, result->items[i].input_buffer);
			if (!at) pigen_fail("internal inline pipeline placement failure");
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
			char *at = strstr(prepared, result->items[i].input_buffer);
			if (!at) pigen_fail("internal inline pipeline placement failure");
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
				if (render == (size_t)-1) pigen_fail("inline pipeline must appear inside a clocked always block");
				result->items[i].render_offset = render;
				pigen_append_range(&marked, prepared + marked_copied, render - marked_copied);
				pigen_append(&marked, "/*__PIGEN_INLINE_RTL_"); pigen_append(&marked, result->items[i].name); pigen_append(&marked, "__*/\n");
				marked_copied = render;
			}
			pigen_append_range(&marked, prepared + marked_copied, *output_length - marked_copied);
			pigen_free_tokens(&prepared_tokens);
			free(prepared);
			prepared = marked.data;
			*output_length = marked.length;
			for (size_t i = 0; i < result->count; i++)
			{
				char *at = strstr(prepared, result->items[i].input_buffer);
				if (!at) pigen_fail("internal inline pipeline placement failure");
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
		pigen_append(out, parts[i].name); pigen_append(out, ";\n");
	}
	for (size_t i = 0; i < count; i++)
	{
		size_t offset_count = count - i - 1;
		pigen_append(out, "\t\t");
		pigen_append(out, parts[i].name); pigen_append(out, " = "); pigen_append(out, packet); pigen_append(out, "[");
		if (!offset_count) pigen_append(out, "0");
		else for (size_t j = i + 1; j < count; j++)
		{
			if (j != i + 1) pigen_append(out, " + ");
			pigen_append(out, "$bits("); pigen_append(out, parts[j].type); pigen_append(out, ")");
		}
		pigen_append(out, " +: $bits("); pigen_append(out, parts[i].type); pigen_append(out, ")];\n");
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

static int header_has_reset(const char *source, size_t module_start, size_t header_end)
{
	const char *at = source + module_start;
	const char *end = source + header_end;
	for (; at + 5 <= end; at++)
		if (!memcmp(at, "reset", 5) && (at == source + module_start || !pigen_is_identifier_char((unsigned char)at[-1])) &&
			(at + 5 == end || !pigen_is_identifier_char((unsigned char)at[5]))) return 1;
	return 0;
}

static void append_pipeline_rtl(pigen_string *out, inline_pipeline *pipe,
	const char *source, const pigen_procedural_ast *ast)
{
	const char *guard = pigen_procedural_guard_for(ast, source + pipe->assignment_offset);
	const char *domain = pigen_procedural_domain_for(ast, source + pipe->assignment_offset);
	const char *reset = pipe->reset_guard ? pipe->reset_guard :
		(pipe->module_has_reset ? "reset" : "1'b0");

	if (!pigen_procedural_statement_for(ast, source + pipe->assignment_offset))
		pigen_fail("inline pipeline must appear inside a clocked always block");
	if (!domain || !*domain) pigen_fail("inline pipeline requires a clock event");
	pigen_append(out, "\n\t/* Pigen inline pipeline: "); pigen_append(out, pipe->name); pigen_append(out, " */\n");
	pigen_append(out, "\t/*__PIGEN_INLINE_BEGIN__*/\n");
	pigen_append(out, "\t(* pigen_internal = 1 *) logic "); pigen_append(out, pipe->reset_marker); pigen_append(out, ";\n");
	for (size_t i = 0; i < pipe->stage_count; i++)
	{
		pipe_stage *stage = &pipe->stages[i];
	pigen_append(out, "\t(* pigen_internal = 1 *) logic "); pigen_append_format(out, "%s__s%zu_valid, %s__s%zu_ready;\n", pipe->name, i, pipe->name, i);
		pigen_append(out, "\t(* pigen_internal = 1 *) logic [("); append_width(out, stage->outputs, stage->output_count);
		pigen_append_format(out, ")-1:0] %s__s%zu_packet, %s__s%zu_comb;\n", pipe->name, i, pipe->name, i);
		if (i + 1 < pipe->stage_count)
		{
			pigen_append(out, "\tinitial if ($bits("); pigen_append_format(out, "%s__s%zu_packet", pipe->name, i);
			pigen_append(out, ") != ("); append_width(out, pipe->stages[i + 1].inputs, pipe->stages[i + 1].input_count);
			pigen_append(out, ")) $fatal(1, \"Pigen inline pipeline stage packing width mismatch\");\n");
		}
		else
		{
			pigen_append(out, "\tinitial if ($bits("); pigen_append_format(out, "%s__s%zu_packet", pipe->name, i);
			pigen_append(out, ") != ("); append_width(out, pipe->header_outputs, pipe->header_output_count);
			pigen_append(out, ")) $fatal(1, \"Pigen inline pipeline output packing width mismatch\");\n");
		}
	}
	pigen_append(out, "\tassign "); pigen_append(out, pipe->input_buffer); pigen_append(out, "__pigen_out_ready = ");
	pigen_append_format(out, "%s__s0_ready;\n", pipe->name);
	pigen_append(out, "\tassign "); pigen_append(out, pipe->output_buffer); pigen_append(out, "__pigen_in_valid = (");
	pigen_append(out, guard && *guard ? guard : "1'b1"); pigen_append(out, ") && ");
	pigen_append_format(out, "%s__s%zu_valid;\n", pipe->name, pipe->stage_count - 1);
	pigen_append(out, "\tassign "); pigen_append(out, pipe->output_buffer); pigen_append(out, "__pigen_packet_in = ");
	pigen_append_format(out, "%s__s%zu_packet;\n", pipe->name, pipe->stage_count - 1);
	for (size_t i = pipe->stage_count; i-- > 0; )
	{
		pigen_append(out, "\tassign "); pigen_append_format(out, "%s__s%zu_ready = (", pipe->name, i);
		pigen_append(out, guard && *guard ? guard : "1'b1"); pigen_append(out, ") && (~");
		pigen_append_format(out, "%s__s%zu_valid || ", pipe->name, i);
		if (i + 1 == pipe->stage_count) { pigen_append(out, pipe->output_buffer); pigen_append(out, "__pigen_in_ready"); }
		else pigen_append_format(out, "%s__s%zu_ready", pipe->name, i + 1);
		pigen_append(out, ");\n");
	}
	for (size_t i = 0; i < pipe->stage_count; i++)
	{
		pipe_stage *stage = &pipe->stages[i];
		const char *packet = i ? NULL : pipe->input_buffer;
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
		else append_local_unpack(out, packet, stage->inputs, stage->input_count);
		/* A stage body has its own lexical scope. Besides matching the language
		 * model, this permits ordinary local declarations after the generated
		 * unpack assignments without violating SV's declaration-before-statement
		 * rule in the enclosing always_comb block. */
		if (stage->body) { pigen_append(out, "\t\tbegin\n"); pigen_append(out, stage->body); pigen_append(out, "\n\t\t"); }
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
		pigen_append_format(out, "%s__s%zu_ready) begin\n", pipe->name, i);
		if (!i) pigen_append(out, "\t\t\t\tif ("); else pigen_append(out, "\t\t\t\tif (");
		if (!i) pigen_append(out, pipe->input_buffer), pigen_append(out, "__pigen_valid");
		else pigen_append_format(out, "%s__s%zu_valid", pipe->name, i - 1);
		pigen_append_format(out, ") begin %s__s%zu_packet <= %s__s%zu_comb; %s__s%zu_valid <= 1'b1; end\n", pipe->name, i, pipe->name, i, pipe->name, i);
		pigen_append_format(out, "\t\t\t\telse %s__s%zu_valid <= 1'b0;\n", pipe->name, i);
		pigen_append(out, "\t\t\tend\n\t\tend\n\tend\n");
	}
	pigen_append(out, "\t/*__PIGEN_INLINE_END__*/\n");
}

static void append_pipeline_declarations(pigen_string *out, const inline_pipeline *pipe)
{
	pigen_append(out, "\n\t/* Pigen inline pipeline endpoints: "); pigen_append(out, pipe->name); pigen_append(out, " */\n");
	pigen_append(out, "\tbuf [("); append_width(out, pipe->stages[0].inputs, pipe->stages[0].input_count);
	pigen_append(out, ")-1:0] "); pigen_append(out, pipe->input_buffer); pigen_append(out, ";\n");
	pigen_append(out, "\tbuf [("); append_width(out, pipe->header_outputs, pipe->header_output_count);
	pigen_append(out, ")-1:0] "); pigen_append(out, pipe->output_buffer); pigen_append(out, ";\n");
}

char *pigen_finish_inline_pipelines(const char *source, size_t length,
	const pigen_procedural_ast *ast, pigen_inline_pipelines *pipelines,
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
			snprintf(marker, sizeof(marker), "/*__PIGEN_INLINE_RTL_%s__*/", pipelines->items[i].name);
			found = strstr(scan, marker);
			if (!found) pigen_fail("internal inline pipeline render placement failure");
			pigen_append_range(&expanded, scan, (size_t)(found - scan));
			append_pipeline_declarations(&expanded, &pipelines->items[i]);
			append_pipeline_rtl(&expanded, &pipelines->items[i], source, ast);
			scan = found + strlen(marker);
		}
		pigen_append(&expanded, scan);
		free(out.data);
		*output_length = expanded.length;
		return expanded.data ? expanded.data : pigen_copy_range(source, length);
	}
}

void pigen_free_inline_pipelines(pigen_inline_pipelines *pipes)
{
	if (!pipes) return;
	for (size_t i = 0; i < pipes->count; i++)
	{
		inline_pipeline *p = &pipes->items[i];
		free(p->name); free(p->input_text); free(p->input_buffer); free(p->output_buffer); free(p->reset_marker); free(p->reset_guard);
		for (size_t j = 0; j < p->name_count; j++) { free(p->names[j].name); free(p->names[j].type); }
		free(p->names);
		for (size_t j = 0; j < p->header_output_count; j++) { free(p->header_outputs[j].text); free(p->header_outputs[j].name); free(p->header_outputs[j].type); }
		free(p->header_outputs);
		for (size_t j = 0; j < p->stage_count; j++)
		{
			pipe_stage *s = &p->stages[j]; free(s->label); free(s->body);
			for (size_t k = 0; k < s->input_count; k++) { free(s->inputs[k].text); free(s->inputs[k].name); free(s->inputs[k].type); }
			for (size_t k = 0; k < s->output_count; k++) { free(s->outputs[k].text); free(s->outputs[k].name); free(s->outputs[k].type); }
			free(s->inputs); free(s->outputs);
		}
		free(p->stages);
	}
	free(pipes->items); free(pipes);
}
