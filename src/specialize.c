#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/lexer.h"
#include "pigen/specialize.h"
#include "pigen/util.h"

typedef struct {
	char *name;
	char *expr;
	char *raw_default;
} spec_parameter;

typedef struct {
	char *name;
	int buffered;
} spec_buffered_port;

typedef struct {
	size_t module_token;
	size_t name_token;
	size_t parameter_hash_token;
	size_t parameter_close_token;
	size_t endmodule_token;
	int pigen;
	spec_parameter *parameters;
	size_t parameter_count;
	spec_buffered_port *buffered_ports;
	size_t buffered_port_count;
} spec_module;

typedef struct {
	size_t start;
	size_t end;
	char *replacement;
} spec_replacement;

static int token_is_text(const char *source, const pigen_token *token,
	const char *text)
{
	return pigen_token_is(source, token, text);
}

static int token_is_identifier(const pigen_token *token)
{
	return token->kind == PIGEN_TOKEN_IDENTIFIER;
}

static char *token_text(const char *source, const pigen_token *token)
{
	return pigen_copy_range(source + token->span.start,
		token->span.end - token->span.start);
}

static int text_equals(const char *left, const char *right)
{
	return !strcmp(left, right);
}

static size_t find_matching(const char *source, const pigen_tokens *tokens,
	size_t open, const char *open_text, const char *close_text, size_t limit)
{
	size_t at;
	size_t depth = 0;

	for (at = open; at < limit; at++)
	{
		if (token_is_text(source, &tokens->items[at], open_text)) depth++;
		else if (token_is_text(source, &tokens->items[at], close_text) && !--depth)
			return at;
	}
	return limit;
}

static int module_has_pigen(const char *source, const pigen_tokens *tokens,
	size_t first, size_t last)
{
	static const char *const words[] = {
		"buf", "port", "fifo", "skid", "pipeline", "fsm", "transfer",
		"validate", "invalidate", "flush", "peek", "pipeline_reset",
	};

	for (size_t at = first; at < last; at++)
		for (size_t k = 0; k < sizeof words / sizeof *words; k++)
			if (token_is_text(source, &tokens->items[at], words[k])) return 1;
	return 0;
}

static char *build_tokens(const char *source, const pigen_tokens *tokens,
	size_t first, size_t last, const spec_parameter *parameters,
	size_t parameter_count)
{
	pigen_string out = {0};
	size_t at;

	for (at = first; at < last; at++)
	{
		const pigen_token *token = &tokens->items[at];
		const char *text = source + token->span.start;
		size_t length = token->span.end - token->span.start;
		int replaced = 0;

		if (at > first) pigen_append(&out, " ");
		if (token_is_identifier(token))
			for (size_t p = 0; p < parameter_count; p++)
				if (parameters[p].name && parameters[p].expr &&
					pigen_is_word(text, length, parameters[p].name))
				{
					pigen_append(&out, parameters[p].expr);
					replaced = 1;
					break;
				}
		if (!replaced) pigen_append_range(&out, text, length);
	}
	return out.data ? out.data : pigen_copy_range("", 0);
}

static void free_parameters(spec_parameter *parameters, size_t count)
{
	for (size_t i = 0; i < count; i++)
	{
		free(parameters[i].name);
		free(parameters[i].expr);
		free(parameters[i].raw_default);
	}
	free(parameters);
}

static int parse_module_parameters(const char *source, const pigen_tokens *tokens,
	spec_module *module)
{
	size_t at = module->parameter_hash_token + 2;
	size_t limit = module->parameter_close_token;
	size_t count = 0;
	size_t capacity = 0;
	spec_parameter *parameters = NULL;

	while (at < limit)
	{
		size_t end = at;
		size_t equals = limit;
		size_t name_token = limit;
		size_t p;

		while (end < limit && !token_is_text(source, &tokens->items[end], ","))
			end++;
		for (p = at; p < end; p++)
			if (token_is_text(source, &tokens->items[p], "="))
			{
				equals = p;
				break;
			}
		if (equals < end)
			for (p = equals; p > at; p--)
				if (token_is_identifier(&tokens->items[p - 1]))
				{
					name_token = p - 1;
					break;
				}
		if (name_token < end)
		{
			char *name = token_text(source, &tokens->items[name_token]);
			char *expr = build_tokens(source, tokens, equals + 1, end,
				parameters, count);
			char *raw = build_tokens(source, tokens, equals + 1, end, NULL, 0);
			int exists = 0;

			for (size_t k = 0; k < count; k++)
				if (text_equals(parameters[k].name, name)) exists = 1;
			if (!exists)
			{
				if (count == capacity)
				{
					capacity = capacity ? capacity * 2 : 4;
					parameters = pigen_resize(parameters,
						capacity * sizeof(*parameters));
				}
				parameters[count++] = (spec_parameter){name, expr, raw};
			}
			else
			{
				free(name);
				free(expr);
				free(raw);
			}
		}
		at = end + 1;
	}
	module->parameters = parameters;
	module->parameter_count = count;
	return 1;
}

static int parse_buffered_ports(const char *source, const pigen_tokens *tokens,
	spec_module *module, size_t port_open, size_t port_close)
{
	static const char *const transfers[] = {"buf", "port", "fifo", "skid"};
	size_t at = port_open + 1;

	while (at < port_close)
	{
		size_t end = at;
		size_t inner = 0;
		int buffered = 0;
		size_t name = port_close;

		while (end < port_close)
		{
			if (token_is_text(source, &tokens->items[end], "(") ||
				token_is_text(source, &tokens->items[end], "["))
				inner++;
			else if (token_is_text(source, &tokens->items[end], ")") ||
				token_is_text(source, &tokens->items[end], "]"))
			{
				if (inner) inner--;
			}
			else if (!inner && token_is_text(source, &tokens->items[end], ","))
				break;
			end++;
		}
		for (size_t k = at; k < end; k++)
			for (size_t t = 0; t < sizeof transfers / sizeof *transfers; t++)
				if (token_is_text(source, &tokens->items[k], transfers[t]))
					buffered = 1;
		for (size_t k = end; k > at; k--)
			if (token_is_identifier(&tokens->items[k - 1]))
			{
				name = k - 1;
				break;
			}
		if (name < end)
		{
			module->buffered_ports = pigen_resize(module->buffered_ports,
				(module->buffered_port_count + 1) *
				sizeof(*module->buffered_ports));
			module->buffered_ports[module->buffered_port_count++] =
				(spec_buffered_port){token_text(source,
					&tokens->items[name]), buffered};
		}
		at = end + 1;
	}
	return 1;
}


