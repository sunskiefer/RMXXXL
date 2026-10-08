// RMXXXL v.1 (ANDREALPHEUS): an RMX-1000-style performance effect for MPC OS (MPC Live/One/X/Key, Force), as one native insert effect.
//
// Signal flow, per 32-frame sub-block of each 128-frame MPC block:
//
//   in -> In Gain -> Isolator (Mixxx LR8) -> Filter (Mixxx biquads, + Scene sweeps) -> Clouds (Parasites)
//      -> dry (gated by Echo Out) + Echo (tempo-synced) + Riser noise -> Reverb send (Dragonfly Plate/Room/Hall)
//      -> Tape (Echo Out / Vinyl Brake / Backspin) -> Release (kill to dry) -> + X-Pad drums
//      -> brickwall limiter (Drive, Ceiling) -> out
//
// Scene FX: Build Up / Break Down are macros over filter, echo, Clouds, reverb and the noise riser.
// Release FX (button or MIDI) plays Echo Out / Vinyl Brake / Backspin for a number of beats, and latches the Scene
// macros off until both knobs are turned back to zero (as the RMX's release snaps the scene back to dry).
// Release (1.2) is a kill switch: On crossfades the effect chain to the dry input (Hard: 5 ms, Smooth: over Release
// Beats) and Off brings it back the same way. No parameter moves, and the effects keep running underneath.
// Panic puts every effect parameter back to its default, clears the tails and stops the pads; the pad setup stays.
//
// MPC OS sends no MIDI to insert effects, so each instance opens its own ALSA MIDI input, "RMXXXL N"
// (src/midi_in.cc, from FullPace/overcast). Notes, chromatic from MIDI Root: +0..3 Kick/Snare/Clap/Hat,
// +4..7 the same as rolls while held, +8 Release FX, +9 Release on/off, +10 Panic.
//
// Presets (1.2): 16 slots, files "<slot>.txt" holding the state string, in "RMXXXL Presets" beside the sample folder
// (/sdcard/RMXXXL Presets on the device). Save snapshots the state on the caller's thread and the worker writes it;
// Load has the worker read the file and the audio thread apply it. Pad sounds and envelopes are part of a preset.
//
// PADS page: Edit Pad selects which pad the Attack/Decay/Sustain/Release and Sound controls show; the engine keeps
// the values of all four and pushes the selected pad's into those controls (HAS_DISPLAY_REV). Sound picks the pad's
// built-in drum or one of 16 sample slots: the .wav files in /sdcard/RMXXXL Samples, sorted by name.

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <new>

#include "clouds/dsp/granular_processor.h"
#include "extra/ScopedDenormalDisable.hpp"
#include "dsp.h"
#include "pads.h"
#include "samples.h"
#include "midi_in.h"
#include "param_table.h"
#include "reverbs.h"

extern "C" {
#include "engine.h"
}

using namespace clouds;
using rfx::Clamp;

namespace {

const int kSub = 32;                          // sub-block: Clouds' block size, and our control rate
const size_t kLargeBufferSize = 118784;       // Clouds' block_mem
const size_t kSmallBufferSize = 65536 - 128;  // Clouds' block_ccm
const float kMaxKnob = 65535.0f / 65536.0f;   // Clouds' pots never reach 1.0 (see FullPace/overcast CLAUDE.md)
const float kSmoothing = 0.15f;               // per sub-block
const int kReconfigureBlocks = 689;           // Clouds mode/quality change at most every 0.5 s
const int kReverbTailBlocks = 12 * 44100 / kSub;   // keep a reverb running 12 s after its send closes
const uint64_t kStepLockFrames = 8820;      // 0.2 s: the shortest gap between two one-step moves of an option list
const uint64_t kGestureFrames = 15435;      // 0.35 s: sets of an on/off switch closer than this are one gesture
const int kNumReverbs = 3;                    // Plate, Room, Hall; type 3 = Clouds' own reverb
const int kRvClouds = 3;

const ReverbApi* const kReverbs[kNumReverbs] = { &kPlateReverb, &kRoomReverb, &kHallReverb };

const float kEchoBeats[8] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
const float kReleaseBeats[4] = { 0.5f, 1.0f, 2.0f, 4.0f };
const float kRollBeats[5] = { 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f };

// Clouds control names per mode (as FullPace/overcast, from the Clouds and Parasites manuals), for Q-Link names.
struct ModeNames { const char* key; const char* name[6]; };
const ModeNames kCloudNames[] = {
  { "cl_position", { "Position", "Scrub", "Delay", "Buffer", "Predelay", "Burst" } },
  { "cl_size", { "Size", "Window", "Loop Size", "Warp", "Room Size", "Chord" } },
  { "cl_texture", { "Texture", "Filter", "Filter", "Quantize", "Damping", "Damping" } },
  { "cl_density", { "Density", "Diffusion", "Diffusion", "Refresh", "Decay", "Decay" } },
  { "cl_pitch", { "Pitch", "Pitch", "Pitch", "Pitch", "Shimmer", "Pitch" } },
  { "cl_spread", { "Spread", "Spread", "Spread", "Spread", "Diffusion", "Voices L/R" } },
  { "cl_blend", { "Blend", "Blend", "Blend", "Blend", "Dry/Wet", "Distortion" } },
  { "cl_feedback", { "Feedback", "Feedback", "Feedback", "Feedback", "Mod Speed", "Harmonics" } },
  { "cl_reverb", { "Cloud Verb", "Cloud Verb", "Cloud Verb", "Cloud Verb", "Mod Amount", "Scatter" } },
};

struct Instance {
  // ---- Clouds (zeroed memory: GranularProcessor::Init() leaves members unset)
  GranularProcessor processor;
  uint8_t* large_buffer;
  uint8_t* small_buffer;
  ShortFrame cl_in[kSub], cl_out[kSub];
  int applied_mode, applied_quality, blocks_since_reconfigure;
  float clouds_mix;            // 0..1 crossfade between dry and Clouds' output
  bool clouds_trigger;
  pthread_t prepare_thread;
  bool prepare_running;
  volatile bool prepare_quit;
  pthread_mutex_t prepare_mutex;
  pthread_cond_t prepare_wake;

  // ---- parameters
  float param[P_COUNT];
  float smooth[P_COUNT];
  bool smooth_started;
  volatile int trig_pending[P_COUNT];   // momentary params: rising edges, consumed by the audio thread

  // ---- our DSP
  rfx::Isolator iso;
  rfx::DjFilter filter;
  rfx::Echo echo;
  rfx::Tape tape;
  rfx::Noise noise;
  rfx::Pads pads;
  rfx::Limiter limiter;
  float* echo_mem;
  float* tape_mem;

  // ---- reverbs
  void* rv[kNumReverbs];
  int rv_active;               // which Dragonfly reverb runs (0..2)
  float rv_gain;               // output fade for type switches
  bool rv_running;
  int rv_tail;                 // sub-blocks left to run after the send closed
  int rv_push_countdown;
  float rv_sent[8];            // last values pushed: decay, size, tone, predelay, width, algorithm
  int rv_size_countdown;

  // ---- scene / release
  bool scene_latched_off;

