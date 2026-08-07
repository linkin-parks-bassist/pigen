/* Transport assignment recognition, validation, and ready/valid lowering. */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/assignments.h"
#include "pigen/declarations.h"
#include "pigen/util.h"

int pigen_extract_transport_assignment(const char *start, const char *end, pigen_primitives *primitives, const char **prefix_end, const char **destination, size_t *destination_length, const char **expression, size_t *expression_length, char *destination_kind)
{
	const char *operator = NULL;
	const char *cursor;
	const char *left_end;
	const char *left_start;
	pigen_primitive *primitive;

	for (cursor = start; cursor + 1 < end; cursor++)
	{
		if (cursor[0] == '<' && cursor[1] == '=')
			operator = cursor;
	}

	if (!operator)
		return 0;

	left_end = pigen_trim_end(start, operator);
	left_start = left_end;

	while (left_start > start && pigen_is_identifier_char((unsigned char)left_start[-1]))
		left_start--;

	primitive = pigen_find_primitive(primitives, left_start, (size_t)(left_end - left_start));

	if (!primitive)
		return 0;

	if (primitive->kind == 'w')
		pigen_fail("cannot assign to wire transport value");

	if (primitive->kind != 'r' && primitive->kind != 'l' && (!pigen_is_storage_kind(primitive->kind) || !primitive->is_internal))
		pigen_fail("draft 0 assignment lowering currently requires an internal storage or reg destination");

	*prefix_end = left_start;
	*destination = left_start;
	*destination_length = (size_t)(left_end - left_start);
	*expression = pigen_skip_spaces(operator + 2, end);
	*expression_length = (size_t)(pigen_trim_end(*expression, end) - *expression);
	*destination_kind = primitive->kind;
	return 1;
}

int pigen_extract_clear_action(const char *start, const char *end, pigen_primitives *primitives,
			       const char **prefix_end, const char **target, size_t *target_length, int *is_flush)
{
	const char *cursor = start;
	const char *action;
	const char *open;
	const char *close;
	const char *name;
	const char *name_end;
	pigen_primitive *primitive;
	int flush;

	for (;;)
	{
		const char *opaque = pigen_skip_opaque(cursor, end);
		if (opaque)
		{
			cursor = opaque;
			continue;
		}
		if ((size_t)(end - cursor) >= 10 && !memcmp(cursor, "invalidate", 10) &&
			(cursor == start || !pigen_is_identifier_char((unsigned char)cursor[-1])) &&
			(cursor + 10 == end || !pigen_is_identifier_char((unsigned char)cursor[10])))
		{
			flush = 0;
			break;
		}
		if ((size_t)(end - cursor) >= 5 && !memcmp(cursor, "flush", 5) &&
			(cursor == start || !pigen_is_identifier_char((unsigned char)cursor[-1])) &&
			(cursor + 5 == end || !pigen_is_identifier_char((unsigned char)cursor[5])))
		{
			flush = 1;
			break;
		}
		if (cursor == end)
			return 0;
		cursor++;
	}
	action = cursor;
	if (cursor == end)
		return 0;

	open = pigen_skip_spaces(cursor + (flush ? 5 : 10), end);
	if (open == end || *open != '(')
		pigen_fail("invalidate/flush requires one transport identifier");
	name = pigen_skip_spaces(open + 1, end);
	close = name;
	while (close < end && *close != ')')
		close++;
	if (close == end || pigen_skip_spaces(close + 1, end) != end)
		pigen_fail("invalidate/flush requires one transport identifier");
	name_end = pigen_trim_end(name, close);
	for (cursor = name; cursor < name_end && pigen_is_identifier_char((unsigned char)*cursor); cursor++)
		;
	if (cursor != name_end)
		pigen_fail("invalidate/flush requires one transport identifier");
	primitive = pigen_find_primitive(primitives, name, (size_t)(name_end - name));
	if (!primitive)
		pigen_fail("invalidate/flush requires a declared transport value");
	if (!pigen_is_storage_kind(primitive->kind) || !primitive->is_internal)
		pigen_fail("invalidate/flush requires locally stored transport value");

	*target = name;
	*target_length = (size_t)(name_end - name);
	*is_flush = flush;
	*prefix_end = action;
	return 1;
}