static void parse_module_localparams(const char *source,
	const pigen_tokens *tokens, spec_module *module, size_t first, size_t last)
{
	for (size_t at = first; at < last; at++)
	{
		size_t semi, equals, name;
		char *parameter_name, *expr;
		int exists = 0;

		if (!token_is_text(source, &tokens->items[at], "localparam")) continue;
		for (semi = at + 1; semi < last; semi++)
			if (token_is_text(source, &tokens->items[semi], ";")) break;
		if (semi == last) break;
		for (equals = at + 1; equals < semi; equals++)
			if (token_is_text(source, &tokens->items[equals], "=")) break;
		if (equals == semi) { at = semi; continue; }
		for (name = equals; name > at; name--)
			if (token_is_identifier(&tokens->items[name - 1])) break;
		if (name <= at) { at = semi; continue; }
		parameter_name = token_text(source, &tokens->items[name - 1]);
		for (size_t p = 0; p < module->parameter_count; p++)
			if (text_equals(module->parameters[p].name, parameter_name))
				exists = 1;
		if (exists) free(parameter_name);
		else
		{
			expr = build_tokens(source, tokens, equals + 1, semi,
				module->parameters, module->parameter_count);
			{
				char *raw = build_tokens(source, tokens, equals + 1, semi,
					NULL, 0);
				module->parameters = pigen_resize(module->parameters,
					(module->parameter_count + 1) *
					sizeof(*module->parameters));
				module->parameters[module->parameter_count++] =
					(spec_parameter){parameter_name, expr, raw};
			}
		}
		at = semi;
	}
}

static int collect_modules(const char *source, const pigen_tokens *tokens,
	spec_module **modules, size_t *module_count)
{
	spec_module *out = NULL;
	size_t count = 0;
	size_t capacity = 0;

	for (size_t at = 0; at + 1 < tokens->count; at++)
	{
		size_t header_end, endmodule, paren_depth = 0;
		spec_module module = {0};
		size_t name = at + 1;

		if (!token_is_text(source, &tokens->items[at], "module") ||
			!token_is_identifier(&tokens->items[name])) continue;
		for (header_end = name + 1; header_end < tokens->count; header_end++)
		{
			if (token_is_text(source, &tokens->items[header_end], "("))
				paren_depth++;
			else if (token_is_text(source, &tokens->items[header_end], ")") &&
				paren_depth) paren_depth--;
			else if (!paren_depth &&
				token_is_text(source, &tokens->items[header_end], ";")) break;
		}
		if (header_end == tokens->count) continue;
		for (endmodule = header_end + 1; endmodule < tokens->count; endmodule++)
			if (token_is_text(source, &tokens->items[endmodule], "endmodule"))
				break;
		if (endmodule == tokens->count) continue;
		{
			size_t port_open = name + 1;
			int parameterized = name + 2 < header_end &&
				token_is_text(source, &tokens->items[name + 1], "#") &&
				token_is_text(source, &tokens->items[name + 2], "(");
			int pigen = module_has_pigen(source, tokens, at, endmodule);

			if (!parameterized && !pigen) continue;
			module.module_token = at;
			module.name_token = name;
			module.endmodule_token = endmodule;
			module.pigen = pigen;
			if (parameterized)
			{
				size_t close = find_matching(source, tokens, name + 2, "(", ")",
					header_end);

				if (close == header_end) continue;
				module.parameter_hash_token = name + 1;
				module.parameter_close_token = close;
				port_open = close + 1;
				parse_module_parameters(source, tokens, &module);
			}
			if (module.pigen &&
				token_is_text(source, &tokens->items[port_open], "("))
			{
				size_t port_close = find_matching(source, tokens, port_open,
					"(", ")", header_end);

				if (port_close < header_end)
					parse_buffered_ports(source, tokens, &module, port_open,
						port_close);
			}
			parse_module_localparams(source, tokens, &module, header_end + 1,
				endmodule);
			if (count == capacity)
			{
				capacity = capacity ? capacity * 2 : 4;
				out = pigen_resize(out, capacity * sizeof(*out));
			}
			out[count++] = module;
			at = endmodule;
		}
	}
	*modules = out;
	*module_count = count;
	return 1;
}

static char *build_specialized_module(const char *source,
	const pigen_tokens *tokens, const spec_module *module,
	const spec_parameter *values, size_t value_count, const char *clone_name)
{
	pigen_string out = {0};
	size_t last = tokens->items[module->module_token].span.start;
	size_t skip_first = tokens->items[module->parameter_hash_token].span.start;
	size_t skip_last = tokens->items[module->parameter_close_token].span.end;
	size_t at;

	for (at = module->module_token; at <= module->endmodule_token; at++)
	{
		const pigen_token *token = &tokens->items[at];
		const char *text = source + token->span.start;
		size_t length = token->span.end - token->span.start;
		int replaced = 0;

		if (token->span.start >= skip_first && token->span.end <= skip_last)
		{
			last = token->span.end;
			continue;
		}
		pigen_append_range(&out, source + last, token->span.start - last);
		if (at == module->name_token)
		{
			pigen_append(&out, clone_name);
			replaced = 1;
		}
		else if (token_is_identifier(token))
		{
			for (size_t p = 0; p < value_count; p++)
				if (values[p].name && values[p].expr &&
					pigen_is_word(text, length, values[p].name))
				{
					pigen_append(&out, values[p].expr);
					replaced = 1;
					break;
				}
		}
		if (!replaced) pigen_append_range(&out, text, length);
		last = token->span.end;
	}
	pigen_append_range(&out, source + last,
		tokens->items[module->endmodule_token].span.end - last);
	return out.data ? out.data : pigen_copy_range("", 0);
}

static int parse_instance_overrides(const char *source,
	const pigen_tokens *tokens, size_t first, size_t last,
	const spec_parameter *parent_parameters, size_t parent_parameter_count,
	spec_parameter **overrides, size_t *override_count)
{
	spec_parameter *out = NULL;
	size_t count = 0;
	size_t capacity = 0;
	size_t at = first;

	while (at < last)
	{
		size_t end = at;
		char *name = NULL;
		char *expr = NULL;

		while (end < last &&
			!token_is_text(source, &tokens->items[end], ",")) end++;
		if (at + 3 <= end && token_is_text(source, &tokens->items[at], ".") &&
			token_is_identifier(&tokens->items[at + 1]) &&
			token_is_text(source, &tokens->items[at + 2], "("))
		{
			size_t close = find_matching(source, tokens, at + 2, "(", ")", end);

			name = token_text(source, &tokens->items[at + 1]);
			expr = build_tokens(source, tokens, at + 3, close,
				parent_parameters, parent_parameter_count);
		}
		else
			expr = build_tokens(source, tokens, at, end,
				parent_parameters, parent_parameter_count);
		if (count == capacity)
		{
			capacity = capacity ? capacity * 2 : 4;
			out = pigen_resize(out, capacity * sizeof(*out));
		}
		out[count++] = (spec_parameter){name, expr, NULL};
		at = end + 1;
	}
	*overrides = out;
	*override_count = count;
	return 1;
}

static spec_module *module_containing(spec_module *modules, size_t count,
	size_t token)
{
	spec_module *best = NULL;

	for (size_t i = 0; i < count; i++)
		if (modules[i].pigen || modules[i].parameter_count)
			if (token >= modules[i].module_token &&
				token <= modules[i].endmodule_token)
				if (!best || modules[i].module_token > best->module_token)
					best = &modules[i];
	return best;
}

