#!/bin/sh
set -eu

tool=${1:-./pigen}
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT

if "$tool" tests/pipeline_old_syntax_error.pigen -o "$temporary/old.sv" \
	2>"$temporary/old.err"; then
	echo "expected stage-local yield to fail" >&2
	exit 1
fi
grep -q 'yield.*after the final stage' "$temporary/old.err"

if "$tool" tests/pipeline_header_error.pigen -o "$temporary/header.sv" \
	2>"$temporary/header.err"; then
	echo 'pipeline header packing unexpectedly compiled' >&2
	exit 1
fi
grep -q 'pipeline header packing is not supported' "$temporary/header.err"

"$tool" tests/pipeline_shadow.pigen -o "$temporary/shadow.sv" \
	2>"$temporary/shadow.err"
grep -q 'warning: pipeline-local declaration shadows a module-local name' \
	"$temporary/shadow.err"
grep -q 'warning: stage-local declaration shadows a less-local name' \
	"$temporary/shadow.err"

if "$tool" tests/pipeline_wire_transfer_error.pigen \
	-o "$temporary/wire-transfer.sv" 2>"$temporary/wire-transfer.err"; then
	echo 'stage-local wire transfer unexpectedly compiled' >&2
	exit 1
fi
grep -q 'wire is not a transfer destination' "$temporary/wire-transfer.err"

if "$tool" tests/pipeline_export_error.pigen -o "$temporary/export.sv" \
	2>"$temporary/export.err"; then
	echo 'reserved export unexpectedly compiled' >&2
	exit 1
fi
grep -q 'export.*reserved but not yet supported' "$temporary/export.err"

if "$tool" tests/pipeline_first_stage_field_error.pigen \
	-o "$temporary/first-stage.sv" 2>"$temporary/first-stage.err"; then
	echo 'first-stage pipeline-local read unexpectedly compiled' >&2
	exit 1
fi
grep -q 'first-stage expression cannot read a pipeline-local value' \
	"$temporary/first-stage.err"

echo "PASS: former syntax is rejected and lexical shadowing is diagnosed"