static int expression_mentions(const char *expression, const char *name);

static void bind_primitive_domain(char **domains, size_t index, const char *domain)
{
	if (!domains[index])
	{
		domains[index] = pigen_copy_range(domain, strlen(domain));
		return;
	}
	if (strcmp(domains[index], domain)) pigen_fail("transport value is used across synchronous domains");
}

static int is_one_posedge_domain(const char *domain)
{
	const char *p = domain;
	if (strncmp(p, "posedge", 7) || pigen_is_identifier_char((unsigned char)p[7])) return 0;
	p += 7;
	while (isspace((unsigned char)*p)) p++;
	if (!pigen_is_identifier_char((unsigned char)*p)) return 0;
	while (pigen_is_identifier_char((unsigned char)*p)) p++;
	while (isspace((unsigned char)*p)) p++;
	return !*p;
}

/*
 * Guards come from the structured procedural parser.  We only prove the
 * simple, useful case here: two paths contain complementary conjuncts (the
 * shape produced by an if/else, including nested ifs).  Anything we cannot
 * prove remains an error.
 */
static char *compact_guard(const char *guard)
{
	size_t i, length = 0;
	char *result = pigen_resize(NULL, strlen(guard) + 1);
	for (i = 0; guard[i]; i++)
		if (!isspace((unsigned char)guard[i])) result[length++] = guard[i];
	result[length] = 0;
	return result;
}

static void strip_outer_parentheses(char *text)
{
	size_t length;
	int depth;
	for (;;)
	{
		length = strlen(text);
		if (length < 2 || text[0] != '(' || text[length - 1] != ')') return;
		depth = 0;
		for (size_t i = 0; i + 1 < length; i++)
		{
			if (text[i] == '(') depth++;
			else if (text[i] == ')' && --depth == 0) return;
		}
		memmove(text, text + 1, length - 2);
		text[length - 2] = 0;
	}
}

static int terms_are_complements(const char *left, const char *right)
{
	char *a = pigen_copy_range(left, strlen(left));
	char *b = pigen_copy_range(right, strlen(right));
	char *inner = NULL;
	int result = 0;
	strip_outer_parentheses(a);
	strip_outer_parentheses(b);
	if (a[0] == '!' && a[1] == '(' && a[strlen(a) - 1] == ')')
	{
		inner = pigen_copy_range(a + 2, strlen(a) - 3);
		strip_outer_parentheses(inner);
		result = !strcmp(inner, b);
		free(inner);
	}
	if (b[0] == '!' && b[1] == '(' && b[strlen(b) - 1] == ')')
	{
		inner = pigen_copy_range(b + 2, strlen(b) - 3);
		strip_outer_parentheses(inner);
		result |= !strcmp(inner, a);
		free(inner);
	}
	free(a);
	free(b);
	return result;
}

static int guards_mutually_exclusive(const char *left, const char *right)
{
	char *a = compact_guard(left);
	char *b = compact_guard(right);
	char *a_term = a;
	int result = 0;

	strip_outer_parentheses(a);
	strip_outer_parentheses(b);
	if (!a[0] || !b[0]) goto done;
	/* Test every top-level conjunction term against every term in the other path. */
	for (;;)
	{
		char *a_end = a_term;
		char *b_term;
		int depth = 0;
		while (*a_end)
		{
			if (*a_end == '(') depth++;
			else if (*a_end == ')') depth--;
			else if (depth == 0 && a_end[0] == '&' && a_end[1] == '&') break;
			a_end++;
		}
		{
			char saved = *a_end;
			*a_end = 0;
			b_term = b;
			for (;;)
			{
				char *b_end = b_term;
				depth = 0;
				while (*b_end)
				{
					if (*b_end == '(') depth++;
					else if (*b_end == ')') depth--;
					else if (depth == 0 && b_end[0] == '&' && b_end[1] == '&') break;
					b_end++;
				}
				{
					char b_saved = *b_end;
					*b_end = 0;
					if (terms_are_complements(a_term, b_term)) result = 1;
					*b_end = b_saved;
					if (result || !b_saved) break;
					b_term = b_end + 2;
				}
			}
			*a_end = saved;
			if (result || !saved) break;
			a_term = a_end + 2;
		}
	}
done:
	free(a);
	free(b);
	return result;
}

