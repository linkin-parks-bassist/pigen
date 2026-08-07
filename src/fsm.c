#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/fsm.h"
#include "pigen/model.h"
#include "pigen/util.h"

typedef struct { char *name; char *body; } fsm_state;
static const char *fsm_source_start;

static int word_at(const char *p, const char *end, const char *word)
{
	size_t n = strlen(word);
	return (size_t)(end - p) >= n && !memcmp(p, word, n) &&
		(p == fsm_source_start || !pigen_is_identifier_char((unsigned char)p[-1])) &&
		(p + n == end || !pigen_is_identifier_char((unsigned char)p[n]));
}

static const char *skip(const char *p, const char *end) { return pigen_skip_trivia(p, end); }

static const char *identifier_end(const char *p, const char *end)
{
	if (p == end || !pigen_is_identifier_char((unsigned char)*p)) pigen_fail("expected identifier in fsm");
	while (p < end && pigen_is_identifier_char((unsigned char)*p)) p++;
	return p;
}

static const char *matching_begin(const char *begin, const char *end)
{
	const char *p = begin;
	int depth = 0;
	for (; p < end; p++)
	{
		const char *opaque = pigen_skip_opaque(p, end);
		if (opaque) { p = opaque - 1; continue; }
		if (word_at(p, end, "begin")) { depth++; p += 4; continue; }
		if (word_at(p, end, "end"))
		{
			if (--depth == 0) return p;
			p += 2;
		}
	}
	pigen_fail("unterminated fsm block");
	return end;
}

static void validate_posedge_event(const char *open, const char *after_close)
{
	const char *p = skip(open + 1, after_close - 1);
	const char *edge_end;
	const char *clock_end;
	if (!word_at(p, after_close - 1, "posedge"))
		pigen_fail("fsm event control requires exactly one posedge clock edge");
	edge_end = skip(p + 7, after_close - 1);
	clock_end = identifier_end(edge_end, after_close - 1);
	if (skip(clock_end, after_close - 1) != after_close - 1)
		pigen_fail("fsm event control requires exactly one posedge clock edge");
}

static void append_state_ref(pigen_string *out, const char *fsm, const char *state)
{
	pigen_append(out, fsm); pigen_append(out, "__pigen_"); pigen_append(out, state);
}

static int state_exists(const fsm_state *states, size_t count, const char *name, size_t length)
{
	for (size_t i = 0; i < count; i++)
		if (strlen(states[i].name) == length && !memcmp(states[i].name, name, length)) return 1;
	return 0;
}

static void append_rewritten_body(pigen_string *out, const char *body, const char *body_end, const char *fsm,
				  const fsm_state *states, size_t state_count)
{
	const char *p = body;
	while (p < body_end)
	{
		const char *opaque = pigen_skip_opaque(p, body_end);
		if (opaque) { pigen_append_range(out, p, (size_t)(opaque - p)); p = opaque; continue; }
		if (word_at(p, body_end, "goto"))
		{
			const char *name = skip(p + 4, body_end);
			const char *name_end = identifier_end(name, body_end);
			const char *semi = skip(name_end, body_end);
			if (semi == body_end || *semi != ';') pigen_fail("goto requires a state name followed by semicolon");
			if (!state_exists(states, state_count, name, (size_t)(name_end - name)))
				pigen_fail("goto references an undeclared fsm state");
			pigen_append(out, fsm); pigen_append(out, "__pigen_state <= ");
			pigen_append(out, fsm); pigen_append(out, "__pigen_");
			pigen_append_range(out, name, (size_t)(name_end - name)); pigen_append(out, ";");
			p = semi + 1;
			continue;
		}
		pigen_append_range(out, p, 1); p++;
	}
}

