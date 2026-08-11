#!/bin/sh
set -eu

pigen=${1:-./pigen}
temporary=${TMPDIR:-/tmp}/pigen-blocks-$$
mkdir -p "$temporary"
trap 'find "$temporary" -type f -delete; rmdir "$temporary"' EXIT HUP INT TERM

"$pigen" tests/pipeline_block.pigen -o "$temporary/pipeline.sv"
grep -q 'module pipeline_mac__stage_multiply' "$temporary/pipeline.sv"
grep -q 'u_skid_1' "$temporary/pipeline.sv"
iverilog -g2012 -s pipeline_block_tb -o "$temporary/pipeline" "$temporary/pipeline.sv" tests/pipeline_block_tb.sv
vvp "$temporary/pipeline"

"$pigen" tests/pipeline_forms.pigen -o "$temporary/pipeline-forms.sv"
grep -q 'module compact_pipeline' "$temporary/pipeline-forms.sv"
grep -q 'y = x + 1' "$temporary/pipeline-forms.sv"
if grep -q 'skid_controls__skid.*u_skid_4' "$temporary/pipeline-forms.sv"; then
    echo 'no_skid failed to suppress the periodic fourth-stage skid' >&2
    exit 1
fi
grep -q 'periodic_controls__skid.*u_skid_4' "$temporary/pipeline-forms.sv"
iverilog -g2012 -s pipeline_forms_tb -o "$temporary/pipeline-forms" "$temporary/pipeline-forms.sv" tests/pipeline_forms_tb.sv
vvp "$temporary/pipeline-forms"

"$pigen" tests/fabric_block.pigen -o "$temporary/fabric.sv"
grep -q 'module routed_fabric__fabric_router' "$temporary/fabric.sv"
grep -q 'ROUTE__source_a__tx__to_sink' "$temporary/fabric.sv"
grep -q 'route manifest: payload=PAYLOAD_W path_width=2' "$temporary/fabric.sv"
grep -q '^<?xml version="1.0" encoding="UTF-8"?>' "$temporary/fabric.sv.svg"
grep -q 'data-layout="deterministic-grid"' "$temporary/fabric.sv.svg"
grep -q 'data-kind="unit" data-unit="source_a"' "$temporary/fabric.sv.svg"
grep -q 'data-kind="router" data-router="r0"' "$temporary/fabric.sv.svg"
grep -q 'data-endpoint="source:source_a.tx.to_sink"' "$temporary/fabric.sv.svg"
grep -q 'data-endpoint="destination:sink.rx"' "$temporary/fabric.sv.svg"
grep -q 'data-kind="direct-link"' "$temporary/fabric.sv.svg"
grep -q 'data-kind="router-link"' "$temporary/fabric.sv.svg"
grep -q 'marker-end="url(#arrow)"' "$temporary/fabric.sv.svg"
grep -q '>r0.p' "$temporary/fabric.sv.svg"
awk -f tests/check_fabric_svg.awk "$temporary/fabric.sv.svg"
if command -v xmlwf >/dev/null 2>&1; then
    xmlwf "$temporary/fabric.sv.svg"
fi
iverilog -g2012 -s fabric_block_tb -o "$temporary/fabric" "$temporary/fabric.sv" tests/fabric_block_tb.sv
vvp "$temporary/fabric"

"$pigen" tests/fabric_block.pigen -o "$temporary/fabric-repeat.sv"
cmp "$temporary/fabric.sv.svg" "$temporary/fabric-repeat.sv.svg"

"$pigen" tests/fabric_block.pigen -o "$temporary/fabric-custom.sv" --diagram "$temporary/network.svg"
test -f "$temporary/network.svg"
test ! -e "$temporary/fabric-custom.sv.svg"

"$pigen" tests/fabric_block.pigen -o "$temporary/fabric-suppressed.sv" --no-diagram
test ! -e "$temporary/fabric-suppressed.sv.svg"

"$pigen" tests/multiple_fabrics.pigen -o "$temporary/multiple.sv"
test -f "$temporary/multiple.sv.first_diagram.svg"
test -f "$temporary/multiple.sv.second_diagram.svg"
grep -q 'Pigen routing network: first_diagram' "$temporary/multiple.sv.first_diagram.svg"
grep -q 'Pigen routing network: second_diagram' "$temporary/multiple.sv.second_diagram.svg"

