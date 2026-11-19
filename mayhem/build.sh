#!/usr/bin/env bash
#
# mayhem/build.sh — build ESPHome's device firmware (C++ core + selected components) for the
# `host` platform, the way upstream's own host build does, plus the fuzz harnesses in mayhem/fuzz/
# and upstream's C++ unit tests (tests/components/<c>/*.cpp, gtest) as the functional suite.
#
# Upstream's build tool (esphome/build_gen, driven by Python codegen from a YAML config) copies
# esphome/ into a build dir and writes a generated esphome/core/defines.h there. We do the same
# without Python: stage a symlink tree of esphome/ under mayhem/out/ and drop mayhem/fuzz/defines.h
# in as esphome/core/defines.h, then compile the staged sources directly (no network, no codegen).
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${ARDUINOJSON_DIR:=/opt/toolchains/deps/arduinojson-7.4.3/src}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
OUT="$SRC/mayhem/out"
STAGE="$OUT/src"

# Components compiled into the firmware library (each fuzz target links only what it uses).
COMPONENTS=(
  uart sensor text_sensor binary_sensor json
  teleinfo fingerprint_grow sml modbus remote_base ld2450 packet_transport xxtea api socket
  light e131 tuya
)

# 1) Stage the source tree (rebuilt every run so edits to esphome/ are always picked up).
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -as "$SRC/esphome" "$STAGE/esphome"
rm -f "$STAGE/esphome/core/defines.h"
cp "$SRC/mayhem/fuzz/defines.h" "$STAGE/esphome/core/defines.h"

# remote_base: the list of every protocol dumper (what upstream's codegen emits for `dump: all`).
mkdir -p "$OUT/gen"
bash "$SRC/mayhem/fuzz/gen_remote_dumpers.sh" "$STAGE/esphome/components/remote_base" > "$OUT/gen/remote_base_dumpers.inc"

