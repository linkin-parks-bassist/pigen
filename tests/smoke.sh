#!/bin/sh
set -eu
tool=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"$tool" tests/basic.pigen -o "$tmp/out.sv"
grep -Eq 'logic[[:space:]]+\[7:0\] combinatorial;' "$tmp/out.sv"
grep -q 'pigen_buf #(' "$tmp/out.sv"
grep -q '.in_ready(stage__pigen_in_ready)' "$tmp/out.sv"
grep -q '.out_ready(stage__pigen_out_ready)' "$tmp/out.sv"
if grep -q '__pigen_ready' "$tmp/out.sv"; then
	echo "generated transport readiness must use in_ready/out_ready symmetrically" >&2
	exit 1
fi
"$tool" tests/no_reset.pigen -o "$tmp/no_reset.sv"
grep -q '.reset(1'"'"'b0)' "$tmp/no_reset.sv"
verilator --lint-only -Wno-fatal --top-module no_reset rtl/pigen_primitives.sv "$tmp/no_reset.sv"
"$tool" tests/always_block.pigen -o "$tmp/always_block.sv"
grep -q 'assign destination__pigen_in_valid = ((enable)) && source__pigen_valid;' "$tmp/always_block.sv"
grep -q 'always @\*' "$tmp/always_block.sv"
verilator --lint-only -Wno-fatal --top-module always_block rtl/pigen_primitives.sv "$tmp/always_block.sv"
"$tool" tests/transfer_block.pigen -o "$tmp/transfer_block.sv"
grep -Fq '$bits({high, low, history}) != $bits({source[15:8], source[7:0], source[7:0]})' "$tmp/transfer_block.sv"
test "$(grep -c 'assign source_ready =' "$tmp/transfer_block.sv")" = 1
verilator --lint-only -Wno-fatal --top-module transfer_block rtl/pigen_primitives.sv "$tmp/transfer_block.sv"
"$tool" tests/ports.pigen -o "$tmp/ports.sv"
grep -q 'input  logic \[7:0\] incoming' "$tmp/ports.sv"
grep -q 'input  logic incoming_valid' "$tmp/ports.sv"
grep -q 'output logic incoming_ready' "$tmp/ports.sv"
grep -q 'output logic \[7:0\] outgoing' "$tmp/ports.sv"
grep -q 'output logic outgoing_valid' "$tmp/ports.sv"
grep -q 'input  logic outgoing_ready' "$tmp/ports.sv"
cp tests/basic.pigen "$tmp/default-name.pigen"
"$tool" "$tmp/default-name.pigen"
test -f "$tmp/default-name.sv"
"$tool" tests/fifo.pigen -o "$tmp/fifo.sv"
grep -q 'pigen_fifo #(' "$tmp/fifo.sv"
grep -q '.DEPTH(4)' "$tmp/fifo.sv"
grep -q 'assign queue__pigen_in_valid = 1'"'"'b1;' "$tmp/fifo.sv"
"$tool" tests/accessors.pigen -o "$tmp/accessors.sv"
grep -q 'observed_valid = item__pigen_valid;' "$tmp/accessors.sv"
grep -q 'observed_ready = item__pigen_out_ready;' "$tmp/accessors.sv"
"$tool" tests/reg_assignment.pigen -o "$tmp/reg_assignment.sv"
grep -q 'if (source__pigen_valid)' "$tmp/reg_assignment.sv"
"$tool" tests/skid_port.pigen -o "$tmp/skid_port.sv"
grep -q 'pigen_skid #(' "$tmp/skid_port.sv"
grep -q 'assign pulse__pigen_out_ready = 1'"'"'b1;' "$tmp/skid_port.sv"
grep -q 'assign skid_queue__pigen_out_ready = 1'"'"'b1;' "$tmp/skid_port.sv"
"$tool" tests/signed_transport.pigen -o "$tmp/signed_transport.sv"
grep -q 'input  logic signed \[23:0\] input_sample' "$tmp/signed_transport.sv"
grep -q 'output logic signed \[23:0\] output_sample' "$tmp/signed_transport.sv"
grep -q '.PAYLOAD_T(logic signed \[23:0\])' "$tmp/signed_transport.sv"
verilator --lint-only -Wno-fatal --top-module signed_transport rtl/pigen_primitives.sv "$tmp/signed_transport.sv"
"$tool" tests/discard.pigen -o "$tmp/discard.sv"
grep -q 'assign source__pigen_out_ready =' "$tmp/discard.sv"
verilator --lint-only -Wno-fatal --top-module discard_example rtl/pigen_primitives.sv "$tmp/discard.sv"
"$tool" tests/guarded_assignment.pigen -o "$tmp/guarded_assignment.sv"
grep -q 'assign stage__pigen_in_valid = ((enable)) && 1'"'"'b1;' "$tmp/guarded_assignment.sv"
"$tool" tests/nested_guard.pigen -o "$tmp/nested_guard.sv"
grep -q 'assign first_stage__pigen_in_valid = (((outer_enable)) && (inner_enable)) && 1'"'"'b1;' "$tmp/nested_guard.sv"
grep -q 'assign second_stage__pigen_in_valid = (((outer_enable)) && (inner_enable)) && 1'"'"'b1;' "$tmp/nested_guard.sv"
verilator --lint-only -Wno-fatal --top-module nested_guard rtl/pigen_primitives.sv "$tmp/nested_guard.sv"
"$tool" tests/dangling_else_guard.pigen -o "$tmp/dangling_else_guard.sv"
grep -q 'assign first_stage__pigen_in_valid = (((outer_enable)) && (inner_enable)) && 1'"'"'b1;' "$tmp/dangling_else_guard.sv"
grep -q 'assign second_stage__pigen_in_valid = (((outer_enable)) && (!(inner_enable))) && 1'"'"'b1;' "$tmp/dangling_else_guard.sv"
verilator --lint-only -Wno-fatal --top-module dangling_else_guard rtl/pigen_primitives.sv "$tmp/dangling_else_guard.sv"
"$tool" tests/else_guard.pigen -o "$tmp/else_guard.sv"
grep -q 'assign first_stage__pigen_in_valid = ((select_first)) && 1'"'"'b1;' "$tmp/else_guard.sv"
grep -q 'assign second_stage__pigen_in_valid = ((!(select_first))) && 1'"'"'b1;' "$tmp/else_guard.sv"
verilator --lint-only -Wno-fatal --top-module else_guard rtl/pigen_primitives.sv "$tmp/else_guard.sv"
"$tool" tests/else_if_guard.pigen -o "$tmp/else_if_guard.sv"
grep -q 'assign first_stage__pigen_in_valid = ((first_select)) && 1'"'"'b1;' "$tmp/else_if_guard.sv"
grep -q 'assign second_stage__pigen_in_valid = (((!(first_select))) && (second_select)) && 1'"'"'b1;' "$tmp/else_if_guard.sv"
verilator --lint-only -Wno-fatal --top-module else_if_guard rtl/pigen_primitives.sv "$tmp/else_if_guard.sv"
"$tool" tests/repeated_operand.pigen -o "$tmp/repeated_operand.sv"
test "$(grep -c 'assign source__pigen_out_ready =' "$tmp/repeated_operand.sv")" = 1
test "$(grep -c 'source__pigen_valid' "$tmp/repeated_operand.sv")" -ge 2
verilator --lint-only -Wno-fatal --top-module repeated_operand rtl/pigen_primitives.sv "$tmp/repeated_operand.sv"
"$tool" tests/accessor_guard.pigen -o "$tmp/accessor_guard.sv"
grep -q 'assign destination__pigen_in_valid = ((source__pigen_valid)) && source__pigen_valid;' "$tmp/accessor_guard.sv"
verilator --lint-only -Wno-fatal --top-module accessor_guard rtl/pigen_primitives.sv "$tmp/accessor_guard.sv"
"$tool" tests/lexical.pigen -o "$tmp/lexical.sv"
grep -q 'fake declaration; valid(stage); must stay a comment' "$tmp/lexical.sv"
grep -q 'literal semicolon; valid(stage) is not an accessor' "$tmp/lexical.sv"
grep -q 'assign stage__pigen_in_valid = 1'"'"'b1;' "$tmp/lexical.sv"
verilator --lint-only -Wno-fatal --top-module lexical rtl/pigen_primitives.sv "$tmp/lexical.sv"
"$tool" tests/degenerate_ports.pigen -o "$tmp/degenerate_ports.sv"
grep -q 'assign source_ready = 1'"'"'b0;' "$tmp/degenerate_ports.sv"
grep -q 'assign state_valid = 1'"'"'b1;' "$tmp/degenerate_ports.sv"
if grep -q '__pigen_\(valid\|in_ready\|out_ready\)' "$tmp/degenerate_ports.sv"; then
    echo "degenerate ports must not emit private control nets" >&2
    exit 1
