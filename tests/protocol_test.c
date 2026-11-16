#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pigen/protocol.h"

static pigen_protocol_channel_layout layout_for(const pigen_protocol *protocol,
	pigen_protocol_channel channel, size_t bits)
{
	pigen_protocol_channel_layout layout;
	assert(pigen_protocol_layout(protocol, channel, &layout));
	assert(layout.payload_bits == bits);
	return layout;
}

static void field_is(const pigen_protocol_channel_layout *layout,
	pigen_protocol_field field, size_t offset, size_t bits)
{
	const pigen_protocol_field_layout *found = pigen_protocol_layout_field(layout, field);
	assert(found && found->offset == offset && found->bits == bits);
}

static void lite(void)
{
	for (size_t bits = 32; bits <= 64; bits *= 2)
	{
		pigen_protocol protocol = {.family = PIGEN_PROTOCOL_AXI4_LITE,
			.data_bits = bits, .address_bits = 40};
		pigen_protocol_channel_layout aw = layout_for(&protocol, PIGEN_CHANNEL_AW, 43);
		pigen_protocol_channel_layout ar = layout_for(&protocol, PIGEN_CHANNEL_AR, 43);
		pigen_protocol_channel_layout w = layout_for(&protocol, PIGEN_CHANNEL_W, bits + bits / 8);
		pigen_protocol_channel_layout b = layout_for(&protocol, PIGEN_CHANNEL_B, 2);
		pigen_protocol_channel_layout r = layout_for(&protocol, PIGEN_CHANNEL_R, bits + 2);
		field_is(&aw, PIGEN_FIELD_ADDR, 0, 40);
		field_is(&ar, PIGEN_FIELD_PROT, 40, 3);
		field_is(&w, PIGEN_FIELD_DATA, 0, bits);
		field_is(&w, PIGEN_FIELD_STRB, bits, bits / 8);
		field_is(&b, PIGEN_FIELD_RESP, 0, 2);
		field_is(&r, PIGEN_FIELD_RESP, bits, 2);
		assert(!pigen_protocol_layout_field(&w, PIGEN_FIELD_LAST));
		assert(!pigen_protocol_layout_field(&aw, PIGEN_FIELD_ID));
		assert(!pigen_protocol_layout_field(&aw, PIGEN_FIELD_LEN));
		assert(!pigen_protocol_has_channel(&protocol, PIGEN_CHANNEL_BEAT));
	}
}

static void full(void)
{
	/* Exercise every AXI4 data width, zero/nonzero IDs and per-channel USER. */
	for (size_t bits = 8; bits <= 1024; bits *= 2)
	for (size_t ids = 0; ids <= 11; ids += 11)
	{
		pigen_protocol protocol = {.family = PIGEN_PROTOCOL_AXI4,
			.data_bits = bits, .address_bits = 48, .id_bits = ids,
			.user_bits = {0, 3, 7, 5, 11, 13}};
		/* Ax: ADDR + ID + LEN/SIZE/BURST/LOCK/CACHE/PROT/QOS/REGION = 29. */
		pigen_protocol_channel_layout aw = layout_for(&protocol, PIGEN_CHANNEL_AW, 48 + ids + 29 + 3);
		pigen_protocol_channel_layout ar = layout_for(&protocol, PIGEN_CHANNEL_AR, 48 + ids + 29 + 11);
		pigen_protocol_channel_layout w = layout_for(&protocol, PIGEN_CHANNEL_W, bits + bits / 8 + 1 + 7);
		pigen_protocol_channel_layout b = layout_for(&protocol, PIGEN_CHANNEL_B, 2 + ids + 5);
		pigen_protocol_channel_layout r = layout_for(&protocol, PIGEN_CHANNEL_R, bits + 2 + ids + 1 + 13);
		field_is(&aw, PIGEN_FIELD_ADDR, 0, 48);
		field_is(&aw, PIGEN_FIELD_LEN, 48 + ids, 8);
		field_is(&aw, PIGEN_FIELD_SIZE, 56 + ids, 3);
		field_is(&aw, PIGEN_FIELD_BURST, 59 + ids, 2);
		field_is(&aw, PIGEN_FIELD_LOCK, 61 + ids, 1);
		field_is(&aw, PIGEN_FIELD_CACHE, 62 + ids, 4);
		field_is(&aw, PIGEN_FIELD_PROT, 66 + ids, 3);
		field_is(&aw, PIGEN_FIELD_QOS, 69 + ids, 4);
		field_is(&aw, PIGEN_FIELD_REGION, 73 + ids, 4);
		field_is(&ar, PIGEN_FIELD_USER, 77 + ids, 11);
		field_is(&w, PIGEN_FIELD_LAST, bits + bits / 8, 1);
		field_is(&w, PIGEN_FIELD_USER, bits + bits / 8 + 1, 7);
		field_is(&b, PIGEN_FIELD_USER, 2 + ids, 5);
		field_is(&r, PIGEN_FIELD_LAST, bits + 2 + ids, 1);
		field_is(&r, PIGEN_FIELD_USER, bits + 3 + ids, 13);
		assert(!pigen_protocol_layout_field(&w, PIGEN_FIELD_ID)); /* No AXI3 WID. */
		if (ids) field_is(&r, PIGEN_FIELD_ID, bits + 2, ids);
		else assert(!pigen_protocol_layout_field(&r, PIGEN_FIELD_ID));
	}
}

