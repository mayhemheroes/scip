#!/usr/bin/env bash
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
export SANITIZER_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS
cd "$SRC"
PREFIX=/mayhem/install
mkdir -p "$PREFIX" /mayhem/bin
if [ ! -d /mayhem/soplex-prefix/lib ]; then
  rm -rf /tmp/soplex-src /tmp/soplex-build
  git clone --depth 1 --branch v8.0.2 https://github.com/scipopt/soplex.git /tmp/soplex-src
  cmake -S /tmp/soplex-src -B /tmp/soplex-build -DCMAKE_INSTALL_PREFIX=/mayhem/soplex-prefix -DCMAKE_BUILD_TYPE=Release
  cmake --build /tmp/soplex-build -j"$MAYHEM_JOBS"
  cmake --install /tmp/soplex-build
fi
export SOPLEX_DIR=/mayhem/soplex-prefix
rm -rf /mayhem/build-fuzz
cmake -S "$SRC" -B /mayhem/build-fuzz -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_C_FLAGS="$SANITIZER_FLAGS" -DCMAKE_CXX_FLAGS="$SANITIZER_FLAGS" \
  -DPAPILO=OFF -DZIMPL=OFF -DIPOPT=OFF -DSOPLEX_DIR="$SOPLEX_DIR"
cmake --build /mayhem/build-fuzz -j"$MAYHEM_JOBS"
cmake --install /mayhem/build-fuzz
install -m 0755 "$PREFIX/bin/scip" /mayhem/bin/scip
$CXX $SANITIZER_FLAGS $LIB_FUZZING_ENGINE "$SRC/mayhem/fuzz_SCIPintervalNegateReal.cpp" \
  -I"$PREFIX/include/scip" -I"$PREFIX/include" -L"$PREFIX/lib" -Wl,-rpath,"$PREFIX/lib" -lscip -o /mayhem/fuzz_SCIPintervalNegateReal
rm -rf /mayhem/build-oracle
cmake -S "$SRC" -B /mayhem/build-oracle -DCMAKE_INSTALL_PREFIX=/mayhem/oracle-prefix \
  -DPAPILO=OFF -DZIMPL=OFF -DIPOPT=OFF -DSOPLEX_DIR="$SOPLEX_DIR"
cmake --build /mayhem/build-oracle -j"$MAYHEM_JOBS"
cmake --install /mayhem/build-oracle
echo build complete; ls -la /mayhem/fuzz_SCIPintervalNegateReal /mayhem/bin/scip