if "$pigen" tests/multiple_fabrics.pigen -o "$temporary/multiple-custom.sv" --diagram "$temporary/ambiguous.svg" 2>"$temporary/multiple-custom.err"; then
    echo 'custom diagram path unexpectedly accepted for multiple fabrics' >&2
    exit 1
fi
grep -q 'requires exactly one fabric block' "$temporary/multiple-custom.err"

if "$pigen" tests/pipeline_block.pigen -o "$temporary/pipeline-diagram.sv" --diagram "$temporary/not-a-fabric.svg" 2>"$temporary/pipeline-diagram.err"; then
    echo 'diagram option unexpectedly accepted without a fabric' >&2
    exit 1
fi
grep -q 'require at least one fabric block' "$temporary/pipeline-diagram.err"

iverilog -g2012 -s fabric_skid_tb -o "$temporary/fabric-skid" "$temporary/fabric.sv" tests/fabric_skid_tb.sv
vvp "$temporary/fabric-skid"

iverilog -g2012 -s fabric_router_tb -o "$temporary/router" "$temporary/fabric.sv" tests/fabric_router_tb.sv
vvp "$temporary/router"

"$pigen" tests/random_fabric_block.pigen -o "$temporary/random-fabric.sv"
iverilog -g2012 -s random_fabric_block_tb -o "$temporary/random-fabric" "$temporary/random-fabric.sv" tests/random_fabric_block_tb.sv
vvp "$temporary/random-fabric"

"$pigen" tests/direct_fabric_block.pigen -o "$temporary/direct.sv"
if grep -q 'fabric_router' "$temporary/direct.sv"; then
    echo 'direct-only fabric unexpectedly emitted a router' >&2
    exit 1
fi
if grep -q 'PATH_W' "$temporary/direct.sv"; then
    echo 'direct-only fabric unexpectedly emitted a route width' >&2
    exit 1
fi
iverilog -g2012 -s direct_only -o "$temporary/direct" "$temporary/direct.sv"

"$pigen" tests/mixer_fabric_block.pigen -o "$temporary/mixer.sv"
grep -q 'parameter integer PATH_W = 7' "$temporary/mixer.sv"
grep -q 'ROUTE__input_left__samples__main = 110' "$temporary/mixer.sv"
grep -q 'ui__telemetry__SOURCE__analyser = 64' "$temporary/mixer.sv"
awk -f tests/check_fabric_svg.awk "$temporary/mixer.sv.svg"
if command -v xmlwf >/dev/null 2>&1; then
    xmlwf "$temporary/mixer.sv.svg"
fi
iverilog -g2012 -s mixer_network -o "$temporary/mixer" "$temporary/mixer.sv"

"$pigen" tests/mixed_blocks.pigen -o "$temporary/mixed.sv"
grep -q 'module ordinary_marker' "$temporary/mixed.sv"
grep -q 'module embedded_pipeline' "$temporary/mixed.sv"
grep -q 'module embedded_fabric' "$temporary/mixed.sv"
iverilog -g2012 -o "$temporary/mixed" "$temporary/mixed.sv"

"$pigen" examples/language_blocks.pigen -o "$temporary/language-blocks-example.sv"
iverilog -g2012 -o "$temporary/language-blocks-example" "$temporary/language-blocks-example.sv"

if "$pigen" tests/pipeline_width_error.pigen -o "$temporary/bad-pipeline.sv" 2>"$temporary/bad-pipeline.err"; then
    echo 'pipeline width mismatch unexpectedly compiled' >&2
    exit 1
fi
grep -q 'tuple width' "$temporary/bad-pipeline.err"

if "$pigen" tests/pipeline_declaration_error.pigen -o "$temporary/bad-declaration.sv" 2>"$temporary/bad-declaration.err"; then
    echo 'undeclared first-stage pipeline input unexpectedly compiled' >&2
    exit 1
fi
grep -q 'first stage requires a declaration' "$temporary/bad-declaration.err"

if "$pigen" tests/fabric_direct_error.pigen -o "$temporary/bad-fabric.sv" 2>"$temporary/bad-fabric.err"; then
    echo 'invalid direct fabric destination unexpectedly compiled' >&2
    exit 1
fi
grep -q 'direct fabric destination' "$temporary/bad-fabric.err"

if "$pigen" tests/fabric_depth_error.pigen -o "$temporary/bad-depth.sv" 2>"$temporary/bad-depth.err"; then
    echo 'unsupported fabric buffer depth unexpectedly compiled' >&2
    exit 1
fi
grep -q 'fixes.*router_buffer_depth.*2 entries' "$temporary/bad-depth.err"
