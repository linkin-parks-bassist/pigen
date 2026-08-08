/* Transport assignment recognition, validation, and ready/valid lowering. */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/assignments.h"
#include "pigen/declarations.h"
#include "pigen/util.h"

static const char *find_top_level_operator(const char *start, const char *end)
{
	const char *cursor;
	int depth = 0;
	for (cursor = start; cursor + 1 < end; cursor++)
	{
		const char *opaque = pigen_skip_opaque(cursor, end);
		if (opaque) { cursor = opaque - 1; continue; }
		if (*cursor == '(' || *cursor == '[' || *cursor == '{') depth++;
		else if (*cursor == ')' || *cursor == ']' || *cursor == '}') depth--;
		else if (!depth && cursor[0] == '<' && cursor[1] == '=') return cursor;
	}
	return NULL;
}

static int split_top_level_braces(const char *start, const char *end, const char ***parts, size_t **lengths, size_t *count)
{
	const char *cursor;
	const char *item;
	int depth = 0;
	size_t capacity = 0;
	start = pigen_skip_spaces(start, end);
	end = pigen_trim_end(start, end);
	if (end - start < 2 || *start != '{' || end[-1] != '}') return 0;
	item = start + 1;
	for (cursor = item; cursor <= end - 1; cursor++)
	{
		const char *opaque = pigen_skip_opaque(cursor, end - 1);
		if (opaque) { cursor = opaque - 1; continue; }
		if (cursor == end - 1 || (*cursor == ',' && depth == 0))
		{
			const char *part = pigen_skip_spaces(item, cursor);
			const char *part_end = pigen_trim_end(part, cursor);
			if (part == part_end) pigen_fail("empty co-sliced transfer item");
			if (*count == capacity)
			{
				capacity = capacity ? capacity * 2 : 4;
				*parts = pigen_resize((void *)*parts, capacity * sizeof(**parts));
				*lengths = pigen_resize(*lengths, capacity * sizeof(**lengths));
			}
			(*parts)[*count] = part;
			(*lengths)[(*count)++] = (size_t)(part_end - part);
			item = cursor + 1;
			continue;
		}
		if (*cursor == '(' || *cursor == '[' || *cursor == '{') depth++;
		else if (*cursor == ')' || *cursor == ']' || *cursor == '}') depth--;
	}
	return 1;
}

static int is_identifier_range(const char *start, size_t length)
{
	size_t i;
	if (!length || !pigen_is_identifier_char((unsigned char)start[0])) return 0;
	for (i = 1; i < length; i++)
		if (!pigen_is_identifier_char((unsigned char)start[i])) return 0;
	return 1;
}