static int buffered_port(const spec_module *module, const char *name)
{
	for (size_t i = 0; i < module->buffered_port_count; i++)
		if (module->buffered_ports[i].buffered &&
			text_equals(module->buffered_ports[i].name, name)) return 1;
	return 0;
}

static int connection_has_port(const char *source,
	const pigen_tokens *tokens, size_t open, size_t close, const char *name)
{
	for (size_t at = open + 1; at + 2 < close; at++)
		if (token_is_text(source, &tokens->items[at], ".") &&
			token_is_identifier(&tokens->items[at + 1]) &&
			token_is_text(source, &tokens->items[at + 2], "("))
		{
			const pigen_token *port = &tokens->items[at + 1];

			if (pigen_is_word(source + port->span.start,
				port->span.end - port->span.start, name))
				return 1;
		}
	return 0;
}

static char *build_instance_text(const char *source, const pigen_tokens *tokens,
	const spec_module *module, const char *clone_name,
	size_t instance_token, size_t open, size_t close)
{
	pigen_string out = {0};
	size_t at = open + 1;
	size_t positional_index = 0;

	pigen_append(&out, clone_name);
	pigen_append(&out, " ");
	pigen_append_range(&out, source + tokens->items[instance_token].span.start,
		tokens->items[instance_token].span.end -
		tokens->items[instance_token].span.start);
	pigen_append(&out, " (");
	while (at < close)
	{
		size_t end = at;
		int first = at == open + 1;

		while (end < close && !token_is_text(source, &tokens->items[end], ","))
			end++;
		if (!first) pigen_append(&out, ", ");
		if (at + 3 < end && token_is_text(source, &tokens->items[at], ".") &&
			token_is_identifier(&tokens->items[at + 1]) &&
			token_is_text(source, &tokens->items[at + 2], "("))
		{
			char *port = token_text(source, &tokens->items[at + 1]);
			size_t expr_last = end;

			if (expr_last > at + 3 &&
				token_is_text(source, &tokens->items[expr_last - 1], ")"))
				expr_last--;
			pigen_append(&out, ".");
			pigen_append(&out, port);
			pigen_append(&out, "(");
			pigen_append_range(&out,
				source + tokens->items[at + 3].span.start,
				tokens->items[expr_last - 1].span.end -
				tokens->items[at + 3].span.start);
			pigen_append(&out, ")");
			if (buffered_port(module, port) && expr_last == at + 4 &&
				token_is_identifier(&tokens->items[at + 3]))
			{
				char *base = token_text(source, &tokens->items[at + 3]);
				char valid_name[256], ready_name[256];

				snprintf(valid_name, sizeof valid_name, "%s_valid", port);
				snprintf(ready_name, sizeof ready_name, "%s_ready", port);
				if (!connection_has_port(source, tokens, open, close,
					valid_name))
				{
					pigen_append(&out, ", .");
					pigen_append(&out, valid_name);
					pigen_append(&out, "(");
					pigen_append(&out, base);
					pigen_append(&out, "_valid)");
				}
				if (!connection_has_port(source, tokens, open, close,
					ready_name))
				{
					pigen_append(&out, ", .");
					pigen_append(&out, ready_name);
					pigen_append(&out, "(");
					pigen_append(&out, base);
					pigen_append(&out, "_ready)");
				}
				free(base);
			}
			free(port);
		}
		else
		{
			const spec_buffered_port *port =
				positional_index < module->buffered_port_count ?
				&module->buffered_ports[positional_index] : NULL;

			pigen_append(&out, ".");
			if (port) pigen_append(&out, port->name);
			pigen_append(&out, "(");
			pigen_append_range(&out, source + tokens->items[at].span.start,
				tokens->items[end - 1].span.end -
				tokens->items[at].span.start);
			pigen_append(&out, ")");
			if (port && port->buffered && end == at + 1 &&
				token_is_identifier(&tokens->items[at]))
			{
				char *base = token_text(source, &tokens->items[at]);

				pigen_append(&out, ", .");
				pigen_append(&out, port->name);
				pigen_append(&out, "_valid(");
				pigen_append(&out, base);
				pigen_append(&out, "_valid), .");
				pigen_append(&out, port->name);
				pigen_append(&out, "_ready(");
				pigen_append(&out, base);
				pigen_append(&out, "_ready)");
				free(base);
			}
			positional_index++;
		}
		at = end + 1;
	}
	pigen_append(&out, ");");
	return out.data ? out.data : pigen_copy_range("", 0);
}

typedef struct {
	char **names;
	size_t count;
	size_t capacity;
} spec_function_names;

static int has_function_name(const spec_function_names *functions,
	const char *name)
{
	for (size_t i = 0; i < functions->count; i++)
		if (text_equals(functions->names[i], name)) return 1;
	return 0;
}

static void collect_function_names(const char *source,
	const pigen_tokens *tokens, spec_function_names *functions)
{
	for (size_t i = 0; i + 2 < tokens->count; i++)
	{
		size_t open, name;

		if (!token_is_text(source, &tokens->items[i], "function")) continue;
		for (open = i + 1; open < tokens->count; open++)
			if (token_is_text(source, &tokens->items[open], "(") ||
				token_is_text(source, &tokens->items[open], ";")) break;
		if (open == tokens->count) continue;
		for (name = open; name > i; name--)
			if (token_is_identifier(&tokens->items[name - 1])) break;
		if (name <= i) continue;
		if (functions->count == functions->capacity)
		{
			functions->capacity = functions->capacity ?
				functions->capacity * 2 : 8;
			functions->names = pigen_resize(functions->names,
				functions->capacity * sizeof(*functions->names));
		}
		functions->names[functions->count++] = token_text(source,
			&tokens->items[name - 1]);
	}
}

static void collect_task_names(const char *source,
	const pigen_tokens *tokens, spec_function_names *functions)
{
	for (size_t i = 0; i + 2 < tokens->count; i++)
	{
		size_t open, name;

		if (!token_is_text(source, &tokens->items[i], "task")) continue;
		for (open = i + 1; open < tokens->count; open++)
			if (token_is_text(source, &tokens->items[open], "(") ||
				token_is_text(source, &tokens->items[open], ";")) break;
		if (open == tokens->count) continue;
		for (name = open; name > i; name--)
			if (token_is_identifier(&tokens->items[name - 1])) break;
		if (name <= i) continue;
		if (functions->count == functions->capacity)
		{
			functions->capacity = functions->capacity ?
				functions->capacity * 2 : 8;
			functions->names = pigen_resize(functions->names,
				functions->capacity * sizeof(*functions->names));
		}
		functions->names[functions->count++] = token_text(source,
			&tokens->items[name - 1]);
	}
}

static int statement_has_function(const char *source, const pigen_tokens *tokens,
	const spec_function_names *functions, size_t first, size_t last)
{
	for (size_t i = first; i + 1 < last; i++)
	{
		char *name;

		if (!token_is_identifier(&tokens->items[i]) ||
			(!token_is_text(source, &tokens->items[i + 1], "(") && i != first))
			continue;
		name = token_text(source, &tokens->items[i]);
		{
			int found = has_function_name(functions, name);

			free(name);
			if (found) return 1;
		}
	}
	return 0;
}

