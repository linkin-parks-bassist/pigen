---
status: green
revised_at: "2026-11-16T01:16:22+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Declaration endpoint lowering is owned by include/pigen/rtl_lower.h and src/rtl_lower.c. pigen_lower_rtl_module_declarations(lowering,module) reports -1 on failure and stops compilation; partial state remains caller-owned (how/should/compiler/builders/fail.md). pigen_rtl_signal_endpoints records payload, valid/ready expressions, input payload/control identities and validity-action controls. The signal-indexed lowered_endpoints map grows on demand; init/free own its lifetime. Repeated declaration lowering preserves endpoint identities without arena growth.

The private realization_endpoint_adapters table dispatches by transfer realization rather than source transfer enum. Descriptor laws provide capacity, readiness, occupancy and reset behavior. BOUNDARY handles abstract module inputs with distinct payload/valid/ready ports and external controls; semantic construction rejects non-input abstract signals. The three input-side identities reuse those ports. There are no constant valid/ready controls for this boundary (what/is/an/abstract/input.md).

COMBINATIONAL_NET and PROCEDURAL_VARIABLE each publish one payload object, reused by input-side endpoints, and descriptor-derived one-bit unsigned control expressions without control objects or storage instances. WIRE uses valid=1, ready=0; REG/LOGIC use valid=1, ready=1. tests/rtl_lower_test.c checks two static signals produce exactly two objects, no instances and bounded constant-expression growth, with idempotent repeat calls.

ELASTIC_SLOT, PULSE_REGISTER, PARAMETERIZED_QUEUE and SKID_QUEUE select owned BUF, PORT, FIFO and SKID primitive instances. Each storage signal has payload, out-valid, in-valid, in-ready, packet-in and out-ready objects plus four validity-action controls. Its valid/ready expressions reference driven objects. Primitive connections carry clock/reset, clear/discard/force controls and input/output handshake/data identities. Instance primitive enum, payload type, connections, final name and owner are explicit finished RTL metadata; rendering does not recover a primitive from source text.

FIFO depth is the positive constant transfer argument converted to the primitive's signed 32-bit DEPTH interface. Values above INT32_MAX reject. Its RTL integer literal stores 64 bits while its type has width 32, signedness SIGNED and FOUR state domain. The authored argument's type does not set this interface type. Fixed-capacity slot/port/skid instances have no depth parameter. The four-storage-signal fixture checks forty objects, four distinct instances, eight to ten expressions and unchanged counts/records on a second call.

make rtl-lower-test checks all 24 type/expression, declaration and transfer sections, including boundary rejection, scalar controls, storage matrices, exact FIFO depth value/type, ownership and idempotence. The target links rtl_lower.c with SEMANTIC_SOURCES, including transfer_type.c; tests/contracts.json records that coverage. Type/expression lowering is owned by what/is/elastic/rtl/task/five.md; transfer lowering by what/is/elastic/rtl/task/eight.md. Native endpoint clock/reset and standard-pin contracts have their separate AXI owners.