int pigen_extract_transport_transfer(const char *start, const char *end, pigen_primitives *primitives, pigen_transfer *transfer)
{
	const char *operator = find_top_level_operator(start, end);
	const char *left_end;
	const char *left_start;
	const char *expression;
	const char **left_parts = NULL;
	const char **right_parts = NULL;
	size_t *left_lengths = NULL;
	size_t *right_lengths = NULL;
	size_t left_count = 0, right_count = 0, i;
	pigen_primitive *primitive;

	memset(transfer, 0, sizeof(*transfer));
	if (!operator) return 0;
	left_end = pigen_trim_end(start, operator);
	left_start = left_end;
	if (left_start > start && left_start[-1] == '}')
	{
		int depth = 0;
		const char *scan;
		for (scan = left_start; scan > start; )
		{
			scan--;
			if (*scan == '}') depth++;
			else if (*scan == '{' && --depth == 0) { left_start = scan; break; }
		}
	}
	else left_start = pigen_skip_spaces(start, left_end);
	expression = pigen_skip_spaces(operator + 2, end);
	end = pigen_trim_end(expression, end);

	if (split_top_level_braces(left_start, left_end, &left_parts, &left_lengths, &left_count))
	{
		if (left_count < 2) pigen_fail("co-sliced transfer requires at least two destinations");
		if (!split_top_level_braces(expression, end, &right_parts, &right_lengths, &right_count))
			pigen_fail("co-sliced transfer requires a matching RHS concatenation");
		if (left_count != right_count) pigen_fail("co-sliced transfer has mismatched destination and expression counts");
		transfer->items = pigen_resize(NULL, left_count * sizeof(*transfer->items));
		transfer->count = left_count;
		transfer->prefix_end = left_start;
		for (i = 0; i < left_count; i++)
		{
			if (!is_identifier_range(left_parts[i], left_lengths[i])) pigen_fail("co-sliced transfer destinations must be transport identifiers");
			if (left_lengths[i] == 1 && left_parts[i][0] == '_')
			{
				transfer->items[i] = (pigen_transfer_item){ left_parts[i], left_lengths[i], right_parts[i], right_lengths[i], 'd' };
				continue;
			}
			primitive = pigen_find_primitive(primitives, left_parts[i], left_lengths[i]);
			if (!primitive) pigen_fail("co-sliced transfer destinations must be declared transports");
			if (primitive->kind != 'r' && primitive->kind != 'l' && (!pigen_is_storage_kind(primitive->kind) || !primitive->is_internal))
				pigen_fail("draft 0 assignment lowering currently requires an internal storage or reg destination");
			for (size_t j = 0; j < i; j++)
				if (left_lengths[j] == left_lengths[i] && !memcmp(left_parts[j], left_parts[i], left_lengths[i])) pigen_fail("co-sliced transfer repeats a destination");
			transfer->items[i] = (pigen_transfer_item){ left_parts[i], left_lengths[i], right_parts[i], right_lengths[i], primitive->kind };
		}
		free(left_parts); free(right_parts); free(left_lengths); free(right_lengths);
		return 1;
	}

	left_end = pigen_trim_end(start, operator);
	left_start = left_end;
	while (left_start > start && pigen_is_identifier_char((unsigned char)left_start[-1])) left_start--;
	if (left_end - left_start == 1 && left_start[0] == '_')
	{
		transfer->items = pigen_resize(NULL, sizeof(*transfer->items));
		transfer->count = 1;
		transfer->prefix_end = left_start;
		transfer->items[0] = (pigen_transfer_item){ left_start, 1, expression, (size_t)(end - expression), 'd' };
		return 1;
	}
	primitive = pigen_find_primitive(primitives, left_start, (size_t)(left_end - left_start));
	if (!primitive) return 0;
	if (primitive->kind != 'r' && primitive->kind != 'l' && (!pigen_is_storage_kind(primitive->kind) || !primitive->is_internal))
		pigen_fail("draft 0 assignment lowering currently requires an internal storage or reg destination");
	transfer->items = pigen_resize(NULL, sizeof(*transfer->items));
	transfer->count = 1;
	transfer->prefix_end = left_start;
	transfer->items[0] = (pigen_transfer_item){ left_start, (size_t)(left_end - left_start), expression, (size_t)(end - expression), primitive->kind };
	return 1;
}

void pigen_free_transfer(pigen_transfer *transfer)
{
	free(transfer->items);
	memset(transfer, 0, sizeof(*transfer));
}

/* Legacy single-transfer callers were replaced by pigen_transfer. */
#if 0
int pigen_extract_transport_assignment(const char *start, const char *end, pigen_primitives *primitives, const char **prefix_end, const char **destination, size_t *destination_length, const char **expression, size_t *expression_length, char *destination_kind)
{

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
#endif

int pigen_extract_clear_action(const char *start, const char *end, pigen_primitives *primitives,
			       const char **prefix_end, const char **target, size_t *target_length, int *action_kind)
{
	const char *cursor = start;
	const char *action;
	const char *open;
	const char *close;
	const char *name;
	const char *name_end;
	pigen_primitive *primitive;
	int kind;

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
			kind = 0;
			break;
		}
		if ((size_t)(end - cursor) >= 5 && !memcmp(cursor, "flush", 5) &&
			(cursor == start || !pigen_is_identifier_char((unsigned char)cursor[-1])) &&
			(cursor + 5 == end || !pigen_is_identifier_char((unsigned char)cursor[5])))
		{
			kind = 1;
			break;
		}
		if ((size_t)(end - cursor) >= 8 && !memcmp(cursor, "validate", 8) &&
			(cursor == start || !pigen_is_identifier_char((unsigned char)cursor[-1])) &&
			(cursor + 8 == end || !pigen_is_identifier_char((unsigned char)cursor[8])))
		{
			kind = 2;
			break;
		}
		if (cursor == end)
			return 0;
		cursor++;
	}
	action = cursor;
	if (cursor == end)
		return 0;

	open = pigen_skip_spaces(cursor + (kind == 0 ? 10 : kind == 1 ? 5 : 8), end);
	if (open == end || *open != '(')
		pigen_fail("validate/invalidate/flush requires one transport identifier");
	name = pigen_skip_spaces(open + 1, end);
	close = name;
	while (close < end && *close != ')')
		close++;
	if (close == end || pigen_skip_spaces(close + 1, end) != end)
		pigen_fail("validate/invalidate/flush requires one transport identifier");
	name_end = pigen_trim_end(name, close);
	for (cursor = name; cursor < name_end && pigen_is_identifier_char((unsigned char)*cursor); cursor++)
		;
	if (cursor != name_end)
		pigen_fail("validate/invalidate/flush requires one transport identifier");
	primitive = pigen_find_primitive(primitives, name, (size_t)(name_end - name));
	if (!primitive)
		pigen_fail("validate/invalidate/flush requires a declared transport value");
	if (kind == 1 && (!pigen_is_storage_kind(primitive->kind) || !primitive->is_internal))
		pigen_fail("flush requires locally stored transport value");
	if (kind != 1 && (!pigen_is_storage_kind(primitive->kind) || !primitive->is_internal))
		pigen_fail("validate/invalidate requires locally stored transport value");

	*target = name;
	*target_length = (size_t)(name_end - name);
	*action_kind = kind;
	*prefix_end = action;
	return 1;
}