  // ---- MIDI / pads
  midi_in::Port* midi;
  float stored[rfx::PAD_COUNT][5];      // per pad: env_a, env_d, env_s, env_r (knob 0..1), sound (option)
  volatile int display_rev;
  char sample_dir[512];
  rfx::SampleBank* bank;               // audio thread's
  rfx::SampleBank* pending;            // loaded by the worker, taken by the audio thread (atomic)
  rfx::SampleBank* retired;            // given back by the audio thread, freed by the worker (atomic)
  volatile int reload_req;
  bool roll_held[rfx::PAD_COUNT];
  float roll_vel[rfx::PAD_COUNT];
  float roll_count[rfx::PAD_COUNT];
  bool roll_latched_was[rfx::PAD_COUNT];
  volatile int midi_release;
  volatile int midi_kill_toggle;

  // ---- option lists: one step per kStepLockFrames from a Q-Link / wheel nudge (see SetParam)
  uint64_t frames;                     // audio frames processed
  uint64_t last_step[P_COUNT];         // when each option last moved one step
  uint64_t last_touch[P_COUNT];        // switches: when the host last set them (a Q-Link turn is a burst of sets)
  bool loading;                        // LoadState: preset or project values are set outright

  // ---- release (kill) and riser
  float kill_amt;                      // 0 = effects, 1 = dry
  float duck_env;                      // riser ducker: input envelope

  // ---- presets (file I/O on the worker thread)
  char preset_dir[512];
  pthread_mutex_t preset_mutex;        // guards the request fields below (screen thread vs worker)
  char* save_text;                     // a snapshot waiting to be written (worker frees it)
  int save_slot;
  int load_slot;                       // -1: none
  char* loaded_text;                   // read by the worker, applied by the audio thread (atomic hand-over)
  char* retired_text;                  // applied, handed back to the worker to free
  int loaded_slot;
  volatile unsigned slot_used;         // bit per slot: a file exists
  volatile int preset_status;          // 0 idle, 1 saved, 2 loaded, 3 save failed, 4 empty slot, 5 load failed
  volatile int status_slot;

  float bpm;

  float bl[kSub], br[kSub];          // working buffers
  float sl[kSub], sr[kSub];          // sends
  float dl[kSub], dr[kSub];          // dry input (after In Gain), for Release
  float wl[kSub], wr[kSub];          // reverb wet
};

int Option(const Instance* s, int p) { return static_cast<int>(s->param[p] + 0.5f); }

Instance* NewInstance() {
  void* mem = calloc(1, sizeof(Instance));
  return mem ? new (mem) Instance : NULL;
}

// ------------------------------------------------------------------------------------------------ presets
const int kPresetSlots = 16;

void PresetPath(const Instance* s, int slot, char* buf, size_t len) {
  snprintf(buf, len, "%s/Preset %02d.txt", s->preset_dir, slot + 1);
}

void ScanPresets(Instance* s) {
  unsigned used = 0;
  char path[600];
  for (int i = 0; i < kPresetSlots; ++i) {
    struct stat st;
    PresetPath(s, i, path, sizeof path);
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) used |= 1u << i;
  }
  s->slot_used = used;
}

void SetPresetStatus(Instance* s, int slot, int status) {
  s->status_slot = slot;
  s->preset_status = status;
  __atomic_add_fetch(&s->display_rev, 1, __ATOMIC_RELAXED);
}

bool WriteText(const char* dir, const char* path, const char* text) {
  mkdir(dir, 0777);
  char tmp[640];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE* f = fopen(tmp, "w");
  if (!f) return false;
  size_t len = strlen(text);
  bool ok = fwrite(text, 1, len, f) == len;
  ok = (fclose(f) == 0) && ok;
  if (ok) ok = rename(tmp, path) == 0;
  if (!ok) remove(tmp);
  return ok;
}

char* ReadText(const char* path) {
  FILE* f = fopen(path, "r");
  if (!f) return NULL;
  char* buf = static_cast<char*>(malloc(8192));
  size_t len = buf ? fread(buf, 1, 8191, f) : 0;
  fclose(f);
  if (!buf) return NULL;
  buf[len] = 0;
  return buf;
}

// Worker side: write a pending save, read a pending load, free what the audio thread gave back.
void PresetWork(Instance* s) {
  free(__atomic_exchange_n(&s->retired_text, (char*)NULL, __ATOMIC_ACQ_REL));
  pthread_mutex_lock(&s->preset_mutex);
  char* text = s->save_text;
  int save_slot = s->save_slot, load_slot = s->load_slot;
  s->save_text = NULL;
  s->load_slot = -1;
  pthread_mutex_unlock(&s->preset_mutex);
  char path[600];
  if (text) {
    PresetPath(s, save_slot, path, sizeof path);
    bool ok = WriteText(s->preset_dir, path, text);
    free(text);
    if (ok) s->slot_used = s->slot_used | (1u << save_slot);
    SetPresetStatus(s, save_slot, ok ? 1 : 3);
  }
  if (load_slot >= 0) {
    PresetPath(s, load_slot, path, sizeof path);
    char* loaded = ReadText(path);
    if (!loaded) {
      s->slot_used = s->slot_used & ~(1u << load_slot);
      SetPresetStatus(s, load_slot, 4);
    } else {
      s->loaded_slot = load_slot;
      free(__atomic_exchange_n(&s->loaded_text, loaded, __ATOMIC_ACQ_REL));   // an unapplied older load is dropped
    }
  }
}

void* PrepareLoop(void* arg) {
  Instance* s = static_cast<Instance*>(arg);
  while (!s->prepare_quit) {
    pthread_mutex_lock(&s->prepare_mutex);
    timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += 3000000;
    if (until.tv_nsec >= 1000000000) { until.tv_sec += 1; until.tv_nsec -= 1000000000; }
    pthread_cond_timedwait(&s->prepare_wake, &s->prepare_mutex, &until);
    pthread_mutex_unlock(&s->prepare_mutex);
    for (int i = 0; i < 4 && !s->prepare_quit; ++i) s->processor.Prepare();
    rfx::SampleBank* old = __atomic_exchange_n(&s->retired, (rfx::SampleBank*)NULL, __ATOMIC_ACQ_REL);
    if (old) rfx::FreeSampleBank(old);
    if (s->reload_req && !__atomic_load_n(&s->pending, __ATOMIC_ACQUIRE)) {
      s->reload_req = 0;
      rfx::SampleBank* nb = rfx::LoadSampleBank(s->sample_dir);
      if (nb) __atomic_store_n(&s->pending, nb, __ATOMIC_RELEASE);
    }
    PresetWork(s);
  }
  return NULL;
}

void Destroy(void* inst);

