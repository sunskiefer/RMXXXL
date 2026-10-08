# Latency diagnosis and Force optimization plan

Kept for the next round of work (from the first Force test, 2026-10-08). Decisions so far:
- **Phase 2 comes first.** The reported bug is "screen pads take a few seconds", which points at the
  host → setParameter path, not at CPU load.
- **Item 6 is limited to trying -O3.** The NEON / CMSIS-DSP FFT rewrite is dropped.
- Nothing here has been run yet. On-device steps need the Force reachable over SSH, and the user's go-ahead
  before anything stops or restarts MPC.

## Goal
Answer "is the latency the DSP thread (CPU) or a specific module?" with on-device numbers, then apply targeted
optimizations and re-measure.

## Findings from code review (baseline hypothesis)
Per 128-frame block (2902 µs budget, Cortex-A17 @ 1.8 GHz):
- Wrapper RT-thread housekeeping (vst2_wrap.c:381): `audioMasterGetTime` every block; string-param polls every
  10 ms (snprintf per param); readout poll + `audioMasterUpdateDisplay` every 100 ms. Periodic spikes.
- Reverb (strev.cpp:81): heaviest per-sample block when active; only the selected type runs (engine.cc:722),
  all 3 always allocated (engine.cc:184).
- Limiter (dsp.h:358): O(64) min-scan per sample.
- Clouds spectral: FFT/IFFT on background Prepare thread (engine.cc:161, 3 ms × 4); stale output → glitches if
  it lags. Custom C++ FFT, no NEON (USE_ARM_FFT undefined).
- midi_in::Poll (engine.cc:749): ALSA seq read every block.
- Inherent latency: limiter lookahead 1.45 ms + trigger quantization to block start (≤2.9 ms).
- ROADMAP "screen pads take a few seconds" is the host→setParameter path, not DSP.

## Phase 1 — Measure on the Force (disambiguates DSP-thread vs module)
1. Build armhf; run `tools/bench.sh build/x.so <device-ip>` (idle + noise-input stages). Record p99 / worst
   block / threads%.
2. Bench under-reports wall-clock-paced worker threads (BENCH.md caveat): while actually playing, sample the
   Prepare thread's ticks via `/proc/<pid>/task/*/stat` on the device.
3. Matrix runs to attribute cost: reverb off / Plate / Room / Hall; Clouds granular vs spectral; limiter drive 0
   vs max. Delta per module.
4. Verdict against BENCH.md thresholds (PASS ≤15% p99 / ≤50% worst).
   - Device is shared with the user's live setup: ask before running (mpc-vst-plugins CLAUDE.md ground rule).

## Phase 2 — "Pads take a few seconds" (trigger path, not CPU) — do this first
- Timestamp host→setParameter→engine for momentary/trigger params; check JUCE-host behaviour on device
  (NOTES.md); isolate whether the delay is host-side or wrapper/engine-side.

## Phase 3 — Optimizations (priority order, each re-benched before/after)
1. Limiter: replace O(64) min-scan with sliding-minimum (monotonic deque), O(1)/sample — src/dsp.h:358. Same
   64-sample lookahead, same sound.
2. Throttle `audioMasterGetTime` from every block to ~every 10 blocks (vst2_wrap.c:383) — BPM changes are slow;
   keeps JUCE lock off the hot path.
3. String polling: add `poll:false` (no_poll) to params that don't need tile/readout updates; cut per-poll
   snprintf count (vst2_wrap.c:401-427).
4. Reverb size changes: verify whether freeverb3 setSize reallocates delay lines on the RT thread
   (delay.cpp/allpass.cpp); if so, pre-allocate max sizes at construction so `size`/`decay` changes are
   allocation-free.
5. Allocate only the selected reverb type on demand (lazy create/destroy) — cuts RAM for multi-instance use;
   engine.cc:184.
6. Spectral FFT: try an -O3 build only; keep -O2 if it doesn't help. (NEON / CMSIS-DSP rewrite dropped.)
7. Rebuild, re-run Phase 1 bench, record before/after in docs/BENCH.md reference table; date + device-verified
   facts go to docs/NOTES.md (repo doc-sync rule).

## Verification
- `tools/test_port.sh vst.json` (x86 host, ASan) after each code change.
- On-device bench before/after per optimization; final verdict table in BENCH.md.

## Risks / notes
- Bench `threads%` is unreliable for wall-clock-paced threads — use /proc sampling (BENCH.md caveat).
- -O3 can regress on some ARM code; keep -O2 if it doesn't help.
- Reverb lazy-create must handle type switches without audible clicks (existing gain-ramp logic at
  engine.cc:723-732 already fades).
