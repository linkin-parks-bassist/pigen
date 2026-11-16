/* AXI protocol contracts, without storage policy or SystemVerilog encoding. */
#include <stdint.h>
#include <string.h>

#include "pigen/protocol.h"

enum width_source { FIXED, DATA, ADDRESS, BYTES, ID, DESTINATION, USER };

typedef struct {
	pigen_protocol_field field;
	enum width_source width;
	size_t fixed_bits;
	unsigned stream_flag;
} field_rule;

/* Arrays define native record order from least to most significant field. */
static const field_rule stream[] = {
	{PIGEN_FIELD_DATA, DATA, 0, 0},
	{PIGEN_FIELD_STRB, BYTES, 0, PIGEN_STREAM_STRB},
	{PIGEN_FIELD_KEEP, BYTES, 0, PIGEN_STREAM_KEEP},
	{PIGEN_FIELD_LAST, FIXED, 1, PIGEN_STREAM_LAST},
	{PIGEN_FIELD_ID, ID, 0, 0},
	{PIGEN_FIELD_DEST, DESTINATION, 0, 0},
	{PIGEN_FIELD_USER, USER, 0, 0}
};
static const field_rule lite_address[] = {
	{PIGEN_FIELD_ADDR, ADDRESS, 0, 0},
	{PIGEN_FIELD_PROT, FIXED, 3, 0}
};
static const field_rule address[] = {
	{PIGEN_FIELD_ADDR, ADDRESS, 0, 0},
	{PIGEN_FIELD_ID, ID, 0, 0},
	{PIGEN_FIELD_LEN, FIXED, 8, 0},
	{PIGEN_FIELD_SIZE, FIXED, 3, 0},
	{PIGEN_FIELD_BURST, FIXED, 2, 0},
	{PIGEN_FIELD_LOCK, FIXED, 1, 0},
	{PIGEN_FIELD_CACHE, FIXED, 4, 0},
	{PIGEN_FIELD_PROT, FIXED, 3, 0},
	{PIGEN_FIELD_QOS, FIXED, 4, 0},
	{PIGEN_FIELD_REGION, FIXED, 4, 0},
	{PIGEN_FIELD_USER, USER, 0, 0}
};
static const field_rule lite_write[] = {
	{PIGEN_FIELD_DATA, DATA, 0, 0},
	{PIGEN_FIELD_STRB, BYTES, 0, 0}
};
static const field_rule write_data[] = {
	{PIGEN_FIELD_DATA, DATA, 0, 0},
	{PIGEN_FIELD_STRB, BYTES, 0, 0},
	{PIGEN_FIELD_LAST, FIXED, 1, 0},
	{PIGEN_FIELD_USER, USER, 0, 0}
};
static const field_rule lite_response[] = {
	{PIGEN_FIELD_RESP, FIXED, 2, 0}
};
static const field_rule response[] = {
	{PIGEN_FIELD_RESP, FIXED, 2, 0},
	{PIGEN_FIELD_ID, ID, 0, 0},
	{PIGEN_FIELD_USER, USER, 0, 0}
};
static const field_rule lite_read[] = {
	{PIGEN_FIELD_DATA, DATA, 0, 0},
	{PIGEN_FIELD_RESP, FIXED, 2, 0}
};
static const field_rule read_data[] = {
	{PIGEN_FIELD_DATA, DATA, 0, 0},
	{PIGEN_FIELD_RESP, FIXED, 2, 0},
	{PIGEN_FIELD_ID, ID, 0, 0},
	{PIGEN_FIELD_LAST, FIXED, 1, 0},
	{PIGEN_FIELD_USER, USER, 0, 0}
};

static int family_has_channel(pigen_protocol_family family,
	pigen_protocol_channel channel)
{
	if (family == PIGEN_PROTOCOL_AXI4_STREAM)
		return channel == PIGEN_CHANNEL_BEAT;
	if (family == PIGEN_PROTOCOL_AXI4_LITE || family == PIGEN_PROTOCOL_AXI4)
		return channel >= PIGEN_CHANNEL_AW && channel < PIGEN_CHANNEL_COUNT;
	return 0;
}