static int expression_has_validity_except(const char *expression, pigen_primitives *primitives,
						  const char *excluded_name)
{
	size_t i;

	for (i = 0; i < primitives->count; i++)
	{
		if ((!excluded_name || strcmp(primitives->items[i].name, excluded_name)) &&
			expression_mentions(expression, primitives->items[i].name))
			return 1;
	}

	return 0;
}

static void emit_expression_validity_except(pigen_string *output, const char *expression,
					     pigen_primitives *primitives, const char *excluded_name)
{
	size_t i;
	int emitted = 0;

	for (i = 0; i < primitives->count; i++)
	{
		if ((!excluded_name || strcmp(primitives->items[i].name, excluded_name)) &&
			expression_mentions(expression, primitives->items[i].name))
		{
			if (emitted)
				pigen_append(output, " && ");
			pigen_emit_transport_condition(output, &primitives->items[i], "valid");
			emitted = 1;
		}
	}
}

void pigen_emit_expression_validity(pigen_string *output, const char *expression, pigen_primitives *primitives)
{
	size_t i;
	int emitted = 0;

	for (i = 0; i < primitives->count; i++)
	{
		if (expression_mentions(expression, primitives->items[i].name))
		{
			if (emitted)
				pigen_append(output, " && ");

			pigen_emit_transport_condition(output, &primitives->items[i], "valid");
			emitted = 1;
		}
	}

	if (!emitted)
		pigen_append(output, "1'b1");
}

