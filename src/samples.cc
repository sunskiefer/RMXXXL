#include "samples.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

namespace rfx {

namespace {

uint32_t U32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint16_t U16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

int16_t ToS16(float x) {
  x *= 32767.0f;
  return static_cast<int16_t>(x > 32767.0f ? 32767.0f : (x < -32768.0f ? -32768.0f : x));
}

// One sample of one channel as float -1..1.
float ReadSample(const uint8_t* p, int bits, bool is_float) {
  if (is_float) { float f; memcpy(&f, p, 4); return f != f ? 0.0f : f; }
  switch (bits) {
    case 8: return (p[0] - 128) / 128.0f;
    case 16: return static_cast<int16_t>(U16(p)) / 32768.0f;
    case 24: { int32_t v = (p[0] << 8) | (p[1] << 16) | (static_cast<uint32_t>(p[2]) << 24); return (v >> 8) / 8388608.0f; }
    default: return static_cast<int32_t>(U32(p)) / 2147483648.0f;
  }
}

bool LoadWav(const char* path, Sample* out) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size < 44 || size > 64L * 1024 * 1024) { fclose(f); return false; }
  uint8_t* file = static_cast<uint8_t*>(malloc(size));
  bool ok = file && fread(file, 1, size, f) == static_cast<size_t>(size);
  fclose(f);
  if (!ok || memcmp(file, "RIFF", 4) || memcmp(file + 8, "WAVE", 4)) { free(file); return false; }

  int channels = 0, bits = 0, format = 0;
  uint32_t rate = 0;
  const uint8_t* pcm = NULL;
  uint32_t pcm_bytes = 0;
  long pos = 12;
  while (pos + 8 <= size) {
    const uint8_t* c = file + pos;
    uint32_t len = U32(c + 4);
    if (len > static_cast<uint32_t>(size - pos - 8)) len = static_cast<uint32_t>(size - pos - 8);
    if (!memcmp(c, "fmt ", 4) && len >= 16) {
      format = U16(c + 8);
      channels = U16(c + 10);
      rate = U32(c + 12);
      bits = U16(c + 22);
      if (format == 0xFFFE && len >= 40) format = U16(c + 8 + 24);   // WAVE_FORMAT_EXTENSIBLE: sub-format
    } else if (!memcmp(c, "data", 4)) {
      pcm = c + 8;
      pcm_bytes = len;
    }
    pos += 8 + len + (len & 1);
  }
  bool is_float = format == 3;
  bool supported = (format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (is_float && bits == 32);
  if (!pcm || !supported || channels < 1 || rate < 4000 || rate > 192000) { free(file); return false; }

  int bytes = bits / 8;
  int frames = static_cast<int>(pcm_bytes / (bytes * channels));
  if (frames > kMaxSampleFrames) frames = kMaxSampleFrames;
  if (frames < 1) { free(file); return false; }
  out->data = static_cast<int16_t*>(malloc(sizeof(int16_t) * 2 * frames));
  if (!out->data) { free(file); return false; }
  for (int i = 0; i < frames; ++i) {
    const uint8_t* fr = pcm + static_cast<size_t>(i) * bytes * channels;
    float l = ReadSample(fr, bits, is_float);
    float r = channels > 1 ? ReadSample(fr + bytes, bits, is_float) : l;
    out->data[2 * i] = ToS16(l);
    out->data[2 * i + 1] = ToS16(r);
  }
  out->frames = frames;
  out->rate = rate / 44100.0f;
  free(file);
  return true;
}

bool IsWav(const char* name) {
  size_t n = strlen(name);
  return n > 4 && name[0] != '.' && !strcasecmp(name + n - 4, ".wav");
}

int CompareNames(const void* a, const void* b) { return strcasecmp(*(const char* const*)a, *(const char* const*)b); }

}  // namespace

SampleBank* LoadSampleBank(const char* dir) {
  SampleBank* bank = static_cast<SampleBank*>(calloc(1, sizeof(SampleBank)));
  if (!bank || !dir) return bank;
  mkdir(dir, 0777);   // first run: make the folder so it's there to fill
  DIR* d = opendir(dir);
  if (!d) return bank;
  char* names[256];
  int n = 0;
  while (struct dirent* e = readdir(d)) {
    if (n < 256 && IsWav(e->d_name)) names[n++] = strdup(e->d_name);
  }
  closedir(d);
  qsort(names, n, sizeof names[0], CompareNames);
  for (int i = 0; i < n; ++i) {
    if (bank->count < kMaxSlots) {
      char path[1024];
      snprintf(path, sizeof path, "%s/%s", dir, names[i]);
      Sample* s = &bank->slot[bank->count];
      if (LoadWav(path, s)) {
        snprintf(s->name, sizeof s->name, "%s", names[i]);
        ++bank->count;
      }
    }
    free(names[i]);
  }
  return bank;
}

void FreeSampleBank(SampleBank* bank) {
  if (!bank) return;
  for (int i = 0; i < kMaxSlots; ++i) free(bank->slot[i].data);
  free(bank);
}

}  // namespace rfx