static const field_rule *rules(pigen_protocol_family family,
	pigen_protocol_channel channel, size_t *count)
{
	int lite = family == PIGEN_PROTOCOL_AXI4_LITE;
	if (!family_has_channel(family, channel)) return NULL;
#define RETURN_RULES(array) do { *count = sizeof(array) / sizeof(*(array)); \
	return array; } while (0)
	switch (channel)
	{
		case PIGEN_CHANNEL_BEAT: RETURN_RULES(stream);
		case PIGEN_CHANNEL_AW:
		case PIGEN_CHANNEL_AR:
			if (lite) { RETURN_RULES(lite_address); }
			RETURN_RULES(address);
		case PIGEN_CHANNEL_W:
			if (lite) { RETURN_RULES(lite_write); }
			RETURN_RULES(write_data);
		case PIGEN_CHANNEL_B:
			if (lite) { RETURN_RULES(lite_response); }
			RETURN_RULES(response);
		case PIGEN_CHANNEL_R:
			if (lite) { RETURN_RULES(lite_read); }
			RETURN_RULES(read_data);
		case PIGEN_CHANNEL_COUNT: break;
	}
#undef RETURN_RULES
	return NULL;
}

static size_t field_width(const pigen_protocol *protocol,
	pigen_protocol_channel channel, const field_rule *rule)
{
	if (rule->stream_flag && !(protocol->stream_fields & rule->stream_flag))
		return 0;
	switch (rule->width)
	{
		case FIXED: return rule->fixed_bits;
		case DATA: return protocol->data_bits;
		case ADDRESS: return protocol->address_bits;
		case BYTES: return protocol->data_bits / 8;
		case ID: return protocol->id_bits;
		case DESTINATION: return protocol->destination_bits;
		case USER: return protocol->user_bits[channel];
	}
	return 0;
}

static int build_layout(const pigen_protocol *protocol,
	pigen_protocol_channel channel, pigen_protocol_channel_layout *layout)
{
	size_t count = 0;
	const field_rule *fields = rules(protocol->family, channel, &count);
	if (!fields) return 0;
	for (size_t i = 0; i < count; i++)
	{
		size_t bits = field_width(protocol, channel, &fields[i]);
		if (!bits) continue;
		if (bits > SIZE_MAX - layout->payload_bits) return 0;
		layout->fields[layout->field_count++] =
			(pigen_protocol_field_layout){fields[i].field, layout->payload_bits, bits};
		layout->payload_bits += bits;
	}
	return 1;
}

int pigen_protocol_is_valid(const pigen_protocol *protocol)
{
	if (!protocol) return 0;
	if (protocol->family == PIGEN_PROTOCOL_AXI4_STREAM)
	{
		if (protocol->data_bits % 8 || protocol->address_bits ||
			(protocol->stream_fields & ~(PIGEN_STREAM_KEEP | PIGEN_STREAM_STRB |
				PIGEN_STREAM_LAST))) return 0;
		if (!protocol->data_bits &&
			(protocol->stream_fields & (PIGEN_STREAM_KEEP | PIGEN_STREAM_STRB)))
			return 0;
	}
	else if (protocol->family == PIGEN_PROTOCOL_AXI4_LITE)
	{
		if ((protocol->data_bits != 32 && protocol->data_bits != 64) ||
			!protocol->address_bits || protocol->id_bits ||
			protocol->destination_bits || protocol->stream_fields) return 0;
		for (size_t i = 0; i < PIGEN_CHANNEL_COUNT; i++)
			if (protocol->user_bits[i]) return 0;
	}
	else if (protocol->family == PIGEN_PROTOCOL_AXI4)
	{
		if (protocol->data_bits < 8 || protocol->data_bits > 1024 ||
			(protocol->data_bits & (protocol->data_bits - 1)) ||
			!protocol->address_bits || protocol->destination_bits ||
			protocol->stream_fields) return 0;
	}
	else return 0;
	for (pigen_protocol_channel channel = PIGEN_CHANNEL_BEAT;
		channel < PIGEN_CHANNEL_COUNT; channel++)
	{
		pigen_protocol_channel_layout layout = {0};
		if (!family_has_channel(protocol->family, channel))
		{
			if (protocol->user_bits[channel]) return 0;
		}
		else if (!build_layout(protocol, channel, &layout)) return 0;
	}
	return 1;
}

const char *pigen_protocol_family_name(pigen_protocol_family family)
{
	switch (family)
	{
		case PIGEN_PROTOCOL_AXI4_STREAM: return "axi4_stream";
		case PIGEN_PROTOCOL_AXI4_LITE: return "axi4_lite";
		case PIGEN_PROTOCOL_AXI4: return "axi4";
		case PIGEN_PROTOCOL_NONE: break;
	}
	return NULL;
}

const char *pigen_protocol_channel_name(pigen_protocol_channel channel)
{
	static const char *const names[] = {"beat", "aw", "w", "b", "ar", "r"};
	return (size_t)channel < sizeof(names) / sizeof(*names) ? names[channel] : NULL;
}

