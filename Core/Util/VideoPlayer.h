#pragma once

#include <cstdint>
#include <vector>

struct PMFPlayer;

// A PMF's (PSMF's) muxed ATRAC3+ audio, all of it, decoded to interleaved 44.1 kHz stereo. Empty if
// it has none. Doesn't need FFmpeg: it's our demuxer and decoder, as for games' movies.
std::vector<int16_t> pmf_decode_audio(const uint8_t *data, size_t size);
// Whether a PMF has an audio stream muxed in, without decoding anything.
bool pmf_has_audio(const uint8_t *data, size_t size);

// Currently only supported with built-in FFMPEG.
bool pmf_player_available();

PMFPlayer *pmf_create();
void pmf_destroy(PMFPlayer* ps);

int pmf_init(PMFPlayer* ps, const uint8_t* data, size_t size, int* out_w, int* out_h);
bool pmf_update(PMFPlayer* ps, double current_time_seconds, uint8_t* user_rgb_buffer);
void pmf_deinit(PMFPlayer* ps);
// Whether pmf_update has played the video to its end at least once (it then starts over).
bool pmf_reached_end(PMFPlayer* ps);