sources() {
  ls "$STAGE"/esphome/core/*.cpp
  echo "$STAGE/esphome/core/wake/wake_host.cpp"
  for f in core gpio hal helpers preferences; do echo "$STAGE/esphome/components/host/$f.cpp"; done
  for c in "${COMPONENTS[@]}"; do
    case "$c" in
      logger) sources_logger ;;
      # the native API, except the Noise transport (needs the noise-c library, not cached)
      api) find "$STAGE/esphome/components/api" -maxdepth 1 -name '*.cpp' ! -name 'api_frame_helper_noise.cpp' | sort ;;
      socket) for f in socket bsd_sockets_impl; do echo "$STAGE/esphome/components/socket/$f.cpp"; done ;;
      uart) for f in uart uart_component; do echo "$STAGE/esphome/components/uart/$f.cpp"; done ;;
      # the component plus its entity platforms whose base components are compiled too
      *) find "$STAGE/esphome/components/$c" -maxdepth 2 -name '*.cpp' \
           \( -path "*/$c/*.cpp" -a ! -path "*/$c/*/*" -o -path "*/sensor/*" -o -path "*/text_sensor/*" \
              -o -path "*/binary_sensor/*" \) | sort ;;
    esac
  done
}

# -ffile-prefix-map: the staged tree is symlinks into esphome/, so debug info and __FILE__ name the
# upstream file (/mayhem/esphome/...) rather than the staging directory.
COMMON_CXX=(-std=gnu++20 -DUSE_HOST -fno-exceptions -Wno-unused-result -ffile-prefix-map="$STAGE"="$SRC"
  -I"$STAGE" -I"$ARDUINOJSON_DIR/src" -I"$SRC/mayhem/fuzz" -I"$OUT/gen")

# build_lib <objdir> <flags...>: compile every staged source into <objdir>/libesphome.a
build_lib() {
  local objdir="$1"; shift
  rm -rf "$objdir"; mkdir -p "$objdir"
  sources | while read -r f; do
    rel="${f#"$STAGE"/}"; echo "$f $objdir/${rel//\//_}.o"
  done > "$objdir/units.txt"
  # each line of units.txt is "<src> <obj>"; compile them in parallel with the flags in UNIT_FLAGS
  UNIT_FLAGS="${COMMON_CXX[*]} $*" xargs -P "$MAYHEM_JOBS" -L 1 \
    sh -c '$CXX $UNIT_FLAGS -c "$0" -o "$1"' < "$objdir/units.txt" || true
  # xargs' status is not enough to name the failing unit; check every object exists
  while read -r src obj; do [ -f "$obj" ] || { echo "compile failed: $src" >&2; exit 1; }; done < "$objdir/units.txt"
  rm -f "$objdir/libesphome.a"
  ar rcs "$objdir/libesphome.a" "$objdir"/*.o
}

# 2) Fuzz build: the firmware library and every harness instrumented with ASan+UBSan + DWARF 3.
FUZZ_FLAGS=($SANITIZER_FLAGS $DEBUG_FLAGS -O1)
# libFuzzer owns main(): the host platform's main() (components/host/core.cpp) is renamed in this
# build only; the harnesses construct App themselves (mayhem/fuzz/fuzz_env.cpp), as upstream's
# test main does.
build_lib "$OUT/fuzz-obj" "${FUZZ_FLAGS[@]}" -fsanitize=fuzzer-no-link -Dmain=esphome_host_main
mkdir -p "$OUT/bin"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/standalone_main.o"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$OUT/lsan_off.o"
$CXX "${COMMON_CXX[@]}" "${FUZZ_FLAGS[@]}" -c "$SRC/mayhem/fuzz/fuzz_env.cpp" -o "$OUT/fuzz_env.o"
for h in "$SRC"/mayhem/fuzz/fuzz_*.cpp; do
  name="$(basename "$h" .cpp)"
  [ "$name" = fuzz_env ] && continue
  $CXX "${COMMON_CXX[@]}" "${FUZZ_FLAGS[@]}" -fsanitize=fuzzer-no-link -c "$h" -o "$OUT/$name.o"
  $CXX "${FUZZ_FLAGS[@]}" $LIB_FUZZING_ENGINE "$OUT/$name.o" "$OUT/fuzz_env.o" "$OUT/lsan_off.o" \
    "$OUT/fuzz-obj/libesphome.a" -lpthread -lm -o "$OUT/bin/$name"
  $CXX "${FUZZ_FLAGS[@]}" "$OUT/standalone_main.o" "$OUT/$name.o" "$OUT/fuzz_env.o" "$OUT/lsan_off.o" \
    "$OUT/fuzz-obj/libesphome.a" -lpthread -lm -o "$OUT/bin/$name-standalone"
done

# 3) Functional suite: upstream's own C++ unit tests (tests/components/<c>/, googletest) for the
#    compiled components, built with the project's normal flags (no sanitizers) and linked with
#    upstream's test main (tests/components/main.cpp) and the host platform's main(), as
#    script/cpp_unit_test.py does.
TEST_COMPONENTS=(teleinfo json modbus remote_base ld2450 packet_transport)
TEST_FLAGS=(-O1 -g -DESPHOME_DEBUG -DESPHOME_LOG_LEVEL=ESPHOME_LOG_LEVEL_DEBUG -DMAYHEM_TEST_BUILD $COVERAGE_FLAGS)
COMPONENTS+=(logger)
sources_logger() { for f in logger logger_host task_log_buffer_host; do echo "$STAGE/esphome/components/logger/$f.cpp"; done; }
build_lib "$OUT/test-obj" "${TEST_FLAGS[@]}"
mkdir -p "$OUT/test-obj/tests"
rm -rf "$STAGE/tests"; mkdir -p "$STAGE/tests"
for c in "${TEST_COMPONENTS[@]}"; do cp -as "$SRC/tests/components/$c" "$STAGE/tests/$c"; done
# Tests of entity platforms this build does not compile (number, switch, select, button) or of
# codegen-generated tables (the remote_base dumper registry) are left out.
TEST_EXCLUDE='/ld2450/(number|switch)/|/ld2450_zone_number\.cpp|/ld2450_presence_timeout\.cpp|/receiver_dumpers_test\.cpp'
find "$STAGE/tests" -name '*.cpp' | grep -Ev "$TEST_EXCLUDE" | sort > "$OUT/test-obj/tests/list.txt"
echo "$SRC/tests/components/main.cpp" >> "$OUT/test-obj/tests/list.txt"
while read -r f; do
  rel="${f#"$STAGE"/}"; rel="${rel#"$SRC"/}"; echo "$f $OUT/test-obj/tests/${rel//\//_}.o"
done < "$OUT/test-obj/tests/list.txt" > "$OUT/test-obj/tests/units.txt"
UNIT_FLAGS="${COMMON_CXX[*]} ${TEST_FLAGS[*]}" xargs -P "$MAYHEM_JOBS" -L 1 \
  sh -c '$CXX $UNIT_FLAGS -c "$0" -o "$1"' < "$OUT/test-obj/tests/units.txt" || true
while read -r src obj; do [ -f "$obj" ] || { echo "compile failed: $src" >&2; exit 1; }; done < "$OUT/test-obj/tests/units.txt"
$CXX "${TEST_FLAGS[@]}" "$OUT"/test-obj/tests/*.o "$OUT/test-obj/libesphome.a" -lgmock -lgtest -lpthread -lm \
  -o "$OUT/bin/esphome_cpp_tests"

echo "build.sh: done"
