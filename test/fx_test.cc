// RMXXXL functional test: drives the engine directly (wrapper/engine.h), the way MPC's 128-frame blocks would,
// and checks every section: isolator, filter, scene macros, echo, Clouds (all modes), the three reverbs and the
// Clouds reverb, type switches, the three release FX, pads, rolls, MIDI release, state save/restore, tempo; 1.2: triggers
// that fire on every press, the Release kill switch, Panic, the riser Tune/Duck and user presets.
// Build: test/run_tests.sh (ASan + UBSan). Exit status 1 on any failure.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>
#include <sys/stat.h>
#include <stdint.h>

extern "C" {
#include "engine.h"
}

static int failures = 0;
static void check(bool ok, const char* what, double value) {
  printf("%s %-58s %.4f\n", ok ? "ok  " : "FAIL", what, value);
  if (!ok) ++failures;
}

static const mpc_engine_t* E;
static const int kBlock = 128;

struct Sig {
  // test input: sine at a frequency, or noise
  double phase = 0.0;
  unsigned seed = 1;
  void Sine(int16_t* buf, int n, double hz, double amp) {
    for (int i = 0; i < n; ++i) {
      int16_t v = (int16_t)(amp * 32767.0 * sin(phase));
      buf[2 * i] = buf[2 * i + 1] = v;
      phase += 2.0 * M_PI * hz / 44100.0;
    }
  }
  void Noise(int16_t* buf, int n, double amp) {
    for (int i = 0; i < n; ++i) {
      seed = seed * 1664525u + 1013904223u;
      int16_t v = (int16_t)(amp * ((int)(seed >> 16) - 32768));
      buf[2 * i] = v;
      seed = seed * 1664525u + 1013904223u;
      buf[2 * i + 1] = (int16_t)(amp * ((int)(seed >> 16) - 32768));
    }
  }
};

static void set(void* d, const char* k, double v) {
  char b[32];
  snprintf(b, sizeof b, "%g", v);
  E->set_param(d, k, b);
}

// Runs `blocks` blocks of a sine (hz > 0), noise (hz == 0) or silence (hz < 0); returns RMS of the output over the
// last `measure` blocks, and fails on any non-finite or stuck output.
static double run(void* d, Sig& sig, int blocks, double hz, double amp = 0.3, int measure = -1) {
  if (measure < 0) measure = blocks / 2;
  int16_t in[kBlock * 2], out[kBlock * 2];
  double acc = 0.0;
  long count = 0;
  for (int b = 0; b < blocks; ++b) {
    if (hz > 0) sig.Sine(in, kBlock, hz, amp);
    else if (hz == 0) sig.Noise(in, kBlock, amp);
    else memset(in, 0, sizeof in);
    E->process(d, in, out, kBlock);
    if (b >= blocks - measure) {
      for (int i = 0; i < kBlock * 2; ++i) { double x = out[i] / 32768.0; acc += x * x; ++count; }
    }
  }
  return count ? sqrt(acc / count) : 0.0;
}

static void note(void* d, int n, int vel) {
  uint8_t m[3] = { (uint8_t)(vel ? 0x90 : 0x80), (uint8_t)n, (uint8_t)vel };
  E->midi(d, m, 3);
}

// writes a WAV (sine burst) for the sample-slot tests
static void write_wav(const char* path, int channels, int bits, int rate, int frames, double hz) {
  FILE* f = fopen(path, "wb");
  int bytes = bits / 8, data = frames * channels * bytes;
  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); u32(36 + data); fwrite("WAVEfmt ", 1, 8, f); u32(16);
  u16(bits == 32 ? 3 : 1); u16(channels); u32(rate); u32(rate * channels * bytes); u16(channels * bytes); u16(bits);
  fwrite("data", 1, 4, f); u32(data);
  for (int i = 0; i < frames; ++i) for (int c = 0; c < channels; ++c) {
    double x = 0.5 * sin(2.0 * M_PI * hz * i / rate);
    if (bits == 16) { int16_t v = (int16_t)(x * 32767); fwrite(&v, 2, 1, f); }
    else if (bits == 24) { int32_t v = (int32_t)(x * 8388607); uint8_t b[3] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16) }; fwrite(b, 1, 3, f); }
    else { float v = (float)x; fwrite(&v, 4, 1, f); }
  }
  fclose(f);
}