static void streaming(void)
{
	for (unsigned flags = 0; flags < 8; flags++)
	{
		pigen_protocol protocol = {.family = PIGEN_PROTOCOL_AXI4_STREAM,
			.data_bits = 24, .id_bits = 7, .destination_bits = 9,
			.user_bits = {17}, .stream_fields = flags};
		size_t sideband = ((flags & PIGEN_STREAM_KEEP) ? 3 : 0) +
			((flags & PIGEN_STREAM_STRB) ? 3 : 0) +
			((flags & PIGEN_STREAM_LAST) ? 1 : 0);
		pigen_protocol_channel_layout layout = layout_for(&protocol,
			PIGEN_CHANNEL_BEAT, 24 + sideband + 7 + 9 + 17);
		field_is(&layout, PIGEN_FIELD_DATA, 0, 24);
		field_is(&layout, PIGEN_FIELD_ID, 24 + sideband, 7);
		field_is(&layout, PIGEN_FIELD_DEST, 31 + sideband, 9);
		field_is(&layout, PIGEN_FIELD_USER, 40 + sideband, 17);
		assert(!!pigen_protocol_layout_field(&layout, PIGEN_FIELD_KEEP) ==
			!!(flags & PIGEN_STREAM_KEEP));
		assert(!!pigen_protocol_layout_field(&layout, PIGEN_FIELD_STRB) ==
			!!(flags & PIGEN_STREAM_STRB));
		assert(!!pigen_protocol_layout_field(&layout, PIGEN_FIELD_LAST) ==
			!!(flags & PIGEN_STREAM_LAST));
		assert(!pigen_protocol_has_channel(&protocol, PIGEN_CHANNEL_W));
	}
	/* AXI4-Stream permits omitted TDATA, including handshake-only streams. */
	pigen_protocol empty = {.family = PIGEN_PROTOCOL_AXI4_STREAM};
	assert(pigen_protocol_is_valid(&empty));
	(void)layout_for(&empty, PIGEN_CHANNEL_BEAT, 0);
	empty.stream_fields = PIGEN_STREAM_LAST;
	(void)layout_for(&empty, PIGEN_CHANNEL_BEAT, 1);
	empty.stream_fields |= PIGEN_STREAM_KEEP;
	assert(!pigen_protocol_is_valid(&empty));
}