void* Create(const char* data_dir) {
  Instance* s = NewInstance();
  if (!s) return NULL;
  pthread_mutex_init(&s->preset_mutex, NULL);
  s->load_slot = -1;
  snprintf(s->sample_dir, sizeof s->sample_dir, "%s", data_dir ? data_dir : "/sdcard/RMXXXL Samples");
  {   // presets live beside the sample folder: "<parent>/RMXXXL Presets"
    const char* slash = strrchr(s->sample_dir, '/');
    int parent = slash ? static_cast<int>(slash - s->sample_dir) : 0;
    snprintf(s->preset_dir, sizeof s->preset_dir, "%.*s%sRMXXXL Presets", parent, s->sample_dir, slash ? "/" : "");
  }
  ScanPresets(s);
  s->bank = rfx::LoadSampleBank(s->sample_dir);
  s->large_buffer = new (std::nothrow) uint8_t[kLargeBufferSize];
  s->small_buffer = new (std::nothrow) uint8_t[kSmallBufferSize];
  s->echo_mem = static_cast<float*>(calloc(rfx::kEchoLen * 2, sizeof(float)));
  s->tape_mem = static_cast<float*>(calloc(rfx::kTapeLen * 2, sizeof(float)));
  for (int i = 0; i < kNumReverbs; ++i) s->rv[i] = kReverbs[i]->create(44100.0);
  bool ok = s->large_buffer && s->small_buffer && s->echo_mem && s->tape_mem;
  for (int i = 0; i < kNumReverbs; ++i) ok = ok && s->rv[i];
  if (!ok) { Destroy(s); return NULL; }

  memset(s->large_buffer, 0, kLargeBufferSize);
  memset(s->small_buffer, 0, kSmallBufferSize);
  s->processor.Init(s->large_buffer, kLargeBufferSize, s->small_buffer, kSmallBufferSize);
  memset(s->processor.mutable_parameters(), 0, sizeof(Parameters));
  s->applied_mode = static_cast<int>(kParamDefs[P_CL_MODE].def);
  s->processor.set_playback_mode(PlaybackMode(s->applied_mode));
  s->processor.set_quality(0);
  s->processor.Prepare();
  s->blocks_since_reconfigure = kReconfigureBlocks;

  for (int p = 0; p < P_COUNT; ++p) s->param[p] = s->smooth[p] = kParamDefs[p].def;
  for (int k = 0; k < rfx::PAD_COUNT; ++k) {
    s->stored[k][0] = kParamDefs[P_ENV_A].def; s->stored[k][1] = kParamDefs[P_ENV_D].def;
    s->stored[k][2] = kParamDefs[P_ENV_S].def; s->stored[k][3] = kParamDefs[P_ENV_R].def;
    s->stored[k][4] = 0.0f;
  }
  s->iso.Init();
  s->filter.Init();
  s->echo.Init(s->echo_mem);
  s->tape.Init(s->tape_mem);
  uint32_t seed = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s));
  s->noise.Init(seed);
  s->pads.Init(seed ^ 0x9e3779b9u);
  s->limiter.Init();
  for (int i = 0; i < kNumReverbs; ++i) {
    kReverbs[i]->load_default(s->rv[i]);
    kReverbs[i]->mute(s->rv[i]);
  }
  for (int i = 0; i < 8; ++i) s->rv_sent[i] = -1.0f;
  s->rv_active = 0;
  s->rv_gain = 1.0f;
  s->bpm = 120.0f;
  s->midi = midi_in::Open();
  pthread_mutex_init(&s->prepare_mutex, NULL);
  pthread_cond_init(&s->prepare_wake, NULL);
  s->prepare_running = pthread_create(&s->prepare_thread, NULL, PrepareLoop, s) == 0;
  return s;
}

void Destroy(void* inst) {
  Instance* s = static_cast<Instance*>(inst);
  if (!s) return;
  if (s->prepare_running) {
    s->prepare_quit = true;
    pthread_cond_signal(&s->prepare_wake);
    pthread_join(s->prepare_thread, NULL);
    pthread_cond_destroy(&s->prepare_wake);
    pthread_mutex_destroy(&s->prepare_mutex);
  }
  midi_in::Close(s->midi);
  free(s->save_text);
  free(s->loaded_text);
  free(s->retired_text);
  pthread_mutex_destroy(&s->preset_mutex);
  for (int i = 0; i < kNumReverbs; ++i) if (s->rv[i]) kReverbs[i]->destroy(s->rv[i]);
  free(s->echo_mem);
  free(s->tape_mem);
  rfx::FreeSampleBank(s->bank);
  rfx::FreeSampleBank(s->pending);
  rfx::FreeSampleBank(s->retired);
  delete[] s->large_buffer;
  delete[] s->small_buffer;
  s->~Instance();
  free(s);
}

// ------------------------------------------------------------------------------------------------ pads
const int kPadProxies[5] = { P_ENV_A, P_ENV_D, P_ENV_S, P_ENV_R, P_PAD_SOUND };

// Shows pad k's stored values in the Edit controls; the wrapper reports the moved values to MPC (display_rev).
void SelectPad(Instance* s, int k) {
  k = Clamp(k, 0, rfx::PAD_COUNT - 1);
  s->param[P_PAD_SEL] = s->smooth[P_PAD_SEL] = static_cast<float>(k);
  for (int i = 0; i < 5; ++i) s->param[kPadProxies[i]] = s->smooth[kPadProxies[i]] = s->stored[k][i];
  ++s->display_rev;
}

float Cube(float v) { return v * v * v; }
rfx::Adsr PadAdsr(const Instance* s, int k) {
  rfx::Adsr a;
  a.a_ms = 2000.0f * Cube(s->stored[k][0]);
  a.d_ms = 4000.0f * Cube(s->stored[k][1]);
  a.s = Clamp(s->stored[k][2], 0.0f, 1.0f);
  a.r_ms = 4000.0f * Cube(s->stored[k][3]);
  return a;
}

void PlayPad(Instance* s, int k, float vel, bool held) {
  int sound = static_cast<int>(s->stored[k][4] + 0.5f);
  const rfx::Sample* smp = NULL;
  bool empty = false;
  if (sound > 0) {
    int slot = sound - 1;
    if (s->bank && slot < s->bank->count) smp = &s->bank->slot[slot];
    else empty = true;
  }
  s->pads.Trigger(k, vel, s->param[P_PAD_TUNE], smp, empty, PadAdsr(s, k), held);
}

// ------------------------------------------------------------------------------------------------ MIDI
void Midi(void* inst, const uint8_t* msg, int len) {
  Instance* s = static_cast<Instance*>(inst);
  if (len < 3) return;
  uint8_t status = msg[0] & 0xf0;
  bool on = status == 0x90 && msg[2] > 0;
  bool off = status == 0x80 || (status == 0x90 && msg[2] == 0);
  if (!on && !off) return;
  int rel = msg[1] - static_cast<int>(s->param[P_PAD_ROOT] + 0.5f);
  float vel = msg[2] / 127.0f;
  float tune = s->param[P_PAD_TUNE];
  (void)tune;
  if (rel >= 0 && rel < 4) {
    if (on) PlayPad(s, rel, vel, false);   // one-shot, as MPC drum programs: a quick tap never cuts the sound
  } else if (rel >= 4 && rel < 8) {
    int pad = rel - 4;
    if (on) {
      s->roll_held[pad] = true;
      s->roll_vel[pad] = vel;
      s->roll_count[pad] = 0.0f;   // first hit at once
    } else {
      s->roll_held[pad] = false;
    }
  } else if (rel == 8 && on) {
    s->midi_release = 1;
  } else if (rel == 9 && on) {
    s->midi_kill_toggle = 1;
  } else if (rel == 10 && on) {
    s->trig_pending[P_PANIC] = 1;
  }
}

// ------------------------------------------------------------------------------------------------ params
void SetParam(void* inst, const char* key, const char* val);
int SaveState(const Instance* s, char* buf, int buf_len);

// The ON / OFF switches (drawn as toggles: a tap sends the opposite value). Two-option lists drawn as buttons
// (Hard / Smooth) send the option tapped, so they are not switches.
bool IsSwitch(int p) {
  switch (p) {
    case P_RELEASE: case P_CL_ON: case P_CL_FREEZE: case P_CL_REVERSE:
    case P_ROLL_1: case P_ROLL_2: case P_ROLL_3: case P_ROLL_4:
      return true;
  }
  return false;
}