static int statement_is_pigen(const char *source, const pigen_tokens *tokens,
	const spec_module *module, size_t first, size_t last)
{
	static const char *const words[] = {
		"buf", "port", "fifo", "skid", "pipeline", "fsm", "transfer",
		"validate", "invalidate", "flush", "peek", "pipeline_reset",
	};

	for (size_t i = first; i < last; i++)
	{
		for (size_t k = 0; k < sizeof words / sizeof *words; k++)
			if (token_is_text(source, &tokens->items[i], words[k])) return 1;
	}
	if (module)
	{
		for (size_t i = first; i + 1 < last; i++)
			if (token_is_identifier(&tokens->items[i]) &&
				token_is_text(source, &tokens->items[i + 1], "<="))
			{
				char *name = token_text(source, &tokens->items[i]);
				int found = buffered_port(module, name);

				free(name);
				if (found) return 1;
			}
	}
	return 0;
}

typedef struct {
	size_t first;
	size_t last;
	int function;
	int task;
	int pigen;
} spec_leaf_statement;

static char *statement_lhs_name(const char *source,
	const pigen_tokens *tokens, size_t first, size_t last)
{
	size_t assign = last;
	size_t lhs = last;

	for (size_t i = first; i < last; i++)
		if (token_is_text(source, &tokens->items[i], "<=") ||
			token_is_text(source, &tokens->items[i], "="))
		{
			assign = i;
			break;
		}
	if (assign == last) return NULL;
	for (size_t i = first; i < assign; i++)
		if (token_is_identifier(&tokens->items[i])) lhs = i;
	return lhs < assign ? token_text(source, &tokens->items[lhs]) : NULL;
}

static int statement_is_nba(const char *source, const pigen_tokens *tokens,
	size_t first, size_t last)
{
	for (size_t i = first; i < last; i++)
		if (token_is_text(source, &tokens->items[i], "<=")) return 1;
	return 0;
}

static int span_has_identifier(const char *source, const pigen_tokens *tokens,
	size_t first, size_t last, const char *name)
{
	for (size_t i = first; i < last; i++)
		if (token_is_identifier(&tokens->items[i]) &&
			pigen_is_word(source + tokens->items[i].span.start,
				tokens->items[i].span.end - tokens->items[i].span.start,
				name))
			return 1;
	return 0;
}


typedef struct {
	char *name;
	char *expression;
} spec_substitution;

static int matching_substitution(const spec_substitution *substitutions,
	size_t count, const char *source, const pigen_token *token)
{
	if (token->kind != PIGEN_TOKEN_IDENTIFIER) return -1;
	for (size_t i = 0; i < count; i++)
		if (pigen_is_word(source + token->span.start,
			token->span.end - token->span.start, substitutions[i].name))
			return (int)i;
	return -1;
}

static char *block_statement_rhs(const char *source,
	const pigen_tokens *tokens, size_t first, size_t last)
{
	size_t equals = last;

	for (size_t i = first; i < last; i++)
		if (token_is_text(source, &tokens->items[i], "="))
		{
			equals = i;
			break;
		}
	if (equals == last || equals + 1 >= last - 1) return NULL;
	return build_tokens(source, tokens, equals + 1, last - 1, NULL, 0);
}

static char *block_statement_function(const char *source,
	const pigen_tokens *tokens, const spec_function_names *functions,
	size_t first, size_t last)
{
	for (size_t i = first; i + 1 < last; i++)
	{
		char *name;

		if (!token_is_identifier(&tokens->items[i]) ||
			!token_is_text(source, &tokens->items[i + 1], "(")) continue;
		name = token_text(source, &tokens->items[i]);
		if (has_function_name(functions, name)) return name;
		free(name);
	}
	return NULL;
}

static int function_is_pure(const char *source, const pigen_tokens *tokens,
	const char *name)
{
	for (size_t i = 0; i + 2 < tokens->count; i++)
	{
		size_t open, function_name, header_end, endfunction;

		if (!token_is_text(source, &tokens->items[i], "function")) continue;
		for (open = i + 1; open < tokens->count; open++)
			if (token_is_text(source, &tokens->items[open], "(") ||
				token_is_text(source, &tokens->items[open], ";")) break;
		if (open == tokens->count) continue;
		for (function_name = open; function_name > i; function_name--)
			if (token_is_identifier(&tokens->items[function_name - 1])) break;
		if (function_name <= i) continue;
		{
			char *found = token_text(source,
				&tokens->items[function_name - 1]);

			if (!text_equals(found, name)) { free(found); continue; }
			free(found);
		}
		for (header_end = open; header_end < tokens->count; header_end++)
			if (token_is_text(source, &tokens->items[header_end], ";")) break;
		if (header_end == tokens->count) return 0;
		for (endfunction = header_end + 1; endfunction < tokens->count;
			endfunction++)
			if (token_is_text(source, &tokens->items[endfunction],
				"endfunction")) break;
		if (endfunction == tokens->count) return 0;
		for (size_t at = header_end + 1; at < endfunction; at++)
		{
			size_t lhs = endfunction;

			if (!token_is_text(source, &tokens->items[at], "=") &&
				!token_is_text(source, &tokens->items[at], "<=")) continue;
			for (size_t k = at; k > header_end; k--)
				if (token_is_identifier(&tokens->items[k - 1]))
				{
					lhs = k - 1;
					break;
				}
			if (lhs < endfunction)
			{
				char *written = token_text(source, &tokens->items[lhs]);
				int pure = text_equals(written, name);

				free(written);
				if (!pure) return 0;
			}
		}
		return 1;
	}
	return 0;
}

static char *function_return_type(const char *source,
	const pigen_tokens *tokens, const char *name)
{
	for (size_t i = 0; i + 2 < tokens->count; i++)
	{
		size_t open, function_name, first_type;
		pigen_string out = {0};

		if (!token_is_text(source, &tokens->items[i], "function")) continue;
		for (open = i + 1; open < tokens->count; open++)
			if (token_is_text(source, &tokens->items[open], "(") ||
				token_is_text(source, &tokens->items[open], ";")) break;
		if (open == tokens->count) continue;
		for (function_name = open; function_name > i; function_name--)
			if (token_is_identifier(&tokens->items[function_name - 1])) break;
		if (function_name <= i) continue;
		{
			char *found = token_text(source,
				&tokens->items[function_name - 1]);

			if (!text_equals(found, name)) { free(found); continue; }
			free(found);
		}
		first_type = i + 1;
		if (token_is_text(source, &tokens->items[first_type], "automatic") ||
			token_is_text(source, &tokens->items[first_type], "static"))
			first_type++;
		for (size_t at = first_type; at + 1 < function_name; at++)
		{
			if (out.length) pigen_append(&out, " ");
			pigen_append_range(&out,
				source + tokens->items[at].span.start,
				tokens->items[at].span.end - tokens->items[at].span.start);
		}
		return out.data ? out.data : pigen_copy_range("logic", 5);
	}
	return pigen_copy_range("logic", 5);
}

