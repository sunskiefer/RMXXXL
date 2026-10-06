#!/usr/bin/env bash
# Offline tests (x86, ASan + UBSan): the framework's host test, then test/fx_test.cc on the engine itself.
#   MPC_VST=../mpc-vst-plugins test/run_tests.sh
set -euo pipefail
cd "$(dirname "$0")/.."
MPC_VST="${MPC_VST:-$PWD/third_party/mpc-vst-plugins}"
python3 tools/gen_params.py
bash "$MPC_VST/tools/test_port.sh" vst.json
SRCS=$(python3 -c "import json; print(' '.join(json.load(open('vst.json'))['build']['sources']))")
CFLAGS=$(python3 -c "import json; print(' '.join(json.load(open('vst.json'))['build']['cflags']))")
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"
mkdir -p build/fxtest
OBJS=""
for f in $SRCS test/fx_test.cc; do
  o="build/fxtest/$(echo "$f" | tr / _).o"
  g++ $SAN -std=gnu++11 -w $CFLAGS -I"$MPC_VST/wrapper" -c "$f" -o "$o"
  OBJS="$OBJS $o"
done
g++ $SAN $OBJS -lm -ldl -lpthread -o build/fxtest/fx_test
build/fxtest/fx_test