// Screen side of a preset save: snapshot the state now (the worker writes the file).
void RequestSave(Instance* s) {
  char* text = static_cast<char*>(malloc(8192));
  if (!text) return;
  SaveState(s, text, 8192);
  pthread_mutex_lock(&s->preset_mutex);
  free(s->save_text);   // an unwritten older save of the same press burst is replaced
  s->save_text = text;
  s->save_slot = Clamp(Option(s, P_PRESET_SLOT), 0, kPresetSlots - 1);
  pthread_mutex_unlock(&s->preset_mutex);
}

void RequestLoad(Instance* s) {
  pthread_mutex_lock(&s->preset_mutex);
  s->load_slot = Clamp(Option(s, P_PRESET_SLOT), 0, kPresetSlots - 1);
  pthread_mutex_unlock(&s->preset_mutex);
}

int SaveState(const Instance* s, char* buf, int buf_len) {
  int len = 0;
  for (int p = 0; p < P_COUNT && len < buf_len; ++p) {
    if (kParamDefs[p].kind == K_TRIGGER) continue;
    len += snprintf(buf + len, buf_len - len, "%s=%g;", kParamDefs[p].key, s->param[p]);
  }
  for (int k = 0; k < rfx::PAD_COUNT && len < buf_len; ++k) {
    len += snprintf(buf + len, buf_len - len, "pad%d_a=%g;pad%d_d=%g;pad%d_s=%g;pad%d_r=%g;pad%d_snd=%g;", k,
                    s->stored[k][0], k, s->stored[k][1], k, s->stored[k][2], k, s->stored[k][3], k, s->stored[k][4]);
  }
  return len < buf_len ? len : buf_len - 1;
}

// Not part of a preset: the kill switch (a performance state), the slot itself and the status readout.
bool PresetSkips(const char* key) {
  return !strcmp(key, "release") || !strcmp(key, "preset_slot") || !strcmp(key, "preset_info");
}

// preset: a user preset (skips PresetSkips keys) rather than MPC restoring a project.
void LoadState(Instance* s, const char* state, bool preset) {
  char item[64];
  s->loading = true;
  while (*state) {
    size_t n = strcspn(state, ";");
    if (n < sizeof item) {
      memcpy(item, state, n);
      item[n] = 0;
      char* eq = strchr(item, '=');
      if (eq) {
        *eq = 0;
        if (strcmp(item, "state") && !(preset && PresetSkips(item))) SetParam(s, item, eq + 1);
      }
    }
    state += n;
    if (*state == ';') ++state;
  }
  s->loading = false;
  SelectPad(s, Option(s, P_PAD_SEL));   // the stored pad values (read last) win over the saved Edit controls
}

void SetParam(void* inst, const char* key, const char* val) {
  Instance* s = static_cast<Instance*>(inst);
  if (!strcmp(key, "state")) { LoadState(s, val, false); return; }
  if (!strncmp(key, "pad", 3) && key[3] >= '0' && key[3] <= '3' && key[4] == '_') {   // saved per-pad values
    int k = key[3] - '0';
    const char* f = key + 5;
    int i = !strcmp(f, "a") ? 0 : !strcmp(f, "d") ? 1 : !strcmp(f, "s") ? 2 : !strcmp(f, "r") ? 3 : !strcmp(f, "snd") ? 4 : -1;
    if (i >= 0) s->stored[k][i] = Clamp(static_cast<float>(atof(val)), kParamDefs[kPadProxies[i]].min, kParamDefs[kPadProxies[i]].max);
    return;
  }
  if (!strcmp(key, "lfo_bpm")) {
    float b = static_cast<float>(atof(val));
    if (b >= 20.0f && b <= 400.0f) s->bpm = b;
    return;
  }
  for (int p = 0; p < P_COUNT; ++p) {
    if (strcmp(key, kParamDefs[p].key)) continue;
    float v = Clamp(static_cast<float>(atof(val)), kParamDefs[p].min, kParamDefs[p].max);
    if (kParamDefs[p].kind == K_TRIGGER) {
      // Every press fires. A trigger reads back 0 at once (GetParam), so MPC's button, which toggles the value it
      // read back, always sends 1 on a tap. (Up to 1.1.1 the engine kept the 1, so every second tap sent 0 and did
      // nothing: screen pads seemed to need several taps.)
      if (v > 0.5f) {
        s->trig_pending[p] = 1;
        if (p == P_PAD_RELOAD) s->reload_req = 1;
        if (p == P_PRESET_SAVE) RequestSave(s);
        if (p == P_PRESET_LOAD) RequestLoad(s);
      }
      return;
    }
    float old = s->param[p];
    if (IsSwitch(p) && !s->loading) {
      // On/off switches (1.2.1): a Q-Link turn either way flips the switch, once per turn. A turn arrives as a burst
      // of sets (towards the other value, or the same value again at the end stop), so the first set of a burst
      // flips and the rest of the burst (sets less than kGestureFrames apart) is ignored. A screen tap is a burst of
      // one, so it flips as before. The flip is reported back to MPC through display_rev.
      bool in_gesture = s->last_touch[p] != 0 && s->frames - s->last_touch[p] < kGestureFrames;
      s->last_touch[p] = s->frames ? s->frames : 1;
      if (in_gesture) { ++s->display_rev; return; }   // MPC shows its own guess: put ours back
      int cur = Option(s, p);
      int want = static_cast<int>(v + 0.5f);
      s->param[p] = static_cast<float>(want != cur ? want : 1 - cur);
      ++s->display_rev;
      return;
    }
    if (kParamDefs[p].kind == K_OPTION && kParamDefs[p].nopts > 2 && !s->loading) {
      // A Q-Link turn on the Force arrives as a fast stream of one-step nudges and raced through short lists (Beats
      // skipped 1/2 -> 4). One step per kStepLockFrames of audio time, however fast the knob turns; a tap that jumps
      // straight to another option (more than one step) is never held back.
      int from = static_cast<int>(old + 0.5f), to = static_cast<int>(v + 0.5f);
      if (to - from == 1 || from - to == 1) {
        if (s->frames - s->last_step[p] < kStepLockFrames && s->last_step[p] != 0) return;
        s->last_step[p] = s->frames;
      }
    }
    s->param[p] = v;
    if (p == P_PAD_SEL) {
      if (static_cast<int>(v + 0.5f) != static_cast<int>(old + 0.5f)) SelectPad(s, static_cast<int>(v + 0.5f));
    } else {
      for (int i = 0; i < 5; ++i) if (p == kPadProxies[i]) s->stored[Option(s, P_PAD_SEL)][i] = v;
    }
    return;
  }
}