void pigen_emit_assignment_routes(pigen_string *output, pigen_assignments *assignments, pigen_primitives *primitives)
{
	size_t i;

	/* Emit one driver per storage input; alternatives are disjoint paths. */
	for (i = 0; i < assignments->count; i++)
	{
		pigen_assignment *assignment = &assignments->items[i];
		size_t j;
		int first = 1;
		if (!pigen_is_storage_kind(assignment->destination_kind) || assignment->destination_kind == 'h') continue;
		for (j = 0; j < i; j++)
			if (!strcmp(assignments->items[j].destination, assignment->destination)) break;
		if (j != i) continue;

		pigen_append(output, "\tassign ");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "in_valid");
		pigen_append(output, " = ");
		first = 1;
		for (j = i; j < assignments->count; j++)
		{
			pigen_assignment *alternative = &assignments->items[j];
			if (strcmp(alternative->destination, assignment->destination)) continue;
			if (!first) pigen_append(output, " || ");
			if (alternative->guard[0])
			{
				pigen_append(output, "(");
				pigen_append(output, alternative->guard);
				pigen_append(output, ") && ");
			}
			pigen_emit_expression_validity(output, alternative->expression, primitives);
			first = 0;
		}
		pigen_append(output, ";\n\tassign ");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "packet_in");
		pigen_append(output, " = ");
		first = 1;
		for (j = i; j < assignments->count; j++)
		{
			pigen_assignment *alternative = &assignments->items[j];
			if (strcmp(alternative->destination, assignment->destination)) continue;
			if (alternative->guard[0])
			{
				pigen_append(output, "(("); pigen_append(output, alternative->guard);
				pigen_append(output, ") ? ("); pigen_append(output, alternative->expression);
				pigen_append(output, ") : ");
			}
			else { pigen_append(output, alternative->expression); first = 0; break; }
		}
		if (first)
		{
			pigen_append(output, "'0");
			for (j = i; j < assignments->count; j++)
				if (!strcmp(assignments->items[j].destination, assignment->destination) && assignments->items[j].guard[0]) pigen_append(output, ")");
		}
		pigen_append(output, ";\n\n");
	}

	/* Emit one ready driver per buffered source, OR-ing exclusive consumers. */
	for (i = 0; i < primitives->count; i++)
	{
		pigen_primitive *primitive = &primitives->items[i];
		size_t j;
		int emitted = 0;
		if (!pigen_is_storage_kind(primitive->kind)) continue;
		for (j = 0; j < assignments->count; j++)
		{
			pigen_assignment *assignment = &assignments->items[j];
			if (!expression_mentions(assignment->expression, primitive->name)) continue;
			if (!emitted) { pigen_append(output, "\tassign "); pigen_append_control_name(output, primitive->name, strlen(primitive->name), "ready"); pigen_append(output, " = "); }
			else pigen_append(output, " || ");
			if (assignment->guard[0]) { pigen_append(output, "("); pigen_append(output, assignment->guard); pigen_append(output, ") && "); }
			if (assignment->destination_kind == 'h') pigen_append(output, "1'b1");
			else if (pigen_is_storage_kind(assignment->destination_kind)) pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "in_ready");
			else pigen_append(output, "1'b1");
			if (expression_has_validity_except(assignment->expression, primitives, primitive->name)) { pigen_append(output, " && "); emit_expression_validity_except(output, assignment->expression, primitives, primitive->name); }
			emitted = 1;
		}
		if (emitted) pigen_append(output, ";\n\n");
	}

	/* A port is a manually written payload register plus a Pigen valid pulse.
	 * The payload write is deliberately unconditional: this preserves the
	 * canonical `q <= mem[address]` shape required by synchronous RAM macros. */
	for (i = 0; i < assignments->count; i++)
	{
		pigen_assignment *assignment = &assignments->items[i];
		size_t j;
		if (assignment->destination_kind != 'h') continue;
		for (j = 0; j < i; j++)
			if (!strcmp(assignments->items[j].destination, assignment->destination)) break;
		if (j != i) continue;
		pigen_append(output, "\talways_ff @("); pigen_append(output, assignment->domain); pigen_append(output, ")\n\tbegin\n\t\t");
		pigen_append(output, assignment->destination); pigen_append(output, " <= "); pigen_append(output, assignment->expression); pigen_append(output, ";\n\tend\n\n");
		pigen_append(output, "\talways_ff @("); pigen_append(output, assignment->domain); pigen_append(output, ")\n\tbegin\n\t\tif ("); pigen_append(output, pigen_reset_connection()); pigen_append(output, ")\n\t\t\t");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "valid"); pigen_append(output, " <= 1'b0;\n\t\telse if (");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "clear"); pigen_append(output, " || ");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "discard"); pigen_append(output, ")\n\t\t\t");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "valid"); pigen_append(output, " <= 1'b0;\n\t\telse\n\t\t\t");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "valid"); pigen_append(output, " <= ");
		if (assignment->guard[0]) { pigen_append(output, "("); pigen_append(output, assignment->guard); pigen_append(output, ") && "); }
		pigen_emit_expression_validity(output, assignment->expression, primitives);
		pigen_append(output, ";\n\tend\n\n");
	}
}

static int expression_mentions(const char *expression, const char *name)
{
	const char *cursor = expression;
	const char *end = expression + strlen(expression);
	size_t name_length = strlen(name);

	while (cursor < end)
	{
		const char *opaque = pigen_skip_opaque(cursor, end);

		if (opaque)
		{
			cursor = opaque;
			continue;
		}

		while (cursor < end && !pigen_is_identifier_char((unsigned char)*cursor))
		{
			opaque = pigen_skip_opaque(cursor, end);

			if (opaque)
			{
				cursor = opaque;
				break;
			}

			cursor++;
		}

		if (cursor < end && !strncmp(cursor, name, name_length) &&
			(cursor == expression || !pigen_is_identifier_char((unsigned char)cursor[-1])) &&
			(cursor + name_length == end || !pigen_is_identifier_char((unsigned char)cursor[name_length])))
			return 1;

		while (cursor < end && pigen_is_identifier_char((unsigned char)*cursor))
			cursor++;
	}

	return 0;
}