static void compatibility(void)
{
	pigen_protocol left = {.family = PIGEN_PROTOCOL_AXI4, .data_bits = 32,
		.address_bits = 32, .id_bits = 4};
	pigen_protocol right = left;
	assert(pigen_protocol_equal(&left, &right));
	assert(pigen_protocol_can_connect(&left, PIGEN_PROTOCOL_INITIATOR,
		&right, PIGEN_PROTOCOL_TARGET));
	assert(pigen_protocol_can_connect(&left, PIGEN_PROTOCOL_TARGET,
		&right, PIGEN_PROTOCOL_INITIATOR));
	assert(!pigen_protocol_can_connect(&left, PIGEN_PROTOCOL_TARGET,
		&right, PIGEN_PROTOCOL_TARGET));
	assert(!pigen_protocol_can_connect(&left, (pigen_protocol_role)9,
		&right, PIGEN_PROTOCOL_INITIATOR));
	right.id_bits++;
	assert(!pigen_protocol_equal(&left, &right));
	right = left; right.address_bits++;
	assert(!pigen_protocol_equal(&left, &right));
	right = left; right.data_bits *= 2;
	assert(!pigen_protocol_equal(&left, &right));
	for (size_t i = PIGEN_CHANNEL_AW; i < PIGEN_CHANNEL_COUNT; i++)
	{
		right = left; right.user_bits[i] = 1;
		assert(!pigen_protocol_equal(&left, &right));
	}
	/* Same data/address widths cannot erase protocol identity. */
	left.id_bits = 0; right = left; right.family = PIGEN_PROTOCOL_AXI4_LITE;
	assert(pigen_protocol_is_valid(&right));
	assert(!pigen_protocol_equal(&left, &right));
	for (pigen_protocol_family family = PIGEN_PROTOCOL_AXI4_STREAM;
		family <= PIGEN_PROTOCOL_AXI4; family++)
	for (pigen_protocol_channel channel = PIGEN_CHANNEL_BEAT;
		channel < PIGEN_CHANNEL_COUNT; channel++)
	{
		pigen_protocol_role source;
		int present = family == PIGEN_PROTOCOL_AXI4_STREAM ?
			channel == PIGEN_CHANNEL_BEAT : channel != PIGEN_CHANNEL_BEAT;
		assert(pigen_protocol_channel_source(family, channel, &source) == present);
		if (present) assert(source == ((channel == PIGEN_CHANNEL_B ||
			channel == PIGEN_CHANNEL_R) ? PIGEN_PROTOCOL_TARGET : PIGEN_PROTOCOL_INITIATOR));
	}
}

static void invalid(void)
{
	pigen_protocol protocol = {0};
	pigen_protocol_channel_layout layout = {0};
	assert(!pigen_protocol_is_valid(NULL));
	assert(!pigen_protocol_is_valid(&protocol));
	assert(!pigen_protocol_equal(&protocol, &protocol));
	protocol.family = (pigen_protocol_family)-1;
	assert(!pigen_protocol_is_valid(&protocol));
	protocol = (pigen_protocol){.family = PIGEN_PROTOCOL_AXI4, .data_bits = 32, .address_bits = 32};
	for (size_t bits = 0; bits <= 2048; bits++)
	{
		protocol.data_bits = bits;
		int expected = bits >= 8 && bits <= 1024 && !(bits & (bits - 1));
		assert(pigen_protocol_is_valid(&protocol) == expected);
	}
	protocol.data_bits = 32;
	protocol.address_bits = SIZE_MAX;
	assert(!pigen_protocol_is_valid(&protocol));
	protocol.address_bits = 32;
	protocol.id_bits = SIZE_MAX;
	assert(!pigen_protocol_is_valid(&protocol));
	protocol.id_bits = 0;
	protocol.user_bits[PIGEN_CHANNEL_R] = SIZE_MAX;
	assert(!pigen_protocol_is_valid(&protocol));
	protocol.user_bits[PIGEN_CHANNEL_R] = 0;
	protocol.user_bits[PIGEN_CHANNEL_BEAT] = 1;
	assert(!pigen_protocol_is_valid(&protocol));
	protocol.user_bits[PIGEN_CHANNEL_BEAT] = 0;
	assert(!pigen_protocol_layout(&protocol, (pigen_protocol_channel)-1, &layout));
	assert(!pigen_protocol_layout(&protocol, PIGEN_CHANNEL_COUNT, &layout));
	assert(!pigen_protocol_layout(&protocol, PIGEN_CHANNEL_W, NULL));
	protocol.family = PIGEN_PROTOCOL_AXI4_LITE;
	protocol.id_bits = 1;
	assert(!pigen_protocol_is_valid(&protocol));
	protocol.id_bits = 0;
	protocol.user_bits[PIGEN_CHANNEL_W] = 1;
	assert(!pigen_protocol_is_valid(&protocol));
	protocol = (pigen_protocol){.family = PIGEN_PROTOCOL_AXI4_STREAM, .data_bits = 7};
	assert(!pigen_protocol_is_valid(&protocol));
	protocol.data_bits = 32; protocol.stream_fields = 8;
	assert(!pigen_protocol_is_valid(&protocol));
	protocol.stream_fields = 0; protocol.address_bits = 1;
	assert(!pigen_protocol_is_valid(&protocol));
	assert(!pigen_protocol_family_name(PIGEN_PROTOCOL_NONE));
	assert(!pigen_protocol_family_name((pigen_protocol_family)9));
	assert(!pigen_protocol_channel_name((pigen_protocol_channel)-1));
	assert(!pigen_protocol_field_name(PIGEN_FIELD_COUNT));
	assert(!pigen_protocol_channel_source(PIGEN_PROTOCOL_AXI4, PIGEN_CHANNEL_W, NULL));
	assert(!pigen_protocol_channel_source(PIGEN_PROTOCOL_NONE, PIGEN_CHANNEL_W,
		&(pigen_protocol_role){0}));
	assert(!pigen_protocol_layout_field(NULL, PIGEN_FIELD_DATA));
	assert(!pigen_protocol_layout_field(&layout, (pigen_protocol_field)-1));
	layout.field_count = PIGEN_FIELD_COUNT + 1;
	assert(!pigen_protocol_layout_field(&layout, PIGEN_FIELD_DATA));
}

