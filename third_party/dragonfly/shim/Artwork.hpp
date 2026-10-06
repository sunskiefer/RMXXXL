/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the mpc-vst-dragonfly contributors */
/* Shim (mpc-vst-dragonfly): DistrhoPluginInfo.h includes the UI artwork header for its window size.
 * The MPC port has no DPF UI (its page is an MPC skin), so only the two sizes are provided. */
#pragma once
namespace Artwork { static const unsigned int backgroundWidth = 0, backgroundHeight = 0; }
