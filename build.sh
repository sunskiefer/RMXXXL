#!/usr/bin/env bash
# Build RMXXXL for MPC OS Gen1 (armv7-a hard-float, glibc <= 2.31 so it also loads on MPC OS 2.x).
#   MPC_VST=../mpc-vst-plugins ./build.sh
# Needs: python3 + Pillow, a host gcc (for shadow_art), and Zig (pip install ziglang). No Docker.
# Output: build/package/ANDREALPHEUS - VST - RMXXXL/  (skin + rmxxxl.so) and build/pluginlist-entry.xml
set -euo pipefail
cd "$(dirname "$0")"
MPC_VST="${MPC_VST:-$PWD/third_party/mpc-vst-plugins}"
ZIG="${ZIG:-python3 -m ziglang}"
python3 tools/gen_params.py
mkdir -p build/host build/arm
[ -x build/host/shadow_art ] || gcc -O2 -w -I"$MPC_VST/tools/vendor/force-shadow/tools" -x c -o build/host/shadow_art "$MPC_VST/tools/shadow_art.c" -lm
rm -rf build/skin
SHADOW_TITLE_FONT="$PWD/art/fonts/TitilliumWeb-SemiBold.ttf" SHADOW_ART="$PWD/build/host/shadow_art" python3 "$MPC_VST/tools/gen_vst.py" vst.json
python3 tools/apply_knobs.py "build/skin/ANDREALPHEUS - VST - RMXXXL/Plugin Skins"   # BLACK / GREY knobs + ticks
python3 tools/post_skin.py "build/skin/ANDREALPHEUS - VST - RMXXXL/Plugin Skins" layout.conf   # Titillium text, pads
SRCS=$(python3 -c "import json; print(' '.join(json.load(open('vst.json'))['build']['sources']))")
CFLAGS=$(python3 -c "import json; print(' '.join(json.load(open('vst.json'))['build']['cflags']))")
TGT="-target arm-linux-gnueabihf.2.31 -mcpu=generic+v7a+vfp3d16-d32-neon+thumb2 -ffp-contract=off"
COMMON="-O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -w"
OBJS=""
for f in $SRCS; do
  o="build/arm/$(echo "$f" | tr / _).o"
  $ZIG c++ $TGT $COMMON -std=gnu++11 $CFLAGS -Ibuild -I"$MPC_VST/wrapper" -c "$f" -o "$o"
  OBJS="$OBJS $o"
done
$ZIG cc $TGT $COMMON -std=gnu11 -Ibuild -c "$MPC_VST/wrapper/vst2_wrap.c" -o build/arm/vst2_wrap.o
printf '{\n  global: VSTPluginMain;\n  local: *;\n};\n' > build/arm/exports.map
$ZIG c++ $TGT -shared -fPIC -Wl,--no-undefined -Wl,--version-script=build/arm/exports.map -Wl,--gc-sections -Wl,-s \
  $OBJS build/arm/vst2_wrap.o -lm -ldl -lpthread -o build/arm/rmxxxl.so
PKG="build/package/ANDREALPHEUS - VST - RMXXXL"
rm -rf build/package && mkdir -p "$PKG"
cp -r "build/skin/ANDREALPHEUS - VST - RMXXXL/." "$PKG/"
cp build/arm/rmxxxl.so "$PKG/"
echo "-> $PKG"
