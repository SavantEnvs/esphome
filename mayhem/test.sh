#!/usr/bin/env bash
#
# mayhem/test.sh — RUN upstream's C++ unit tests (tests/components/<c>/*.cpp, googletest) that
# mayhem/build.sh compiled into mayhem/out/bin/esphome_cpp_tests, and report CTRF counts.
# The tests assert decoded values, emitted frames and published states (known-answer tests).
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

runner="$SRC/mayhem/out/bin/esphome_cpp_tests"
if [ ! -x "$runner" ]; then
  echo "test.sh: $runner missing — mayhem/build.sh must build it" >&2
  emit_ctrf gtest 0 1
  exit 1
fi

# The host preferences backend writes under $HOME; keep it in a scratch dir.
work="$(mktemp -d)"
log="$work/gtest.log"
( cd "$work" && HOME="$work" "$runner" --gtest_brief=1 ) > "$log" 2>&1
rc=$?
tail -n 20 "$log"

ran="$(sed -n 's/^\[==========\] \([0-9]*\) tests\{0,1\} from .* ran\..*/\1/p' "$log" | tail -1)"
passed="$(sed -n 's/^\[  PASSED  \] \([0-9]*\) tests\{0,1\}\..*/\1/p' "$log" | tail -1)"
failed="$(sed -n 's/^\[  FAILED  \] \([0-9]*\) tests\{0,1\}, listed below:.*/\1/p' "$log" | tail -1)"
skipped="$(sed -n 's/^\[  SKIPPED \] \([0-9]*\) tests\{0,1\}, listed below:.*/\1/p' "$log" | tail -1)"
rm -rf "$work"
: "${passed:=0}" "${failed:=0}" "${skipped:=0}"
# No gtest summary (the runner died or never ran a test) or a non-zero exit is a failure.
if [ -z "$ran" ] || [ "$ran" -eq 0 ]; then
  echo "test.sh: no gtest summary (exit $rc)" >&2
  failed=$(( failed + 1 ))
elif [ "$rc" -ne 0 ] && [ "$failed" -eq 0 ]; then
  echo "test.sh: runner exited $rc" >&2
  failed=1
fi
emit_ctrf gtest "$passed" "$failed" "$skipped"