static int expression_mentions(const char *expression, const char *name);

static int expression_peeks(const char *expression, const char *name)
{
	const char *cursor = expression;
	const char *end = expression + strlen(expression);
	size_t length = strlen(name);
	while (cursor < end)
	{
		if ((size_t)(end - cursor) >= 4 && !memcmp(cursor, "peek", 4) &&
			(cursor == expression || !pigen_is_identifier_char((unsigned char)cursor[-1])) &&
			(cursor + 4 == end || !pigen_is_identifier_char((unsigned char)cursor[4])))
		{
			const char *open = pigen_skip_spaces(cursor + 4, end);
			const char *arg = open < end && *open == '(' ? pigen_skip_spaces(open + 1, end) : end;
			if (arg + length <= end && !memcmp(arg, name, length) &&
				(arg + length == end || !pigen_is_identifier_char((unsigned char)arg[length]))) return 1;
		}
		cursor++;
	}
	return 0;
}

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

static size_t group_first(pigen_assignments *assignments, size_t group)
{
	for (size_t i = 0; i < assignments->count; i++) if (assignments->items[i].group == group) return i;
	return assignments->count;
}

static size_t group_size(pigen_assignments *assignments, size_t group)
{
	size_t result = 0;
	for (size_t i = 0; i < assignments->count; i++) if (assignments->items[i].group == group) result++;
	return result;
}

static int group_mentions(pigen_assignments *assignments, size_t group, const char *name)
{
	for (size_t i = 0; i < assignments->count; i++)
		if (assignments->items[i].group == group && expression_mentions(assignments->items[i].expression, name)) return 1;
	return 0;
}

static void emit_group_validity(pigen_string *output, pigen_assignments *assignments, size_t group, pigen_primitives *primitives, const char *except)
{
	int emitted = 0;
	for (size_t p = 0; p < primitives->count; p++)
	{
		int mentioned = 0;
		if (except && !strcmp(except, primitives->items[p].name)) continue;
		for (size_t i = 0; i < assignments->count; i++) if (assignments->items[i].group == group && expression_mentions(assignments->items[i].expression, primitives->items[p].name)) mentioned = 1;
		if (mentioned) { if (emitted) pigen_append(output, " && "); pigen_emit_transport_condition(output, &primitives->items[p], "valid"); emitted = 1; }
	}
	if (!emitted) pigen_append(output, "1'b1");
}

