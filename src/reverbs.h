// Dragonfly Reverb (Plate, Room, Hall) as three engines inside one plugin.
//
// Each Dragonfly plugin's DSP defines the same class names (DragonflyReverbDSP, ...), so each is compiled in its
// own translation unit (rv_plate.cc, rv_room.cc, rv_hall.cc) inside its own namespace, and reached through this
// small table. Values are in Dragonfly's own units, set by the upstream parameter symbol.
#pragma once
#include <stdint.h>

struct ReverbApi {
  const char* name;
  void* (*create)(double sample_rate);
  void (*destroy)(void* d);
  void (*mute)(void* d);
  void (*run)(void* d, const float** in, float** out, uint32_t frames);
  int (*set)(void* d, const char* symbol, float value);   // 0 if the symbol isn't one of this reverb's
  void (*load_default)(void* d);                           // the upstream default preset's values
};

extern const ReverbApi kPlateReverb;
extern const ReverbApi kRoomReverb;
extern const ReverbApi kHallReverb;