int main(void)
{
	pigen_protocol routed_source = {.family=PIGEN_PROTOCOL_AXI4, .data_bits=32, .address_bits=32, .id_bits=4};
	pigen_protocol routed_target = routed_source;
	assert(pigen_protocol_can_route(&routed_source, &routed_target, 1));
	assert(!pigen_protocol_can_route(&routed_source, &routed_target, 2));
	routed_target.id_bits=5;
	assert(pigen_protocol_can_route(&routed_source, &routed_target, 2));
	assert(!pigen_protocol_can_route(&routed_source, &routed_target, 3));
	routed_target.id_bits=6;
	assert(pigen_protocol_can_route(&routed_source, &routed_target, 3));
	assert(!pigen_protocol_can_connect(&routed_source, PIGEN_PROTOCOL_INITIATOR,
		&routed_target, PIGEN_PROTOCOL_TARGET));
	assert(pigen_protocol_can_route(&routed_source, &routed_target, 4));
	assert(!pigen_protocol_can_route(&routed_source, &routed_target, 5));
	routed_target.user_bits[PIGEN_CHANNEL_R]=1;
	assert(!pigen_protocol_can_route(&routed_source, &routed_target, 2));
	assert(!pigen_protocol_can_route(&routed_source, &routed_source, 0));
	pigen_protocol stream_source={.family=PIGEN_PROTOCOL_AXI4_STREAM,.data_bits=32,.id_bits=4};
	pigen_protocol stream_target=stream_source;
	assert(pigen_protocol_can_route(&stream_source,&stream_target,2)); /* Legacy exact fan-in. */
	stream_target.id_bits=5;
	assert(pigen_protocol_can_route(&stream_source,&stream_target,2));
	assert(!pigen_protocol_can_route(&stream_source,&stream_target,3));
	assert(!pigen_protocol_can_connect(&stream_source,PIGEN_PROTOCOL_INITIATOR,&stream_target,PIGEN_PROTOCOL_TARGET));
	stream_target.id_bits=6;
	assert(pigen_protocol_can_route(&stream_source,&stream_target,3));
	stream_target.destination_bits=1;
	assert(!pigen_protocol_can_route(&stream_source,&stream_target,3));
	stream_source.id_bits=0;stream_target=stream_source;stream_target.id_bits=1;
	assert(pigen_protocol_can_route(&stream_source,&stream_target,2));
	assert(!pigen_protocol_can_route(&stream_source,&stream_target,3));
	lite();
	full();
	streaming();
	compatibility();
	invalid();
	assert(!strcmp(pigen_protocol_family_name(PIGEN_PROTOCOL_AXI4), "axi4"));
	pigen_protocol_family family;
	assert(pigen_protocol_family_from_spelling("axi4", 4, &family) && family == PIGEN_PROTOCOL_AXI4);
	assert(pigen_protocol_family_from_spelling("axi4_lite", 9, &family) && family == PIGEN_PROTOCOL_AXI4_LITE);
	assert(pigen_protocol_family_from_spelling("axi4_stream", 11, &family) && family == PIGEN_PROTOCOL_AXI4_STREAM);
	assert(!pigen_protocol_family_from_spelling("axi3", 4, &family));
	assert(!pigen_protocol_family_from_spelling("axi4_stream_extra", 17, &family));
	assert(!pigen_protocol_family_from_spelling(NULL, 0, &family));
	assert(!pigen_protocol_family_from_spelling("axi4", 4, NULL));
	assert(!strcmp(pigen_protocol_channel_name(PIGEN_CHANNEL_AR), "ar"));
	assert(!strcmp(pigen_protocol_field_name(PIGEN_FIELD_REGION), "region"));
	puts("PASS: AXI4 protocol contracts, independent channels, fields and compatibility");
	return 0;
}
