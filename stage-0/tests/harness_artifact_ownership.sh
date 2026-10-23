#!/bin/sh
# Hidden pin for the C-test harness artifact-ownership property.
#
# The production Makefile's fifteen pigen-*-test compile/execute targets must
# build and execute a fresh, invocation-owned binary rather than a fixed shared
# path /tmp/pigen-<name>. A fixed path is a defect: a run leaves that binary
# behind and executes whatever occupies the fixed path rather than the binary it
# just compiled. This packet witnesses the property on the CURRENT harness and is
# therefore red today. Once each target builds into a fresh invocation-owned
# directory, runs that directory's binary, and removes the directory, every
# assertion holds and the packet goes green.
#
# The packet is the canonical witness on all fifteen pigen-*-test targets,
# source-test being the representative. It runs a single make target at a time and
# never edits the Makefile, src/, or any C test; it only drives the public make
# targets and observes the shared fixed path they own.
#
# The properties, witnessed per target:
#   (1) NO FIXED PATH: a clean run leaves no executable behind at /tmp/pigen-<name>.
#   (2) NO FIXED-PATH EXECUTANT-REUSE: a RUNNABLE sentinel pre-placed on the fixed
#       path must not execute. A harness that runs whatever occupies the fixed path
#       without recompiling runs this sentinel and fires the marker; the witness
#       then fails. This is the deterministic, state-based form of the
#       invocation-owned-execution requirement.
#   (3) INVOCATION-OWNED DIR CLEANED: the fresh mktemp -d directory the harness
#       created for the run must be removed, so no /tmp/tmp.* created during the
#       run survives after it. The witness records the pre-run set of /tmp/tmp.*
#       and fails if the post-run set differs.
#
# Feasibility boundary (what this state-based witness can and cannot catch):
#   - The "leaves the invocation-owned directory behind" escape IS caught. A
#     leftover mktemp -d dir is durable and observable post-run, so the witness
#     records the pre-run set of /tmp/tmp.* and fails if any created during the
#     run survive (property 3).
#   - The build-to-fixed-then-delete escape is NOT caught here: that escape
#     compiles the real test to the fixed path (truncating the sentinel in place,
#     same inode), executes the freshly compiled binary (not the sentinel, so no
#     marker fires), and then rm -f's the fixed path. Its final observable state
#     -- fixed path absent, no marker, make rc 0, no leftover dir -- is
#     byte-for-byte identical to the correct mktemp -d recipe (rm the fixed path,
#     build into a fresh dir, run it, remove the dir). A deterministic, state-based,
#     POSIX sh witness cannot separate two recipes with identical post-run state;
#     only an event-based observation of the fixed path being written during the
#     run (for example an inotify watch) can, and that is Linux-specific (not POSIX
#     sh) and racy across runs. Catching that escape therefore requires either
#     relaxing the POSIX-sh / determinism requirement to an inotify watch, or a
#     contract change that leaves a durable per-run trace (for example a run-unique
#     output path the harness must publish). The sentinel above is the strongest
#     state-based witness the contract currently permits for that escape: it pins
#     the current Makefile's left-behind fixed path (property 1, red today) and
#     rejects any harness that executes the fixed-path occupant without
#     recompiling (property 2).
#
# The sentinel is a compiled C program: when executed it records its own path via
# /proc/self/exe, drops a fired marker, and exits 0. A correct harness (rm the
# legacy fixed path, mktemp -d, compile into $dir/<name>, run $dir/<name>, remove
# $dir) never executes the fixed path, so the marker is never written and the
# fixed path is left absent.
set -eu

# The per-target shared fixed path and the sentinel's fired marker.
SENTINEL_C=/tmp/pigen-ownership-sentinel.c

# Leave no scratch behind, even when a witness fails under set -e.
cleanup() { rm -f "$SENTINEL_C" /tmp/pigen-ownership-fired-*; }
trap cleanup EXIT

# Record the path of the running executable. Used by the sentinel so the witness
# can distinguish "the sentinel executed" from "a different binary executed at
# the fixed path".
cat > "$SENTINEL_C" <<'SENTINEL'
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc < 2) return 77;
    FILE *f = fopen(argv[1], "a");
    if (f == NULL) return 78;
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n < 0) return 79;
    exe[n] = '\0';
    fprintf(f, "%s\n", exe);
    fclose(f);
    return 0;
}
SENTINEL

