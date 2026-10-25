#!/bin/sh
# Hidden pin for the CLI diagnostic contract of the pigen binary (src/pigen.c).
#
# The pigen driver must report a spanned compile error in the classic
# compiler form and keep a bare prefix for spanless stage failures:
#
#   (1) SPANNED ERROR: ./pigen tests/basic.pigen (no -o) must exit 1,
#       write NO output file, print nothing to stdout, and print to stderr
#       exactly this 3-line form:
#
#         tests/basic.pigen:4:5: data type must precede the transfer type
#                 buf  [7:0] stage;
#                 ^
#
#       First line "<input-path>:<line>:<column>: <message>" (a
#       colon-separated path:line:column prefix), second line the
#       offending source line prefixed with exactly 4 spaces (its own
#       leading whitespace preserved, so the line-4 witness reads as 8
#       leading spaces), third line exactly 4 spaces then (column-1)
#       spaces then a single caret. This assertion is RED on
#       main, which prints only the bare
#       "pigen: data type must precede the transfer type" with no
#       path/line/column, source line or caret, and turns GREEN once the
#       spanned report port is present.
#
#   (2) SPANLESS ERROR: ./pigen tests/direct_fabric_block.pigen (no -o) must
#       exit 1, write NO output file, print nothing to stdout, and print to
#       stderr exactly the single line "pigen: RTL naming failed" (bare
#       "pigen: " prefix, no path/line/column, no source line, no caret).
#       Regression guard for the port's spanless fallback path.
#
#   (3) SILENT SUCCESS: ./pigen tests/owned_signal.pigen -o <tmp>/out.sv must
#       exit 0, write a non-empty out.sv, and print nothing to stdout and
#       nothing to stderr.
#
# POSIX sh, set -eu, self-contained, runnable as
# `sh tests/cli_diagnostics_test.sh` from the repo root. It builds the real
# driver through the Makefile's `pigen` target and asserts every behavior
# against the actual ./pigen executable; it does not re-implement
# diagnostics and does not modify production. Each case runs into its own
# mktemp -d directory, cleaned on exit (harness artifact-ownership).
set -eu

make pigen

d1=$(mktemp -d)
d2=$(mktemp -d)
d3=$(mktemp -d)
trap 'rm -rf "$d1" "$d2" "$d3"' EXIT

# (1) SPANNED ERROR: the discriminator this packet exists to enforce.
./pigen tests/basic.pigen >"$d1/stdout" 2>"$d1/stderr" && {
	printf 'spanned: expected exit 1\n' >&2
	exit 1
}
[ "$(wc -c <"$d1/stdout")" -eq 0 ] || {
	printf 'spanned: stdout must be empty, got:\n' >&2
	cat "$d1/stdout" >&2
	exit 1
}
[ ! -e tests/basic.sv ] || {
	printf 'spanned: a failed compile must not write the output file\n' >&2
	exit 1
}
[ "$(wc -l <"$d1/stderr")" -eq 3 ] || {
	printf 'spanned: stderr must be exactly 3 lines, got:\n' >&2
	cat "$d1/stderr" >&2
	exit 1
}
[ "$(sed -n 1p "$d1/stderr")" = 'tests/basic.pigen:4:5: data type must precede the transfer type' ] || {
	printf 'spanned: first line must be the path:line:column message, got:\n' >&2
	sed -n 1p "$d1/stderr" >&2
	exit 1
}
[ "$(sed -n 2p "$d1/stderr")" = '        buf  [7:0] stage;' ] || {
	printf 'spanned: second line must be the offending source line prefixed with 4 spaces, got:\n' >&2
	sed -n 2p "$d1/stderr" >&2
	exit 1
}
[ "$(sed -n 3p "$d1/stderr")" = '        ^' ] || {
	printf 'spanned: third line must be 4 spaces, (column-1) spaces and a caret, got:\n' >&2
	sed -n 3p "$d1/stderr" >&2
	exit 1
}

# (2) SPANLESS ERROR: regression guard for the bare "pigen: " fallback.
./pigen tests/direct_fabric_block.pigen >"$d2/stdout" 2>"$d2/stderr" && {
	printf 'spanless: expected exit 1\n' >&2
	exit 1
}
[ "$(wc -c <"$d2/stdout")" -eq 0 ] || {
	printf 'spanless: stdout must be empty, got:\n' >&2
	cat "$d2/stdout" >&2
	exit 1
}
[ ! -e tests/direct_fabric_block.sv ] || {
	printf 'spanless: a failed compile must not write the output file\n' >&2
	exit 1
}
[ "$(cat "$d2/stderr")" = 'pigen: RTL naming failed' ] || {
	printf 'spanless: stderr must be exactly one line "pigen: RTL naming failed", got:\n' >&2
	cat "$d2/stderr" >&2
	exit 1
}
[ "$(wc -l <"$d2/stderr")" -eq 1 ] || {
	printf 'spanless: stderr must be exactly 1 line\n' >&2
	exit 1
}

# (3) SILENT SUCCESS: write the output, say nothing.
./pigen tests/owned_signal.pigen -o "$d3/out.sv" >"$d3/stdout" 2>"$d3/stderr" || {
	printf 'success: expected exit 0, stderr was:\n' >&2
	cat "$d3/stderr" >&2
	exit 1
}
[ "$(wc -c <"$d3/stdout")" -eq 0 ] || {
	printf 'success: stdout must be empty, got:\n' >&2
	cat "$d3/stdout" >&2
	exit 1
}
[ "$(wc -c <"$d3/stderr")" -eq 0 ] || {
	printf 'success: stderr must be empty, got:\n' >&2
	cat "$d3/stderr" >&2
	exit 1
}
[ -s "$d3/out.sv" ] || {
	printf 'success: out.sv must be written and non-empty\n' >&2
	exit 1
}

exit 0
