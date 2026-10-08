// RMXXXL DSP building blocks. Everything runs at 44.1 kHz on float stereo, in blocks of up to 32 frames.
//
// Isolator and Filter follow Mixxx 2.5 (GPL-2.0-or-later):
//  - src/effects/backends/builtin/linkwitzriley8eqeffect.cpp  (LR8 isolator: band split order, gain placement,
//    246 / 2484 Hz default crossovers)
//  - src/engine/filters/enginefilterlinkwitzriley8.cpp        (LR8 = Butterworth-4 "LpBu4"/"HpBu4" run twice)
//  - src/effects/backends/builtin/filtereffect.cpp            (RBJ biquad LPF + HPF, 13..22050 Hz, Q 0.4..4,
//    the resonance clamp near crossing corners)
// Mixxx builds its coefficients with fidlib; here they are the same bilinear-transform designs written out
// directly (RBJ cookbook biquads; a Butterworth-4 is two biquads with Q 0.5412 and 1.3066).
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace rfx {

const float kSampleRate = 44100.0f;
const float kPi = 3.14159265358979f;

template <typename T> inline T Clamp(T x, T lo, T hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float DbToGain(float db) { return powf(10.0f, db * 0.05f); }

// ---------------------------------------------------------------- biquad (transposed direct form II)
struct BiquadCoefs {
  float b0, b1, b2, a1, a2;
  void Identity() { b0 = 1.0f; b1 = b2 = a1 = a2 = 0.0f; }
  void LowPass(float f, float q) {
    float w = 2.0f * kPi * Clamp(f, 5.0f, 0.45f * kSampleRate) / kSampleRate;
    float cw = cosf(w), alpha = sinf(w) / (2.0f * q), a0 = 1.0f + alpha;
    b0 = (1.0f - cw) * 0.5f / a0; b1 = (1.0f - cw) / a0; b2 = b0;
    a1 = -2.0f * cw / a0; a2 = (1.0f - alpha) / a0;
  }
  void HighPass(float f, float q) {
    float w = 2.0f * kPi * Clamp(f, 5.0f, 0.45f * kSampleRate) / kSampleRate;
    float cw = cosf(w), alpha = sinf(w) / (2.0f * q), a0 = 1.0f + alpha;
    b0 = (1.0f + cw) * 0.5f / a0; b1 = -(1.0f + cw) / a0; b2 = b0;
    a1 = -2.0f * cw / a0; a2 = (1.0f - alpha) / a0;
  }
};

struct BiquadState {
  float z1, z2;
  void Reset() { z1 = z2 = 0.0f; }
  inline float Run(const BiquadCoefs& c, float x) {
    float y = c.b0 * x + z1;
    z1 = c.b1 * x - c.a1 * y + z2;
    z2 = c.b2 * x - c.a2 * y;
    return y;
  }
};

// Linkwitz-Riley 8th order = Butterworth-4 squared = 4 biquads (Q pair applied twice).
const float kBw4Q[2] = { 0.54119610f, 1.30656296f };

struct Lr8 {
  BiquadCoefs c[2];
  BiquadState s[2][4];   // [channel][stage]
  void SetLow(float f) { c[0].LowPass(f, kBw4Q[0]); c[1].LowPass(f, kBw4Q[1]); }
  void SetHigh(float f) { c[0].HighPass(f, kBw4Q[0]); c[1].HighPass(f, kBw4Q[1]); }
  void Reset() { memset(s, 0, sizeof s); }
  inline float Run(int ch, float x) {
    x = s[ch][0].Run(c[0], x);
    x = s[ch][1].Run(c[1], x);
    x = s[ch][2].Run(c[0], x);
    return s[ch][3].Run(c[1], x);
  }
};

// ---------------------------------------------------------------- isolator (Mixxx LinkwitzRiley8EQEffect)
// knob 0..1: 0 = kill, 0.5 = unity, 1 = +6 dB
inline float IsoGain(float knob) {
  if (knob <= 0.01f) return 0.0f;
  if (knob <= 0.5f) { float t = knob * 2.0f; return t * t; }
  return DbToGain(6.0f * (knob - 0.5f) * 2.0f);
}

struct Isolator {
  Lr8 low1, high1, low2, high2;   // 1 = low/mid crossover, 2 = mid/high crossover (Mixxx's names)
  float lo_freq, hi_freq;
  float old_low, old_mid, old_high;

  void Init() {
    memset(this, 0, sizeof *this);
    old_low = old_mid = old_high = 1.0f;
    SetFreqs(246.0f, 2484.0f);
  }
  void SetFreqs(float lo, float hi) {
    if (hi < lo * 1.5f) hi = lo * 1.5f;
    if (lo == lo_freq && hi == hi_freq) return;
    lo_freq = lo; hi_freq = hi;
    low1.SetLow(lo); high1.SetHigh(lo);
    low2.SetLow(hi); high2.SetHigh(hi);
  }
  // In place. Gains ramp across the block, as Mixxx's applyRampingGain/addWithRampingGain do.
  void Process(float* l, float* r, int n, float g_low, float g_mid, float g_high) {
    float inv = 1.0f / n;
    for (int i = 0; i < n; ++i) {
      float t = (i + 1) * inv;
      float gl = old_low + (g_low - old_low) * t;
      float gm = old_mid + (g_mid - old_mid) * t;
      float gh = old_high + (g_high - old_high) * t;
      float* io[2] = { &l[i], &r[i] };
      for (int ch = 0; ch < 2; ++ch) {
        float x = *io[ch];
        float hb = high2.Run(ch, x);        // HighPass first run
        float lb = low2.Run(ch, x);         // LowPass first run, for low and band
        hb = hb * gh + lb * gm;
        float mb = high1.Run(ch, hb);       // HighPass + band pass, second run
        lb = low1.Run(ch, lb);              // LowPass second run
        *io[ch] = lb * gl + mb;
      }
    }
    old_low = g_low; old_mid = g_mid; old_high = g_high;
  }
};

// ---------------------------------------------------------------- filter (Mixxx FilterEffect, one bipolar knob)
const float kMinCorner = 13.0f, kMaxCorner = 22050.0f;

struct DjFilter {
  BiquadCoefs lpc, hpc;
  BiquadState lps[2], hps[2];
  float lpf, hpf, q;          // current corners
  bool lp_on, hp_on;          // stage active in the last block
  bool lp_on_next, hp_on_next;

  void Init() { memset(this, 0, sizeof *this); lpf = kMaxCorner; hpf = kMinCorner; q = 0.7071f; }

  // Mixxx's resonance clamp: when the corners get close, high Q would make a huge peak.
  static float ClampQ(float hp, float lp, float q) {
    float ratio = hp / lp;
    if (ratio < 1.414f && ratio >= 1.0f) { ratio -= 1.0f; return fminf(q, 2.0f + ratio * ratio * ratio * 29.0f); }
    if (ratio < 1.0f && ratio >= 0.7f) return fminf(q, 2.0f);
    if (ratio < 0.7f && ratio > 0.1f) { ratio -= 0.1f; return fminf(q, 4.0f - 2.0f / 0.6f * ratio); }
    return q;
  }

  void Set(float new_lpf, float new_hpf, float new_q) {
    lpf = new_lpf; hpf = new_hpf; q = new_q;
    float cq = ClampQ(hpf, lpf, q);
    bool lp = lpf < kMaxCorner - 1.0f, hp = hpf > kMinCorner + 0.5f;
    if (lp) { if (!lp_on) { lps[0].Reset(); lps[1].Reset(); } lpc.LowPass(lpf, cq); }
    if (hp) { if (!hp_on) { hps[0].Reset(); hps[1].Reset(); } hpc.HighPass(hpf, cq); }
    lp_on_next = lp; hp_on_next = hp;
  }

  // In place. A stage that switches off crossfades to dry over the block (Mixxx: processAndPauseFilter).
  void Process(float* l, float* r, int n) {
    float* ch[2] = { l, r };
    if (hp_on || hp_on_next) Stage(ch, n, hpc, hps, hp_on, hp_on_next);
    if (lp_on || lp_on_next) Stage(ch, n, lpc, lps, lp_on, lp_on_next);
    hp_on = hp_on_next; lp_on = lp_on_next;
  }

 private:
  static void Stage(float** ch, int n, const BiquadCoefs& c, BiquadState* s, bool was_on, bool is_on) {
    float inv = 1.0f / n;
    for (int k = 0; k < 2; ++k) {
      for (int i = 0; i < n; ++i) {
        float dry = ch[k][i], wet = s[k].Run(c, dry);
        if (was_on && !is_on) { float t = (i + 1) * inv; ch[k][i] = wet + (dry - wet) * t; }
        else if (!was_on && is_on) { float t = (i + 1) * inv; ch[k][i] = dry + (wet - dry) * t; }
        else ch[k][i] = wet;
      }
    }
  }
};

// Bipolar knob -1..1 -> corners. Left: low-pass sweeps down; right: high-pass sweeps up (log scale, as Mixxx's
// linked quick-effect filter). The ends stop at 40 Hz / 16 kHz so a full turn is a near-kill, not a click to silence.
inline void FilterCorners(float knob, float* lpf, float* hpf) {
  *lpf = kMaxCorner; *hpf = kMinCorner;
  if (knob < -0.02f) *lpf = 20000.0f * powf(40.0f / 20000.0f, (-knob - 0.02f) / 0.98f);
  if (knob > 0.02f) *hpf = 20.0f * powf(16000.0f / 20.0f, (knob - 0.02f) / 0.98f);
}
inline float ResonanceQ(float knob) { return 0.4f * powf(10.0f, Clamp(knob, 0.0f, 1.0f)); }   // 0.4..4

// ---------------------------------------------------------------- one-pole filters for loops
struct OnePole {
  float a, z[2];
  void SetLowPass(float f) { a = 1.0f - expf(-2.0f * kPi * f / kSampleRate); }
  inline float Low(int ch, float x) { z[ch] += a * (x - z[ch]); return z[ch]; }
  inline float High(int ch, float x) { z[ch] += a * (x - z[ch]); return x - z[ch]; }
};

// ---------------------------------------------------------------- tempo-synced echo
const int kEchoLen = 1 << 18;   // 5.9 s per channel: 1 bar at 41 BPM

struct Echo {
  float* buf;      // [kEchoLen * 2] interleaved
  int write;
  float delay;     // current delay in samples (slewed towards target)
  OnePole damp, rumble;

  void Init(float* mem) {
    buf = mem; memset(buf, 0, sizeof(float) * kEchoLen * 2);
    write = 0; delay = 22050.0f; clear_left = 0;
    damp.SetLowPass(5500.0f); rumble.SetLowPass(120.0f);
    damp.z[0] = damp.z[1] = rumble.z[0] = rumble.z[1] = 0.0f;
  }
  void Clear() { memset(buf, 0, sizeof(float) * kEchoLen * 2); damp.z[0] = damp.z[1] = rumble.z[0] = rumble.z[1] = 0.0f; }
  // Panic: silent at once, the 2 MB buffer zeroed a slice per call (64 calls, ~46 ms) so no audio block pays for it all.
  static const int kClearSlices = 64;
  int clear_left;
  void StartClear() { clear_left = kClearSlices; damp.z[0] = damp.z[1] = rumble.z[0] = rumble.z[1] = 0.0f; }

  inline void Read(float d, float* l, float* r) const {
    float pos = static_cast<float>(write) - d;
    while (pos < 0.0f) pos += kEchoLen;
    int i0 = static_cast<int>(pos);
    float fr = pos - i0;
    int i1 = (i0 + 1) & (kEchoLen - 1);
    *l = buf[i0 * 2] + (buf[i1 * 2] - buf[i0 * 2]) * fr;
    *r = buf[i0 * 2 + 1] + (buf[i1 * 2 + 1] - buf[i0 * 2 + 1]) * fr;
  }

  // send_l/r: what goes in; adds the wet echo to out_l/r. Ping-pong-free stereo echo with damped feedback.
  void Process(const float* send_l, const float* send_r, float* out_l, float* out_r, int n, float target_delay,
               float feedback) {
    if (clear_left > 0) {
      const int slice = kEchoLen * 2 / kClearSlices;
      memset(buf + (kClearSlices - clear_left) * slice, 0, sizeof(float) * slice);
      --clear_left;
      return;
    }
    target_delay = Clamp(target_delay, 64.0f, static_cast<float>(kEchoLen - 4));
    feedback = Clamp(feedback, 0.0f, 0.95f);
    for (int i = 0; i < n; ++i) {
      delay += (target_delay - delay) * 0.0005f;   // tape-like glide when the tempo or beat changes
      float wl, wr;
      Read(delay, &wl, &wr);
      float fl = rumble.High(0, damp.Low(0, wl)), fr = rumble.High(1, damp.Low(1, wr));
      buf[write * 2] = send_l[i] + fl * feedback;
      buf[write * 2 + 1] = send_r[i] + fr * feedback;
      write = (write + 1) & (kEchoLen - 1);
      out_l[i] += wl;
      out_r[i] += wr;
    }
  }
};

// ---------------------------------------------------------------- tape: Echo Out / Vinyl Brake / Backspin
// Records the final mix continuously. A release plays from that recording instead of the live signal for its
// length, then fades back to live:
//   0 Echo Out:    the last `segment` samples before the press, repeated, each repeat `feedback` quieter and darker
//   1 Vinyl Brake: playback slows from 1x to a stop
//   2 Backspin:    fast reverse (-3x), slowing to a stop
const int kTapeLen = 1 << 19;   // 11.9 s per channel

struct Tape {
  float* buf;          // [kTapeLen * 2]
  int write;
  bool active;
  int kind;
  float read_pos;      // fractional absolute index into buf (brake, backspin)
  int seg_start;       // Echo Out: first sample of the repeated segment
  int segment;         // Echo Out: segment length in samples
  float feedback, echo_gain;
  float lp[2];         // Echo Out: darkening per repeat
  float t, length;     // samples since start, total
  float fade;          // return crossfade, 0..1 (1 = live)

  void Init(float* mem) {
    buf = mem; memset(buf, 0, sizeof(float) * kTapeLen * 2);
    write = 0; active = false; fade = 1.0f; lp[0] = lp[1] = 0.0f; held[0] = held[1] = 0.0f;
  }

  void Stop() { active = false; fade = 1.0f; }   // Panic: back to live at once (the recording itself is harmless)

  void Start(int k, float len_samples, float segment_samples, float fb) {
    kind = k;
    length = Clamp(len_samples, 2205.0f, kTapeLen * 0.3f);
    t = 0.0f;
    active = true;
    read_pos = static_cast<float>((write - 1 + kTapeLen) & (kTapeLen - 1));
    segment = static_cast<int>(Clamp(segment_samples, 1102.0f, kTapeLen * 0.3f));
    seg_start = (write - segment + kTapeLen) & (kTapeLen - 1);
    feedback = Clamp(fb, 0.3f, 0.85f);
    echo_gain = 1.0f;
    lp[0] = lp[1] = 0.0f;
  }

  inline float Rate() const {
    float y = 1.0f - t / length;
    if (kind == 1) return y * y;            // brake: platter slows to a stop
    return -3.0f * y * y;                   // backspin: fast reverse, slowing
  }

  // In place on the final mix.
  void Process(float* l, float* r, int n) {
    for (int i = 0; i < n; ++i) {
      buf[write * 2] = l[i];
      buf[write * 2 + 1] = r[i];
      write = (write + 1) & (kTapeLen - 1);
      if (!active) {
        if (fade < 1.0f) {
          fade = fminf(1.0f, fade + 1.0f / 441.0f);   // 10 ms back to live
          l[i] = held[0] * (1.0f - fade) + l[i] * fade;
          r[i] = held[1] * (1.0f - fade) + r[i] * fade;
          held[0] *= 0.995f; held[1] *= 0.995f;
        }
        continue;
      }
      float tl, tr;
      if (kind == 0) {
        int k = static_cast<int>(t) % segment;
        if (k == 0 && t > 0.0f) echo_gain *= feedback;
        int idx = (seg_start + k) & (kTapeLen - 1);
        float a = 0.35f;   // one-pole low-pass, darkening a little more on every repeat
        lp[0] += a * (buf[idx * 2] - lp[0]);
        lp[1] += a * (buf[idx * 2 + 1] - lp[1]);
        float edge = fminf(1.0f, fminf(k, segment - k) / 64.0f);   // no clicks at the loop points
        tl = lp[0] * echo_gain * edge;
        tr = lp[1] * echo_gain * edge;
      } else {
        float rate = Rate();
        read_pos += rate;
        while (read_pos < 0.0f) read_pos += kTapeLen;
        while (read_pos >= kTapeLen) read_pos -= kTapeLen;
        int i0 = static_cast<int>(read_pos);
        float fr = read_pos - i0;
        int i1 = (i0 + 1) & (kTapeLen - 1);
        float g = fminf(1.0f, fabsf(rate) * 4.0f);   // fade out as the platter stops, avoids a DC step
        tl = (buf[i0 * 2] + (buf[i1 * 2] - buf[i0 * 2]) * fr) * g;
        tr = (buf[i0 * 2 + 1] + (buf[i1 * 2 + 1] - buf[i0 * 2 + 1]) * fr) * g;
      }
      // the first 64 samples crossfade from live into the tape
      float in = fminf(1.0f, t / 64.0f);
      l[i] = l[i] * (1.0f - in) + tl * in;
      r[i] = r[i] * (1.0f - in) + tr * in;
      held[0] = l[i]; held[1] = r[i];
      t += 1.0f;
      if (t >= length) { active = false; fade = 0.0f; }
    }
  }
  float held[2];       // last tape output, faded under the returning live signal
};

// ---------------------------------------------------------------- brickwall limiter
// Stereo-linked look-ahead peak limiter. Drive pushes the signal in, Ceiling is the absolute output maximum.
// The gain needed for each incoming sample is known kLookahead samples before that sample is played; a sliding
// minimum over that window, smoothed to reach its value within the window, pulls the gain down before the peak
// arrives (no distortion on transients), and a final clamp at the ceiling makes it a true brickwall even if the
// smoothing lags. Adds kLookahead samples (1.45 ms) of latency.
const int kLookahead = 64;

struct Limiter {
  float delay[kLookahead][2];
  float need[kLookahead];        // gain each delayed sample needs
  int pos;
  float gain;
  float gr_db;                   // last block's deepest gain reduction (for a display)

  void Init() { memset(this, 0, sizeof *this); gain = 1.0f; for (int i = 0; i < kLookahead; ++i) need[i] = 1.0f; }

  void Process(float* l, float* r, int n, float drive_db, float ceiling_db, float release_ms) {
    float drive = DbToGain(drive_db), ceiling = DbToGain(ceiling_db);
    float attack = 1.0f - expf(-1.0f / (kLookahead / 5.0f));            // ~99 % within the look-ahead
    float release = 1.0f - expf(-1.0f / (Clamp(release_ms, 1.0f, 2000.0f) * 0.001f * kSampleRate));
    float deepest = 1.0f;
    for (int i = 0; i < n; ++i) {
      float a = l[i] * drive, b = r[i] * drive;
      float peak = fmaxf(fabsf(a), fabsf(b));
      need[pos] = peak > ceiling ? ceiling / peak : 1.0f;
      float out_l = delay[pos][0], out_r = delay[pos][1];
      delay[pos][0] = a; delay[pos][1] = b;
      pos = (pos + 1) % kLookahead;
      float target = 1.0f;
      for (int k = 0; k < kLookahead; ++k) target = fminf(target, need[k]);
      gain += (target - gain) * (target < gain ? attack : release);
      float y_l = out_l * gain, y_r = out_r * gain;
      l[i] = Clamp(y_l, -ceiling, ceiling);    // the brickwall
      r[i] = Clamp(y_r, -ceiling, ceiling);
      if (gain < deepest) deepest = gain;
    }
    gr_db = 20.0f * log10f(fmaxf(deepest, 1e-6f));
  }
};

// ---------------------------------------------------------------- noise riser
struct Noise {
  uint32_t seed[2];
  BiquadCoefs hp, lp;
  BiquadState hps[2], lps[2];
  void Init(uint32_t s) { seed[0] = s | 1u; seed[1] = (s * 2654435761u) | 1u; memset(hps, 0, sizeof hps); memset(lps, 0, sizeof lps); }
  inline float White(int ch) {
    seed[ch] ^= seed[ch] << 13; seed[ch] ^= seed[ch] >> 17; seed[ch] ^= seed[ch] << 5;
    return (static_cast<int32_t>(seed[ch]) * (1.0f / 2147483648.0f));
  }
  void Clear() { memset(hps, 0, sizeof hps); memset(lps, 0, sizeof lps); }
  void Set(float hp_freq) { hp.HighPass(hp_freq, 0.9f); lp.LowPass(fminf(hp_freq * 6.0f, 16000.0f), 0.7f); }
  void Add(float* l, float* r, int n, float level) {
    if (level <= 0.0001f) return;
    for (int i = 0; i < n; ++i) {
      l[i] += level * lps[0].Run(lp, hps[0].Run(hp, White(0)));
      r[i] += level * lps[1].Run(lp, hps[1].Run(hp, White(1)));
    }
  }
};

}  // namespace rfx