static void emit_group_ready(pigen_string *output, pigen_assignments *assignments, size_t group,
					 pigen_primitives *primitives)
{
	int emitted = 0;
	for (size_t i = 0; i < assignments->count; i++) if (assignments->items[i].group == group)
	{
		pigen_assignment *a = &assignments->items[i];
		pigen_primitive *destination = pigen_find_primitive(primitives, a->destination,
			strlen(a->destination));
		if (emitted) pigen_append(output, " && ");
		if (a->destination_kind == 'w') pigen_append(output, "1'b0");
		else if (a->destination_kind == 'h' && destination && destination->is_output)
			pigen_append_control_name(output, a->destination, strlen(a->destination), "in_ready");
		else if (a->destination_kind == 'd' || a->destination_kind == 'h' || a->destination_kind == 'r' || a->destination_kind == 'l' || a->destination_kind == 'R' || a->destination_kind == 'L') pigen_append(output, "1'b1");
		else pigen_append_control_name(output, a->destination, strlen(a->destination), "in_ready");
		emitted = 1;
	}
}

void pigen_emit_transfer_accept_condition(pigen_string *output, const pigen_transfer *transfer, const char *guard, pigen_primitives *primitives)
{
	int emitted = 0;
	if (guard && guard[0]) { pigen_append(output, "("); pigen_append(output, guard); pigen_append(output, ") && "); }
	for (size_t i = 0; i < transfer->count; i++)
	{
		if (transfer->items[i].destination_kind == 'w')
		{
			if (emitted) pigen_append(output, " && ");
			pigen_append(output, "1'b0"); emitted = 1;
		}
		else if (transfer->items[i].destination_kind == 'h')
		{
			pigen_primitive *destination = pigen_find_primitive(primitives,
				transfer->items[i].destination, transfer->items[i].destination_length);
			if (destination && destination->is_output)
			{
				if (emitted) pigen_append(output, " && ");
				pigen_append_control_name(output, transfer->items[i].destination,
					transfer->items[i].destination_length, "in_ready");
				emitted = 1;
			}
		}
		else if (transfer->items[i].destination_kind != 'd' && transfer->items[i].destination_kind != 'r' && transfer->items[i].destination_kind != 'l')
		{
			if (emitted) pigen_append(output, " && ");
			pigen_append_control_name(output, transfer->items[i].destination, transfer->items[i].destination_length, "in_ready"); emitted = 1;
		}
	}
	if (emitted) pigen_append(output, " && ");
	/* A temporary assignment group is unnecessary here: expressions are unique-scanned directly. */
	for (size_t p = 0, any = 0; p < primitives->count; p++)
	{
		int mentioned = 0;
		for (size_t i = 0; i < transfer->count; i++)
		{
			char *expression = pigen_copy_range(transfer->items[i].expression, transfer->items[i].expression_length);
			if (expression_mentions(expression, primitives->items[p].name)) mentioned = 1;
			free(expression);
		}
		if (mentioned) { if (any++) pigen_append(output, " && "); pigen_emit_transport_condition(output, &primitives->items[p], "valid"); }
		if (p + 1 == primitives->count && !any) pigen_append(output, "1'b1");
	}
	if (!primitives->count) pigen_append(output, "1'b1");
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
		if (!pigen_is_storage_kind(assignment->destination_kind) ||
			(assignment->destination_kind == 'h' &&
			 (!pigen_find_primitive(primitives, assignment->destination, strlen(assignment->destination)) ||
			  !pigen_find_primitive(primitives, assignment->destination, strlen(assignment->destination))->is_output))) continue;
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
			if (group_size(assignments, alternative->group) > 1)
			{
				emit_group_validity(output, assignments, alternative->group, primitives, NULL);
				pigen_append(output, " && "); emit_group_ready(output, assignments, alternative->group, primitives);
			}
			else pigen_emit_expression_validity(output, alternative->expression, primitives);
			first = 0;
		}
		pigen_append(output, ";\n\tassign ");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "packet_in");
		pigen_append(output, " = ");
		/* A guarded payload mux contains an untyped '0 fallback.  Cast each branch
		 * to the destination payload type before forming that mux; casting only the
		 * complete mux is too late, because its unsigned fallback can already make
		 * signed multiply operands zero-extend during expression sizing. */
		first = 1;
		for (j = i; j < assignments->count; j++)
		{
			pigen_assignment *alternative = &assignments->items[j];
			if (strcmp(alternative->destination, assignment->destination)) continue;
			if (alternative->guard[0])
			{
				pigen_append(output, "(("); pigen_append(output, alternative->guard);
				pigen_append(output, ") ? (");
				pigen_append_control_name(output, assignment->destination,
					strlen(assignment->destination), "payload_t");
				pigen_append(output, "'(");
				pigen_emit_rewritten_expression(output, alternative->expression, alternative->expression + strlen(alternative->expression), primitives);
				pigen_append(output, ")) : ");
			}
			else
			{
				pigen_append_control_name(output, assignment->destination,
					strlen(assignment->destination), "payload_t");
				pigen_append(output, "'(");
				pigen_emit_rewritten_expression(output, alternative->expression, alternative->expression + strlen(alternative->expression), primitives);
				pigen_append(output, ")");
				first = 0;
				break;
			}
		}
		if (first)
		{
			pigen_append_control_name(output, assignment->destination,
				strlen(assignment->destination), "payload_t");
			pigen_append(output, "'('0)");
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
		if (!pigen_is_storage_kind(primitive->kind) || (primitive->kind == 'h' && primitive->is_output)) continue;
		for (j = 0; j < assignments->count; j++)
		{
			pigen_assignment *assignment = &assignments->items[j];
			int mentioned = 0;
			for (size_t k = 0; k < assignments->count; k++)
				if (assignments->items[k].group == assignment->group && expression_mentions(assignments->items[k].expression, primitive->name)) mentioned = 1;
			if (!mentioned) continue;
			for (size_t k = 0; k < j; k++) if (assignments->items[k].group == assignment->group) { mentioned = 0; break; }
			if (!mentioned) continue;
			if (!emitted) { pigen_append(output, "\tassign "); pigen_append_control_name(output, primitive->name, strlen(primitive->name), "ready"); pigen_append(output, " = "); }
			else pigen_append(output, " || ");
			if (assignment->guard[0]) { pigen_append(output, "("); pigen_append(output, assignment->guard); pigen_append(output, ") && "); }
			if (group_size(assignments, assignment->group) > 1)
			{
				emit_group_ready(output, assignments, assignment->group, primitives);
				pigen_append(output, " && "); emit_group_validity(output, assignments, assignment->group, primitives, primitive->name);
			}
			else
			{
				if (assignment->destination_kind == 'h')
				{
					pigen_primitive *destination = pigen_find_primitive(primitives,
						assignment->destination, strlen(assignment->destination));
					if (destination && destination->is_output)
						pigen_append_control_name(output, assignment->destination,
							strlen(assignment->destination), "in_ready");
					else pigen_append(output, "1'b1");
				}
				else if (pigen_is_storage_kind(assignment->destination_kind)) pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "in_ready");
				else pigen_append(output, "1'b1");
				if (expression_has_validity_except(assignment->expression, primitives, primitive->name)) { pigen_append(output, " && "); emit_expression_validity_except(output, assignment->expression, primitives, primitive->name); }
			}
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
		if (assignment->destination_kind != 'h' ||
			(pigen_find_primitive(primitives, assignment->destination, strlen(assignment->destination)) &&
			 pigen_find_primitive(primitives, assignment->destination, strlen(assignment->destination))->is_output)) continue;
		for (j = 0; j < i; j++)
			if (!strcmp(assignments->items[j].destination, assignment->destination)) break;
		if (j != i) continue;
		pigen_append(output, "\talways_ff @("); pigen_append(output, assignment->domain); pigen_append(output, ")\n\tbegin\n\t\t");
		pigen_append(output, assignment->destination); pigen_append(output, " <= "); pigen_append(output, assignment->expression); pigen_append(output, ";\n\tend\n\n");
		pigen_append(output, "\talways_ff @("); pigen_append(output, assignment->domain); pigen_append(output, ")\n\tbegin\n\t\tif ("); pigen_append(output, pigen_reset_connection()); pigen_append(output, ")\n\t\t\t");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "valid"); pigen_append(output, " <= ");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "force_valid"); pigen_append(output, " && !");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "force_invalid"); pigen_append(output, ";\n\t\telse if (");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "clear"); pigen_append(output, " || ");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "discard"); pigen_append(output, ")\n\t\t\t");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "valid"); pigen_append(output, " <= 1'b0;\n\t\telse\n\t\t\t");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "valid"); pigen_append(output, " <= (");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "force_after_transfer"); pigen_append(output, " && ");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "force_valid");
		pigen_append(output, ") ? 1'b1 : ((");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "force_after_transfer"); pigen_append(output, " && ");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "force_invalid");
		pigen_append(output, ") ? 1'b0 : (");
		if (assignment->guard[0]) { pigen_append(output, "("); pigen_append(output, assignment->guard); pigen_append(output, ") && "); }
		pigen_emit_expression_validity(output, assignment->expression, primitives);
		pigen_append(output, ") ? 1'b1 : (");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "force_valid"); pigen_append(output, " ? 1'b1 : (");
		pigen_append_control_name(output, assignment->destination, strlen(assignment->destination), "force_invalid"); pigen_append(output, " ? 1'b0 : 1'b0)));\n\tend\n\n");
	}

	/* Reg/logic members of a conditional co-slice are emitted here because the
	 * conditional transfer itself is registered after procedural parsing. */
	for (i = 0; i < assignments->count; i++)
	{
		pigen_assignment *assignment = &assignments->items[i];
		if (assignment->destination_kind != 'R' && assignment->destination_kind != 'L') continue;
		pigen_append(output, "\talways_ff @("); pigen_append(output, assignment->domain); pigen_append(output, ")\n\tbegin\n\t\tif (");
		if (assignment->guard[0]) { pigen_append(output, "("); pigen_append(output, assignment->guard); pigen_append(output, ") && "); }
		emit_group_validity(output, assignments, assignment->group, primitives, NULL);
		pigen_append(output, " && "); emit_group_ready(output, assignments, assignment->group, primitives);
		pigen_append(output, ")\n\t\t\t"); pigen_append(output, assignment->destination); pigen_append(output, " <= ");
		pigen_emit_rewritten_expression(output, assignment->expression, assignment->expression + strlen(assignment->expression), primitives);
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
		if ((size_t)(end - cursor) >= 4 && !memcmp(cursor, "peek", 4) &&
			(cursor == expression || !pigen_is_identifier_char((unsigned char)cursor[-1])) &&
			(cursor + 4 == end || !pigen_is_identifier_char((unsigned char)cursor[4])))
		{
			const char *open = pigen_skip_spaces(cursor + 4, end);
			if (open < end && *open == '(') { cursor = open + 1; while (cursor < end && *cursor != ')') cursor++; if (cursor < end) cursor++; continue; }
		}

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

		/* The punctuation scan can land directly on `peek`.  Skip the complete
		 * accessor before checking for consuming transport references. */
		if ((size_t)(end - cursor) >= 4 && !memcmp(cursor, "peek", 4) &&
			(cursor == expression || !pigen_is_identifier_char((unsigned char)cursor[-1])) &&
			(cursor + 4 == end || !pigen_is_identifier_char((unsigned char)cursor[4])))
		{
			const char *open = pigen_skip_spaces(cursor + 4, end);
			if (open < end && *open == '(')
			{
				cursor = open + 1;
				while (cursor < end && *cursor != ')') cursor++;
				if (cursor < end) cursor++;
				continue;
			}
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
				expression_mentions(assignments->items[i].expression, primitives->items[j].name) ||
				expression_peeks(assignments->items[i].expression, primitives->items[j].name))
				bind_primitive_domain(domains, j, assignments->items[i].domain);
	}

	for (i = 0; i < assignments->count; i++)
	{
		for (j = i + 1; j < assignments->count; j++)
		{
			pigen_primitive *destination;

			if (assignments->items[i].group == assignments->items[j].group) continue;
			if (assignments->items[i].destination_kind == 'd' || assignments->items[j].destination_kind == 'd') continue;
			destination = pigen_find_primitive(primitives, assignments->items[i].destination,
				strlen(assignments->items[i].destination));
			if (!strcmp(assignments->items[i].destination, assignments->items[j].destination) &&
				assignments->items[i].destination_kind == 'h' &&
				(!destination || !destination->is_output))
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
			if (group_mentions(assignments, assignments->items[j].group, primitives->items[i].name) &&
				group_first(assignments, assignments->items[j].group) == j) uses++;
		}

		if (uses > 1)
		{
			for (j = 0; j < assignments->count; j++)
			{
				size_t k;
				if (!group_mentions(assignments, assignments->items[j].group, primitives->items[i].name) || group_first(assignments, assignments->items[j].group) != j) continue;
				for (k = j + 1; k < assignments->count; k++)
					if (group_mentions(assignments, assignments->items[k].group, primitives->items[i].name) && group_first(assignments, assignments->items[k].group) == k &&
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
				if (clears->items[i].is_flush == 1 && !guards_mutually_exclusive(clears->items[i].guard, assignments->items[j].guard))
					pigen_fail("clear action overlaps a transfer of the same transport value");
			}
		}
		for (j = i + 1; j < clears->count; j++)
			if (!strcmp(clears->items[i].target, clears->items[j].target) &&
				strcmp(clears->items[i].domain, clears->items[j].domain))
				pigen_fail("transport value is used across synchronous domains");
	}
}

