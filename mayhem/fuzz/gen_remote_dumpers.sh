#!/usr/bin/env bash
# mayhem: list every IR/RF protocol dumper remote_base declares, as upstream's codegen does for
# `dump: all` (esphome/components/remote_base/__init__.py), so fuzz_remote_base.cpp decodes the
# input with every in-tree protocol — including ones upstream adds later.
# usage: gen_remote_dumpers.sh <remote_base dir> > dumpers.inc
set -euo pipefail
dir="$1"
for h in "$dir"/*_protocol.h; do
  names="$( (sed -n 's/^DECLARE_REMOTE_PROTOCOL(\([A-Za-z0-9_]*\)).*/\1Dumper/p' "$h"
             sed -n 's/^using \([A-Za-z0-9_]*Dumper\) = RemoteReceiverDumper<.*/\1/p' "$h"
             sed -n 's/^class \([A-Za-z0-9_]*Dumper\) : public RemoteReceiverDumperBase.*/\1/p' "$h") | sort -u)"
  [ -n "$names" ] || continue
  echo "#include \"esphome/components/remote_base/$(basename "$h")\""
  for n in $names; do echo "MAYHEM_DUMPER($n)"; done
done
