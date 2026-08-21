/* Shared allocation, lexical, and signal-model utilities. */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/util.h"

static const pigen_prototype_transfer_descriptor prototype_transfer_types[] =
{
	{ "wire", 'w', 0, NULL },
	{ "reg",  'r', 0, NULL },
	{ "logic", 'l', 0, NULL },
	{ "buf",  'b', 1, "pigen_buf" },
	{ "fifo", 'f', 1, "pigen_fifo" },
	{ "skid", 's', 1, "pigen_skid" },
	{ "port", 'h', 1, "pigen_port" },
	/* Compiler-internal, combinational endpoint used to join first-stage
	 * pipeline inputs directly into their stage register. */
	{ "ingress", 'i', 1, NULL },
};

static const char *diagnostic_path;
static const char *diagnostic_source;
static const char *diagnostic_position;

void pigen_set_diagnostic_context(const char *path, const char *source)
{
	diagnostic_path = path;
	diagnostic_source = source;
	diagnostic_position = source;
}

void pigen_set_diagnostic_position(const char *position)
{
	if (diagnostic_source && position >= diagnostic_source)
		diagnostic_position = position;
}

void pigen_fail(const char *message)
{
	if (diagnostic_path && diagnostic_source && diagnostic_position)
	{
		size_t line = 1;
		size_t column = 1;
		const char *cursor;
		for (cursor = diagnostic_source; cursor < diagnostic_position; cursor++)
		{
			if (*cursor == '\n') { line++; column = 1; }
			else column++;
		}
		fprintf(stderr, "%s:%zu:%zu: %s\n", diagnostic_path, line, column, message);
	}
	else
		fprintf(stderr, "pigen: %s\n", message);
	exit(1);
}

void pigen_warn(const char *message)
{
	if (diagnostic_path && diagnostic_source && diagnostic_position)
	{
		size_t line = 1;
		size_t column = 1;
		const char *cursor;
		for (cursor = diagnostic_source; cursor < diagnostic_position; cursor++)
		{
			if (*cursor == '\n') { line++; column = 1; }
			else column++;
		}
		fprintf(stderr, "%s:%zu:%zu: warning: %s\n", diagnostic_path, line, column,
			message);
	}
	else
		fprintf(stderr, "pigen: warning: %s\n", message);
}

void *pigen_resize(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	
	if (!ptr)
		pigen_fail("out of memory");
	
	return ptr;
}

void pigen_append_range(pigen_string *string, const char *src, size_t length)
{
	if (string->length + length + 1 > string->capacity)
	{
		size_t capacity = string->capacity ? string->capacity * 2 : 1024;
		
		while (capacity < string->length + length + 1)
			capacity *= 2;
		
		string->data = pigen_resize(string->data, capacity);
		string->capacity = capacity;
	}
	
	memcpy(string->data + string->length, src, length);
	string->length += length;
	string->data[string->length] = 0;
}

void pigen_append(pigen_string *string, const char *src)
{
	pigen_append_range(string, src, strlen(src));
}

void pigen_append_format(pigen_string *string, const char *format, ...)
{
	va_list arguments;
	va_list copy;
	int length;
	char *buffer;

	if (!format)
		pigen_fail("cannot format generated output with a null format");
	va_start(arguments, format);
	va_copy(copy, arguments);
	length = vsnprintf(NULL, 0, format, copy);
	va_end(copy);
	if (length < 0)
		pigen_fail("cannot format generated output");
	buffer = pigen_resize(NULL, (size_t)length + 1);
	vsnprintf(buffer, (size_t)length + 1, format, arguments);
	va_end(arguments);
	pigen_append_range(string, buffer, (size_t)length);
	free(buffer);
}

char *pigen_copy_range(const char *src, size_t length)
{
	char *copy = pigen_resize(NULL, length + 1);
	
	memcpy(copy, src, length);
	copy[length] = 0;
	return copy;
}

