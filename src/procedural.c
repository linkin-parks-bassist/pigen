/* Parsed procedural-control layer: no transport or emission policy lives here. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/procedural.h"
#include "pigen/util.h"

static void *resize(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (!ptr)
		pigen_fail("out of memory");
	return ptr;
}

static char *copy_range(const char *src, size_t length)
{
	char *copy = resize(NULL, length + 1);
	memcpy(copy, src, length);
	copy[length] = 0;
	return copy;
}

static char *normalized_event_domain(const char *start, const char *end)
{
	pigen_string result = {0};
	int pending_space = 0;
	for (; start < end; start++)
	{
		if (isspace((unsigned char)*start)) { pending_space = result.length != 0; continue; }
		if (pending_space) { pigen_append(&result, " "); pending_space = 0; }
		pigen_append_range(&result, start, 1);
	}
	return result.data ? result.data : copy_range("", 0);
}

static int identifier_char(int c)
{
	return isalnum((unsigned char)c) || c == '_' || c == '$';
}

static const char *skip_spaces(const char *cursor, const char *end)
{
	while (cursor < end && isspace((unsigned char)*cursor))
		cursor++;
	return cursor;
}

static const char *trim_end(const char *start, const char *end)
{
	while (end > start && isspace((unsigned char)end[-1]))
		end--;
	return end;
}

static const char *skip_opaque(const char *cursor, const char *end)
{
	if (cursor + 1 < end && cursor[0] == '/' && cursor[1] == '/')
	{
		for (cursor += 2; cursor < end && *cursor != '\n'; cursor++)
			;
		return cursor;
	}
	if (cursor + 1 < end && cursor[0] == '/' && cursor[1] == '*')
	{
		for (cursor += 2; cursor + 1 < end && !(cursor[0] == '*' && cursor[1] == '/'); cursor++)
			;
		if (cursor + 1 == end)
			pigen_fail("unterminated block comment");
		return cursor + 2;
	}
	if (cursor < end && *cursor == '"')
	{
		for (cursor++; cursor < end; cursor++)
		{
			if (*cursor == '\\' && cursor + 1 < end)
				cursor++;
			else if (*cursor == '"')
				return cursor + 1;
		}
		pigen_fail("unterminated string literal");
	}
	return NULL;
}

static const char *skip_trivia(const char *cursor, const char *end)
{
	const char *opaque;
	for (;;)
	{
		cursor = skip_spaces(cursor, end);
		opaque = skip_opaque(cursor, end);
		if (!opaque)
			return cursor;
		cursor = opaque;
	}
}

static int starts_word(const char *cursor, const char *end, const char *word)
{
	size_t length = strlen(word);
	return (size_t)(end - cursor) >= length && !memcmp(cursor, word, length) &&
		(cursor + length == end || !identifier_char(cursor[length]));
}

static char *combine_guards(const char *parent, const char *condition)
{
	size_t parent_length = parent ? strlen(parent) : 0;
	size_t condition_length = strlen(condition);
	char *result = resize(NULL, parent_length + condition_length + 12);

	if (parent_length)
		sprintf(result, "(%s) && (%s)", parent, condition);
	else
		sprintf(result, "(%s)", condition);
	return result;
}

static pigen_span span_for(const char *source, const char *start, const char *end)
{
	pigen_span span = { (size_t)(start - source), (size_t)(end - source), 1, 1 };
	const char *cursor;

	for (cursor = source; cursor < start; cursor++)
	{
		if (*cursor == '\n')
		{
			span.line++;
			span.column = 1;
		}
		else
			span.column++;
	}
	return span;
}

static void add_statement(pigen_procedural_ast *ast, const char *source, const char *start, const char *end, const char *guard, const char *domain)
{
	if (ast->count == ast->capacity)
	{
		ast->capacity = ast->capacity ? ast->capacity * 2 : 16;
		ast->items = resize(ast->items, ast->capacity * sizeof(*ast->items));
	}
	ast->items[ast->count++] = (pigen_procedural_statement){ span_for(source, start, end), start, end, copy_range(guard ? guard : "", strlen(guard ? guard : "")), copy_range(domain ? domain : "", strlen(domain ? domain : "")) };
}

static int conditional_transfer_parts(const char *start, const char *end)
{
	const char *cursor = skip_spaces(start, end);
	int depth = 0;
	for (; cursor + 1 < end; cursor++)
	{
		const char *opaque = skip_opaque(cursor, end);
		if (opaque) { cursor = opaque - 1; continue; }
		if (*cursor == '(' || *cursor == '[' || *cursor == '{') depth++;
		else if (*cursor == ')' || *cursor == ']' || *cursor == '}') depth--;
		else if (!depth && cursor[0] == '<' && cursor[1] == '=') return 1;
	}
	return 0;
}

static void add_conditional_transfer(pigen_procedural_ast *ast, const char *start, const char *end,
				     const char *guard, const char *domain)
{
	pigen_conditional_transfer *transfer;
	if (ast->conditional_transfer_count == ast->conditional_transfer_capacity)
	{
		ast->conditional_transfer_capacity = ast->conditional_transfer_capacity ? ast->conditional_transfer_capacity * 2 : 8;
		ast->conditional_transfers = resize(ast->conditional_transfers,
			ast->conditional_transfer_capacity * sizeof(*ast->conditional_transfers));
	}
	transfer = &ast->conditional_transfers[ast->conditional_transfer_count++];
	transfer->start = start;
	transfer->end = end;
	transfer->guard = copy_range(guard ? guard : "", strlen(guard ? guard : ""));
	transfer->domain = copy_range(domain ? domain : "", strlen(domain ? domain : ""));
}

static const char *parse_statement(const char *source, const char *cursor, const char *end, const char *guard, const char *domain, pigen_procedural_ast *ast);
static const char *parse_parenthesized(const char *cursor, const char *end, const char **contents, const char **close);

static char *append_guard_term(const char *parent, const char *term)
{
	return combine_guards(parent, term);
}

static const char *find_case_colon(const char *cursor, const char *end)
{
	int depth = 0;
	for (; cursor < end; cursor++)
	{
		const char *opaque = skip_opaque(cursor, end);
		if (opaque) { cursor = opaque - 1; continue; }
		if (*cursor == '(' || *cursor == '[' || *cursor == '{') depth++;
		else if (*cursor == ')' || *cursor == ']' || *cursor == '}') depth--;
		else if (*cursor == ':' && depth == 0) return cursor;
	}
	pigen_fail("unterminated case item");
	return end;
}

static char *case_item_condition(const char *selector, const char *labels, const char *labels_end, int is_default, const char *match_operator)
{
	pigen_string result = {0};
	const char *item = labels;
	const char *cursor;
	int depth = 0;
	int emitted = 0;
	if (is_default) return copy_range("1'b1", 4);
	for (cursor = labels; ; cursor++)
	{
		int split = cursor == labels_end || (*cursor == ',' && depth == 0);
		if (split)
		{
			const char *start = skip_spaces(item, cursor);
			const char *finish = trim_end(start, cursor);
			if (start == finish) pigen_fail("empty case item");
			if (emitted) pigen_append(&result, " || ");
			pigen_append(&result, "((");
			pigen_append(&result, selector);
			pigen_append(&result, match_operator);
			pigen_append(&result, " (");
			pigen_append_range(&result, start, (size_t)(finish - start));
			pigen_append(&result, "))");
			emitted = 1;
			item = cursor + 1;
		}
		if (cursor == labels_end) break;
		if (*cursor == '(' || *cursor == '[' || *cursor == '{') depth++;
		else if (*cursor == ')' || *cursor == ']' || *cursor == '}') depth--;
	}
	return result.data;
}

static char *case_branch_guard(const char *parent, char **previous, size_t previous_count, const char *condition)
{
	pigen_string terms = {0};
	size_t i;
	for (i = 0; i < previous_count; i++)
	{
		if (i) pigen_append(&terms, " && ");
		pigen_append(&terms, "!(");
		pigen_append(&terms, previous[i]);
		pigen_append(&terms, ")");
	}
	if (previous_count) pigen_append(&terms, " && ");
	pigen_append(&terms, "(");
	pigen_append(&terms, condition);
	pigen_append(&terms, ")");
	{
		char *result = append_guard_term(parent, terms.data);
		free(terms.data);
		return result;
	}
}

static const char *parse_case_statement(const char *source, const char *cursor, const char *end,
					const char *guard, const char *domain, pigen_procedural_ast *ast, const char *keyword)
{
	const char *contents;
	const char *close;
	char *selector;
	char **previous = NULL;
	size_t previous_count = 0;
	size_t previous_capacity = 0;
	cursor = parse_parenthesized(skip_trivia(cursor + strlen(keyword), end), end, &contents, &close);
	selector = copy_range(skip_spaces(contents, close), (size_t)(trim_end(contents, close) - skip_spaces(contents, close)));
	for (;;)
	{
		const char *labels;
		const char *colon;
		char *condition;
		char *branch_guard;
		int is_default;
		cursor = skip_trivia(cursor, end);
		if (starts_word(cursor, end, "endcase")) { cursor += 7; break; }
		if (cursor == end) pigen_fail("unterminated case statement");
		labels = cursor;
		colon = find_case_colon(cursor, end);
		is_default = starts_word(labels, colon, "default") && trim_end(labels + 7, colon) == labels + 7;
		condition = case_item_condition(selector, labels, colon, is_default,
			!strcmp(keyword, "case") ? " ) ===" : " ) ==?");
		branch_guard = case_branch_guard(guard, previous, previous_count, condition);
		cursor = parse_statement(source, colon + 1, end, branch_guard, domain, ast);
		free(branch_guard);
		if (previous_count == previous_capacity)
		{
			previous_capacity = previous_capacity ? previous_capacity * 2 : 4;
			previous = resize(previous, previous_capacity * sizeof(*previous));
		}
		previous[previous_count++] = condition;
	}
	for (size_t i = 0; i < previous_count; i++) free(previous[i]);
	free(previous);
	free(selector);
	return cursor;
}

static const char *parse_parenthesized(const char *cursor, const char *end, const char **contents, const char **close)
{
	int depth = 0;
	if (cursor == end || *cursor != '(')
		pigen_fail("expected parenthesized procedural expression");
	*contents = ++cursor;
	for (; cursor < end; cursor++)
	{
		const char *opaque = skip_opaque(cursor, end);
		if (opaque) { cursor = opaque - 1; continue; }
		if (*cursor == '(') depth++;
		else if (*cursor == ')' && depth-- == 0) { *close = cursor; return cursor + 1; }
	}
	pigen_fail("unterminated procedural expression");
	return end;
}

static const char *parse_statement(const char *source, const char *cursor, const char *end, const char *guard, const char *domain, pigen_procedural_ast *ast)
{
	const char *start;
	const char *contents;
	const char *close;
	cursor = skip_trivia(cursor, end);
	start = cursor;
	if (starts_word(cursor, end, "begin"))
	{
		for (cursor += 5; ;)
		{
			cursor = skip_trivia(cursor, end);
			if (starts_word(cursor, end, "end")) return cursor + 3;
			if (cursor == end) pigen_fail("unterminated begin/end procedural block");
			cursor = parse_statement(source, cursor, end, guard, domain, ast);
		}
	}
	if (starts_word(cursor, end, "if"))
	{
		char *condition;
		char *then_guard;
		char *else_guard;
		cursor = parse_parenthesized(skip_trivia(cursor + 2, end), end, &contents, &close);
		condition = copy_range(skip_spaces(contents, close), (size_t)(trim_end(contents, close) - skip_spaces(contents, close)));
		if (conditional_transfer_parts(contents, close))
			add_conditional_transfer(ast, contents, close, guard, domain);
		then_guard = combine_guards(guard, condition);
		cursor = parse_statement(source, cursor, end, then_guard, domain, ast);
		free(then_guard);
		cursor = skip_trivia(cursor, end);
		if (!starts_word(cursor, end, "else")) { free(condition); return cursor; }
		{
			char *inverted = resize(NULL, strlen(condition) + 4);
			sprintf(inverted, "!(%s)", condition);
			else_guard = combine_guards(guard, inverted);
			free(inverted);
		}
		free(condition);
		cursor = parse_statement(source, cursor + 4, end, else_guard, domain, ast);
		free(else_guard);
		return cursor;
	}
	if (starts_word(cursor, end, "case"))
		return parse_case_statement(source, cursor, end, guard, domain, ast, "case");
	if (starts_word(cursor, end, "casez"))
		return parse_case_statement(source, cursor, end, guard, domain, ast, "casez");
	if (starts_word(cursor, end, "casex"))
		return parse_case_statement(source, cursor, end, guard, domain, ast, "casex");
	if (starts_word(cursor, end, "unique"))
	{
		cursor = skip_trivia(cursor + 6, end);
		if (starts_word(cursor, end, "case")) return parse_case_statement(source, cursor, end, guard, domain, ast, "case");
		if (starts_word(cursor, end, "casez")) return parse_case_statement(source, cursor, end, guard, domain, ast, "casez");
		if (starts_word(cursor, end, "casex")) return parse_case_statement(source, cursor, end, guard, domain, ast, "casex");
	}
	if (starts_word(cursor, end, "priority"))
	{
		cursor = skip_trivia(cursor + 8, end);
		if (starts_word(cursor, end, "case")) return parse_case_statement(source, cursor, end, guard, domain, ast, "case");
		if (starts_word(cursor, end, "casez")) return parse_case_statement(source, cursor, end, guard, domain, ast, "casez");
		if (starts_word(cursor, end, "casex")) return parse_case_statement(source, cursor, end, guard, domain, ast, "casex");
	}
	for (; cursor < end; cursor++)
	{
		const char *opaque = skip_opaque(cursor, end);
		if (opaque) { cursor = opaque - 1; continue; }
		if (*cursor == ';') { add_statement(ast, source, start, cursor, guard, domain); return cursor + 1; }
	}
	pigen_fail("unterminated procedural statement");
	return end;
}

void pigen_parse_procedural_ast(const char *source, const char *end, pigen_procedural_ast *ast)
{
	const char *cursor = source;
	while (cursor < end)
	{
		const char *opaque = skip_opaque(cursor, end);
		const char *contents;
		const char *close;
		size_t keyword_length;
		if (opaque) { cursor = opaque; continue; }
		if (starts_word(cursor, end, "always_ff")) keyword_length = 9;
		else if (starts_word(cursor, end, "always")) keyword_length = 6;
		else { cursor++; continue; }
		char *domain;
		cursor = skip_trivia(cursor + keyword_length, end);
		if (cursor == end || *cursor != '@')
		{
			if (keyword_length == 9) pigen_fail("always_ff requires an event control");
			continue;
		}
		cursor = skip_trivia(cursor + 1, end);
		/* An ordinary `always @*` remains opaque SystemVerilog. Pigen transport
		 * actions require an explicit parenthesized clock event. */
		if (cursor == end || *cursor != '(')
		{
			if (keyword_length == 9) pigen_fail("always_ff requires a parenthesized event control");
			continue;
		}
		cursor = parse_parenthesized(cursor, end, &contents, &close);
		domain = normalized_event_domain(skip_spaces(contents, close), trim_end(contents, close));
		cursor = parse_statement(source, cursor, end, "", domain, ast);
		free(domain);
	}
}

const char *pigen_procedural_guard_for(const pigen_procedural_ast *ast, const char *position)
{
	const pigen_procedural_statement *statement = pigen_procedural_statement_for(ast, position);
	return statement ? statement->guard : "";
}

const pigen_procedural_statement *pigen_procedural_statement_for(const pigen_procedural_ast *ast, const char *position)
{
	size_t i;
	for (i = 0; i < ast->count; i++)
		if (position >= ast->items[i].start && position <= ast->items[i].end)
			return &ast->items[i];
	return NULL;
}

const char *pigen_procedural_domain_for(const pigen_procedural_ast *ast, const char *position)
{
	const pigen_procedural_statement *statement = pigen_procedural_statement_for(ast, position);
	return statement ? statement->domain : "";
}

void pigen_free_procedural_ast(pigen_procedural_ast *ast)
{
	size_t i;
	for (i = 0; i < ast->count; i++) { free(ast->items[i].guard); free(ast->items[i].domain); }
	free(ast->items);
	for (i = 0; i < ast->conditional_transfer_count; i++) { free(ast->conditional_transfers[i].guard); free(ast->conditional_transfers[i].domain); }
	free(ast->conditional_transfers);
}