static void lower_one(pigen_string *out, const char *start, const char *end, const char **after)
{
	const char *p = skip(start + 3, end);
	const char *fsm_end = identifier_end(p, end);
	char *fsm = pigen_copy_range(p, (size_t)(fsm_end - p));
	const char *event_open;
	const char *event_close;
	const char *reset_name;
	const char *reset_end;
	const char *initial;
	const char *initial_end;
	const char *outer_begin;
	const char *outer_end;
	fsm_state *states = NULL;
	size_t count = 0, capacity = 0, i;

	p = skip(fsm_end, end);
	if (p >= end || *p != '@') pigen_fail("fsm requires an event control");
	event_open = skip(p + 1, end);
	if (event_open >= end || *event_open != '(') pigen_fail("fsm requires an event control");
	event_close = event_open + 1;
	{ int depth = 1; while (event_close < end && depth) { if (*event_close == '(') depth++; else if (*event_close == ')') depth--; event_close++; } }
	if (event_close > end || event_close[-1] != ')') pigen_fail("unterminated fsm event control");
	validate_posedge_event(event_open, event_close);
	p = skip(event_close, end);
	if (!word_at(p, end, "reset")) pigen_fail("fsm requires reset (name)");
	p = skip(p + 5, end);
	if (p >= end || *p != '(') pigen_fail("fsm requires reset (name)");
	reset_name = skip(p + 1, end); reset_end = identifier_end(reset_name, end);
	p = skip(reset_end, end); if (p >= end || *p != ')') pigen_fail("fsm requires reset (name)");
	p = skip(p + 1, end);
	if (!word_at(p, end, "initial")) pigen_fail("fsm requires initial state");
	initial = skip(p + 7, end); initial_end = identifier_end(initial, end);
	p = skip(initial_end, end);
	if (!word_at(p, end, "begin")) pigen_fail("fsm requires begin/end body");
	outer_begin = p; outer_end = matching_begin(outer_begin, end);
	p = outer_begin + 5;
	while ((p = skip(p, outer_end)) < outer_end)
	{
		const char *name, *name_end, *body_begin, *body_end;
		if (!word_at(p, outer_end, "state")) pigen_fail("fsm body requires state blocks");
		name = skip(p + 5, outer_end); name_end = identifier_end(name, outer_end);
		for (i = 0; i < count; i++)
			if (strlen(states[i].name) == (size_t)(name_end - name) && !memcmp(states[i].name, name, (size_t)(name_end - name)))
				pigen_fail("fsm state declared more than once");
		p = skip(name_end, outer_end); if (p == outer_end || *p != ':') pigen_fail("state requires colon");
		body_begin = skip(p + 1, outer_end); if (!word_at(body_begin, outer_end, "begin")) pigen_fail("state requires begin/end body");
		body_end = matching_begin(body_begin, outer_end);
		if (count == capacity) { capacity = capacity ? capacity * 2 : 4; states = pigen_resize(states, capacity * sizeof(*states)); }
		states[count].name = pigen_copy_range(name, (size_t)(name_end - name));
		states[count].body = pigen_copy_range(body_begin + 5, (size_t)(body_end - (body_begin + 5)));
		count++; p = body_end + 3;
	}
	if (!count) pigen_fail("fsm requires at least one state");
	if (!state_exists(states, count, initial, (size_t)(initial_end - initial)))
		pigen_fail("fsm initial state is not declared");
	pigen_append(out, "\ttypedef enum logic [");
	{ size_t bits = 1, n = count - 1; while (n > 1) { bits++; n >>= 1; } char width[32]; snprintf(width, sizeof width, "%zu:0", bits - 1); pigen_append(out, width); }
	pigen_append(out, "] { ");
	for (i = 0; i < count; i++) { if (i) pigen_append(out, ", "); append_state_ref(out, fsm, states[i].name); }
	pigen_append(out, " } "); pigen_append(out, fsm); pigen_append(out, "__pigen_state_t;\n\t");
	pigen_append(out, fsm); pigen_append(out, "__pigen_state_t "); pigen_append(out, fsm); pigen_append(out, "__pigen_state;\n\n");
	pigen_append(out, "\talways_ff @"); pigen_append_range(out, event_open, (size_t)(event_close - event_open)); pigen_append(out, "\n\tbegin\n\t\tif ("); pigen_append_range(out, reset_name, (size_t)(reset_end - reset_name)); pigen_append(out, ")\n\t\t\t"); pigen_append(out, fsm); pigen_append(out, "__pigen_state <= ");
	{ char *init = pigen_copy_range(initial, (size_t)(initial_end - initial)); append_state_ref(out, fsm, init); free(init); }
	pigen_append(out, ";\n\t\telse\n\t\tbegin\n\t\t\tcase ("); pigen_append(out, fsm); pigen_append(out, "__pigen_state)\n");
	for (i = 0; i < count; i++)
	{
		pigen_append(out, "\t\t\t\t"); append_state_ref(out, fsm, states[i].name); pigen_append(out, ":\n\t\t\t\tbegin");
		append_rewritten_body(out, states[i].body, states[i].body + strlen(states[i].body), fsm, states, count);
		pigen_append(out, "\n\t\t\t\tend\n");
	}
	pigen_append(out, "\t\t\t\tdefault: "); pigen_append(out, fsm); pigen_append(out, "__pigen_state <= ");
	{ char *init = pigen_copy_range(initial, (size_t)(initial_end - initial)); append_state_ref(out, fsm, init); free(init); }
	pigen_append(out, ";\n\t\t\tendcase\n\t\tend\n\tend\n");
	for (i = 0; i < count; i++) { free(states[i].name); free(states[i].body); }
	free(states); free(fsm);
	*after = outer_end + 3;
}

char *pigen_lower_fsms(const char *source, size_t length, size_t *lowered_length)
{
	pigen_string output = {0};
	fsm_source_start = source;
	const char *p = source, *end = source + length;
	while (p < end)
	{
		const char *opaque = pigen_skip_opaque(p, end);
		if (opaque) { pigen_append_range(&output, p, (size_t)(opaque - p)); p = opaque; continue; }
		if (word_at(p, end, "fsm")) { const char *after; lower_one(&output, p, end, &after); p = after; continue; }
		pigen_append_range(&output, p, 1); p++;
	}
	*lowered_length = output.length;
	return output.data;
}