int pigen_is_identifier_char(int c)
{
	return isalnum((unsigned char)c) || c == '_' || c == '$';
}

int pigen_is_word(const char *src, size_t length, const char *word)
{
	return length == strlen(word) && !memcmp(src, word, length);
}

const pigen_prototype_transfer_descriptor *
pigen_prototype_transfer_descriptor_get(char transfer_type)
{
	size_t i;

	for (i = 0; i < sizeof(prototype_transfer_types) /
		sizeof(prototype_transfer_types[0]); i++)
	{
		if (prototype_transfer_types[i].transfer_type == transfer_type)
			return &prototype_transfer_types[i];
	}

	return NULL;
}

char pigen_transfer_type_for_keyword(const char *word, size_t length)
{
	size_t i;

	for (i = 0; i < sizeof(prototype_transfer_types) /
		sizeof(prototype_transfer_types[0]); i++)
	{
		if (pigen_is_word(word, length, prototype_transfer_types[i].keyword))
			return prototype_transfer_types[i].transfer_type;
	}

	return 0;
}

const char *pigen_skip_spaces(const char *src, const char *end)
{
	while (src < end && isspace((unsigned char)*src))
		src++;
	
	return src;
}

const char *pigen_trim_end(const char *start, const char *end)
{
	while (end > start && isspace((unsigned char)end[-1]))
		end--;
	
	return end;
}

/*
 * Comments and string literals are opaque to the Pigen surface syntax.  This
 * is deliberately a lexical utility rather than a semantic special case, so
 * every later parser pass can share the same boundary.
 */
const char *pigen_skip_opaque(const char *cursor, const char *end)
{
	if (cursor + 1 < end && cursor[0] == '/' && cursor[1] == '/')
	{
		cursor += 2;

		while (cursor < end && *cursor != '\n')
			cursor++;

		return cursor;
	}

	if (cursor + 1 < end && cursor[0] == '/' && cursor[1] == '*')
	{
		cursor += 2;

		while (cursor + 1 < end && !(cursor[0] == '*' && cursor[1] == '/'))
			cursor++;

		if (cursor == end || cursor + 1 == end)
			pigen_fail("unterminated block comment");

		return cursor + 2;
	}

	if (cursor < end && *cursor == '"')
	{
		cursor++;

		while (cursor < end)
		{
			if (*cursor == '\\' && cursor + 1 < end)
				cursor += 2;
			else if (*cursor++ == '"')
				return cursor;
		}

		pigen_fail("unterminated string literal");
	}

	return NULL;
}

const char *pigen_skip_trivia(const char *cursor, const char *end)
{
	for (;;)
	{
		const char *opaque;

		cursor = pigen_skip_spaces(cursor, end);
		opaque = pigen_skip_opaque(cursor, end);

		if (!opaque)
			return cursor;

		cursor = opaque;
	}
}

void pigen_add_primitive(pigen_primitives *primitives, const char *name, size_t name_length, char transfer_type, int is_internal)
{
	size_t i;
	char *copy;

	copy = pigen_copy_range(name, name_length);
	
	for (i = 0; i < primitives->count; i++)
	{
		if (!strcmp(primitives->items[i].name, copy))
			pigen_fail("primitive declared more than once");
	}
	
	if (primitives->count == primitives->capacity)
	{
		size_t capacity = primitives->capacity ? primitives->capacity * 2 : 16;
		
		primitives->items = pigen_resize(primitives->items, capacity * sizeof(pigen_primitive));
		primitives->capacity = capacity;
	}
	
	primitives->items[primitives->count].name = copy;
	primitives->items[primitives->count].transfer_type = transfer_type;
	primitives->items[primitives->count].is_internal = is_internal;
	primitives->items[primitives->count].is_output = 0;
	primitives->items[primitives->count].data_type = NULL;
	primitives->items[primitives->count].fifo_depth = NULL;
	primitives->count++;
}