void pigen_validate_assignments(pigen_assignments *assignments, pigen_primitives *primitives)
{
	size_t i;
	size_t j;
	char **domains = calloc(primitives->count, sizeof(*domains));

	if (primitives->count && !domains) pigen_fail("out of memory");
	for (i = 0; i < assignments->count; i++)
	{
		if (!is_one_posedge_domain(assignments->items[i].domain))
			pigen_fail("transport actions require exactly one posedge event control");
		for (j = 0; j < primitives->count; j++)
			if (!strcmp(assignments->items[i].destination, primitives->items[j].name) ||
				expression_mentions(assignments->items[i].expression, primitives->items[j].name))
				bind_primitive_domain(domains, j, assignments->items[i].domain);
	}

	for (i = 0; i < assignments->count; i++)
	{
		for (j = i + 1; j < assignments->count; j++)
		{
			if (!strcmp(assignments->items[i].destination, assignments->items[j].destination) &&
				assignments->items[i].destination_kind == 'h')
				pigen_fail("port has more than one assignment producer");
			if (!strcmp(assignments->items[i].destination, assignments->items[j].destination) &&
				!guards_mutually_exclusive(assignments->items[i].guard, assignments->items[j].guard))
				pigen_fail("transport value has more than one assignment producer");
		}
	}

	for (i = 0; i < primitives->count; i++)
	{
		int uses = 0;

		if (!pigen_is_storage_kind(primitives->items[i].kind))
			continue;

		for (j = 0; j < assignments->count; j++)
		{
			if (expression_mentions(assignments->items[j].expression, primitives->items[i].name))
				uses++;
		}

		if (uses > 1)
		{
			for (j = 0; j < assignments->count; j++)
			{
				size_t k;
				if (!expression_mentions(assignments->items[j].expression, primitives->items[i].name)) continue;
				for (k = j + 1; k < assignments->count; k++)
					if (expression_mentions(assignments->items[k].expression, primitives->items[i].name) &&
						!guards_mutually_exclusive(assignments->items[j].guard, assignments->items[k].guard))
						pigen_fail("buffered transport value has more than one consumer");
			}
		}
	}
	for (i = 0; i < primitives->count; i++) free(domains[i]);
	free(domains);
}

void pigen_validate_clears(pigen_clears *clears, pigen_assignments *assignments, pigen_primitives *primitives)
{
	size_t i;
	size_t j;

	for (i = 0; i < clears->count; i++)
	{
		if (!is_one_posedge_domain(clears->items[i].domain))
			pigen_fail("transport actions require exactly one posedge event control");
		if (!pigen_find_primitive(primitives, clears->items[i].target, strlen(clears->items[i].target)))
			pigen_fail("clear action references an undeclared transport value");

		for (j = 0; j < assignments->count; j++)
		{
			if (!strcmp(clears->items[i].target, assignments->items[j].destination) ||
				expression_mentions(assignments->items[j].expression, clears->items[i].target))
			{
				if (strcmp(clears->items[i].domain, assignments->items[j].domain))
					pigen_fail("transport value is used across synchronous domains");
				if (!guards_mutually_exclusive(clears->items[i].guard, assignments->items[j].guard))
					pigen_fail("clear action overlaps a transfer of the same transport value");
			}
		}
		for (j = i + 1; j < clears->count; j++)
			if (!strcmp(clears->items[i].target, clears->items[j].target) &&
				strcmp(clears->items[i].domain, clears->items[j].domain))
				pigen_fail("transport value is used across synchronous domains");
	}
}

