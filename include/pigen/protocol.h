#ifndef PIGEN_PROTOCOL_H
#define PIGEN_PROTOCOL_H

#include <stddef.h>

/* Protocol is independent of channel storage and endpoint role. */
typedef enum {
	PIGEN_PROTOCOL_NONE,
	PIGEN_PROTOCOL_AXI4_STREAM,
	PIGEN_PROTOCOL_AXI4_LITE,
	PIGEN_PROTOCOL_AXI4
} pigen_protocol_family;

typedef enum {
	PIGEN_PROTOCOL_INITIATOR,
	PIGEN_PROTOCOL_TARGET
} pigen_protocol_role;

typedef enum {
	PIGEN_CHANNEL_BEAT,
	PIGEN_CHANNEL_AW,
	PIGEN_CHANNEL_W,
	PIGEN_CHANNEL_B,
	PIGEN_CHANNEL_AR,
	PIGEN_CHANNEL_R,
	PIGEN_CHANNEL_COUNT
} pigen_protocol_channel;

typedef enum {
	PIGEN_FIELD_DATA,
	PIGEN_FIELD_ADDR,
	PIGEN_FIELD_STRB,
	PIGEN_FIELD_KEEP,
	PIGEN_FIELD_LAST,
	PIGEN_FIELD_ID,
	PIGEN_FIELD_DEST,
	PIGEN_FIELD_USER,
	PIGEN_FIELD_LEN,
	PIGEN_FIELD_SIZE,
	PIGEN_FIELD_BURST,
	PIGEN_FIELD_LOCK,
	PIGEN_FIELD_CACHE,
	PIGEN_FIELD_PROT,
	PIGEN_FIELD_QOS,
	PIGEN_FIELD_REGION,
	PIGEN_FIELD_RESP,
	PIGEN_FIELD_COUNT
} pigen_protocol_field;

enum {
	PIGEN_STREAM_KEEP = 1u << 0,
	PIGEN_STREAM_STRB = 1u << 1,
	PIGEN_STREAM_LAST = 1u << 2
};

typedef struct {
	pigen_protocol_family family;
	size_t data_bits;
	size_t address_bits;
	size_t id_bits;
	size_t destination_bits; /* AXI4-Stream only. */
	size_t user_bits[PIGEN_CHANNEL_COUNT]; /* Zero means absent. */
	unsigned stream_fields;
} pigen_protocol;

typedef struct {
	pigen_protocol_field field;
	size_t offset; /* Least-significant bit in the native channel record. */
	size_t bits;
} pigen_protocol_field_layout;

typedef struct {
	pigen_protocol_field_layout fields[PIGEN_FIELD_COUNT];
	size_t field_count;
	size_t payload_bits;
} pigen_protocol_channel_layout;

/* Invalid configurations and arithmetic overflow fail closed. */
int pigen_protocol_is_valid(const pigen_protocol *protocol);
const char *pigen_protocol_family_name(pigen_protocol_family family);
int pigen_protocol_family_from_spelling(const char *text, size_t length,
	pigen_protocol_family *family);
const char *pigen_protocol_channel_name(pigen_protocol_channel channel);
const char *pigen_protocol_field_name(pigen_protocol_field field);
int pigen_protocol_has_channel(const pigen_protocol *protocol,
	pigen_protocol_channel channel);
int pigen_protocol_channel_source(pigen_protocol_family family,
	pigen_protocol_channel channel, pigen_protocol_role *source);
int pigen_protocol_layout(const pigen_protocol *protocol,
	pigen_protocol_channel channel, pigen_protocol_channel_layout *layout);
const pigen_protocol_field_layout *pigen_protocol_layout_field(
	const pigen_protocol_channel_layout *layout, pigen_protocol_field field);
/* Exact contracts; adaptation needs an explicit conversion operation. */
int pigen_protocol_equal(const pigen_protocol *left,
	const pigen_protocol *right);
int pigen_protocol_can_connect(const pigen_protocol *left,
	pigen_protocol_role left_role, const pigen_protocol *right,
	pigen_protocol_role right_role);
/* AXI4 and widened Stream ID prefixing; other fields match exactly.
 * Legacy exact Stream contracts remain valid here. Completed-graph validation
 * additionally requires provenance space at switched Stream merges and bounds
 * exact-ID packet-set tracking by the generated-bit policy. */
int pigen_protocol_can_route(const pigen_protocol *source,
	const pigen_protocol *destination, size_t source_count);

#endif