static char *copy_block_with_replacements(const char *source,
	const pigen_tokens *tokens, size_t first, size_t last,
	const spec_leaf_statement *leaves, size_t leaf_count, int keep_function,
	const spec_substitution *substitutions, size_t substitution_count)
{
	pigen_string out = {0};
	size_t block_end = tokens->items[last].span.end;
	size_t cursor = tokens->items[first].span.start;
	size_t leaf_index = 0;

	for (size_t at = first; at <= last; at++)
	{
		const pigen_token *token = &tokens->items[at];
		int substitution;

		if (leaf_index < leaf_count && at == leaves[leaf_index].first &&
			leaves[leaf_index].function != keep_function)
		{
			pigen_append_range(&out, source + cursor,
				token->span.start - cursor);
			cursor = tokens->items[leaves[leaf_index].last - 1].span.end;
			at = leaves[leaf_index].last - 1;
			leaf_index++;
			continue;
		}
		if (leaf_index < leaf_count && at == leaves[leaf_index].first)
			leaf_index++;
		substitution = keep_function ? -1 :
			matching_substitution(substitutions, substitution_count, source,
				token);
		if (substitution >= 0)
		{
			pigen_append_range(&out, source + cursor,
				token->span.start - cursor);
			pigen_append(&out, substitutions[substitution].expression);
			cursor = token->span.end;
		}
	}
	pigen_append_range(&out, source + cursor, block_end - cursor);
	return out.data ? out.data : pigen_copy_range("", 0);
}


static int leaf_starts_control(const char *source,
	const pigen_tokens *tokens, size_t first)
{
	return token_is_text(source, &tokens->items[first], "if") ||
		token_is_text(source, &tokens->items[first], "for") ||
		token_is_text(source, &tokens->items[first], "while") ||
		token_is_text(source, &tokens->items[first], "case") ||
		token_is_text(source, &tokens->items[first], "casex") ||
		token_is_text(source, &tokens->items[first], "casez");
}

static char *leaf_if_condition(const char *source,
	const pigen_tokens *tokens, size_t first, size_t count)
{
	size_t open, close;

	if (!token_is_text(source, &tokens->items[first], "if") ||
		first + 2 >= count ||
		!token_is_text(source, &tokens->items[first + 1], "("))
		return NULL;
	open = first + 1;
	close = find_matching(source, tokens, open, "(", ")", tokens->count);
	if (close == tokens->count) return NULL;
	return build_tokens(source, tokens, open + 1, close, NULL, 0);
}

typedef struct {
	size_t begin_token;
	char *condition;
} guard_condition_entry;

static char *leaf_guard_expression(const char *source,
	const pigen_tokens *tokens, size_t outer_begin, size_t leaf_first,
	size_t leaf_last, int *safe)
{
	guard_condition_entry *entries = NULL;
	size_t entry_count = 0;
	size_t *begins = NULL;
	size_t depth = 0;
	pigen_string out = {0};

	*safe = 1;
	for (size_t at = outer_begin + 1; at < leaf_first; at++)
	{
		if (token_is_text(source, &tokens->items[at], "for") ||
			token_is_text(source, &tokens->items[at], "while") ||
			token_is_text(source, &tokens->items[at], "case") ||
			token_is_text(source, &tokens->items[at], "casex") ||
			token_is_text(source, &tokens->items[at], "casez"))
		{
			*safe = 0;
			goto done;
		}
		if (token_is_text(source, &tokens->items[at], "if") &&
			token_is_text(source, &tokens->items[at + 1], "("))
		{
			size_t close = find_matching(source, tokens, at + 1, "(", ")",
				tokens->count);

			if (close < tokens->count &&
				token_is_text(source, &tokens->items[close + 1], "begin"))
			{
				entries = pigen_resize(entries,
					(entry_count + 1) * sizeof(*entries));
				entries[entry_count].begin_token = close + 1;
				entries[entry_count].condition =
					build_tokens(source, tokens, at + 2, close, NULL, 0);
				entry_count++;
			}
		}
		if (token_is_text(source, &tokens->items[at], "else"))
		{
			char *negated = NULL;

			for (size_t e = 0; e < entry_count; e++)
			{
				size_t begin_end = find_matching(source, tokens,
					entries[e].begin_token, "begin", "end", tokens->count);

				if (begin_end + 1 == at)
				{
					pigen_string text = {0};

					pigen_append(&text, "!(");
					pigen_append(&text, entries[e].condition);
					pigen_append(&text, ")");
					negated = text.data;
					break;
				}
			}
			if (negated && token_is_text(source, &tokens->items[at + 1],
				"begin"))
			{
				entries = pigen_resize(entries,
					(entry_count + 1) * sizeof(*entries));
				entries[entry_count].begin_token = at + 1;
				entries[entry_count].condition = negated;
				entry_count++;
			}
			else
			{
				free(negated);
				*safe = 0;
				goto done;
			}
		}
	}
	for (size_t at = outer_begin + 1; at < leaf_first; at++)
	{
		if (token_is_text(source, &tokens->items[at], "begin"))
		{
			begins = pigen_resize(begins, (depth + 1) * sizeof(*begins));
			begins[depth++] = at;
		}
		else if (token_is_text(source, &tokens->items[at], "end") && depth)
			depth--;
	}
	for (size_t level = 0; level < depth; level++)
		for (size_t e = 0; e < entry_count; e++)
			if (entries[e].begin_token == begins[level])
			{
				if (out.length) pigen_append(&out, " && ");
				pigen_append(&out, "(");
				pigen_append(&out, entries[e].condition);
				pigen_append(&out, ")");
			}
	{
		char *leaf_condition = leaf_if_condition(source, tokens, leaf_first,
			leaf_last);

		if (leaf_condition)
		{
			if (out.length) pigen_append(&out, " && ");
			pigen_append(&out, "(");
			pigen_append(&out, leaf_condition);
			pigen_append(&out, ")");
			free(leaf_condition);
		}
	}
done:
	for (size_t e = 0; e < entry_count; e++) free(entries[e].condition);
	free(entries);
	free(begins);
	if (!*safe)
	{
		if (out.data) free(out.data);
		return NULL;
	}
	return out.data ? out.data : pigen_copy_range("", 0);
}

static void leaf_guard_range(const char *source, const pigen_tokens *tokens,
	size_t outer_begin, size_t leaf_first, size_t *guard_start,
	size_t *guard_end)
{
	size_t *stack = NULL;
	size_t depth = 0;
	size_t capacity = 0;
	size_t top = outer_begin;

	for (size_t at = outer_begin + 1; at < leaf_first; at++)
	{
		if (token_is_text(source, &tokens->items[at], "begin"))
		{
			if (depth == capacity)
			{
				capacity = capacity ? capacity * 2 : 8;
				stack = pigen_resize(stack, capacity * sizeof(*stack));
			}
			stack[depth++] = at;
		}
		else if (token_is_text(source, &tokens->items[at], "end") && depth)
			depth--;
	}
	if (depth) top = stack[depth - 1];
	*guard_start = top;
	*guard_end = find_matching(source, tokens, top, "begin", "end",
		tokens->count);
	free(stack);
}