void pigen_emit_clear_routes(pigen_string *output, pigen_clears *clears, pigen_primitives *primitives)
{
	size_t i;
	size_t j;
	int flush;

	for (flush = 0; flush <= 1; flush++)
	{
		for (i = 0; i < primitives->count; i++)
		{
			pigen_primitive *primitive = &primitives->items[i];
			int emitted = 0;

			if (!pigen_is_storage_kind(primitive->kind) || !primitive->is_internal)
				continue;

			pigen_append(output, "\tassign ");
			pigen_append_control_name(output, primitive->name, strlen(primitive->name), flush ? "clear" : "discard");
			pigen_append(output, " = ");
			for (j = 0; j < clears->count; j++)
			{
				pigen_clear *clear = &clears->items[j];
				if (clear->is_flush != flush || strcmp(clear->target, primitive->name)) continue;
				if (emitted) pigen_append(output, " || ");
				if (clear->guard[0])
				{
					pigen_append(output, "(");
					pigen_append(output, clear->guard);
					pigen_append(output, ")");
				}
				else pigen_append(output, "1'b1");
				emitted = 1;
			}
			if (!emitted) pigen_append(output, "1'b0");
			pigen_append(output, ";\n");
		}
	}

	if (primitives->count)
		pigen_append(output, "\n");
}

void pigen_emit_rewritten_expression(pigen_string *output, const char *start, const char *end, pigen_primitives *primitives)
{
	const char *cursor = start;

	while (cursor < end)
	{
		const char *name;
		const char *close;
		const char *open;
		const char *argument;
		const char *argument_end;
		pigen_primitive *primitive;
		const char *suffix;
		size_t primitive_length;
		int is_accepts;

		const char *opaque = pigen_skip_opaque(cursor, end);

		if (opaque)
		{
			pigen_append_range(output, cursor, (size_t)(opaque - cursor));
			cursor = opaque;
			continue;
		}

		if ((size_t)(end - cursor) >= 2 && !memcmp(cursor, "if", 2) &&
			(cursor == start || !pigen_is_identifier_char((unsigned char)cursor[-1])) &&
			(cursor + 2 == end || !pigen_is_identifier_char((unsigned char)cursor[2])))
		{
			const char *open = pigen_skip_spaces(cursor + 2, end);
			const char *close = open;
			int depth = 0;
			if (open < end && *open == '(')
			{
				for (close = open + 1; close < end; close++)
				{
					const char *nested_opaque = pigen_skip_opaque(close, end);
					if (nested_opaque) { close = nested_opaque - 1; continue; }
					if (*close == '(') depth++;
					else if (*close == ')' && depth-- == 0) break;
				}
				if (close < end)
				{
					pigen_string condition = {0};
					if (pigen_emit_conditional_transfer_condition(&condition, open + 1, close, primitives))
					{
						pigen_append_range(output, cursor, (size_t)(open + 1 - cursor));
						pigen_append(output, condition.data);
						pigen_append(output, ")");
						free(condition.data);
						cursor = close + 1;
						continue;
					}
					free(condition.data);
				}
			}
		}

		if ((size_t)(end - cursor) >= 7 && !memcmp(cursor, "accepts", 7))
		{
			primitive_length = 7;
			is_accepts = 1;
		}
		else if ((size_t)(end - cursor) >= 5 && !memcmp(cursor, "valid", 5))
		{
			primitive_length = 5;
			is_accepts = 0;
			suffix = "valid";
		}
		else if ((size_t)(end - cursor) >= 5 && !memcmp(cursor, "ready", 5))
		{
			primitive_length = 5;
			is_accepts = 0;
			suffix = "ready";
		}
		else
		{
			pigen_append_range(output, cursor, 1);
			cursor++;
			continue;
		}

		if ((cursor > start && pigen_is_identifier_char((unsigned char)cursor[-1])) ||
			(cursor + primitive_length < end && pigen_is_identifier_char((unsigned char)cursor[primitive_length])))
		{
			pigen_append_range(output, cursor, 1);
			cursor++;
			continue;
		}

		open = pigen_skip_spaces(cursor + primitive_length, end);

		if (open == end || *open != '(')
		{
			pigen_append_range(output, cursor, 1);
			cursor++;
			continue;
		}

		argument = pigen_skip_spaces(open + 1, end);
		close = argument;

		while (close < end && *close != ')')
			close++;

		if (close == end)
			pigen_fail("unterminated valid/ready expression");

		argument_end = pigen_trim_end(argument, close);

		if (is_accepts)
		{
			const char *comma = argument;
			const char *destination_start;
			const char *source;
			const char *source_end;
			pigen_primitive *destination;
			pigen_primitive *source_primitive;

			while (comma < argument_end && *comma != ',')
				comma++;
			if (comma == argument_end)
				pigen_fail("accepts requires destination and source transport identifiers");

			destination_start = pigen_skip_spaces(argument, comma);
			name = pigen_trim_end(destination_start, comma);
			for (primitive = NULL; destination_start < name && pigen_is_identifier_char((unsigned char)destination_start[0]); destination_start++)
				;
			if (destination_start != name)
				pigen_fail("accepts requires destination and source transport identifiers");

			destination_start = pigen_skip_spaces(open + 1, comma);
			destination = pigen_find_primitive(primitives, destination_start,
				(size_t)(name - destination_start));
			source = pigen_skip_spaces(comma + 1, argument_end);
			source_end = pigen_trim_end(source, argument_end);
			for (name = source; name < source_end && pigen_is_identifier_char((unsigned char)*name); name++)
				;
			if (name != source_end)
				pigen_fail("accepts requires destination and source transport identifiers");
			source_primitive = pigen_find_primitive(primitives, source, (size_t)(source_end - source));
			if (!destination || !source_primitive)
				pigen_fail("accepts requires declared transport values");
			pigen_emit_transport_condition(output, destination, "ready");
			pigen_append(output, " && ");
			pigen_emit_transport_condition(output, source_primitive, "valid");
		}
		else
		{
			name = argument;
			while (name < argument_end && pigen_is_identifier_char((unsigned char)*name))
				name++;
			if (name != argument_end)
				pigen_fail("valid/ready requires a transport identifier");
			primitive = pigen_find_primitive(primitives, argument, (size_t)(argument_end - argument));
			if (!primitive)
				pigen_fail("valid/ready requires a declared transport value");
			pigen_emit_transport_condition(output, primitive, suffix);
		}
		cursor = close + 1;
	}
}

