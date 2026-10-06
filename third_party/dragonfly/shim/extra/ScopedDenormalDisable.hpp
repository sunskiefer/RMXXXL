/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the mpc-vst-dragonfly contributors */
/* Shim for DPF's extra/ScopedDenormalDisable.hpp (mpc-vst-dragonfly): flush denormals to zero
 * for the duration of a DSP run. A decaying reverb tail is exactly where denormals appear. */
#pragma once
#include <cstdint>
#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

class ScopedDenormalDisable {
public:
    ScopedDenormalDisable() noexcept {
#if defined(__arm__)
        __asm__ __volatile__("vmrs %0, fpscr" : "=r"(old));
        uint32_t fz = old | (1u << 24);                 /* FPSCR.FZ */
        __asm__ __volatile__("vmsr fpscr, %0" : : "r"(fz));
#elif defined(__aarch64__)
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(old64));
        uint64_t fz = old64 | (1ull << 24);             /* FPCR.FZ */
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fz));
#elif defined(__SSE__) || defined(__x86_64__)
        old = _mm_getcsr();
        _mm_setcsr(old | 0x8040);                       /* FTZ | DAZ */
#endif
    }
    ~ScopedDenormalDisable() noexcept {
#if defined(__arm__)
        __asm__ __volatile__("vmsr fpscr, %0" : : "r"(old));
#elif defined(__aarch64__)
        __asm__ __volatile__("msr fpcr, %0" : : "r"(old64));
#elif defined(__SSE__) || defined(__x86_64__)
        _mm_setcsr(old);
#endif
    }
private:
    uint32_t old = 0;
    uint64_t old64 = 0;
    ScopedDenormalDisable(const ScopedDenormalDisable &) = delete;
    ScopedDenormalDisable &operator=(const ScopedDenormalDisable &) = delete;
};