static int add_mixed_process_splits(const char *source,
	const pigen_tokens *tokens, spec_module *modules, size_t module_count,
	spec_replacement **replacements, size_t *replacement_count,
	const char **error, size_t *error_offset)
{
	spec_function_names functions = {0};
	spec_function_names tasks = {0};
	size_t count = 0;

	collect_function_names(source, tokens, &functions);
	collect_task_names(source, tokens, &tasks);
	for (size_t i = 0; i + 4 < tokens->count; i++)
	{
		size_t event_open, event_close, begin, end, at;
		spec_module *module = module_containing(modules, module_count, i);
		spec_leaf_statement *leaves = NULL;
		size_t leaf_count = 0;
		size_t boundary;
		int parens = 0;
		int has_function = 0, has_task = 0, has_pigen = 0;
		spec_substitution *substitutions = NULL;
		size_t substitution_count = 0;
		pigen_string prologue = {0};
		size_t expression_index = 0;

		if (!(token_is_text(source, &tokens->items[i], "always_ff") ||
			token_is_text(source, &tokens->items[i], "always")) ||
			!token_is_text(source, &tokens->items[i + 1], "@") ||
			!token_is_text(source, &tokens->items[i + 2], "(")) continue;
		event_open = i + 2;
		event_close = find_matching(source, tokens, event_open, "(", ")",
			tokens->count);
		if (event_close == tokens->count ||
			!token_is_text(source, &tokens->items[event_close + 1], "begin"))
			continue;
		begin = event_close + 1;
		end = begin + 1;
		{
			size_t depth = 0;

			for (; end < tokens->count; end++)
			{
				if (token_is_text(source, &tokens->items[end], "begin")) depth++;
				else if (token_is_text(source, &tokens->items[end], "end") &&
					!depth) break;
				else if (token_is_text(source, &tokens->items[end], "end") &&
					depth) depth--;
			}
		}
		if (end == tokens->count) continue;
		boundary = begin + 1;
		for (at = begin + 1; at < end; at++)
		{
			if (token_is_text(source, &tokens->items[at], "(")) parens++;
			else if (token_is_text(source, &tokens->items[at], ")")) parens--;
			if (parens == 0 && token_is_text(source, &tokens->items[at], ";"))
			{
				if (at > boundary)
				{
					spec_leaf_statement leaf = {0};

					leaf.first = boundary;
					leaf.last = at + 1;
					leaf.function = statement_has_function(source, tokens,
						&functions, boundary, at + 1);
					leaf.task = statement_has_function(source, tokens, &tasks,
						boundary, at + 1);
					leaf.pigen = statement_is_pigen(source, tokens, module,
						boundary, at + 1);
					leaves = pigen_resize(leaves,
						(leaf_count + 1) * sizeof(*leaves));
					leaves[leaf_count++] = leaf;
					if (leaf.function) has_function = 1;
					if (leaf.task) has_task = 1;
					if (leaf.pigen) has_pigen = 1;
				}
				boundary = at + 1;
			}
			else if (parens == 0 &&
				(token_is_text(source, &tokens->items[at], "begin") ||
				 token_is_text(source, &tokens->items[at], "end") ||
				 token_is_text(source, &tokens->items[at], "else") ||
				 token_is_text(source, &tokens->items[at], ":")))
				boundary = at + 1;
		}
		if (has_task && has_pigen)
		{
			*error = "mixed clocked body with a task call cannot be split "
				"without changing side-effect ordering";
			*error_offset = tokens->items[boundary].span.start;
			free(leaves);
			if (prologue.data) free(prologue.data);
			for (size_t x = 0; x < substitution_count; x++)
			{ free(substitutions[x].name); free(substitutions[x].expression); }
			free(substitutions);
			for (size_t x = 0; x < functions.count; x++) free(functions.names[x]);
			for (size_t x = 0; x < tasks.count; x++) free(tasks.names[x]);
			free(functions.names);
			free(tasks.names);
			return -1;
		}
		if (has_function && has_pigen)
		{
			char *structured;
			char *retained;
			int unsafe = 0;

			for (size_t f = 0; f < leaf_count && !unsafe; f++)
			{
				char *lhs;
				int nba;

				if (!leaves[f].function) continue;
				lhs = statement_lhs_name(source, tokens, leaves[f].first,
					leaves[f].last);
				if (!lhs)
				{
					*error = "mixed clocked body with a task or bare call "
						"cannot be split without changing ordering";
					*error_offset = tokens->items[leaves[f].first].span.start;
					unsafe = 1;
					break;
				}
				nba = statement_is_nba(source, tokens, leaves[f].first,
					leaves[f].last);
				{
					int substituted = 0;

					if (!nba)
					{
						char *rhs = block_statement_rhs(source, tokens,
							leaves[f].first, leaves[f].last);
						char *called = block_statement_function(source, tokens,
							&functions, leaves[f].first, leaves[f].last);

						if (rhs && called && function_is_pure(source, tokens,
							called))
						{
							size_t guard_start, guard_end;
							int covered = 1;
							int guard_safe = 0;
							char *guard_expression = leaf_guard_expression(
								source, tokens, begin, leaves[f].first,
								leaves[f].last, &guard_safe);

							for (size_t other = 0; other < leaf_count; other++)
							{
								char *other_lhs;

								if (other == f || !leaves[other].function)
									continue;
								other_lhs = statement_lhs_name(source, tokens,
									leaves[other].first, leaves[other].last);
								if (other_lhs && text_equals(lhs, other_lhs))
									covered = 0;
								free(other_lhs);
							}
							leaf_guard_range(source, tokens, begin,
								leaves[f].first, &guard_start, &guard_end);
							for (size_t p = 0; p < leaf_count; p++)
							{
								if (leaves[p].function ||
									!span_has_identifier(source, tokens,
										leaves[p].first, leaves[p].last, lhs))
									continue;
								if (guard_safe)
								{
									if (leaves[p].first < leaves[f].last)
										covered = 0;
								}
								else if (leaves[p].first <= guard_start ||
									leaves[p].first >= guard_end ||
									(leaf_starts_control(source, tokens,
										leaves[p].first) &&
									 guard_start != begin))
									covered = 0;
							}
							if (!covered)
							{
								rhs = NULL;
								goto no_guarded_substitution;
							}
							char helper_name[64];
							char *type = function_return_type(source, tokens,
								called);

							snprintf(helper_name, sizeof helper_name,
								"__pigen_expr_%zu", expression_index++);
							pigen_append(&prologue, "wire ");
							pigen_append(&prologue, type);
							pigen_append(&prologue, " ");
							pigen_append(&prologue, helper_name);
							pigen_append(&prologue, "; assign ");
							pigen_append(&prologue, helper_name);
							pigen_append(&prologue, " = ");
							if (guard_expression && guard_expression[0])
							{
								pigen_append(&prologue, guard_expression);
								pigen_append(&prologue, " ? ");
								pigen_append(&prologue, rhs);
								pigen_append(&prologue, " : ");
								pigen_append(&prologue, lhs);
							}
							else pigen_append(&prologue, rhs);
							free(guard_expression);
							pigen_append(&prologue, ";\n");
							substitutions = pigen_resize(substitutions,
								(substitution_count + 1) *
								sizeof(*substitutions));
							substitutions[substitution_count++] =
								(spec_substitution){lhs,
									pigen_copy_range(helper_name,
										strlen(helper_name))};
							lhs = NULL;
							substituted = 1;
							free(type);
						}
					no_guarded_substitution:
						free(rhs);
						free(called);
					}
					if (!substituted)
						for (size_t p = 0; p < leaf_count; p++)
						{
							char *pigen_lhs;

							if (leaves[p].function) continue;
							pigen_lhs = statement_lhs_name(source, tokens,
								leaves[p].first, leaves[p].last);
							if (pigen_lhs && text_equals(lhs, pigen_lhs))
							{
								free(pigen_lhs);
								*error = "a function statement and a Pigen statement "
									"write the same destination; preserving order is "
									"unsupported";
								*error_offset =
									tokens->items[leaves[p].first].span.start;
								unsafe = 1;
								break;
							}
							free(pigen_lhs);
							if (!nba && span_has_identifier(source, tokens,
								leaves[p].first, leaves[p].last, lhs))
							{
								*error = "mixed clocked body reads a value written by "
									"a blocking function statement; preserving order is "
									"unsupported";
								*error_offset = tokens->items[leaves[p].first].span.start;
								unsafe = 1;
								break;
							}
						}
				}
				free(lhs);
			}
			for (size_t p = 0; p < leaf_count && !unsafe; p++)
			{
				char *lhs;

				if (leaves[p].function) continue;
				lhs = statement_lhs_name(source, tokens, leaves[p].first,
					leaves[p].last);
				if (!lhs) continue;
				for (size_t f = 0; f < leaf_count; f++)
					if (leaves[f].function &&
						!statement_is_nba(source, tokens, leaves[f].first,
							leaves[f].last) &&
						span_has_identifier(source, tokens, leaves[f].first,
							leaves[f].last, lhs))
					{
						*error = "a blocking function statement reads a value "
							"written by a Pigen statement; preserving order is "
							"unsupported";
						*error_offset = tokens->items[leaves[f].first].span.start;
						unsafe = 1;
						break;
					}
				free(lhs);
			}
			if (unsafe)
			{
				free(leaves);
				if (prologue.data) free(prologue.data);
				for (size_t x = 0; x < substitution_count; x++)
				{ free(substitutions[x].name); free(substitutions[x].expression); }
				free(substitutions);
				for (size_t x = 0; x < functions.count; x++) free(functions.names[x]);
				for (size_t x = 0; x < tasks.count; x++) free(tasks.names[x]);
				free(functions.names);
				free(tasks.names);
				return -1;
			}
			structured = copy_block_with_replacements(source, tokens, i,
				end, leaves, leaf_count, 0, substitutions, substitution_count);
			retained = copy_block_with_replacements(source, tokens, i,
				end, leaves, leaf_count, 1, NULL, 0);
			pigen_string text = {0};
			spec_replacement *grown;

			if (prologue.length) pigen_append(&text, prologue.data);
			pigen_append(&text, structured);
			pigen_append(&text, "\n");
			pigen_append(&text, retained);
			free(structured);
			free(retained);
			grown = pigen_resize(*replacements,
				(*replacement_count + 1) * sizeof(**replacements));
			*replacements = grown;
			(*replacements)[(*replacement_count)++] = (spec_replacement){
				tokens->items[i].span.start, tokens->items[end].span.end,
				text.data};
			count++;
			i = end;
		}
		free(leaves);
		if (prologue.data) free(prologue.data);
		for (size_t x = 0; x < substitution_count; x++)
		{
			free(substitutions[x].name);
			free(substitutions[x].expression);
		}
		free(substitutions);
	}
	for (size_t i = 0; i < functions.count; i++) free(functions.names[i]);
	for (size_t i = 0; i < tasks.count; i++) free(tasks.names[i]);
	free(functions.names);
	free(tasks.names);
	return count;
}