pigen_primitive *pigen_find_primitive(pigen_primitives *primitives, const char *name, size_t name_length)
{
	size_t i;

	for (i = 0; i < primitives->count; i++)
	{
		if (strlen(primitives->items[i].name) == name_length && !memcmp(primitives->items[i].name, name, name_length))
			return &primitives->items[i];
	}

	return NULL;
}

void pigen_set_port_metadata(pigen_primitives *primitives, const char *name, size_t name_length, const char *data_type, size_t data_type_length, const char *fifo_depth, size_t fifo_depth_length, int is_output)
{
	pigen_primitive *primitive = pigen_find_primitive(primitives, name, name_length);

	if (!primitive)
		pigen_fail("internal port metadata error");

	primitive->is_output = is_output;
	primitive->data_type = pigen_copy_range(data_type, data_type_length);

	if (fifo_depth)
		primitive->fifo_depth = pigen_copy_range(fifo_depth, fifo_depth_length);
}

void pigen_add_assignment_in_group(pigen_assignments *assignments, const char *destination, size_t destination_length, const char *expression, size_t expression_length, const char *guard, size_t guard_length, const char *domain, size_t domain_length, char destination_kind, size_t group, size_t order)
{
	if (assignments->count == assignments->capacity)
	{
		size_t capacity = assignments->capacity ? assignments->capacity * 2 : 16;

		assignments->items = pigen_resize(assignments->items, capacity * sizeof(pigen_assignment));
		assignments->capacity = capacity;
	}

	assignments->items[assignments->count].destination = pigen_copy_range(destination, destination_length);
	assignments->items[assignments->count].expression = pigen_copy_range(expression, expression_length);
	assignments->items[assignments->count].guard = pigen_copy_range(guard, guard_length);
	assignments->items[assignments->count].domain = pigen_copy_range(domain, domain_length);
	assignments->items[assignments->count].destination_code = destination_kind;
	assignments->items[assignments->count].group = group;
	assignments->items[assignments->count].order = order;
	assignments->count++;
}

void pigen_add_assignment(pigen_assignments *assignments, const char *destination, size_t destination_length, const char *expression, size_t expression_length, const char *guard, size_t guard_length, const char *domain, size_t domain_length, char destination_kind, size_t order)
{
	pigen_add_assignment_in_group(assignments, destination, destination_length, expression, expression_length,
		guard, guard_length, domain, domain_length, destination_kind, assignments->next_group++, order);
}

void pigen_add_width_check(pigen_width_checks *checks, const char *lhs, size_t lhs_length,
	const char *rhs, size_t rhs_length, size_t group)
{
	if (checks->count == checks->capacity)
	{
		checks->capacity = checks->capacity ? checks->capacity * 2 : 16;
		checks->items = pigen_resize(checks->items,
			checks->capacity * sizeof(*checks->items));
	}
	checks->items[checks->count].lhs = pigen_copy_range(lhs, lhs_length);
	checks->items[checks->count].rhs = pigen_copy_range(rhs, rhs_length);
	checks->items[checks->count].group = group;
	checks->count++;
}

void pigen_add_clear(pigen_clears *clears, const char *target, size_t target_length,
			     const char *guard, size_t guard_length, const char *domain, size_t domain_length, int action_code, size_t order)
{
	if (clears->count == clears->capacity)
	{
		clears->capacity = clears->capacity ? clears->capacity * 2 : 8;
		clears->items = pigen_resize(clears->items, clears->capacity * sizeof(*clears->items));
	}

	clears->items[clears->count].target = pigen_copy_range(target, target_length);
	clears->items[clears->count].guard = pigen_copy_range(guard, guard_length);
	clears->items[clears->count].domain = pigen_copy_range(domain, domain_length);
	clears->items[clears->count].is_flush = action_code;
	clears->items[clears->count].order = order;
	clears->count++;
}
