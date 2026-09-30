/* Collision-safe RTL name identity for the elastic RTL vertical slice.
 *
 * One module-local pass assigns every request an immutable, model-owned,
 * collision-free terminal name. Source roles copy the provenance spelling of
 * their checked identifier span; internal roles derive a role stem and gain a
 * deterministic numeric suffix only on collision. A request whose identity is
 * already present (same role and same origin span) reuses the existing name,
 * so repeating a request set is stable and grows the arena by nothing. A
 * failed assignment publishes nothing: the names arena is left exactly as it
 * was before the call, and no staged text leaks (every allocation succeeds or
 * exits, so each staged text is always stored). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/rtl_name.h"
#include "pigen/util.h"

/* A request is well-formed when its kind is a known role and, for a source
 * role, its identifier span is checked by the source manager (valid source id
 * and in bounds). Internal roles may carry an invalid span. */
static int request_valid(const pigen_source_manager *sources,
	const pigen_rtl_name_request *request)
{
	if (request->kind < PIGEN_RTL_NAME_SOURCE ||
		request->kind > PIGEN_RTL_NAME_TEMPORARY)
		return 0;
	if (request->kind == PIGEN_RTL_NAME_SOURCE &&
		!pigen_source_span_valid(sources, request->origin))
		return 0;
	return 1;
}

/* Identity is the role plus the origin span (source id and half-open byte
 * range). A published record matches a request when they share that identity;
 * two requests do the same. Source roles use distinct checked spans, so their
 * requests never share an identity; synthetic internal roles carry an invalid
 * span, so their identity is the role alone. */
static int same_identity(pigen_rtl_name_kind a_kind,
	const pigen_source_span *a, pigen_rtl_name_kind b_kind,
	const pigen_source_span *b)
{
	return a_kind == b_kind &&
		a->source.index == b->source.index &&
		a->start == b->start &&
		a->end == b->end;
}

static int request_matches_name(const pigen_rtl_name *name,
	const pigen_rtl_name_request *request)
{
	return same_identity(name->kind, &name->origin, request->kind,
		&request->origin);
}

static int requests_match(const pigen_rtl_name_request *a,
	const pigen_rtl_name_request *b)
{
	return same_identity(a->kind, &a->origin, b->kind, &b->origin);
}

/* A stem unique to each request's role, so requests of different roles never
 * share a stem and collide. */
static const char *role_stem(pigen_rtl_name_kind kind)
{
	switch (kind) {
	case PIGEN_RTL_NAME_SOURCE: return "source";
	case PIGEN_RTL_NAME_PAYLOAD: return "payload";
	case PIGEN_RTL_NAME_VALID: return "valid";
	case PIGEN_RTL_NAME_READY: return "ready";
	case PIGEN_RTL_NAME_INSTANCE: return "instance";
	case PIGEN_RTL_NAME_TEMPORARY: return "temporary";
	default: return "name";
	}
}

/* True when a published terminal name, or a name staged by an earlier request
 * in this call, equals NAME. The arena is read-only for this call, so the
 * staged names are checked separately from it. */
static int name_taken(const pigen_rtl_model *model, const char **staged,
	size_t staged_count, const char *name)
{
	size_t i;

	for (i = 0; i < model->name_count; i++)
		if (model->names[i].text && strcmp(model->names[i].text, name) == 0)
			return 1;
	for (i = 0; i < staged_count; i++)
		if (staged[i] && strcmp(staged[i], name) == 0)
			return 1;
	return 0;
}

/* A terminal name that is not already taken: the bare stem, then the stem with
 * a deterministic _1, _2, ... suffix. The suffix loop is the collision backstop;
 * under the current identity model each role has one stem and one invalid-span
 * identity, so the first free suffix is _1 and higher numbers are unreachable. */
static char *unique_name(const pigen_rtl_model *model, const char **staged,
	size_t staged_count, const char *stem)
{
	const size_t stem_length = strlen(stem);
	char *name = pigen_copy_range(stem, stem_length);
	size_t candidate = 1;

	if (!name_taken(model, staged, staged_count, name))
		return name;
	free(name);
	for (;;)
	{
		char buffer[stem_length + sizeof("_") + 24];
		int length = snprintf(buffer, sizeof(buffer), "%s_%zu", stem,
			candidate);
		char *suffixed;

		if (length < 0 || (size_t)length >= sizeof(buffer))
			pigen_fail("name collision suffix does not fit");
		suffixed = pigen_copy_range(buffer, (size_t)length);
		if (!name_taken(model, staged, staged_count, suffixed))
			return suffixed;
		free(suffixed);
		candidate++;
	}
}