static char *substitute_text_parameters(const char *text,
	const spec_parameter *values, size_t value_count, size_t skip)
{
	pigen_string out = {0};
	size_t at = 0;

	while (text && text[at])
	{
		size_t start = at;

		if (!pigen_is_identifier_char((unsigned char)text[at]))
		{
			pigen_append_range(&out, text + at, 1);
			at++;
			continue;
		}
		while (text[at] && pigen_is_identifier_char((unsigned char)text[at]))
			at++;
		{
			size_t length = at - start;
			int substituted = 0;

			for (size_t p = 0; p < value_count; p++)
				if (p != skip && values[p].name && values[p].expr &&
					strlen(values[p].name) == length &&
					!memcmp(values[p].name, text + start, length))
				{
					pigen_append(&out, values[p].expr);
					substituted = 1;
					break;
				}
			if (!substituted) pigen_append_range(&out, text + start, length);
		}
	}
	return out.data ? out.data : pigen_copy_range("", 0);
}

char *pigen_specialize_parameterized_instances(const char *source, size_t length,
	const char **error, size_t *error_offset)
{
	pigen_tokens tokens = {0};
	spec_module *modules = NULL;
	size_t module_count = 0;
	spec_replacement *replacements = NULL;
	size_t replacement_count = 0;
	size_t replacement_capacity = 0;
	pigen_string clones = {0};
	pigen_string output = {0};
	size_t cursor = 0;
	size_t clone_index = 0;
	int changed = 0;

	if (!source || !length) return NULL;
	if (error) *error = NULL;
	if (error_offset) *error_offset = 0;
	pigen_lex_source(source, length, &tokens);
	collect_modules(source, &tokens, &modules, &module_count);
	{
		int split = add_mixed_process_splits(source, &tokens, modules,
			module_count, &replacements, &replacement_count, error,
			error_offset);

		if (split < 0)
		{
			for (size_t i = 0; i < replacement_count; i++)
				free(replacements[i].replacement);
			free(replacements);
			for (size_t i = 0; i < module_count; i++)
			{
				for (size_t p = 0; p < modules[i].buffered_port_count; p++)
					free(modules[i].buffered_ports[p].name);
				free(modules[i].buffered_ports);
				free_parameters(modules[i].parameters,
					modules[i].parameter_count);
			}
			free(modules);
			pigen_free_tokens(&tokens);
			return NULL;
		}
		changed = split > 0;
	}
	for (size_t i = 0; i < tokens.count; i++)
	{
		const pigen_token *token = &tokens.items[i];
		char *module_name;
		spec_module *module = NULL;
		size_t close, instance_close, semicolon;
		spec_parameter *overrides = NULL;
		size_t override_count = 0;
		spec_parameter *values = NULL;
		size_t value_count = 0;
		char *clone_name = NULL;
		spec_module *parent = module_containing(modules, module_count, i);

		if (token_is_identifier(token) && i + 3 < tokens.count &&
			!token_is_text(source, token, "module") &&
			token_is_identifier(&tokens.items[i + 1]) &&
			token_is_text(source, &tokens.items[i + 2], "("))
		{
			module_name = token_text(source, token);
			for (size_t m = 0; m < module_count; m++)
			{
				char *candidate = token_text(source,
					&tokens.items[modules[m].name_token]);

				if (modules[m].pigen && modules[m].buffered_port_count &&
					text_equals(module_name, candidate))
					module = &modules[m];
				free(candidate);
				if (module) break;
			}
			if (module)
			{
				size_t open = i + 2;
				size_t close_plain = find_matching(source, &tokens, open,
					"(", ")", tokens.count);

				if (close_plain < tokens.count - 1 &&
					token_is_text(source, &tokens.items[close_plain + 1], ";"))
				{
					char *plain_name = token_text(source,
						&tokens.items[module->name_token]);
					char *instance_text = build_instance_text(source, &tokens,
						module, plain_name, i + 1, open, close_plain);
					spec_replacement *grown = pigen_resize(replacements,
						(replacement_count + 1) * sizeof(*replacements));

					replacements = grown;
					replacements[replacement_count++] = (spec_replacement){
						token->span.start,
						tokens.items[close_plain + 1].span.end,
						instance_text};
					free(plain_name);
					free(module_name);
					changed = 1;
					i = close_plain + 1;
					continue;
				}
			}
			free(module_name);
		}
		if (!token_is_identifier(token) || i + 4 >= tokens.count ||
			!token_is_text(source, &tokens.items[i + 1], "#") ||
			!token_is_text(source, &tokens.items[i + 2], "("))
			continue;
		close = find_matching(source, &tokens, i + 2, "(", ")", tokens.count);
		if (close == tokens.count || close + 3 >= tokens.count ||
			!token_is_identifier(&tokens.items[close + 1]) ||
			!token_is_text(source, &tokens.items[close + 2], "("))
			continue;
		instance_close = find_matching(source, &tokens, close + 2, "(", ")",
			tokens.count);
		if (instance_close == tokens.count) continue;
		semicolon = instance_close + 1;
		if (semicolon >= tokens.count ||
			!token_is_text(source, &tokens.items[semicolon], ";")) continue;
		module_name = token_text(source, token);
		for (size_t m = 0; m < module_count; m++)
		{
			char *candidate = token_text(source,
				&tokens.items[modules[m].name_token]);

			if (modules[m].pigen && text_equals(module_name, candidate))
				module = &modules[m];
			free(candidate);
			if (module) break;
		}
		if (!module) { free(module_name); continue; }
		free(module_name);
		parse_instance_overrides(source, &tokens, i + 3, close,
			parent ? parent->parameters : NULL,
			parent ? parent->parameter_count : 0,
			&overrides, &override_count);
		for (size_t p = 0; p < module->parameter_count; p++)
		{
			char *value = module->parameters[p].expr ?
				pigen_copy_range(module->parameters[p].expr,
					strlen(module->parameters[p].expr)) : NULL;
			char *raw_default = module->parameters[p].raw_default ?
				pigen_copy_range(module->parameters[p].raw_default,
					strlen(module->parameters[p].raw_default)) : NULL;
			size_t positional = 0;
			int overridden = 0;

			for (size_t o = 0; o < override_count; o++)
			{
				if (overrides[o].name)
				{
					if (text_equals(overrides[o].name,
						module->parameters[p].name))
					{
						free(value);
						value = overrides[o].expr ?
							pigen_copy_range(overrides[o].expr,
								strlen(overrides[o].expr)) : NULL;
						overridden = 1;
					}
				}
				else if (positional == p)
				{
					free(value);
					value = overrides[o].expr ?
						pigen_copy_range(overrides[o].expr,
							strlen(overrides[o].expr)) : NULL;
					overridden = 1;
					positional++;
				}
				else positional++;
			}
			if (overridden)
			{
				free(raw_default);
				raw_default = NULL;
			}
			values = pigen_resize(values, (value_count + 1) * sizeof(*values));
			values[value_count++] = (spec_parameter){
				pigen_copy_range(module->parameters[p].name,
					strlen(module->parameters[p].name)), value, raw_default};
		}
		for (size_t iteration = 0; iteration < value_count; iteration++)
			for (size_t p = 0; p < value_count; p++)
			{
				const char *base = values[p].raw_default ?
					values[p].raw_default : values[p].expr;
				char *substituted = substitute_text_parameters(base, values,
					value_count, p);

				free(values[p].expr);
				values[p].expr = substituted;
			}
		clone_name = pigen_copy_range("", 0);
		free(clone_name);
		clone_name = NULL;
		{
			char number[32];
			pigen_string name = {0};
			snprintf(number, sizeof number, "%zu", clone_index++);
			pigen_append(&name, "__pigen_spec_");
			for (size_t m = 0; m < module_count; m++)
				if (&modules[m] == module)
				{
					char *base = token_text(source,
						&tokens.items[modules[m].name_token]);
					pigen_append(&name, base);
					free(base);
				}
			pigen_append(&name, "_");
			pigen_append(&name, number);
			clone_name = name.data;
		}
		{
			char *specialized = build_specialized_module(source, &tokens, module,
				values, value_count, clone_name);
			pigen_append(&clones, "\n");
			pigen_append(&clones, specialized);
			free(specialized);
		}
		if (replacement_count == replacement_capacity)
		{
			replacement_capacity = replacement_capacity ?
				replacement_capacity * 2 : 8;
			replacements = pigen_resize(replacements,
				replacement_capacity * sizeof(*replacements));
		}
		{
			char *instance_text = build_instance_text(source, &tokens, module,
				clone_name, close + 1, close + 2, instance_close);

			replacements[replacement_count++] = (spec_replacement){
				token->span.start,
				tokens.items[semicolon].span.end,
				instance_text};
		}
		changed = 1;
		free(clone_name);
		free_parameters(overrides, override_count);
		free_parameters(values, value_count);
		i = semicolon;
	}
	if (changed)
	{
		for (size_t i = 0; i + 1 < replacement_count; i++)
			for (size_t j = i + 1; j < replacement_count; j++)
				if (replacements[j].start < replacements[i].start)
				{
					spec_replacement swap = replacements[i];
					replacements[i] = replacements[j];
					replacements[j] = swap;
				}
		if (clones.length)
		{
			pigen_append_range(&output, clones.data, clones.length);
			pigen_append(&output, "\n");
		}
		for (size_t i = 0; i < replacement_count; i++)
		{
			pigen_append_range(&output, source + cursor,
				replacements[i].start - cursor);
			pigen_append(&output, replacements[i].replacement);
			cursor = replacements[i].end;
		}
		pigen_append_range(&output, source + cursor, length - cursor);
	}
	for (size_t i = 0; i < replacement_count; i++)
		free(replacements[i].replacement);
	free(replacements);
	for (size_t i = 0; i < module_count; i++)
	{
		for (size_t p = 0; p < modules[i].buffered_port_count; p++)
			free(modules[i].buffered_ports[p].name);
		free(modules[i].buffered_ports);
		free_parameters(modules[i].parameters, modules[i].parameter_count);
	}
	free(modules);
	if (clones.data) free(clones.data);
	pigen_free_tokens(&tokens);
	return changed ? output.data : NULL;
}