fi
verilator --lint-only -Wno-fatal --top-module degenerate_ports rtl/pigen_primitives.sv "$tmp/degenerate_ports.sv"
"$tool" tests/fifo_preferred.pigen -o "$tmp/fifo_preferred.sv"
grep -q 'pigen_fifo #(' "$tmp/fifo_preferred.sv"
grep -q '.DEPTH(4)' "$tmp/fifo_preferred.sv"
verilator --lint-only -Wno-fatal --top-module fifo_preferred rtl/pigen_primitives.sv "$tmp/fifo_preferred.sv"
"$tool" tests/fifo_named_type.pigen -o "$tmp/fifo_named_type.sv"
grep -q '.PAYLOAD_T(packet_t)' "$tmp/fifo_named_type.sv"
grep -q '.DEPTH(4)' "$tmp/fifo_named_type.sv"
verilator --lint-only -Wno-fatal --top-module fifo_named_type rtl/pigen_primitives.sv "$tmp/fifo_named_type.sv"
"$tool" tests/fifo_named_port.pigen -o "$tmp/fifo_named_port.sv"
grep -q 'input  packet_t incoming' "$tmp/fifo_named_port.sv"
verilator --lint-only -Wno-fatal --top-module fifo_named_port rtl/pigen_primitives.sv "$tmp/fifo_named_port.sv"
if "$tool" tests/fifo_reversed_error.pigen -o "$tmp/fifo_reversed_error.sv" 2>"$tmp/fifo_reversed_error.log"; then
    echo "expected reversed fifo spelling rejection" >&2
    exit 1
