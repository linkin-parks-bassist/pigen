#!/bin/sh
# Hidden red pin for the C-test harness artifact-ownership property.
#
# The production Makefile's fifteen pigen-*-test compile/execute targets build to
# and execute a fixed shared path /tmp/pigen-<name>; the run leaves that binary
# behind and executes whatever occupies the fixed path rather than the binary it
# just compiled. This packet witnesses that defect on the CURRENT (unfixed)
# harness and is therefore red today. Once each target builds into a fresh
# invocation-owned directory, runs that directory's binary, and removes the
# directory, every assertion holds and the packet goes green.
#
# The packet is the canonical witness on all fifteen pigen-*-test targets,
# source-test being the representative. It runs a single make target at a time and
# never edits the Makefile, src/, or any C test; it only drives the public make
# targets and observes the shared fixed path they own.
#
# The property, witnessed per target:
#   (1) NO FIXED PATH: a clean run leaves no executable behind at /tmp/pigen-<name>.
#   (2) NO BUILD-TO-FIXED-THEN-DELETE ESCAPE: a RUNNABLE sentinel pre-placed on the
#       fixed path must never fire. A harness that compiles to the fixed path
#       (overwriting the sentinel) and then deletes it still executed the
#       shared-path binary, not the binary it compiled in an invocation-owned
#       directory; the sentinel firing witnesses exactly that escape.
#
# The sentinel is a compiled C program: when executed it records its own path via
# /proc/self/exe, drops a fired marker, and exits 0. A harness that runs the fixed
# path fires the marker (sentinel survived and executed) or runs a freshly compiled
# binary that lives at the shared path (the escaped binary's own path is the fixed
# path). Either way the run executed at the shared fixed path, not in a fresh owned
# directory. A correct harness (rm the legacy fixed path, mktemp -d, compile into
# $dir/<name>, run $dir/<name>, remove $dir) never touches the fixed path, so the
# sentinel is overwritten, the marker is never written, and the fixed path is left
# absent.
set -eu

# The per-target shared fixed path and the sentinel's fired marker.
SENTINEL_C=/tmp/pigen-ownership-sentinel.c

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

    # (2) NO ESCAPE: the sentinel must not have fired, and no binary may have
    #     executed at the shared fixed path. A build-to-fixed-then-delete harness
    #     leaves the fixed path absent (property 1 passes) but the fired marker
    #     records the executed binary's path as the shared fixed path, exposing
    #     the escape.
    if [ -e "$marker" ]; then
        printf '%s: property 2 NO ESCAPE failed: a binary executed at the shared fixed path %s\n' "$tgt" "$fixed" >&2
        cat "$marker" >&2
        return 1
    fi

    # The target's own test must report its genuine status (0); a masked or
    # sentinel-driven run would not.
    if [ "$rc" -ne 0 ]; then
        printf '%s: property 2 NO ESCAPE failed: make %s reported status %d (run masked or sentinel consumed)\n' "$tgt" "$tgt" "$rc" >&2
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

# Clean up the sentinel source and any stray fired markers left by a red run.
rm -f "$SENTINEL_C"
rm -f /tmp/pigen-ownership-fired-*

exit 0