const char* const kNotes[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

int FormatHz(char* buf, int len, float hz) {
  return hz >= 1000.0f ? snprintf(buf, len, "%.1f kHz", hz / 1000.0f) : snprintf(buf, len, "%d Hz", static_cast<int>(hz + 0.5f));
}

float DecaySeconds(float knob) { return 0.1f * powf(100.0f, Clamp(knob, 0.0f, 1.0f)); }   // 0.1..10 s
float ToneHz(float knob) { return 1000.0f * powf(16.0f, Clamp(knob, 0.0f, 1.0f)); }       // 1..16 kHz

int Display(const Instance* s, int p, char* buf, int len) {
  float v = s->param[p];
  switch (p) {
    case P_FILTER: {
      float lpf, hpf;
      rfx::FilterCorners(v, &lpf, &hpf);
      if (lpf < rfx::kMaxCorner) { int n = snprintf(buf, len, "LPF "); return n + FormatHz(buf + n, len - n, lpf); }
      if (hpf > rfx::kMinCorner) { int n = snprintf(buf, len, "HPF "); return n + FormatHz(buf + n, len - n, hpf); }
      return snprintf(buf, len, "Off");
    }
    case P_FILTER_RES: return snprintf(buf, len, "Q %.2f", rfx::ResonanceQ(v));
    case P_ISO_LOW: case P_ISO_MID: case P_ISO_HIGH: {
      float g = rfx::IsoGain(v);
      if (g <= 0.0f) return snprintf(buf, len, "Kill");
      return snprintf(buf, len, "%+.1f dB", 20.0f * log10f(g));
    }
    case P_RV_DECAY: return snprintf(buf, len, "%.1f s", DecaySeconds(v));
    case P_RV_TONE: return FormatHz(buf, len, ToneHz(v));
    case P_RV_SIZE: {
      int type = Option(s, P_RV_TYPE);
      if (type == 0) return snprintf(buf, len, "%s", v < 0.34f ? "Small Tank" : (v < 0.67f ? "Plate" : "Large Tank"));
      if (type == 1) return snprintf(buf, len, "%.0f m", 8.0f + 24.0f * v);
      if (type == 2) return snprintf(buf, len, "%.0f m", 10.0f + 50.0f * v);
      return snprintf(buf, len, "%.0f %%", v * 100.0f);
    }
    case P_ENV_A: case P_ENV_D: case P_ENV_R: {
      float ms = (p == P_ENV_A ? 2000.0f : 4000.0f) * Cube(v);
      return ms >= 1000.0f ? snprintf(buf, len, "%.2f s", ms / 1000.0f) : snprintf(buf, len, "%.0f ms", ms);
    }
    case P_ENV_S: return snprintf(buf, len, "%.0f %%", v * 100.0f);
    case P_PAD_FILE: {
      int k = Clamp(Option(s, P_PAD_SEL), 0, rfx::PAD_COUNT - 1);
      int sound = static_cast<int>(s->stored[k][4] + 0.5f);
      static const char* const kBuiltin[rfx::PAD_COUNT] = { "KICK", "SNARE", "CLAP", "HAT" };
      if (sound == 0) return snprintf(buf, len, "PAD %d: BUILT-IN %s", k + 1, kBuiltin[k]);
      const rfx::SampleBank* b = s->bank;
      if (b && sound - 1 < b->count) return snprintf(buf, len, "PAD %d: %s", k + 1, b->slot[sound - 1].name);
      return snprintf(buf, len, "PAD %d: SLOT %d EMPTY", k + 1, sound);
    }
    case P_PAD_ROOT: {
      int n = static_cast<int>(v + 0.5f);
      return snprintf(buf, len, "%s%d", kNotes[n % 12], n / 12 - 2);   // MPC numbering: note 60 = C3
    }
    case P_NOISE_MOD:
      return v < 0.005f ? snprintf(buf, len, "Off") : snprintf(buf, len, "%d %%", static_cast<int>(v * 100.0f + 0.5f));
    case P_PRESET_INFO: {
      int slot = Clamp(Option(s, P_PRESET_SLOT), 0, kPresetSlots - 1);
      int st = s->status_slot == slot ? s->preset_status : 0;
      static const char* const kStatus[6] = { NULL, "SAVED", "LOADED", "SAVE FAILED", "EMPTY", "LOAD FAILED" };
      if (st > 0 && st < 6) return snprintf(buf, len, "PRESET %d: %s", slot + 1, kStatus[st]);
      return snprintf(buf, len, "PRESET %d: %s", slot + 1, (s->slot_used >> slot) & 1u ? "STORED" : "EMPTY");
    }
  }
  return 0;
}

int GetParam(void* inst, const char* key, char* buf, int buf_len) {
  const Instance* s = static_cast<const Instance*>(inst);
  if (!strcmp(key, "state")) return SaveState(s, buf, buf_len);
  if (!strcmp(key, "display_rev")) return snprintf(buf, buf_len, "%d", s->display_rev);
  size_t key_len = strlen(key);
  if (key_len > 8 && !strcmp(key + key_len - 8, "_display")) {
    for (int p = 0; p < P_COUNT; ++p) {
      if (strlen(kParamDefs[p].key) == key_len - 8 && !strncmp(key, kParamDefs[p].key, key_len - 8)) return Display(s, p, buf, buf_len);
    }
    return 0;
  }
  if (key_len > 5 && !strcmp(key + key_len - 5, "_name")) {
    int mode = Clamp(Option(s, P_CL_MODE), 0, 5);
    for (size_t i = 0; i < sizeof(kCloudNames) / sizeof(kCloudNames[0]); ++i) {
      if (strlen(kCloudNames[i].key) == key_len - 5 && !strncmp(key, kCloudNames[i].key, key_len - 5)) {
        return snprintf(buf, buf_len, "%s", kCloudNames[i].name[mode]);
      }
    }
    return 0;
  }
  for (int p = 0; p < P_COUNT; ++p) {
    if (strcmp(key, kParamDefs[p].key)) continue;
    if (kParamDefs[p].kind == K_OPTION) return snprintf(buf, buf_len, "%d", Option(s, p));
    return snprintf(buf, buf_len, "%g", s->param[p]);
  }
  return 0;
}

// ------------------------------------------------------------------------------------------------ audio
float BeatSamples(const Instance* s) { return 60.0f / s->bpm * 44100.0f; }

bool TakeTrigger(Instance* s, int p) {
  if (!s->trig_pending[p]) return false;
  s->trig_pending[p] = 0;
  return true;
}

// Scene macro amounts after the release latch.
void SceneAmounts(Instance* s, float* build, float* brk) {
  float b = s->smooth[P_BUILD_UP], d = s->smooth[P_BREAK_DOWN];
  if (s->scene_latched_off) {
    if (s->param[P_BUILD_UP] < 0.05f && s->param[P_BREAK_DOWN] < 0.05f) s->scene_latched_off = false;
    else { b = 0.0f; d = 0.0f; }
  }
  *build = Clamp(b, 0.0f, 1.0f);
  *brk = Clamp(d, 0.0f, 1.0f);
}

void StartRelease(Instance* s) {
  float len = kReleaseBeats[Clamp(Option(s, P_RELEASE_LEN), 0, 3)] * BeatSamples(s);
  int kind = Clamp(Option(s, P_RELEASE_FX), 0, 2);
  // Echo Out repeats the last echo-beat of audio; a beat longer than the release is halved until it fits twice.
  float segment = kEchoBeats[Clamp(Option(s, P_ECHO_DIV), 0, 7)] * BeatSamples(s);
  while (segment > len * 0.5f && segment > 2205.0f) segment *= 0.5f;
  s->tape.Start(kind, len, segment, s->param[P_ECHO_FB] + 0.25f);
  if (s->param[P_BUILD_UP] > 0.05f || s->param[P_BREAK_DOWN] > 0.05f) s->scene_latched_off = true;
}

// Push reverb settings to the active Dragonfly reverb, only when they changed (they recompute coefficients, and
// Room/Hall's size re-allocates delay lines), size at most every 0.25 s.
void PushReverb(Instance* s, bool force) {
  if (!force && --s->rv_push_countdown > 0) return;
  s->rv_push_countdown = 4;
  if (s->rv_size_countdown > 0) --s->rv_size_countdown;
  const ReverbApi* api = kReverbs[s->rv_active];
  void* d = s->rv[s->rv_active];
  float vals[6] = { DecaySeconds(s->smooth[P_RV_DECAY]), s->smooth[P_RV_SIZE], ToneHz(s->smooth[P_RV_TONE]),
                    s->smooth[P_RV_PREDELAY], s->smooth[P_RV_WIDTH], 0.0f };
  const float eps[6] = { 0.02f, 0.01f, 50.0f, 0.5f, 0.5f, 0.0f };
  for (int i = 0; i < 5; ++i) {
    if (!force && fabsf(vals[i] - s->rv_sent[i]) < eps[i]) continue;
    if (i == 1 && !force && s->rv_size_countdown > 0) continue;
    s->rv_sent[i] = vals[i];
    switch (i) {
      case 0: api->set(d, "decay", vals[0]); break;
      case 1:
        s->rv_size_countdown = 86;   // 86 x 4 sub-blocks ~= 0.25 s
        if (s->rv_active == 0) {
          api->set(d, "algorithm", vals[1] < 0.34f ? 0.0f : (vals[1] < 0.67f ? 1.0f : 2.0f));
        } else if (s->rv_active == 1) {
          api->set(d, "size", 8.0f + 24.0f * vals[1]);
        } else {
          api->set(d, "size", 10.0f + 50.0f * vals[1]);
        }
        break;
      case 2:
        api->set(d, "high_cut", vals[2]);       // Plate, Hall
        api->set(d, "in_high_cut", vals[2]);    // Room
        api->set(d, "early_damp", fminf(vals[2], 16000.0f));
        api->set(d, "late_damp", fminf(vals[2] * 0.8f, 16000.0f));
        break;
      case 3: api->set(d, "predelay", vals[3]); api->set(d, "delay", vals[3]); break;
      case 4: api->set(d, "width", vals[4]); break;
    }
  }
}

// Wet-only levels per reverb, so the send alone decides how much is heard (the presets mix 80 % dry).
void ConfigureReverbLevels(Instance* s, int i) {
  const ReverbApi* api = kReverbs[i];
  void* d = s->rv[i];
  api->set(d, "dry_level", 0.0f);
  if (i == 0) {
    api->set(d, "early_level", 60.0f);   // Plate's "Wet Level" symbol
  } else {
    api->set(d, "early_level", 20.0f);
    api->set(d, "late_level", 60.0f);
  }
  api->set(d, "low_cut", 60.0f);
  api->set(d, "in_low_cut", 60.0f);
}

void UpdateSmoothing(Instance* s) {
  for (int p = 0; p < P_COUNT; ++p) {
    if (kParamDefs[p].kind != K_CONT) { s->smooth[p] = s->param[p]; continue; }
    if (!s->smooth_started) s->smooth[p] = s->param[p];
    s->smooth[p] += kSmoothing * (s->param[p] - s->smooth[p]);
  }
  s->smooth_started = true;
}

inline int16_t ToShort(float x) {
  x *= 32768.0f;
  return static_cast<int16_t>(x > 32767.0f ? 32767.0f : (x < -32768.0f ? -32768.0f : x));
}

inline float SoftSaturate(float x) {
  const float t = 0.9f;
  float a = fabsf(x);
  if (a <= t) return x;
  float over = (a - t) / (1.0f - t);
  float y = t + (1.0f - t) * over / (1.0f + over);
  return x < 0.0f ? -y : y;
}

void Render(void* inst, int16_t* out_lr, int frames) {
  (void)inst;
  memset(out_lr, 0, sizeof(int16_t) * 2 * frames);   // an effect: the wrapper calls Process()
}

// Panic keeps these: the pad setup (sounds, envelopes, level, tune, roll beat, MIDI root) and the preset slot.
bool PanicKeeps(int p) {
  switch (p) {
    case P_PAD_LEVEL: case P_PAD_TUNE: case P_PAD_ROLL: case P_PAD_ROOT: case P_PAD_SEL: case P_ENV_A: case P_ENV_D:
    case P_ENV_S: case P_ENV_R: case P_PAD_SOUND: case P_PAD_FILE: case P_PRESET_SLOT: case P_PRESET_INFO:
      return true;
  }
  return false;
}

// Every effect parameter back to its default (the knobs follow through display_rev), every tail cleared, every pad
// and roll stopped. Release is switched off at once.
void Panic(Instance* s) {
  for (int p = 0; p < P_COUNT; ++p) {
    s->trig_pending[p] = 0;
    if (PanicKeeps(p) || kParamDefs[p].kind == K_TRIGGER) continue;
    s->param[p] = s->smooth[p] = kParamDefs[p].def;
  }
  s->kill_amt = 0.0f;
  s->midi_release = s->midi_kill_toggle = 0;
  s->scene_latched_off = false;
  s->iso.Init();
  s->filter.Init();
  s->echo.StartClear();
  s->tape.Stop();
  s->noise.Clear();
  s->duck_env = 0.0f;
  s->limiter.Init();
  for (int i = 0; i < kNumReverbs; ++i) kReverbs[i]->mute(s->rv[i]);
  s->rv_running = false;
  s->rv_tail = 0;
  s->clouds_mix = 0.0f;   // Clouds is off at its default; its own buffer is left to the next time it runs
  s->clouds_trigger = false;
  s->pads.StopAll();
  for (int k = 0; k < rfx::PAD_COUNT; ++k) { s->roll_held[k] = false; s->roll_latched_was[k] = false; }
  ++s->display_rev;
}

void ProcessSub(Instance* s, const int16_t* in, int16_t* out, int n) {
  if (TakeTrigger(s, P_PANIC)) Panic(s);

  // ---- a preset the worker read
  char* preset = __atomic_exchange_n(&s->loaded_text, (char*)NULL, __ATOMIC_ACQ_REL);
  if (preset) {
    LoadState(s, preset, true);
    free(__atomic_exchange_n(&s->retired_text, preset, __ATOMIC_ACQ_REL));   // the worker frees it (rarely us)
    SetPresetStatus(s, s->loaded_slot, 2);
  }
  if (s->midi_kill_toggle) {
    s->midi_kill_toggle = 0;
    s->param[P_RELEASE] = Option(s, P_RELEASE) ? 0.0f : 1.0f;
    ++s->display_rev;
  }

  UpdateSmoothing(s);
  float build, brk;
  SceneAmounts(s, &build, &brk);

  // ---- a freshly loaded sample bank
  rfx::SampleBank* nb = __atomic_exchange_n(&s->pending, (rfx::SampleBank*)NULL, __ATOMIC_ACQ_REL);
  if (nb) {
    s->pads.StopSamples();
    rfx::SampleBank* old = s->bank;
    s->bank = nb;
    ++s->display_rev;
    if (old) __atomic_store_n(&s->retired, old, __ATOMIC_RELEASE);
  }

  // ---- triggers
  TakeTrigger(s, P_PAD_RELOAD);   // handled in SetParam (the worker loads)
  TakeTrigger(s, P_PRESET_SAVE);  // handled in SetParam / the worker
  TakeTrigger(s, P_PRESET_LOAD);
  if (TakeTrigger(s, P_RELEASE_GO) || s->midi_release) { s->midi_release = 0; StartRelease(s); }
  const int pad_params[rfx::PAD_COUNT] = { P_PAD_KICK, P_PAD_SNARE, P_PAD_CLAP, P_PAD_HAT };
  for (int k = 0; k < rfx::PAD_COUNT; ++k) {
    if (!TakeTrigger(s, pad_params[k])) continue;
    PlayPad(s, k, 1.0f, false);
    if (Option(s, P_PAD_SEL) != k) SelectPad(s, k);   // tapping a pad selects it for editing, as on the MPC
  }
  float roll = kRollBeats[Clamp(Option(s, P_PAD_ROLL), 0, 4)] * BeatSamples(s);
  const int roll_params[rfx::PAD_COUNT] = { P_ROLL_1, P_ROLL_2, P_ROLL_3, P_ROLL_4 };
  for (int k = 0; k < rfx::PAD_COUNT; ++k) {
    bool latched = Option(s, roll_params[k]) != 0;
    if (!s->roll_held[k] && !latched) { if (!latched) s->roll_latched_was[k] = false; continue; }
    if (latched && !s->roll_held[k] && !s->roll_latched_was[k]) { s->roll_latched_was[k] = true; s->roll_count[k] = 0.0f; s->roll_vel[k] = 1.0f; }
    s->roll_count[k] -= n;
    if (s->roll_count[k] <= 0.0f) { PlayPad(s, k, s->roll_vel[k], false); s->roll_count[k] += roll; }
  }
  if (TakeTrigger(s, P_CL_TRIGGER)) s->clouds_trigger = true;

  // ---- input
  float in_gain = rfx::DbToGain(s->smooth[P_IN_GAIN]);
  float* l = s->bl;
  float* r = s->br;
  for (int i = 0; i < n; ++i) {
    l[i] = s->dl[i] = in[2 * i] * (in_gain / 32768.0f);
    r[i] = s->dr[i] = in[2 * i + 1] * (in_gain / 32768.0f);
  }

  // ---- isolator
  s->iso.SetFreqs(s->smooth[P_ISO_LO_FREQ], s->smooth[P_ISO_HI_FREQ]);
  s->iso.Process(l, r, n, rfx::IsoGain(s->smooth[P_ISO_LOW]), rfx::IsoGain(s->smooth[P_ISO_MID]),
                 rfx::IsoGain(s->smooth[P_ISO_HIGH]));

  // ---- filter + scene sweeps
  float lpf, hpf;
  rfx::FilterCorners(s->smooth[P_FILTER], &lpf, &hpf);
  if (build > 0.005f) hpf = fmaxf(hpf, 20.0f * powf(s->smooth[P_BUILD_HPF] / 20.0f, build));
  if (brk > 0.005f) lpf = fminf(lpf, 20000.0f * powf(s->smooth[P_BREAK_LPF] / 20000.0f, brk));
  s->filter.Set(lpf, hpf, rfx::ResonanceQ(s->smooth[P_FILTER_RES]));
  s->filter.Process(l, r, n);

  // ---- reverb send amount (needed before Clouds: type "Clouds" uses Clouds' own reverb)
  int rv_type = Clamp(Option(s, P_RV_TYPE), 0, 3);
  float rv_send = Clamp(s->smooth[P_RV_SEND] + s->smooth[P_RV_SCENE] * (0.8f * build + 0.6f * brk), 0.0f, 1.0f);

  // ---- Clouds
  bool clouds_on = Option(s, P_CL_ON) != 0;
  bool clouds_for_reverb = rv_type == kRvClouds && rv_send > 0.001f;
  float clouds_target = (clouds_on || clouds_for_reverb) ? 1.0f : 0.0f;
  if (s->clouds_mix > 0.0f || clouds_target > 0.0f) {
    GranularProcessor& g = s->processor;
    int mode = Clamp(Option(s, P_CL_MODE), 0, int(PLAYBACK_MODE_LAST) - 1);
    int quality = Clamp(Option(s, P_CL_QUALITY), 0, 3);
    if (s->blocks_since_reconfigure < kReconfigureBlocks) ++s->blocks_since_reconfigure;
    if ((mode != s->applied_mode || quality != s->applied_quality) && s->blocks_since_reconfigure >= kReconfigureBlocks) {
      s->applied_mode = mode;
      s->applied_quality = quality;
      s->blocks_since_reconfigure = 0;
    }
    g.set_playback_mode(PlaybackMode(s->applied_mode));
    g.set_quality(s->applied_quality);
    float cs = s->smooth[P_CL_SCENE];
    Parameters* p = g.mutable_parameters();
    p->position = Clamp(s->smooth[P_CL_POSITION], 0.0f, kMaxKnob);
    p->size = Clamp(s->smooth[P_CL_SIZE], 0.0f, kMaxKnob);
    p->pitch = Clamp(s->smooth[P_CL_PITCH], -48.0f, 48.0f);
    p->density = Clamp(s->smooth[P_CL_DENSITY], 0.0f, kMaxKnob);
    p->texture = Clamp(s->smooth[P_CL_TEXTURE], 0.0f, kMaxKnob);
    // Level (1.2.1): Clouds' own dry/wet keeps the dry at -3 dB (equal-power fade) and its output stage halves
    // everything (SoftConvert), so switching it on dropped the dry by 9 dB. So Clouds now runs fully wet and RMXXXL
    // mixes: the dry stays at unity up to Blend 50 %, then fades out; the wet comes in up to 50 %, then stays.
    // Resonestor uses Blend as Distortion (Parasites), so it keeps its own mix; the Clouds reverb type (Clouds off)
    // runs it dry-through with its reverb, both brought back to unity.
    float blend = Clamp(s->smooth[P_CL_BLEND] + cs * 0.4f * (build + brk), 0.0f, 1.0f);
    bool resonestor = s->applied_mode == PLAYBACK_MODE_RESONESTOR;
    p->dry_wet = !clouds_on ? 0.0f : resonestor ? Clamp(blend, 0.0f, kMaxKnob) : kMaxKnob;
    p->stereo_spread = Clamp(s->smooth[P_CL_SPREAD], 0.0f, kMaxKnob);
    p->feedback = Clamp(s->smooth[P_CL_FEEDBACK] + (clouds_on ? cs * 0.6f * build : 0.0f), 0.0f, kMaxKnob);
    float verb = s->smooth[P_CL_REVERB];
    if (rv_type == kRvClouds) verb = fmaxf(verb, rv_send);
    p->reverb = Clamp(verb, 0.0f, kMaxKnob);
    p->freeze = Option(s, P_CL_FREEZE) != 0;
    p->granular.reverse = Option(s, P_CL_REVERSE) != 0;
    p->trigger = s->clouds_trigger;
    p->gate = s->clouds_trigger;
    s->clouds_trigger = false;
    for (int i = 0; i < n; ++i) {
      s->cl_in[i].l = ToShort(SoftSaturate(l[i]));
      s->cl_in[i].r = ToShort(SoftSaturate(r[i]));
    }
    g.Process(s->cl_in, s->cl_out, n);
    if (!s->prepare_running) g.Prepare();
    float step = 1.0f / 441.0f;   // 10 ms crossfade in and out
    // makeup: undo SoftConvert's 0.5 (and the -3 dB dry, or Clouds' wet gain of 1.2 x 0.707)
    const float kDryMakeup = 2.8284f, kWetMakeup = 2.357f, kResonestorMakeup = 1.5f;
    // per-mode wet trim, measured with noise so a fully wet Clouds sits near the dry level (granular is sparse,
    // Oliverb and spectral are dense): granular, stretch, looping delay, spectral, Oliverb
    static const float kWetTrim[5] = { 1.6f, 1.2f, 1.3f, 0.8f, 0.6f };
    float trim = s->applied_mode >= 0 && s->applied_mode < 5 ? kWetTrim[s->applied_mode] : 1.0f;
    float dry_g = blend < 0.5f ? 1.0f : 2.0f * (1.0f - blend);
    float wet_g = blend < 0.5f ? 2.0f * blend : 1.0f;
    for (int i = 0; i < n; ++i) {
      s->clouds_mix += clouds_target > s->clouds_mix ? step : -step;
      s->clouds_mix = Clamp(s->clouds_mix, 0.0f, 1.0f);
      float cl = s->cl_out[i].l / 32768.0f, cr = s->cl_out[i].r / 32768.0f;
      float ml, mr;
      if (!clouds_on) { ml = cl * kDryMakeup; mr = cr * kDryMakeup; }
      else if (resonestor) { ml = cl * kResonestorMakeup; mr = cr * kResonestorMakeup; }
      else { ml = l[i] * dry_g + cl * kWetMakeup * trim * wet_g; mr = r[i] * dry_g + cr * kWetMakeup * trim * wet_g; }
      l[i] += (ml - l[i]) * s->clouds_mix;
      r[i] += (mr - r[i]) * s->clouds_mix;
    }
  }
  if (s->prepare_running) pthread_cond_signal(&s->prepare_wake);

  // ---- echo (send)
  float beat = BeatSamples(s);
  float echo_delay = kEchoBeats[Clamp(Option(s, P_ECHO_DIV), 0, 7)] * beat;
  float echo_send = Clamp(s->smooth[P_ECHO_SEND] + 0.6f * powf(build, 1.5f) + 0.5f * brk, 0.0f, 1.0f);
  float echo_fb = Clamp(s->smooth[P_ECHO_FB] + 0.35f * build + 0.2f * brk, 0.0f, 0.92f);
  for (int i = 0; i < n; ++i) { s->sl[i] = l[i] * echo_send; s->sr[i] = r[i] * echo_send; }
  s->echo.Process(s->sl, s->sr, l, r, n, echo_delay, echo_fb);

  // ---- noise riser: its band sweeps up with Build Up, shifted by Tune; Duck pumps it under the input's hits
  float noise = s->smooth[P_SCENE_NOISE] * build * build * 0.15f;
  float duck_amount = s->smooth[P_NOISE_MOD];
  {
    float peak = 0.0f;
    for (int i = 0; i < n; ++i) peak = fmaxf(peak, fmaxf(fabsf(s->dl[i]), fabsf(s->dr[i])));
    // attack within a sub-block, release about 150 ms (0.9952 per 32-frame step)
    s->duck_env = peak > s->duck_env ? peak : s->duck_env * 0.9952f;
  }
  if (noise > 0.0001f) {
    float tune = powf(2.0f, s->smooth[P_NOISE_TUNE] / 12.0f);
    s->noise.Set(Clamp(300.0f * powf(8000.0f / 300.0f, build) * tune, 40.0f, 12000.0f));
    float duck = 1.0f - duck_amount * Clamp(s->duck_env * 2.5f, 0.0f, 1.0f);
    s->noise.Add(l, r, n, noise * duck);
  }

  // ---- reverb (Dragonfly)
  if (rv_type != kRvClouds && rv_type != s->rv_active && s->rv_gain <= 0.0f) {
    s->rv_active = rv_type;
    kReverbs[rv_type]->mute(s->rv[rv_type]);
    ConfigureReverbLevels(s, rv_type);
    for (int i = 0; i < 8; ++i) s->rv_sent[i] = -1.0f;
    PushReverb(s, true);
  }
  bool rv_wanted = rv_type != kRvClouds && rv_send > 0.001f;
  if (rv_wanted) { s->rv_running = true; s->rv_tail = kReverbTailBlocks; }
  else if (s->rv_running && --s->rv_tail <= 0) { s->rv_running = false; kReverbs[s->rv_active]->mute(s->rv[s->rv_active]); }
  if (s->rv_running) {
    if (s->rv_sent[0] < 0.0f) { ConfigureReverbLevels(s, s->rv_active); PushReverb(s, true); }
    else PushReverb(s, false);
    float send = rv_type == s->rv_active ? rv_send : 0.0f;   // during a type switch the old one only rings out
    for (int i = 0; i < n; ++i) { s->sl[i] = l[i] * send; s->sr[i] = r[i] * send; }
    const float* ins[2] = { s->sl, s->sr };
    float* outs[2] = { s->wl, s->wr };
    kReverbs[s->rv_active]->run(s->rv[s->rv_active], ins, outs, n);
    float target = (rv_type == s->rv_active || rv_type == kRvClouds) ? 1.0f : 0.0f;
    for (int i = 0; i < n; ++i) {
      s->rv_gain += target > s->rv_gain ? 1.0f / 256.0f : -1.0f / 256.0f;
      s->rv_gain = Clamp(s->rv_gain, 0.0f, 1.0f);
      l[i] += s->wl[i] * s->rv_gain;
      r[i] += s->wr[i] * s->rv_gain;
    }
  } else if (rv_type != kRvClouds && rv_type != s->rv_active) {
    s->rv_gain = 0.0f;   // nothing ringing: switch at the next sub-block
  }

  // ---- release tape
  s->tape.Process(l, r, n);

  // ---- Release (kill): crossfade the effect chain to the dry input. Hard: 5 ms (no click), Smooth: Release Beats.
  float kill_target = Option(s, P_RELEASE) ? 1.0f : 0.0f;
  if (s->kill_amt != kill_target || kill_target > 0.0f) {
    float ramp = Option(s, P_KILL_MODE) ? kReleaseBeats[Clamp(Option(s, P_RELEASE_LEN), 0, 3)] * BeatSamples(s) : 220.5f;
    float step = 1.0f / fmaxf(ramp, 1.0f);
    for (int i = 0; i < n; ++i) {
      s->kill_amt = kill_target > s->kill_amt ? fminf(kill_target, s->kill_amt + step) : fmaxf(kill_target, s->kill_amt - step);
      float k = s->kill_amt * s->kill_amt * (3.0f - 2.0f * s->kill_amt);   // smoothstep: an even fade
      l[i] += (s->dl[i] - l[i]) * k;
      r[i] += (s->dr[i] - r[i]) * k;
    }
  }

  // ---- pads (on top of everything: Release does not mute what you play)
  s->pads.Add(l, r, n, s->smooth[P_PAD_LEVEL]);

  // ---- brickwall limiter: Drive in, Ceiling out
  s->limiter.Process(l, r, n, s->smooth[P_LIM_DRIVE], s->smooth[P_LIM_CEILING], s->smooth[P_LIM_RELEASE]);
  for (int i = 0; i < n; ++i) {
    out[2 * i] = ToShort(l[i]);
    out[2 * i + 1] = ToShort(r[i]);
  }
}

void Process(void* inst, const int16_t* in_lr, int16_t* out_lr, int frames) {
  Instance* s = static_cast<Instance*>(inst);
  ScopedDenormalDisable no_denormals;
  midi_in::Poll(s->midi, Midi, s);
  for (int off = 0; off < frames; off += kSub) {
    int n = frames - off < kSub ? frames - off : kSub;
    ProcessSub(s, in_lr + 2 * off, out_lr + 2 * off, n);
  }
  s->frames += frames;
}

const mpc_engine_t kEngine = { Create, Destroy, Midi, SetParam, GetParam, Render, Process };

}  // namespace

extern "C" const mpc_engine_t* mpc_engine(void) { return &kEngine; }