fi
grep -q 'payload before depth' "$tmp/fifo_reversed_error.log"
"$tool" tests/output_buf.pigen -o "$tmp/output_buf.sv"
grep -q 'out_packet__pigen_buffer' "$tmp/output_buf.sv"
grep -q 'assign out_packet__pigen_in_valid = in_packet__pigen_valid;' "$tmp/output_buf.sv"
"$tool" tests/output_port_reset.pigen -o "$tmp/output_port_reset.sv"
grep -Fq "assign value__pigen_in_valid = ((reset)) && 1'b1 || ((!(reset))) && 1'b1;" "$tmp/output_port_reset.sv"
verilator --lint-only -Wno-fatal --top-module output_port_reset rtl/pigen_primitives.sv "$tmp/output_port_reset.sv"
"$tool" tests/output_fifo.pigen -o "$tmp/output_fifo.sv"
grep -q 'out_packet__pigen_buffer' "$tmp/output_fifo.sv"
grep -q '.DEPTH(4)' "$tmp/output_fifo.sv"
"$tool" tests/accepts.pigen -o "$tmp/accepts.sv"
grep -q 'destination__pigen_in_valid = ((destination__pigen_out_ready && source__pigen_valid)) && source__pigen_valid;' "$tmp/accepts.sv"
verilator --lint-only -Wno-fatal --top-module accepts_example rtl/pigen_primitives.sv "$tmp/accepts.sv"
"$tool" tests/conditional_transfer.pigen -o "$tmp/conditional_transfer.sv"
grep -q 'if (destination__pigen_out_ready && source__pigen_valid)' "$tmp/conditional_transfer.sv"
grep -q 'assign destination__pigen_in_valid = source__pigen_valid;' "$tmp/conditional_transfer.sv"
verilator --lint-only -Wno-fatal --top-module conditional_transfer rtl/pigen_primitives.sv "$tmp/conditional_transfer.sv"
"$tool" tests/coslice.pigen -o "$tmp/coslice.sv"
grep -q 'assign left__pigen_in_valid = source__pigen_valid' "$tmp/coslice.sv"
grep -q 'left__pigen_in_ready && right__pigen_in_ready' "$tmp/coslice.sv"
grep -q 'history <= state;' "$tmp/coslice.sv"
grep -q 'state_delay <=' "$tmp/coslice.sv"
verilator --lint-only -Wno-fatal --top-module coslice rtl/pigen_primitives.sv "$tmp/coslice.sv"
"$tool" tests/port_bram.pigen -o "$tmp/port_bram.sv"
grep -q 'bram_port <= mem\[read_address\];' "$tmp/port_bram.sv"
grep -q 'bram_port__pigen_valid <= (' "$tmp/port_bram.sv"
grep -q 'if (data_in__pigen_valid)' "$tmp/port_bram.sv"
grep -q 'mem\[write_address\] <= data_in;' "$tmp/port_bram.sv"
if grep -q 'pigen_port' "$tmp/port_bram.sv"; then
    echo "internal ports must not wrap direct payload writes" >&2
    exit 1