void pigen_emit_clear_routes(pigen_string *output, pigen_clears *clears, pigen_assignments *assignments, pigen_primitives *primitives)
{
	size_t i;
	size_t j;
	int flush;

	/* `flush` remains an occupancy operation.  validate/invalidate are ordered
	 * next-state valid writes and are routed through the primitive controls. */
	for (flush = 1; flush <= 1; flush++)
	{
		for (i = 0; i < primitives->count; i++)
		{
			pigen_primitive *primitive = &primitives->items[i];
			int emitted = 0;

			if (!pigen_is_storage_kind(primitive->kind) || !primitive->is_internal || primitive->kind == 'h')
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

	for (i = 0; i < primitives->count; i++)
	{
		pigen_primitive *primitive = &primitives->items[i];
		size_t max_transfer_order = 0;
		int has_transfer = 0;
		if (!pigen_is_storage_kind(primitive->kind) || !primitive->is_internal)
			continue;
		for (j = 0; j < assignments->count; j++)
			if (!strcmp(assignments->items[j].destination, primitive->name) &&
				(!has_transfer || assignments->items[j].order > max_transfer_order))
			{ max_transfer_order = assignments->items[j].order; has_transfer = 1; }

		for (int wanted = 2; wanted >= 0; wanted -= 2)
		{
			int emitted = 0;
			int terminated = 0;
			pigen_append(output, "\tassign ");
			pigen_append_control_name(output, primitive->name, strlen(primitive->name), wanted ? "force_valid" : "force_invalid");
			pigen_append(output, " = ");
			for (size_t k = clears->count; k > 0; k--)
			{
				pigen_clear *action = &clears->items[k - 1];
				if (strcmp(action->target, primitive->name) || (action->is_flush != 0 && action->is_flush != 2)) continue;
				if (action->guard[0]) { pigen_append(output, "("); pigen_append(output, action->guard); pigen_append(output, ") ? "); }
				pigen_append(output, action->is_flush == wanted ? "1'b1" : "1'b0");
				if (action->guard[0]) pigen_append(output, " : ");
				emitted++;
				if (!action->guard[0]) { terminated = 1; break; }
			}
			if (!emitted) pigen_append(output, "1'b0");
			else if (!terminated) pigen_append(output, "1'b0");
			pigen_append(output, ";\n");
		}
		pigen_append(output, "\tassign ");
		pigen_append_control_name(output, primitive->name, strlen(primitive->name), "force_after_transfer");
		pigen_append(output, " = ");
		{
			int emitted = 0;
			int terminated = 0;
			for (size_t k = clears->count; k > 0; k--)
			{
				pigen_clear *action = &clears->items[k - 1];
				if (strcmp(action->target, primitive->name) || (action->is_flush != 0 && action->is_flush != 2)) continue;
				if (action->guard[0]) { pigen_append(output, "("); pigen_append(output, action->guard); pigen_append(output, ") ? "); }
				pigen_append(output, has_transfer && action->order > max_transfer_order ? "1'b1" : "1'b0");
				if (action->guard[0]) pigen_append(output, " : ");
				emitted++;
				if (!action->guard[0]) { terminated = 1; break; }
			}
			if (!emitted) pigen_append(output, "1'b0");
			else if (!terminated) pigen_append(output, "1'b0");
		}
		pigen_append(output, ";\n");
	}

	if (primitives->count)
		pigen_append(output, "\n");
}

void pigen_emit_rewritten_expression(pigen_string *output, const char *start, const char *end, pigen_primitives *primitives)
{
	const char *cursor = start;
	pigen_string whole_condition = {0};
	if (pigen_emit_conditional_transfer_condition(&whole_condition, start, end, primitives))
	{
		pigen_append(output, whole_condition.data);
		free(whole_condition.data);
		return;
	}

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

		/* A transport's payload type is carried by a parameterised primitive.
		 * Some SV tools lose its signedness when it is read through that boundary,
		 * especially in mixed-width arithmetic and comparisons.  Recover the
		 * declaration's signed intent at every Pigen transport read. */
		if (pigen_is_identifier_char((unsigned char)*cursor) &&
			(cursor == start || !pigen_is_identifier_char((unsigned char)cursor[-1])))
		{
			const char *identifier_end = cursor + 1;
			pigen_primitive *identifier_primitive;
			while (identifier_end < end && pigen_is_identifier_char((unsigned char)*identifier_end))
				identifier_end++;
			identifier_primitive = pigen_find_primitive(primitives, cursor,
				(size_t)(identifier_end - cursor));
			/* Only storage-backed transports cross a primitive type parameter and
			 * need their declaration signedness recovered here.  `logic`, `reg`,
			 * and `wire` are ordinary SV declarations with degenerate transport
			 * controls; wrapping those identifiers also corrupts assignment LHSs. */
			if (identifier_primitive && pigen_is_storage_kind(identifier_primitive->kind) &&
				identifier_primitive->payload_type &&
				strstr(identifier_primitive->payload_type, "signed") &&
				!strstr(identifier_primitive->payload_type, "unsigned"))
			{
				pigen_append(output, "$signed(");
				pigen_append_range(output, cursor, (size_t)(identifier_end - cursor));
				pigen_append(output, ")");
				cursor = identifier_end;
				continue;
			}
		}
		if ((size_t)(end - cursor) >= 4 && !memcmp(cursor, "peek", 4) &&
			(cursor == start || !pigen_is_identifier_char((unsigned char)cursor[-1])) &&
			(cursor + 4 == end || !pigen_is_identifier_char((unsigned char)cursor[4])))
		{
			const char *open_peek = pigen_skip_spaces(cursor + 4, end);
			const char *name_peek = open_peek < end && *open_peek == '(' ? pigen_skip_spaces(open_peek + 1, end) : end;
			const char *end_peek = name_peek;
			while (end_peek < end && pigen_is_identifier_char((unsigned char)*end_peek)) end_peek++;
			const char *close_peek = pigen_skip_spaces(end_peek, end);
			if (open_peek == end || *open_peek != '(' || name_peek == end_peek || close_peek == end || *close_peek != ')' ||
				!pigen_find_primitive(primitives, name_peek, (size_t)(end_peek - name_peek))) pigen_fail("peek requires one declared transport identifier");
			pigen_append_range(output, name_peek, (size_t)(end_peek - name_peek));
			cursor = close_peek + 1;
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
	pigen_transfer transfer;
	start = pigen_skip_spaces(start, end);
	end = pigen_trim_end(start, end);
	if (end - start >= 2 && *start == '(' && end[-1] == ')')
	{
		int depth = 0; int wraps = 1;
		for (const char *p = start; p < end - 1; p++) { if (*p == '(') depth++; else if (*p == ')' && --depth == 0) wraps = 0; }
		if (wraps) { start++; end--; }
	}
	if (!pigen_extract_transport_transfer(start, end, primitives, &transfer) ||
		pigen_skip_spaces(start, end) != transfer.prefix_end)
		return 0;
	if (transfer.count == 1)
	{
		const char *expression = transfer.items[0].expression;
		const char *expression_end = expression + transfer.items[0].expression_length;
		pigen_primitive *source;
		while (expression < expression_end && isspace((unsigned char)*expression)) expression++;
		for (const char *p = expression; p < expression_end; p++) if (!pigen_is_identifier_char((unsigned char)*p)) { source = NULL; goto grouped; }
		source = pigen_find_primitive(primitives, expression, (size_t)(expression_end - expression));
		if (source)
		{
			pigen_primitive *destination = pigen_find_primitive(primitives, transfer.items[0].destination, transfer.items[0].destination_length);
			pigen_emit_transport_condition(output, destination, "ready"); pigen_append(output, " && "); pigen_emit_transport_condition(output, source, "valid");
			pigen_free_transfer(&transfer); return 1;
		}
	}
grouped:
	pigen_emit_transfer_accept_condition(output, &transfer, "", primitives);
	pigen_free_transfer(&transfer);
	return 1;
}
