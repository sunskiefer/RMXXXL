// RMXXXL X-Pad: four pads, each playing its built-in drum (synthesised) or a sample slot, through its own ADSR.
//
// Envelope (one-shot, as MPC drum programs): Attack rises from the current level (a retrigger never clicks), Decay
// falls to Sustain, Sustain holds, and the gate closes by itself at the later of (Attack + Decay) and
// (sound length - Release), so Release fades the end of the sound. Screen taps, MIDI notes and roll hits all play
// this way. (held = true, gate until NoteOff, is kept for a possible "note-on" pad mode.)
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "dsp.h"
#include "samples.h"

namespace rfx {

enum PadSound { PAD_KICK, PAD_SNARE, PAD_CLAP, PAD_HAT, PAD_COUNT };
const float kBuiltinSeconds[PAD_COUNT] = { 1.6f, 0.9f, 0.9f, 0.4f };

struct Adsr { float a_ms, d_ms, s, r_ms; };

enum EnvStage { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

struct PadVoice {
  EnvStage stage;
  float env, release_step;
  int gate_left;          // samples until the gate closes by itself; -1 = held until note-off
  Adsr adsr;
  float t;                // seconds since trigger (built-in drums)
  float phase;
  float vel;
  float tune;             // pitch ratio
  BiquadState f[2];
  const Sample* sample;   // NULL = built-in drum
  double pos;             // sample frame position
  bool silent_slot;       // an empty slot: plays nothing
};

struct Pads {
  PadVoice v[PAD_COUNT];
  BiquadCoefs snare_hp, clap_bp_lo, clap_bp_hi, hat_hp;
  uint32_t seed;

  void Init(uint32_t s) {
    memset(this, 0, sizeof *this);
    seed = s | 1u;
    snare_hp.HighPass(1200.0f, 0.7f);
    clap_bp_lo.HighPass(800.0f, 0.8f);
    clap_bp_hi.LowPass(2600.0f, 0.8f);
    hat_hp.HighPass(7000.0f, 0.8f);
  }
  inline float White() {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return static_cast<int32_t>(seed) * (1.0f / 2147483648.0f);
  }

  // sample: NULL = the pad's built-in drum. held: a MIDI note (gate until NoteOff) vs a tap/roll hit.
  void Trigger(int pad, float vel, float semis, const Sample* sample, bool empty_slot, const Adsr& adsr, bool held) {
    if (pad < 0 || pad >= PAD_COUNT) return;
    PadVoice& p = v[pad];
    p.adsr = adsr;
    p.vel = vel;
    p.tune = powf(2.0f, semis / 12.0f);
    p.sample = sample;
    p.silent_slot = empty_slot;
    p.t = 0.0f; p.phase = 0.0f; p.pos = 0.0;
    p.f[0].Reset(); p.f[1].Reset();
    if (p.stage == ENV_OFF) p.env = 0.0f;   // else attack from where it is: no click on a retrigger
    p.stage = ENV_ATTACK;
    if (held) {
      p.gate_left = -1;
    } else {
      float length_s = sample ? sample->frames / (sample->rate * p.tune) / kSampleRate : kBuiltinSeconds[pad];
      float ad_s = (adsr.a_ms + adsr.d_ms) * 0.001f;
      float gate_s = fmaxf(ad_s, length_s - adsr.r_ms * 0.001f);
      p.gate_left = static_cast<int>(fmaxf(gate_s, 0.001f) * kSampleRate);
    }
  }

  void NoteOff(int pad) {
    if (pad < 0 || pad >= PAD_COUNT) return;
    PadVoice& p = v[pad];
    if (p.stage != ENV_OFF && p.stage != ENV_RELEASE && p.gate_left < 0) StartRelease(p);
  }

  // Stops every voice playing a sample (the sample memory is about to change).
  void StopSamples() {
    for (int i = 0; i < PAD_COUNT; ++i) if (v[i].sample) { v[i].stage = ENV_OFF; v[i].sample = NULL; }
  }

  // Silences every voice at once (Panic).
  void StopAll() {
    for (int i = 0; i < PAD_COUNT; ++i) { v[i].stage = ENV_OFF; v[i].sample = NULL; v[i].env = 0.0f; }
  }

