// Included by rv_<name>.cc with RV_NS / RV_API / RV_NAME / RV_DIR and a preset kind defined. See reverbs.h.
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <cstring>
#include "Param.hpp"
#include "freeverb/earlyref.hpp"
#include "freeverb/zrev2.hpp"
#include "freeverb/nrev.hpp"
#include "freeverb/nrevb.hpp"
#include "freeverb/strev.hpp"
#include "freeverb/progenitor2.hpp"
#include "AbstractDSP.hpp"
#include "DistrhoPlugin.hpp"
#include "extra/ScopedDenormalDisable.hpp"
#include "Artwork.hpp"
#include "reverbs.h"

#define RV_STR2(x) #x
#define RV_STR(x) RV_STR2(x)

namespace RV_NS {
#include RV_STR(../third_party/dragonfly/plugins/RV_DIR/DistrhoPluginInfo.h)
#include RV_STR(../third_party/dragonfly/plugins/RV_DIR/DSP.cpp)

static const float* DefaultValues() {
#if defined(RV_PRESETS_BANKS)
  return banks[DEFAULT_BANK].presets[DEFAULT_PRESET].params;
#else
  return presets[DEFAULT_PRESET].params;
#endif
}

static void* Create(double sr) { return new (std::nothrow) DragonflyReverbDSP(sr); }
static void Destroy(void* d) { delete static_cast<DragonflyReverbDSP*>(d); }
static void Mute(void* d) { static_cast<DragonflyReverbDSP*>(d)->mute(); }
static void Run(void* d, const float** in, float** out, uint32_t n) { static_cast<DragonflyReverbDSP*>(d)->run(in, out, n); }
static int Set(void* d, const char* symbol, float v) {
  for (int i = 0; i < (int)paramCount; ++i) {
    if (strcmp(PARAMS[i].symbol, symbol)) continue;
    if (v < PARAMS[i].range_min) v = PARAMS[i].range_min;
    if (v > PARAMS[i].range_max) v = PARAMS[i].range_max;
    static_cast<DragonflyReverbDSP*>(d)->setParameterValue(i, v);
    return 1;
  }
  return 0;
}
static void LoadDefault(void* d) {
  const float* v = DefaultValues();
  for (int i = 0; i < (int)paramCount; ++i) static_cast<DragonflyReverbDSP*>(d)->setParameterValue(i, v[i]);
}
}  // namespace RV_NS

const ReverbApi RV_API = { RV_NAME, RV_NS::Create, RV_NS::Destroy, RV_NS::Mute, RV_NS::Run, RV_NS::Set, RV_NS::LoadDefault };
