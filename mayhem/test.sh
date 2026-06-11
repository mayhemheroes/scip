#!/usr/bin/env bash
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
emit_ctrf(){ local tool="$1" p="$2" f="$3" s="${4:-0}"; local t=$((p+f+s));
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":0,"skipped":%d,"other":0}}}\n' "$tool" "$t" "$p" "$f" "$s"; [ "$f" -eq 0 ]; }
ORACLE=/mayhem/build-oracle
[ -d "$ORACLE" ] || { emit_ctrf ctest 0 1 0; exit 1; }
cd "$ORACLE"
set +e
out=$(ctest -R readertest -j"${MAYHEM_JOBS:-$(nproc)}" --output-on-failure 2>&1)
rc=$?
set -e
echo "$out" | tail -3
counts=$(python3 -c "
import re,sys
text=open('/dev/stdin').read()
m=re.search(r'(\d+) tests failed out of (\d+)', text)
if m:
 f,t=int(m.group(1)),int(m.group(2)); print(t-f,f,0)
else:
 print(0,1 if $rc else 0,0)
" <<< "$out")
read -r passed failed skipped <<< "$counts"
emit_ctrf ctest "$passed" "$failed" "$skipped"
