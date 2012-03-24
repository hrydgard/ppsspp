#include "base/basictypes.h"

// Simple mixer intended for sound effects for games.
// Intended both for fire and forget sfx (auto channels) and for
// realtime-modifiable sounds like pitched engine noises (fixed channels).

struct Mixer;
struct Clip;
struct Channel;

// This struct is public for easy manipulation of running channels.
struct PlayParams {
  uint8_t volume;  // 0-255
  uint8_t pan;  // 0-255, 127 is dead center.
  int32_t delta;
};

// Mixer
// ==========================
// For now, channels is required to be 2.
Mixer *mixer_create(int sample_rate, int channels, int fixed_channels);
void mixer_destroy(Mixer *mixer);

// Buffer must be r/w, for efficient mixing
// TODO: Use local buffer instead.
void mixer_mix(Mixer *mixer, short *buffer, int num_samples);

// Clip
// ==========================
Clip *clip_load(const char *filename);
void clip_destroy(Clip *clip);

int clip_length();
void clip_set_loop(int start, int end);


// The returned PlayState pointer can be used to set the playback parameters,
// but must not be kept around unless you are using a fixed channel.
// Channel must be either -1 for auto assignment to a free channel, or less
// than the number of fixed channels that the mixer was created with.
PlayParams *mixer_play_clip(Mixer *mixer, const Clip *clip, int channel);