int pigen_protocol_family_from_spelling(const char *text, size_t length,
	pigen_protocol_family *family)
{
	if (!text || !family) return 0;
	for (pigen_protocol_family candidate = PIGEN_PROTOCOL_AXI4_STREAM;
		candidate <= PIGEN_PROTOCOL_AXI4; candidate++)
	{
		const char *name = pigen_protocol_family_name(candidate);
		if (strlen(name) == length && !memcmp(text, name, length))
		{ *family = candidate; return 1; }
	}
	return 0;
}

const char *pigen_protocol_field_name(pigen_protocol_field field)
{
	static const char *const names[] = {
		"data", "addr", "strb", "keep", "last", "id", "dest", "user",
		"len", "size", "burst", "lock", "cache", "prot", "qos", "region", "resp"
	};
	return (size_t)field < sizeof(names) / sizeof(*names) ? names[field] : NULL;
}

int pigen_protocol_has_channel(const pigen_protocol *protocol,
	pigen_protocol_channel channel)
{
	return pigen_protocol_is_valid(protocol) &&
		family_has_channel(protocol->family, channel);
}

int pigen_protocol_channel_source(pigen_protocol_family family,
	pigen_protocol_channel channel, pigen_protocol_role *source)
{
	if (!source || !family_has_channel(family, channel)) return 0;
	*source = channel == PIGEN_CHANNEL_B || channel == PIGEN_CHANNEL_R ?
		PIGEN_PROTOCOL_TARGET : PIGEN_PROTOCOL_INITIATOR;
	return 1;
}

int pigen_protocol_layout(const pigen_protocol *protocol,
	pigen_protocol_channel channel, pigen_protocol_channel_layout *layout)
{
	pigen_protocol_channel_layout result = {0};
	if (!layout) return 0;
	memset(layout, 0, sizeof(*layout));
	if (!pigen_protocol_is_valid(protocol) ||
		!build_layout(protocol, channel, &result)) return 0;
	*layout = result;
	return 1;
}

const pigen_protocol_field_layout *pigen_protocol_layout_field(
	const pigen_protocol_channel_layout *layout, pigen_protocol_field field)
{
	if (!layout || layout->field_count > PIGEN_FIELD_COUNT ||
		(size_t)field >= PIGEN_FIELD_COUNT) return NULL;
	for (size_t i = 0; i < layout->field_count; i++)
		if (layout->fields[i].field == field) return &layout->fields[i];
	return NULL;
}

int pigen_protocol_equal(const pigen_protocol *left, const pigen_protocol *right)
{
	if (!pigen_protocol_is_valid(left) || !pigen_protocol_is_valid(right) ||
		left->family != right->family || left->data_bits != right->data_bits ||
		left->address_bits != right->address_bits || left->id_bits != right->id_bits ||
		left->destination_bits != right->destination_bits ||
		left->stream_fields != right->stream_fields) return 0;
	for (size_t i = 0; i < PIGEN_CHANNEL_COUNT; i++)
		if (left->user_bits[i] != right->user_bits[i]) return 0;
	return 1;
}

int pigen_protocol_can_connect(const pigen_protocol *left,
	pigen_protocol_role left_role, const pigen_protocol *right,
	pigen_protocol_role right_role)
{
	if ((left_role != PIGEN_PROTOCOL_INITIATOR && left_role != PIGEN_PROTOCOL_TARGET) ||
		(right_role != PIGEN_PROTOCOL_INITIATOR && right_role != PIGEN_PROTOCOL_TARGET))
		return 0;
	return left_role != right_role && pigen_protocol_equal(left, right);
}

int pigen_protocol_can_route(const pigen_protocol *source,
	const pigen_protocol *destination, size_t source_count)
{
	if (!source_count || !pigen_protocol_is_valid(source) || !pigen_protocol_is_valid(destination)) return 0;
	if (source->family==PIGEN_PROTOCOL_AXI4_STREAM && source->id_bits==destination->id_bits)
		return pigen_protocol_equal(source,destination);
	if (source->family!=PIGEN_PROTOCOL_AXI4 && source->family!=PIGEN_PROTOCOL_AXI4_STREAM)
		return pigen_protocol_equal(source,destination);
	size_t tag_bits = 0;
	for (size_t n = source_count-1; n; n >>= 1) tag_bits++;
	if (source->id_bits > SIZE_MAX-tag_bits || destination->id_bits < source->id_bits+tag_bits) return 0;
	pigen_protocol expanded = *source;
	expanded.id_bits = destination->id_bits;
	return pigen_protocol_equal(&expanded, destination);
}