int main() {
  E = mpc_engine();
  Sig sig;
  void* d = E->create(NULL);
  check(d != NULL, "create", 0);
  set(d, "lfo_bpm", 120);
  set(d, "lim_ceiling", 0);

  // ---------------------------------------------------------------- dry path
  double dry1k = run(d, sig, 200, 1000);
  check(fabs(dry1k - 0.3 / sqrt(2.0)) < 0.02, "defaults pass 1 kHz near unity (rms)", dry1k);

  // ---------------------------------------------------------------- isolator
  set(d, "iso_low", 0);
  double lowkill_100 = run(d, sig, 200, 80);
  check(lowkill_100 < 0.005, "Low kill removes 80 Hz", lowkill_100);
  double lowkill_1k = run(d, sig, 200, 1000);
  check(fabs(lowkill_1k - dry1k) < 0.03, "Low kill leaves 1 kHz", lowkill_1k);
  set(d, "iso_low", 0.5);
  set(d, "iso_mid", 0);
  double midkill = run(d, sig, 200, 900);
  check(midkill < 0.01, "Mid kill removes 900 Hz", midkill);
  set(d, "iso_mid", 0.5);
  set(d, "iso_high", 0);
  double hikill = run(d, sig, 200, 9000);
  check(hikill < 0.01, "High kill removes 9 kHz", hikill);
  set(d, "iso_high", 1.0);
  double hiboost = run(d, sig, 200, 9000);
  check(hiboost > dry1k * 1.7, "High +6 dB boosts 9 kHz", hiboost);
  set(d, "iso_high", 0.5);
  double unity80 = run(d, sig, 200, 80);
  check(fabs(unity80 - dry1k) < 0.03, "isolator at unity is flat (80 Hz)", unity80);

  char buf[64];
  set(d, "iso_mid", 0);
  E->get_param(d, "iso_mid_display", buf, sizeof buf);
  check(!strcmp(buf, "Kill"), "iso display says Kill", 0);
  set(d, "iso_mid", 0.5);

  // ---------------------------------------------------------------- filter
  set(d, "filter", -0.8);
  double lp = run(d, sig, 200, 5000);
  check(lp < 0.01, "Filter left (LPF) removes 5 kHz", lp);
  E->get_param(d, "filter_display", buf, sizeof buf);
  check(!strncmp(buf, "LPF", 3), "filter display shows LPF", 0);
  set(d, "filter", 0.8);
  double hp = run(d, sig, 200, 100);
  check(hp < 0.01, "Filter right (HPF) removes 100 Hz", hp);
  set(d, "filter_res", 1.0);
  double res = run(d, sig, 300, 3000, 0.05);
  check(isfinite(res), "full resonance stays finite", res);
  set(d, "filter", 0);
  set(d, "filter_res", 0.25);
  double back = run(d, sig, 200, 1000);
  check(fabs(back - dry1k) < 0.02, "filter centre is dry again", back);

  // ---------------------------------------------------------------- echo
  set(d, "echo_send", 0.8);
  run(d, sig, 100, 1000);
  double tail = run(d, sig, 250, -1);   // silence in: the 1-beat echo (0.5 s = 172 blocks) should still ring
  check(tail > 0.01, "echo rings after input stops", tail);
  set(d, "echo_send", 0);
  run(d, sig, 3000, -1);
  double quiet = run(d, sig, 100, -1);
  check(quiet < 0.001, "echo decays to silence", quiet);

  // ---------------------------------------------------------------- reverbs, each type
  const char* names[3] = { "Plate", "Room", "Hall" };
  for (int t = 0; t < 3; ++t) {
    set(d, "rv_type", t);
    set(d, "rv_send", 0.8);
    run(d, sig, 100, 0, 0.2);
    double rv_tail = run(d, sig, 60, -1);
    char what[64];
    snprintf(what, sizeof what, "%s reverb tail after noise burst", names[t]);
    check(rv_tail > 0.003 && rv_tail < 0.5, what, rv_tail);
    set(d, "rv_size", 0.9);
    set(d, "rv_decay", 0.9);
    set(d, "rv_tone", 0.2);
    run(d, sig, 300, 0, 0.2);
    snprintf(what, sizeof what, "%s survives size/decay/tone changes", names[t]);
    double r2 = run(d, sig, 50, -1);
    check(isfinite(r2) && r2 < 0.9, what, r2);
    set(d, "rv_size", 0.5);
    set(d, "rv_decay", 0.65);
    set(d, "rv_tone", 0.6);
  }
  // Clouds as reverb type
  set(d, "rv_type", 3);
  run(d, sig, 200, 0, 0.2);
  double clv = run(d, sig, 60, -1);
  check(clv > 0.001, "Clouds reverb type rings", clv);
  set(d, "rv_send", 0);
  set(d, "rv_type", 0);
  run(d, sig, 13 * 44100 / kBlock, -1);
  double rv_off = run(d, sig, 100, -1);
  check(rv_off < 0.002, "reverb goes quiet when the send closes", rv_off);

  // ---------------------------------------------------------------- Clouds, every mode
  set(d, "cl_on", 1);
  set(d, "cl_blend", 0.8);
  for (int m = 0; m < 6; ++m) {
    set(d, "cl_mode", m);
    run(d, sig, 400, 0, 0.2);   // > 0.5 s: the mode change applies
    double r = run(d, sig, 100, 440, 0.3);
    char what[64];
    snprintf(what, sizeof what, "Clouds mode %d produces sound", m);
    check(r > 0.005 && isfinite(r), what, r);
    E->get_param(d, "cl_size_name", buf, sizeof buf);
  }
  set(d, "cl_freeze", 1);
  double frozen = run(d, sig, 200, -1);
  check(isfinite(frozen), "Clouds freeze on silence stays finite", frozen);
  set(d, "cl_freeze", 0);
  set(d, "cl_trigger", 1); set(d, "cl_trigger", 0);
  run(d, sig, 20, 440);
  set(d, "cl_on", 0);
  run(d, sig, 100, 1000);
  double clouds_off = run(d, sig, 100, 1000);
  check(fabs(clouds_off - dry1k) < 0.03, "Clouds off returns to dry", clouds_off);

  // ---------------------------------------------------------------- scene macros
  set(d, "build_up", 1.0);
  set(d, "rv_send", 0);
  run(d, sig, 300, 200);
  double build_lows = run(d, sig, 100, 100, 0.3);
  // the HPF at 1.5 kHz removes 100 Hz; what remains is echo/reverb/noise of it, well below dry
  check(build_lows < dry1k * 0.6, "Build Up thins the lows", build_lows);
  double build_noise = run(d, sig, 100, -1);
  check(build_noise > 0.005, "Build Up adds riser/echo/reverb on silence", build_noise);
  set(d, "build_up", 0);
  set(d, "break_down", 1.0);
  run(d, sig, 100, 6000);
  double brk_hi = run(d, sig, 100, 6000);
  check(brk_hi < dry1k * 0.3, "Break Down darkens the highs", brk_hi);
  set(d, "break_down", 0);
  run(d, sig, 13 * 44100 / kBlock, -1);

  // ---------------------------------------------------------------- release FX
  // Vinyl brake: output first continues, then fades to near silence, then the live signal comes back.
  set(d, "release_fx", 1);
  set(d, "release_len", 1);   // 1 beat = 0.5 s at 120 BPM
  run(d, sig, 100, 1000);
  set(d, "release_go", 1);
  run(d, sig, 140, 1000);    // most of the beat
  double end_of_brake = run(d, sig, 20, 1000, 0.3, 4);
  check(end_of_brake < dry1k * 0.5, "Vinyl Brake slows to near silence", end_of_brake);
  run(d, sig, 60, 1000);
  double after = run(d, sig, 50, 1000);
  check(fabs(after - dry1k) < 0.03, "after the brake the live signal is back", after);

  set(d, "release_fx", 2);
  set(d, "release_go", 1);
  double spin = run(d, sig, 150, 1000);
  check(isfinite(spin) && spin > 0.001, "Backspin plays", spin);
  run(d, sig, 100, 1000);
  double after2 = run(d, sig, 50, 1000);
  check(fabs(after2 - dry1k) < 0.03, "after the backspin the live signal is back", after2);

  set(d, "release_fx", 0);
  set(d, "echo_fb", 0.45);
  set(d, "release_len", 2);   // 2 beats = 1 s
  run(d, sig, 100, 1000);
  set(d, "release_go", 1);
  double echo_out = run(d, sig, 100, -1);    // input stops at the press: the repeats carry on
  check(echo_out > 0.03, "Echo Out repeats the last beat after the input stops", echo_out);
  run(d, sig, 300, -1);
  double echo_done = run(d, sig, 50, -1);
  check(echo_done < 0.002, "after Echo Out the (silent) live signal is back", echo_done);
  set(d, "release_len", 1);

  // Release latches the scene off until the knobs return to zero
  run(d, sig, 300, 1000);
  set(d, "build_up", 1.0);
  run(d, sig, 50, 100);
  set(d, "release_fx", 1);
  set(d, "release_go", 1);
  run(d, sig, 200, 100);
  double latched = run(d, sig, 100, 100);
  check(fabs(latched - dry1k) < 0.05, "after Release the Build Up is latched off", latched);
  set(d, "build_up", 0);
  run(d, sig, 13 * 44100 / kBlock, -1);

  // ---------------------------------------------------------------- pads + rolls + MIDI release
  set(d, "pad_root", 36);
  note(d, 36, 127); note(d, 36, 0);
  double kick = run(d, sig, 30, -1);
  check(kick > 0.02, "MIDI kick plays", kick);
  set(d, "pad_snare", 1);
  double snare = run(d, sig, 20, -1);
  check(snare > 0.01, "screen Snare pad plays", snare);
  set(d, "pad_clap", 1);
  check(run(d, sig, 20, -1) > 0.005, "screen Clap pad plays", 0);
  set(d, "pad_hat", 1);
  check(run(d, sig, 10, -1) > 0.002, "screen Hat pad plays", 0);
  run(d, sig, 300, -1);
  note(d, 41, 100);   // root + 5 = snare roll
  double rolling = run(d, sig, 300, -1);
  note(d, 41, 0);
  check(rolling > 0.01, "held roll note keeps playing", rolling);
  run(d, sig, 300, -1);
  double roll_stopped = run(d, sig, 100, -1);
  check(roll_stopped < 0.002, "roll stops on note off", roll_stopped);
  set(d, "release_fx", 1);
  run(d, sig, 100, 1000);
  note(d, 44, 100); note(d, 44, 0);   // root + 8 = Release
  run(d, sig, 140, 1000);
  double midi_brake = run(d, sig, 20, 1000, 0.3, 4);
  check(midi_brake < dry1k * 0.5, "MIDI Release note fires the brake", midi_brake);
  E->get_param(d, "pad_root_display", buf, sizeof buf);
  check(!strcmp(buf, "C1"), "MIDI root 36 shows as C1", 0);


  // ---------------------------------------------------------------- 1.2: triggers fire on every press
  {
    // MPC's screen button toggles the value it read back; a trigger must read back 0 so every tap sends 1.
    set(d, "pad_root", 36);
    run(d, sig, 400, -1);
    set(d, "pad_kick", 1);
    E->get_param(d, "pad_kick", buf, sizeof buf);
    check(atof(buf) == 0.0, "a trigger reads back 0 right after a press", atof(buf));
    double k1 = run(d, sig, 20, -1);
    run(d, sig, 300, -1);
    set(d, "pad_kick", 1);   // the second tap: no 0 in between, as MPC sends it
    double k2 = run(d, sig, 20, -1);
    check(k1 > 0.02 && k2 > 0.02, "a second tap with no release in between fires again", k2);
    run(d, sig, 300, -1);
  }

  // ---------------------------------------------------------------- 1.2: Release = kill switch
  {
    // effects on: echo + reverb + a filter. Release On -> output equals the dry input, and no knob moves.
    set(d, "echo_send", 0.6); set(d, "rv_send", 0.6); set(d, "rv_type", 0); set(d, "filter", -0.6);
    double wet = run(d, sig, 200, 3000);
    check(wet < dry1k * 0.7, "effects on: the filter takes the 3 kHz down", wet);
    set(d, "kill_mode", 0);
    set(d, "release", 1);
    double killed = run(d, sig, 40, 3000);
    check(fabs(killed - dry1k) < 0.01, "Release Hard: dry within one block", killed);
    E->get_param(d, "echo_send", buf, sizeof buf);
    double es = atof(buf);
    E->get_param(d, "filter", buf, sizeof buf);
    check(fabs(es - 0.6) < 1e-6 && fabs(atof(buf) + 0.6) < 1e-6, "Release moves no parameter", es);
    run(d, sig, 130, 3000);   // taps on a switch closer than 0.35 s count as one gesture
    set(d, "release", 0);
    double back = run(d, sig, 40, 3000);
    check(fabs(back - wet) < 0.03, "Release Off: the effects come back as they were", back);
    // Smooth: over Release Beats (1 beat = 0.5 s at 120 BPM): halfway through it is between wet and dry
    set(d, "kill_mode", 1); set(d, "release_len", 1);
    run(d, sig, 130, 3000);
    set(d, "release", 1);
    double half = run(d, sig, 86, 3000, 0.3, 10);   // ~0.25 s in
    double full = run(d, sig, 120, 3000, 0.3, 20);
    check(half > wet + 0.01 && half < dry1k - 0.01 && fabs(full - dry1k) < 0.01, "Release Smooth fades over the beats", half);
    // MIDI root + 9 toggles it
    note(d, 45, 100); note(d, 45, 0);
    run(d, sig, 300, 3000);
    E->get_param(d, "release", buf, sizeof buf);
    check(atoi(buf) == 0, "MIDI note root + 9 toggles Release", atoi(buf));
    set(d, "kill_mode", 0);
    set(d, "echo_send", 0); set(d, "rv_send", 0); set(d, "filter", 0);
    run(d, sig, 13 * 44100 / kBlock, -1);
  }

  // ---------------------------------------------------------------- 1.2: riser Tune and Duck
  {
    // the input is killed by the isolator, so what comes out is the riser (the Duck still hears the input)
    set(d, "iso_low", 0); set(d, "iso_mid", 0); set(d, "iso_high", 0);
    set(d, "build_up", 0.8); set(d, "scene_noise", 1); set(d, "rv_scene", 0); set(d, "cl_scene", 0);
    int16_t in[kBlock * 2], out[kBlock * 2];
    auto zcr = [&](double tune) {
      set(d, "noise_tune", tune);
      long crossings = 0, count = 0;
      int16_t prev = 0;
      for (int b = 0; b < 200; ++b) {
        memset(in, 0, sizeof in);
        E->process(d, in, out, kBlock);
        if (b < 100) continue;
        for (int i = 0; i < kBlock; ++i) { if ((out[2 * i] >= 0) != (prev >= 0)) ++crossings; prev = out[2 * i]; ++count; }
      }
      return (double)crossings / count;
    };
    set(d, "build_up", 0.5);   // the band's centre ~1.5 kHz: well inside the range either way
    double lo = zcr(-12), hi = zcr(12);
    set(d, "build_up", 0.8);
    check(hi > lo * 1.5, "Riser Tune moves the noise band up", hi / lo);
    set(d, "noise_tune", 0);
    double free_ = run(d, sig, 200, 80, 0.8);
    set(d, "noise_mod", 1);
    double ducked = run(d, sig, 200, 80, 0.8);
    check(ducked < free_ * 0.5, "Riser Duck pushes the riser down under a loud input", ducked / free_);
    E->get_param(d, "noise_mod_display", buf, sizeof buf);
    check(!strcmp(buf, "100 %"), "Riser Duck shows a percentage", 0);
    set(d, "noise_mod", 0);
    set(d, "iso_low", 0.5); set(d, "iso_mid", 0.5); set(d, "iso_high", 0.5);
    set(d, "build_up", 0); set(d, "rv_scene", 0.7); set(d, "cl_scene", 0.5);
    run(d, sig, 13 * 44100 / kBlock, -1);
  }

  // ---------------------------------------------------------------- 1.2: Panic
  {
    set(d, "echo_send", 0.8); set(d, "echo_fb", 0.9); set(d, "rv_send", 0.8); set(d, "rv_type", 2);
    set(d, "filter", 0.5); set(d, "cl_on", 1); set(d, "release", 1); set(d, "in_gain", -6);
    set(d, "pad_level", 0.3); set(d, "pad_sel", 2); set(d, "env_d", 0.8);
    run(d, sig, 300, 0, 0.3);
    set(d, "panic", 1);
    double after_panic = run(d, sig, 10, -1, 0.3, 8);   // silent input: no tail may ring on
    check(after_panic < 0.001, "Panic clears every tail at once", after_panic);
    const char* reset[] = { "echo_send", "echo_fb", "rv_send", "rv_type", "filter", "cl_on", "release", "in_gain" };
    bool all_default = true;
    for (const char* k : reset) {
      E->get_param(d, k, buf, sizeof buf);
      double v = atof(buf);
      double def = !strcmp(k, "echo_fb") ? 0.45 : 0.0;
      if (fabs(v - def) > 1e-6) { all_default = false; printf("     %s = %g\n", k, v); }
    }
    check(all_default, "Panic puts the effect controls back to their defaults", 0);
    E->get_param(d, "pad_level", buf, sizeof buf);
    double pl = atof(buf);
    E->get_param(d, "env_d", buf, sizeof buf);
    check(fabs(pl - 0.3) < 1e-6 && fabs(atof(buf) - 0.8) < 1e-6, "Panic keeps the pad setup", pl);
    double dry_again = run(d, sig, 100, 1000);
    check(fabs(dry_again - dry1k) < 0.02, "after Panic the signal passes dry", dry_again);
    set(d, "pad_level", 0.7); set(d, "lim_ceiling", 0);
    note(d, 36 + 10, 100); note(d, 46, 0);   // root + 10 = Panic, from MIDI too
    run(d, sig, 2, -1);
    E->get_param(d, "lim_ceiling", buf, sizeof buf);
    check(fabs(atof(buf) + 0.3) < 1e-6, "MIDI note root + 10 is Panic", atof(buf));
    set(d, "lim_ceiling", 0);
  }


  // ---------------------------------------------------------------- 1.2.1: Q-Link behaviour of switches and lists
  {
    run(d, sig, 200, -1);
    E->get_param(d, "cl_freeze", buf, sizeof buf);
    int f0 = atoi(buf);
    // a Q-Link turn at the end stop: the same value again and again, a few ms apart -> one flip
    for (int i = 0; i < 6; ++i) { set(d, "cl_freeze", f0); run(d, sig, 2, -1); }
    E->get_param(d, "cl_freeze", buf, sizeof buf);
    int f1 = atoi(buf);
    run(d, sig, 200, -1);
    // the next turn, the other way (towards the other value): one flip back
    for (int i = 0; i < 6; ++i) { set(d, "cl_freeze", 1 - f1); run(d, sig, 2, -1); }
    E->get_param(d, "cl_freeze", buf, sizeof buf);
    int f2 = atoi(buf);
    check(f1 == 1 - f0 && f2 == f0, "a Q-Link turn either way flips a switch once", f1);
    // Hard / Smooth are buttons: tapping the one already lit keeps it
    set(d, "kill_mode", 0); run(d, sig, 200, -1); set(d, "kill_mode", 0);
    E->get_param(d, "kill_mode", buf, sizeof buf);
    check(atoi(buf) == 0, "tapping the lit Hard button keeps Hard", atoi(buf));
    // an option list: a fast burst of one-step nudges moves one step per 0.2 s, a tap jumps at once
    set(d, "release_len", 0); run(d, sig, 200, -1);
    for (int i = 1; i <= 3; ++i) {   // MPC nudges from the value it read back
      E->get_param(d, "release_len", buf, sizeof buf);
      set(d, "release_len", atoi(buf) + 1); run(d, sig, 3, -1);
    }
    E->get_param(d, "release_len", buf, sizeof buf);
    int burst = atoi(buf);
    run(d, sig, 200, -1);
    set(d, "release_len", 3);
    E->get_param(d, "release_len", buf, sizeof buf);
    check(burst == 1 && atoi(buf) == 3, "a fast Q-Link turn moves Beats one step; a tap jumps", burst);
    set(d, "release_len", 1);
    run(d, sig, 200, -1);
  }


  // ---------------------------------------------------------------- 1.2.1: Clouds keeps the dry level
  {
    set(d, "cl_mode", 2); set(d, "cl_blend", 0); set(d, "cl_on", 1);
    run(d, sig, 400, 1000);
    double cl_dry = run(d, sig, 300, 1000);
    check(fabs(20 * log10(cl_dry / dry1k)) < 0.5, "Clouds on at Blend 0 keeps the dry level (was -9 dB)", 20 * log10(cl_dry / dry1k));
    run(d, sig, 130, 1000);
    set(d, "cl_on", 0); set(d, "cl_blend", 0.5);
    run(d, sig, 200, -1);
  }

  // ---------------------------------------------------------------- state
  set(d, "rv_type", 2);
  set(d, "iso_low", 0.1);
  set(d, "cl_mode", 4);
  char state[8192];
  int n = E->get_param(d, "state", state, sizeof state);
  void* d2 = E->create(NULL);
  E->set_param(d2, "state", state);
  char a[32], b[32];
  bool same = true;
  const char* keys[] = { "rv_type", "iso_low", "cl_mode", "echo_div", "pad_root" };
  for (const char* k : keys) {
    E->get_param(d, k, a, sizeof a);
    E->get_param(d2, k, b, sizeof b);
    if (strcmp(a, b)) same = false;
  }
  check(n > 0 && same, "state chunk round-trips", n);

  // ---------------------------------------------------------------- brickwall limiter
  {
    int16_t in[kBlock * 2], out[kBlock * 2];
    set(d, "lim_drive", 18); set(d, "lim_ceiling", -6);
    int peak = 0;
    for (int b = 0; b < 400; ++b) {
      sig.Noise(in, kBlock, 0.9);
      E->process(d, in, out, kBlock);
      if (b > 20) for (int i = 0; i < kBlock * 2; ++i) if (abs(out[i]) > peak) peak = abs(out[i]);
    }
    double peak_db = 20.0 * log10(peak / 32768.0);
    check(peak_db <= -5.99, "limiter: +18 dB drive into a -6 dB ceiling never exceeds it", peak_db);
    check(peak_db > -7.0, "limiter: and the level sits right at the ceiling", peak_db);
    set(d, "lim_drive", 0); set(d, "lim_ceiling", 0);
    double quiet_lim = run(d, sig, 200, 1000, 0.3);
    check(fabs(quiet_lim - dry1k) < 0.02, "limiter: below the ceiling it leaves the level alone", quiet_lim);
  }

  // ---------------------------------------------------------------- PADS page: ADSR, selected pad, sample slots
  {
    const char* dir = "/tmp/rmxxxl_test_samples";
    mkdir(dir, 0777);
    remove("/tmp/rmxxxl_test_samples/a_long.wav"); remove("/tmp/rmxxxl_test_samples/b_short24.wav");
    remove("/tmp/rmxxxl_test_samples/c_float.wav");
    write_wav("/tmp/rmxxxl_test_samples/a_long.wav", 2, 16, 48000, 48000 * 2, 220);   // 2 s stereo 16-bit 48 kHz
    write_wav("/tmp/rmxxxl_test_samples/b_short24.wav", 1, 24, 44100, 4410, 440);      // 0.1 s mono 24-bit
    void* p = E->create(dir);
    set(p, "lfo_bpm", 120); set(p, "lim_ceiling", 0);
    Sig ps;
    char t[96];

    // the Edit controls follow the selected pad
    set(p, "pad_sel", 1); set(p, "env_a", 0.5); set(p, "pad_sound", 2);
    set(p, "pad_sel", 0);
    E->get_param(p, "env_a", t, sizeof t);
    check(atof(t) == 0.0, "selecting pad 1 shows its own Attack (0)", atof(t));
    set(p, "pad_sel", 1);
    E->get_param(p, "env_a", t, sizeof t);
    check(fabs(atof(t) - 0.5) < 1e-6, "back on pad 2 its Attack (0.5) returns", atof(t));
    E->get_param(p, "pad_sound", t, sizeof t);
    check(atoi(t) == 2, "and its Sound (slot 2)", atoi(t));
    E->get_param(p, "display_rev", t, sizeof t);
    check(atoi(t) > 0, "display_rev moves so MPC redraws the knobs", atoi(t));
    E->get_param(p, "pad_file_display", t, sizeof t);
    check(!strcmp(t, "PAD 2: b_short24.wav"), "Loaded Sound names the file", 0);
    printf("     (%s)\n", t);

    // slot 1 (2 s file) on pad 1 via MIDI: one-shot; Release (~500 ms) fades the end before the file ends
    set(p, "pad_sel", 0); set(p, "pad_sound", 1); set(p, "env_r", 0.5);
    set(p, "pad_root", 36);
    note(p, 36, 127); note(p, 36, 0);   // a quick tap
    double held = run(p, ps, 60, -1);
    check(held > 0.1, "sample slot 1 plays from a quick MIDI tap (not cut by note-off)", held);
    run(p, ps, 500, -1);                // to ~1.66 s, inside the release
    double fading = run(p, ps, 40, -1, 0.3, 20);
    check(fading < held * 0.6, "Release fades the end of the sample", fading / held);
    run(p, ps, 200, -1);
    double after_end = run(p, ps, 20, -1);
    check(after_end < 0.0005, "and it is silent after", after_end);
    set(p, "env_r", 0.3);

    // Attack shapes the start: 1 s attack -> the first 50 ms are much quieter than full level
    set(p, "env_a", 0.794);   // 2000 * 0.794^3 ~ 1000 ms
    note(p, 36, 127); note(p, 36, 0);
    double early = run(p, ps, 17, -1, 0.3, 17);
    double later = run(p, ps, 400, -1, 0.3, 40);
    check(early < later * 0.3, "Attack: slow rise", early / later);
    set(p, "env_a", 0);
    run(p, ps, 400, -1);

    // Sustain 0 + short Decay: a held note dies away even though the sample continues
    set(p, "env_s", 0); set(p, "env_d", 0.3);   // D ~ 108 ms
    note(p, 36, 127); note(p, 36, 0);
    run(p, ps, 60, -1);
    double sustained0 = run(p, ps, 20, -1);
    check(sustained0 < 0.001, "Sustain 0: decays to silence though the sample goes on", sustained0);
    set(p, "env_s", 1);

    // screen tap on pad 2 (24-bit mono slot) plays and selects pad 2
    set(p, "pad_sel", 1); set(p, "env_a", 0);
    set(p, "pad_sel", 0);
    set(p, "pad_snare", 1);
    double tap = run(p, ps, 4, -1);
    check(tap > 0.05, "screen tap plays a 24-bit mono sample", tap);
    E->get_param(p, "pad_sel", t, sizeof t);
    check(atoi(t) == 1, "tapping pad 2 selects it for editing", atoi(t));

    // empty slot: silent, named EMPTY
    set(p, "pad_sound", 9);
    E->get_param(p, "pad_file_display", t, sizeof t);
    check(strstr(t, "EMPTY") != NULL, "an empty slot says EMPTY", 0);
    run(p, ps, 100, -1);
    set(p, "pad_snare", 1);
    double silent = run(p, ps, 10, -1);
    check(silent < 0.0005, "an empty slot plays nothing", silent);

    // reload picks up a new file (sorted: c_float.wav becomes slot 3)
    write_wav("/tmp/rmxxxl_test_samples/c_float.wav", 2, 32, 22050, 22050, 330);
    set(p, "pad_sound", 3);
    set(p, "pad_reload", 1);
    for (int i = 0; i < 100; ++i) { run(p, ps, 2, -1); struct timespec ts = {0, 5000000}; nanosleep(&ts, NULL); }
    E->get_param(p, "pad_file_display", t, sizeof t);
    check(!strcmp(t, "PAD 2: c_float.wav"), "Reload loads a new 32-bit float file into slot 3", 0);
    set(p, "pad_snare", 1);
    check(run(p, ps, 10, -1) > 0.05, "and it plays", 0);

    // roll latch: keeps hitting until switched off
    set(p, "pad_sound", 0);
    run(p, ps, 200, -1);
    set(p, "roll_3", 1);
    double rl = run(p, ps, 300, -1);
    set(p, "roll_3", 0);
    run(p, ps, 300, -1);
    double rl_off = run(p, ps, 100, -1);
    check(rl > 0.01 && rl_off < 0.002, "Roll latch on screen: rolls, then stops", rl);


    // ---------------------------------------------------------------- 1.2: user presets
    {
      // presets live beside the sample folder: /tmp/RMXXXL Presets for /tmp/rmxxxl_test_samples
      system("rm -rf '/tmp/RMXXXL Presets'");
      void* q = E->create(dir);
      Sig qs;
      auto settle = [&](void* x) {
        for (int i = 0; i < 60; ++i) { run(x, qs, 1, -1); struct timespec ts = {0, 3000000}; nanosleep(&ts, NULL); }
      };
      set(q, "preset_slot", 2);
      E->get_param(q, "preset_info_display", t, sizeof t);
      check(!strcmp(t, "PRESET 3: EMPTY"), "an unused preset slot shows EMPTY", 0);
      set(q, "echo_send", 0.42); set(q, "rv_type", 1); set(q, "pad_sel", 1); set(q, "env_a", 0.25); set(q, "pad_sound", 4);
      set(q, "release", 1);
      set(q, "preset_save", 1);
      settle(q);
      E->get_param(q, "preset_info_display", t, sizeof t);
      FILE* f = fopen("/tmp/RMXXXL Presets/Preset 03.txt", "r");
      check(f != NULL && !strcmp(t, "PRESET 3: SAVED"), "Save writes Preset 03.txt and says so", 0);
      if (f) fclose(f);
      set(q, "echo_send", 0.0); set(q, "rv_type", 2); set(q, "pad_sel", 1); set(q, "env_a", 0.9); set(q, "pad_sound", 0);
      run(q, qs, 130, -1);
      set(q, "release", 0);
      set(q, "preset_load", 1);
      settle(q);
      E->get_param(q, "echo_send", t, sizeof t);
      double es = atof(t);
      E->get_param(q, "rv_type", t, sizeof t);
      int rt = atoi(t);
      set(q, "pad_sel", 1);
      E->get_param(q, "env_a", t, sizeof t);
      double ea = atof(t);
      E->get_param(q, "pad_sound", t, sizeof t);
      int ps2 = atoi(t);
      check(fabs(es - 0.42) < 1e-6 && rt == 1 && fabs(ea - 0.25) < 1e-6 && ps2 == 4, "Load brings back effects, pad envelope and sound", es);
      E->get_param(q, "release", t, sizeof t);
      check(atoi(t) == 0, "a preset leaves Release (the kill switch) alone", atoi(t));
      E->get_param(q, "preset_info_display", t, sizeof t);
      check(!strcmp(t, "PRESET 3: LOADED"), "Load says LOADED", 0);
      E->get_param(q, "display_rev", t, sizeof t);
      check(atoi(t) > 0, "a load bumps display_rev, so MPC redraws the knobs", atoi(t));
      // a fresh instance sees the stored slot; an empty slot reports EMPTY and changes nothing
      void* q2 = E->create(dir);
      set(q2, "preset_slot", 2);
      E->get_param(q2, "preset_info_display", t, sizeof t);
      check(!strcmp(t, "PRESET 3: STORED"), "a new instance finds the stored preset", 0);
      set(q2, "preset_slot", 9); set(q2, "echo_send", 0.33);
      set(q2, "preset_load", 1);
      settle(q2);
      E->get_param(q2, "preset_info_display", t, sizeof t);
      E->get_param(q2, "echo_send", buf, sizeof buf);
      check(!strcmp(t, "PRESET 10: EMPTY") && fabs(atof(buf) - 0.33) < 1e-6, "loading an empty slot changes nothing", atof(buf));
      E->destroy(q2);
      E->destroy(q);
    }

    // per-pad values survive save/restore
    set(p, "pad_sel", 3); set(p, "env_d", 0.9); set(p, "pad_sound", 1);
    char st[8192];
    E->get_param(p, "state", st, sizeof st);
    void* p2 = E->create(dir);
    E->set_param(p2, "state", st);
    E->get_param(p2, "env_d", t, sizeof t);
    double d2 = atof(t);
    E->get_param(p2, "pad_sel", t, sizeof t);
    int sel2 = atoi(t);
    E->get_param(p2, "pad_sound", t, sizeof t);
    int snd4 = atoi(t);
    set(p2, "pad_sel", 1);
    E->get_param(p2, "env_a", t, sizeof t);
    check(fabs(d2 - 0.9) < 1e-6 && sel2 == 3 && snd4 == 1 && atof(t) == 0.0, "per-pad ADSR + sounds round-trip the state", d2);
    E->destroy(p2);
    E->destroy(p);
  }

  // ---------------------------------------------------------------- everything at once, timing
  set(d, "cl_on", 1); set(d, "cl_mode", 2); set(d, "rv_type", 2); set(d, "rv_send", 0.6);
  set(d, "echo_send", 0.5); set(d, "build_up", 0.7); set(d, "filter", -0.3);
  run(d, sig, 400, 0, 0.2);
  clock_t c0 = clock();
  const int bench_blocks = 3445;   // 10 s of audio
  double all = run(d, sig, bench_blocks, 0, 0.2);
  double secs = (double)(clock() - c0) / CLOCKS_PER_SEC;
  check(isfinite(all) && all < 0.95, "all sections at once stay finite and below clip", all);
  printf("info everything on (Hall + Clouds + echo + scene): %.3f s CPU for 10 s audio on this x86 (sanitizers on)\n", secs);

  E->destroy(d);
  E->destroy(d2);
  printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
  return failures ? 1 : 0;
}