/* Copy a request's terminal name, kind and origin into the model-owned names
 * arena, growing it on demand, and return its stable identity. */
static pigen_rtl_name_id append_name(pigen_rtl_model *model,
	pigen_rtl_name_request request, char *text)
{
	pigen_rtl_name *record;

	if (model->name_count == model->name_capacity)
	{
		model->name_capacity =
			model->name_capacity ? model->name_capacity * 2 : 8;
		model->names = pigen_resize(model->names,
			model->name_capacity * sizeof(*model->names));
	}
	record = &model->names[model->name_count];
	record->origin = request.origin;
	record->kind = request.kind;
	record->text = text;
	return (pigen_rtl_name_id){(uint32_t)model->name_count++};
}

pigen_rtl_name_id pigen_rtl_assign_names(pigen_rtl_model *model,
	const pigen_source_manager *sources,
	const pigen_rtl_name_request *requests, size_t request_count)
{
	pigen_rtl_name_id *resolved;
	const char **staged_text;
	size_t i;
	size_t additions = 0;

	if (!model)
		return (pigen_rtl_name_id){PIGEN_INVALID_ID};
	if (request_count && !requests)
		return (pigen_rtl_name_id){PIGEN_INVALID_ID};
	if (request_count == 0)
		return (pigen_rtl_name_id){PIGEN_INVALID_ID};

	/* Phase 1: validate every request before any arena is touched, so a
	 * single bad request publishes nothing (Task 3 pattern). */
	for (i = 0; i < request_count; i++)
		if (!request_valid(sources, &requests[i]))
			return (pigen_rtl_name_id){PIGEN_INVALID_ID};

	/* Phase 2: resolve each request to a name id without touching the arena.
	 * A request reuses an existing published name when one matches its
	 * identity; otherwise it reuses the slot an earlier request in this call
	 * already claimed for the same identity; otherwise it takes a fresh arena
	 * slot. */
	resolved = pigen_resize(NULL, request_count * sizeof(*resolved));
	staged_text = pigen_resize(NULL, request_count * sizeof(*staged_text));
	for (i = 0; i < request_count; i++)
	{
		size_t j;
		pigen_rtl_name_id match = (pigen_rtl_name_id){PIGEN_INVALID_ID};

		for (j = 0; j < model->name_count; j++)
			if (request_matches_name(&model->names[j], &requests[i]))
			{
				match.index = (uint32_t)j;
				break;
			}
		if (match.index == PIGEN_INVALID_ID)
			for (j = 0; j < i; j++)
				if (requests_match(&requests[j], &requests[i]))
				{
					match = resolved[j];
					break;
				}
		if (match.index == PIGEN_INVALID_ID)
			match = (pigen_rtl_name_id){(uint32_t)(model->name_count + additions++)};
		resolved[i] = match;
	}

	/* Phase 3: publish only the genuinely new names, in request order, each
	 * staged against the arena and the earlier staged names so the first free
	 * number is stable. */
	for (i = 0; i < request_count; i++)
	{
		if (resolved[i].index < model->name_count)
			continue;
		{
			char *text;

			if (requests[i].kind == PIGEN_RTL_NAME_SOURCE)
			{
				size_t length = 0;
				const char *span_text =
					pigen_source_span_text(sources, requests[i].origin,
						&length);

				text = pigen_copy_range(span_text, length);
			}
			else
				text = unique_name(model, staged_text, i,
					role_stem(requests[i].kind));
			append_name(model, requests[i], text);
			staged_text[i] = text;
		}
	}
	free(resolved);
	free(staged_text);

	return (pigen_rtl_name_id){0};
}

const pigen_rtl_name *pigen_rtl_name_get(const pigen_rtl_model *model,
	pigen_rtl_name_id name)
{
	if (!model || name.index == PIGEN_INVALID_ID ||
		name.index >= model->name_count)
		return NULL;
	return &model->names[name.index];
}