fi
verilator --lint-only -Wno-fatal --top-module port_bram rtl/pigen_primitives.sv "$tmp/port_bram.sv"
"$tool" tests/reset_domain.pigen -o "$tmp/reset_domain.sv"
grep -q 'assign destination__pigen_in_valid = ((!(reset))) && source__pigen_valid;' "$tmp/reset_domain.sv"
verilator --lint-only -Wno-fatal --top-module reset_domain rtl/pigen_primitives.sv "$tmp/reset_domain.sv"
"$tool" tests/clear_actions.pigen -o "$tmp/clear_actions.sv"
grep -q 'assign stage__pigen_force_invalid = ((discard)) ? 1'"'"'b1 : 1'"'"'b0;' "$tmp/clear_actions.sv"
grep -q 'assign queue__pigen_clear = ((!(discard)));' "$tmp/clear_actions.sv"
verilator --lint-only -Wno-fatal --top-module clear_actions rtl/pigen_primitives.sv "$tmp/clear_actions.sv"
"$tool" tests/exclusive_routes.pigen -o "$tmp/exclusive_routes.sv"
test "$(grep -c 'assign source__pigen_out_ready =' "$tmp/exclusive_routes.sv")" = 1
test "$(grep -c 'assign merged__pigen_in_valid =' "$tmp/exclusive_routes.sv")" = 1
verilator --lint-only -Wno-fatal --top-module exclusive_routes rtl/pigen_primitives.sv "$tmp/exclusive_routes.sv"
"$tool" tests/exclusive_clear.pigen -o "$tmp/exclusive_clear.sv"
verilator --lint-only -Wno-fatal --top-module exclusive_clear rtl/pigen_primitives.sv "$tmp/exclusive_clear.sv"
"$tool" tests/case_guard.pigen -o "$tmp/case_guard.sv"
test "$(grep -c 'assign stage__pigen_in_valid =' "$tmp/case_guard.sv")" = 1
verilator --lint-only -Wno-fatal --top-module case_guard rtl/pigen_primitives.sv "$tmp/case_guard.sv"
"$tool" tests/casez_guard.pigen -o "$tmp/casez_guard.sv"
grep -q '==?' "$tmp/casez_guard.sv"
verilator --lint-only -Wno-fatal --top-module casez_guard rtl/pigen_primitives.sv "$tmp/casez_guard.sv"
"$tool" tests/fsm.pigen -o "$tmp/fsm.sv"
grep -q 'typedef enum logic' "$tmp/fsm.sv"
grep -q 'sender__pigen_state <= sender__pigen_idle' "$tmp/fsm.sv"
verilator --lint-only -Wno-fatal --top-module fsm_example rtl/pigen_primitives.sv "$tmp/fsm.sv"
if "$tool" tests/fsm_bad_goto.pigen -o "$tmp/fsm_bad_goto.sv" 2>"$tmp/fsm_bad_goto.log"; then
    echo "expected unknown fsm goto rejection" >&2
    exit 1
fi
grep -q 'undeclared fsm state' "$tmp/fsm_bad_goto.log"
if "$tool" tests/fsm_bad_initial.pigen -o "$tmp/fsm_bad_initial.sv" 2>"$tmp/fsm_bad_initial.log"; then
    echo "expected unknown fsm initial-state rejection" >&2
    exit 1
fi
grep -q 'initial state is not declared' "$tmp/fsm_bad_initial.log"
if "$tool" tests/fsm_duplicate_state.pigen -o "$tmp/fsm_duplicate_state.sv" 2>"$tmp/fsm_duplicate_state.log"; then
    echo "expected duplicate fsm state rejection" >&2
    exit 1
fi
grep -q 'state declared more than once' "$tmp/fsm_duplicate_state.log"
if "$tool" tests/fsm_bad_event.pigen -o "$tmp/fsm_bad_event.sv" 2>"$tmp/fsm_bad_event.log"; then
    echo "expected asynchronous fsm event rejection" >&2
    exit 1
fi
grep -q 'exactly one posedge' "$tmp/fsm_bad_event.log"
if "$tool" tests/cross_domain_error.pigen -o "$tmp/cross_domain_error.sv" 2>"$tmp/cross_domain_error.log"; then
    echo "expected cross-domain transport rejection" >&2
    exit 1
fi
grep -q 'across synchronous domains' "$tmp/cross_domain_error.log"
if "$tool" tests/async_domain_error.pigen -o "$tmp/async_domain_error.sv" 2>"$tmp/async_domain_error.log"; then
    echo "expected asynchronous transport-domain rejection" >&2
    exit 1
fi
grep -q 'exactly one posedge event control' "$tmp/async_domain_error.log"
if "$tool" tests/outside_domain_error.pigen -o "$tmp/outside_domain_error.sv" 2>"$tmp/outside_domain_error.log"; then
	echo "expected outside clocked always transport assignment rejection" >&2
	exit 1
fi
grep -q 'only in clocked always blocks' "$tmp/outside_domain_error.log"
grep -Eq 'tests/outside_domain_error\.pigen:[0-9]+:[0-9]+:' "$tmp/outside_domain_error.log"
"$tool" tests/clear_overlap_error.pigen -o "$tmp/clear_overlap.sv"
verilator --lint-only -Wno-fatal --top-module clear_overlap_error rtl/pigen_primitives.sv "$tmp/clear_overlap.sv"
if "$tool" tests/fanout_error.pigen -o "$tmp/fanout_error.sv" 2>"$tmp/fanout_error.log"; then
    echo "expected fan-out rejection" >&2
    exit 1
fi
grep -q 'more than one consumer' "$tmp/fanout_error.log"
