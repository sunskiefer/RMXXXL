/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the mpc-vst-dragonfly contributors */
/* Shim for the two things Dragonfly's DSP.cpp takes from DPF's DistrhoPlugin.hpp (mpc-vst-dragonfly).
 * The MPC port has no DPF: the VST2 layer is vst/dragonfly_vst.cpp. */
#pragma once
#include <cmath>
#include <cstdint>
#include <limits>

template <typename T>
static inline bool d_isNotEqual(const T &v1, const T &v2) {
    return std::abs(v1 - v2) >= std::numeric_limits<T>::epsilon();
}
