#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/lexer.h"
#include "pigen/specialize.h"
#include "pigen/util.h"

typedef struct {
	char *name;
	char *expr;
} spec_parameter;

typedef struct {
	char *name;
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
				parameters[count++] = (spec_parameter){name, expr};
			}
			else
			{
				free(name);
				free(expr);
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
		if (buffered)
			for (size_t k = end; k > at; k--)
				if (token_is_identifier(&tokens->items[k - 1]))
				{
					name = k - 1;
					break;
				}
		if (buffered && name < end)
		{
			module->buffered_ports = pigen_resize(module->buffered_ports,
				(module->buffered_port_count + 1) *
				sizeof(*module->buffered_ports));
			module->buffered_ports[module->buffered_port_count++].name =
				token_text(source, &tokens->items[name]);
		}
		at = end + 1;
	}
	return 1;
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
		if (name + 2 < header_end &&
			token_is_text(source, &tokens->items[name + 1], "#") &&
			token_is_text(source, &tokens->items[name + 2], "("))
		{
			size_t close = find_matching(source, tokens, name + 2, "(", ")",
				header_end);

			if (close < header_end)
			{
				module.module_token = at;
				module.name_token = name;
				module.parameter_hash_token = name + 1;
				module.parameter_close_token = close;
				module.endmodule_token = endmodule;
				module.pigen = module_has_pigen(source, tokens, at, endmodule);
				if (module.pigen)
				{
					size_t port_open = close + 1;

					parse_module_parameters(source, tokens, &module);
					if (token_is_text(source, &tokens->items[port_open], "("))
					{
						size_t port_close = find_matching(source, tokens,
							port_open, "(", ")", header_end);

						if (port_close < header_end)
							parse_buffered_ports(source, tokens, &module,
								port_open, port_close);
					}
				}
				if (count == capacity)
				{
					capacity = capacity ? capacity * 2 : 4;
					out = pigen_resize(out, capacity * sizeof(*out));
				}
				out[count++] = module;
				at = endmodule;
			}
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
			expr = build_tokens(source, tokens, at + 3, close, NULL, 0);
		}
		else
			expr = build_tokens(source, tokens, at, end, NULL, 0);
		if (count == capacity)
		{
			capacity = capacity ? capacity * 2 : 4;
			out = pigen_resize(out, capacity * sizeof(*out));
		}
		out[count++] = (spec_parameter){name, expr};
		at = end + 1;
	}
	*overrides = out;
	*override_count = count;
	return 1;
}

static int buffered_port(const spec_module *module, const char *name)
{
	for (size_t i = 0; i < module->buffered_port_count; i++)
		if (text_equals(module->buffered_ports[i].name, name)) return 1;
	return 0;
}

static char *build_instance_text(const char *source, const pigen_tokens *tokens,
	const spec_module *module, const char *clone_name,
	size_t instance_token, size_t open, size_t close)
{
	pigen_string out = {0};
	size_t at = open + 1;

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

				pigen_append(&out, ", .");
				pigen_append(&out, port);
				pigen_append(&out, "_valid(");
				pigen_append(&out, base);
				pigen_append(&out, "_valid), .");
				pigen_append(&out, port);
				pigen_append(&out, "_ready(");
				pigen_append(&out, base);
				pigen_append(&out, "_ready)");
				free(base);
			}
			free(port);
		}
		else
			pigen_append_range(&out, source + tokens->items[at].span.start,
				tokens->items[end - 1].span.end -
				tokens->items[at].span.start);
		at = end + 1;
	}
	pigen_append(&out, ");");
	return out.data ? out.data : pigen_copy_range("", 0);
}

char *pigen_specialize_parameterized_instances(const char *source, size_t length)
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
	pigen_lex_source(source, length, &tokens);
	collect_modules(source, &tokens, &modules, &module_count);
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
			&overrides, &override_count);
		for (size_t p = 0; p < module->parameter_count; p++)
		{
			char *value = module->parameters[p].expr ?
				pigen_copy_range(module->parameters[p].expr,
					strlen(module->parameters[p].expr)) : NULL;
			size_t positional = 0;

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
					}
				}
				else if (positional == p)
				{
					free(value);
					value = overrides[o].expr ?
						pigen_copy_range(overrides[o].expr,
							strlen(overrides[o].expr)) : NULL;
					positional++;
				}
				else positional++;
			}
			values = pigen_resize(values, (value_count + 1) * sizeof(*values));
			values[value_count++] = (spec_parameter){
				pigen_copy_range(module->parameters[p].name,
					strlen(module->parameters[p].name)), value};
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
