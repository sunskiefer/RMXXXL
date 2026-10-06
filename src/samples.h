// RMXXXL X-Pad sample slots: WAV files from one folder, loaded off the audio thread.
#pragma once
#include <stdint.h>

namespace rfx {

const int kMaxSlots = 16;
const int kMaxSampleFrames = 441000;   // 10 s at 44.1 kHz per file (longer files are cut)

struct Sample {
  int16_t* data;     // interleaved stereo (a mono file is duplicated)
  int frames;
  float rate;        // file sample rate / 44100: playback step at original pitch
  char name[64];     // file name, for the screen
};

struct SampleBank {
  Sample slot[kMaxSlots];
  int count;
};

// Loads up to kMaxSlots .wav files from dir, sorted by name (slot 1 = first). Creates dir if missing.
// PCM 8/16/24/32-bit and 32-bit float, mono or stereo (extra channels ignored), any sample rate.
SampleBank* LoadSampleBank(const char* dir);
void FreeSampleBank(SampleBank* bank);

}  // namespace rfx