# Witness one target. $1 is the target name, the remaining args are extra make
# arguments (for sectioned targets such as rtl-lower-test). It returns nonzero the
# moment any single property fails, with a message naming the target and the
# violated property; it returns zero only if every property holds.
witness() {
    tgt=$1
    shift
    fixed=/tmp/pigen-$tgt
    marker=/tmp/pigen-ownership-fired-$tgt

    # Start from a clean fixed path and a clean marker.
    rm -f "$fixed" "$marker"

    # Occupy the fixed path with a RUNNABLE sentinel before the run. The sentinel
    # records its own execution (path) into the marker. A harness that executes
    # the fixed path either runs this sentinel (firing the marker) or overwrites
    # it and runs a freshly compiled binary at the shared path.
    cc -std=c17 -O0 -o "$fixed" "$SENTINEL_C"
    chmod 755 "$fixed"

    # Record the invocation-owned directories present before the run. Any /tmp/tmp.*
    # directory the harness creates (mktemp -d) and removes leaves this set
    # unchanged; a leftover dir is durable and post-run observable (property 3).
    before_dirs=$(ls -d /tmp/tmp.* 2>/dev/null | LC_ALL=C sort)

    # Run the target and preserve its status. Under the broken recipe the compile
    # overwrites the sentinel, the run executes the fixed binary, and (for the
    # build-to-fixed-then-delete escape) the fixed binary is removed afterward.
    rc=0
    make $tgt "$@" >/dev/null 2>&1 || rc=$?

    # (1) NO FIXED PATH: a clean run leaves no executable behind at the shared
    #     fixed path.
    if [ -e "$fixed" ]; then
        printf '%s: property 1 NO FIXED PATH failed: run left an executable at %s\n' "$tgt" "$fixed" >&2
        return 1
    fi

    # (2) NO FIXED-PATH EXECUTION: the sentinel must not have fired. A harness
    #     that runs whatever occupies the fixed path without recompiling runs the
    #     sentinel and fires the marker; the marker records the executed path.
    #     (The build-to-fixed-then-delete escape is NOT caught here: it overwrites
    #     the sentinel with the real binary, so the sentinel does not fire; see the
    #     feasibility boundary in the header.)
    if [ -e "$marker" ]; then
        printf '%s: property 2 NO FIXED-PATH EXECUTION failed: the fixed-path occupant executed %s\n' "$tgt" "$fixed" >&2
        cat "$marker" >&2
        return 1
    fi

    # The target's own test must report its genuine status (0); a masked or
    # sentinel-driven run would not.
    if [ "$rc" -ne 0 ]; then
        printf '%s: property 2 NO FIXED-PATH EXECUTION failed: make %s reported status %d (run masked or sentinel consumed)\n' "$tgt" "$tgt" "$rc" >&2
        return 1
    fi

    # (3) INVOCATION-OWNED DIR CLEANED: the fresh mktemp -d directory the harness
    #     created for the run must be removed. Record the post-run set of /tmp/tmp.*
    #     and fail if it differs from the pre-run set (a leftover dir survives).
    after_dirs=$(ls -d /tmp/tmp.* 2>/dev/null | LC_ALL=C sort)
    if [ "$before_dirs" != "$after_dirs" ]; then
        printf '%s: property 3 INVOCATION-OWNED DIR CLEANED failed: run left an invocation-owned directory\n' "$tgt" >&2
        printf '%s\n' "$after_dirs" >&2
        return 1
    fi

    return 0
}

# Witness every one of the fifteen pigen-*-test targets. Sectioned targets are
# given a single representative section so the witness exercises the same
# compile/execute path the harness uses in production; the artifact-ownership
# property is per-target and independent of section choice.
witness source-test
witness preprocess-test
witness transfer-type-test
witness syntax-model-test
witness integer-test
witness semantic-test
witness predicate-test
witness expression-resolve-test
witness expression-use-test
witness resolve-test
witness rtl-test
witness rtl-name-test
witness output-model-test T=t9-skeleton
witness rtl-lower-test T=t5-skeleton
witness sv-emit-test T=t10-skeleton

exit 0