  static void StartRelease(PadVoice& p) {
    p.stage = ENV_RELEASE;
    float r = fmaxf(p.adsr.r_ms, 1.0f) * 0.001f * kSampleRate;
    p.release_step = fmaxf(p.env, 1e-4f) / r;
  }

  static inline float StepEnv(PadVoice& p) {
    switch (p.stage) {
      case ENV_ATTACK: {
        float a = fmaxf(p.adsr.a_ms, 0.5f) * 0.001f * kSampleRate;
        p.env += 1.0f / a;
        if (p.env >= 1.0f) { p.env = 1.0f; p.stage = ENV_DECAY; }
        break;
      }
      case ENV_DECAY: {
        float d = fmaxf(p.adsr.d_ms, 1.0f) * 0.001f * kSampleRate;
        p.env -= (1.0f - p.adsr.s) / d;
        if (p.env <= p.adsr.s) { p.env = p.adsr.s; p.stage = ENV_SUSTAIN; }
        break;
      }
      case ENV_RELEASE:
        p.env -= p.release_step;
        if (p.env <= 0.0f) { p.env = 0.0f; p.stage = ENV_OFF; }
        break;
      default:
        break;
    }
    if (p.gate_left > 0 && --p.gate_left == 0 && p.stage != ENV_RELEASE && p.stage != ENV_OFF) StartRelease(p);
    return p.env;
  }

  void Add(float* l, float* r, int n, float level) {
    const float dt = 1.0f / kSampleRate;
    for (int pad = 0; pad < PAD_COUNT; ++pad) {
      PadVoice& p = v[pad];
      if (p.stage == ENV_OFF) continue;
      float g = level * p.vel;
      for (int i = 0; i < n; ++i) {
        float env = StepEnv(p);
        float sl = 0.0f, sr = 0.0f;
        if (p.silent_slot) {
          // an empty slot: nothing to play, the envelope just runs out
        } else if (p.sample) {
          const Sample* s = p.sample;
          int i0 = static_cast<int>(p.pos);
          if (i0 + 1 >= s->frames) { p.stage = ENV_OFF; break; }
          float fr = static_cast<float>(p.pos - i0);
          const int16_t* d = s->data + 2 * i0;
          sl = (d[0] + (d[2] - d[0]) * fr) * (1.0f / 32768.0f);
          sr = (d[1] + (d[3] - d[1]) * fr) * (1.0f / 32768.0f);
          p.pos += s->rate * p.tune;
        } else {
          float x = 0.0f, t = p.t;
          bool done = false;
          switch (pad) {
            case PAD_KICK: {
              float f = (48.0f + 110.0f * expf(-t / 0.035f)) * p.tune;
              p.phase += f * dt; if (p.phase > 1.0f) p.phase -= 1.0f;
              x = sinf(2.0f * kPi * p.phase) * expf(-t / 0.32f);
              x += 0.25f * White() * expf(-t / 0.003f);   // beater click
              x *= 0.95f;
              break;
            }
            case PAD_SNARE: {
              p.phase += 185.0f * p.tune * dt; if (p.phase > 1.0f) p.phase -= 1.0f;
              float tone = sinf(2.0f * kPi * p.phase) * expf(-t / 0.07f);
              float nz = p.f[0].Run(snare_hp, White()) * expf(-t / 0.16f);
              x = 0.45f * tone + 0.7f * nz;
              break;
            }
            case PAD_CLAP: {
              float e = 0.0f;   // three quick bursts, then the room tail
              for (int b = 0; b < 3; ++b) { float tb = t - b * 0.011f; if (tb >= 0.0f && tb < 0.011f) e = fmaxf(e, expf(-tb / 0.004f)); }
              if (t >= 0.033f) e = fmaxf(e, 0.8f * expf(-(t - 0.033f) / 0.14f));
              float nz = p.f[1].Run(clap_bp_hi, p.f[0].Run(clap_bp_lo, White()));
              x = 1.3f * nz * e;
              break;
            }
            default:   // PAD_HAT
              x = 0.6f * p.f[0].Run(hat_hp, White()) * expf(-t / 0.045f);
              break;
          }
          p.t += dt;
          if (p.t > kBuiltinSeconds[pad]) done = true;
          sl = sr = x;
          if (done) { p.stage = ENV_OFF; }
        }
        l[i] += sl * g * env;
        r[i] += sr * g * env;
        if (p.stage == ENV_OFF) break;
      }
    }
  }
};

}  // namespace rfx
