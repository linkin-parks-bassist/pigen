#!/bin/sh
set -eu

tool=${1:-./pigen}
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT

if "$tool" tests/pipeline_misplaced_yield_error.pigen \
	-o "$temporary/misplaced-yield.sv" 2>"$temporary/misplaced-yield.err"; then
	echo "expected stage-local yield to fail" >&2
	exit 1
fi
grep -q 'yield.*only after the final stage' "$temporary/misplaced-yield.err"

if "$tool" tests/pipeline_shadow.pigen -o "$temporary/shadow.sv" \
	2>"$temporary/shadow.err"; then
	echo 'duplicate pipeline field unexpectedly compiled' >&2
	exit 1
fi
grep -q 'duplicate pipeline field name' "$temporary/shadow.err"

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
grep -q 'buffered transfer cannot source its destination' \
	"$temporary/first-stage.err"

echo "PASS: pipeline grammar and lexical shadowing are diagnosed"
