#pragma once

#include <cstdint>

struct PMFPlayer;

// Currently only supported with built-in FFMPEG.
bool pmf_player_available();

PMFPlayer *pmf_create();
void pmf_destroy(PMFPlayer* ps);

int pmf_init(PMFPlayer* ps, const uint8_t* data, size_t size, int* out_w, int* out_h);
bool pmf_update(PMFPlayer* ps, double current_time_seconds, uint8_t* user_rgb_buffer);
void pmf_deinit(PMFPlayer* ps);
// Whether pmf_update has played the video to its end at least once (it then starts over).
bool pmf_reached_end(PMFPlayer* ps);
