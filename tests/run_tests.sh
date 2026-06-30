#!/bin/sh
# LamoLanguage test runner (POSIX sh).
#
# Tests are organized under tests/:
#   tests/valid/*.lamo    - programs that must `lamo check` successfully
#   tests/invalid/*.lamo  - programs that must FAIL `lamo check`
#   tests/runtime/*.lamo  - programs that must `lamo run` and produce stdout
#                            matching the sibling .expected file
#
# Usage:
#   sh tests/run_tests.sh           # auto-detect ./lamo or ./lamo.exe
#   sh tests/run_tests.sh /path/lamo
#
# Exit code: 0 if all tests passed, 1 otherwise.

set -u

# ---------------------------------------------------------------------------
# Resolve the Lamo binary.
# ---------------------------------------------------------------------------
if [ $# -ge 1 ]; then
    LAMO="$1"
else
    if [ -x "./lamo" ]; then
        LAMO="./lamo"
    elif [ -x "./lamo.exe" ]; then
        LAMO="./lamo.exe"
    else
        echo "error: lamo binary not found. Build it first with 'make' or pass its path as arg." >&2
        exit 2
    fi
fi

TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"
VALID_DIR="$TESTS_DIR/valid"
INVALID_DIR="$TESTS_DIR/invalid"
RUNTIME_DIR="$TESTS_DIR/runtime"

TMP_DIR="$(mktemp -d 2>/dev/null || mktemp -d /tmp/lamo.XXXXXX)"
trap 'rm -rf "$TMP_DIR"' EXIT INT TERM

PASS=0
FAIL=0
FAILED_CASES=""

record_pass() {
    PASS=$((PASS + 1))
}

record_fail() {
    FAIL=$((FAIL + 1))
    FAILED_CASES="$FAILED_CASES
  - $1"
}

# Sprint 1 fix: prevent runaway runtime tests from hanging the suite.
# Try GNU coreutils `timeout`, then the BusyBox `timeout` (same syntax),
# then `gtimeout` (macOS Homebrew coreutils), then fall back to bare
# execution if none is available (we still run the test, just without
# protection — same as before this fix).
TIMEOUT_BIN=""
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_BIN="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_BIN="gtimeout"
fi

# Wraps a command with a 10s timeout if a timeout binary is available.
# Usage: run_with_timeout <cmd> <args...>
# Returns the exit status of the wrapped command (or 124 on timeout).
run_with_timeout() {
    if [ -n "$TIMEOUT_BIN" ]; then
        "$TIMEOUT_BIN" 10 "$@"
    else
        "$@"
    fi
}

# ---------------------------------------------------------------------------
# 1. Valid cases: must pass `lamo check` with exit 0.
# ---------------------------------------------------------------------------
echo "== Valid programs (must check successfully) =="
if [ -d "$VALID_DIR" ]; then
    for src in "$VALID_DIR"/*.lamo; do
        [ -e "$src" ] || continue
        name=$(basename "$src")
        if "$LAMO" check "$src" >"$TMP_DIR/out" 2>"$TMP_DIR/err"; then
            record_pass
            printf "  PASS  %s\n" "$name"
        else
            record_fail "valid/$name"
            printf "  FAIL  %s\n" "$name"
            sed 's/^/        | /' "$TMP_DIR/err" >&2
        fi
    done
fi

# ---------------------------------------------------------------------------
# 2. Invalid cases: must FAIL `lamo check` with non-zero exit.
# ---------------------------------------------------------------------------
echo
echo "== Invalid programs (must fail check) =="
if [ -d "$INVALID_DIR" ]; then
    for src in "$INVALID_DIR"/*.lamo; do
        [ -e "$src" ] || continue
        name=$(basename "$src")
        if "$LAMO" check "$src" >"$TMP_DIR/out" 2>"$TMP_DIR/err"; then
            record_fail "invalid/$name (accepted but should have been rejected)"
            printf "  FAIL  %s (accepted but should have been rejected)\n" "$name"
        else
            record_pass
            printf "  PASS  %s\n" "$name"
        fi
    done
fi

# ---------------------------------------------------------------------------
# 3. Runtime cases: must `lamo run` and produce stdout matching .expected.
# ---------------------------------------------------------------------------
echo
echo "== Runtime cases (must run and match expected stdout) =="
if [ -d "$RUNTIME_DIR" ]; then
    for src in "$RUNTIME_DIR"/*.lamo; do
        [ -e "$src" ] || continue
        name=$(basename "$src")
        expected_file="${src%.lamo}.expected"
        stdin_file="${src%.lamo}.stdin"
        [ -e "$stdin_file" ] || stdin_file=""
        if [ ! -e "$expected_file" ]; then
            record_fail "runtime/$name (missing .expected file)"
            printf "  FAIL  %s (missing .expected file)\n" "$name"
            continue
        fi
        if run_with_timeout "$LAMO" run "$src" >"$TMP_DIR/actual" 2>"$TMP_DIR/err" <"${stdin_file:-/dev/null}"; then
            # Strip trailing whitespace differences by trimming both files.
            # `lamo run` may emit trailing spaces depending on backend; we
            # compare ignoring them.
            tr -d '\r' < "$TMP_DIR/actual" > "$TMP_DIR/actual_clean"
            tr -d '\r' < "$expected_file" > "$TMP_DIR/expected_clean"
            if diff -u "$TMP_DIR/expected_clean" "$TMP_DIR/actual_clean" >"$TMP_DIR/diff" 2>&1; then
                record_pass
                printf "  PASS  %s\n" "$name"
            else
                record_fail "runtime/$name (stdout mismatch)"
                printf "  FAIL  %s (stdout mismatch)\n" "$name"
                sed 's/^/        | /' "$TMP_DIR/diff" >&2
            fi
        else
            rc=$?
            if [ "$rc" = 124 ]; then
                record_fail "runtime/$name (timed out after 10s)"
                printf "  FAIL  %s (timed out after 10s)\n" "$name"
            else
                record_fail "runtime/$name (run failed)"
                printf "  FAIL  %s (run failed)\n" "$name"
                sed 's/^/        | /' "$TMP_DIR/err" >&2
            fi
        fi
    done
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo
echo "=========================================="
printf "Total: %d passed, %d failed\n" "$PASS" "$FAIL"
if [ "$FAIL" -ne 0 ]; then
    echo "Failed cases:$FAILED_CASES"
    exit 1
fi
exit 0