int pigen_emit_conditional_transfer_condition(pigen_string *output, const char *start, const char *end, pigen_primitives *primitives)
{
	const char *prefix_end;
	const char *destination;
	const char *expression;
	size_t destination_length;
	size_t expression_length;
	char destination_kind;
	pigen_primitive *destination_primitive;
	pigen_primitive *source_primitive;
	const char *source_end;

	if (!pigen_extract_transport_assignment(start, end, primitives, &prefix_end, &destination,
		&destination_length, &expression, &expression_length, &destination_kind) ||
		pigen_skip_spaces(start, end) != prefix_end ||
		pigen_trim_end(expression, expression + expression_length) != expression + expression_length)
		return 0;
	/* Conditions are intentionally the same restricted identifier pair as accepts(). */
	for (source_end = expression; source_end < expression + expression_length && pigen_is_identifier_char((unsigned char)*source_end); source_end++) ;
	if (source_end != expression + expression_length)
		pigen_fail("conditional transfer requires destination and source transport identifiers");
	destination_primitive = pigen_find_primitive(primitives, destination, destination_length);
	source_primitive = pigen_find_primitive(primitives, expression, expression_length);
	if (!destination_primitive || !source_primitive)
		pigen_fail("conditional transfer requires declared transport values");
	pigen_emit_transport_condition(output, destination_primitive, "ready");
	pigen_append(output, " && ");
	pigen_emit_transport_condition(output, source_primitive, "valid");
	return 1;
}
