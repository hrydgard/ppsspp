#pragma once

#include <cstdint>

#include "ppsspp_config.h"

#include "Core/HW/GranularMixer.h"
#include "Core/HW/StereoResampler.h"

extern StereoResampler g_resampler;
extern GranularMixer g_granular;

// The desktop frontends report whether their window has focus, for bMuteOnLostFocus.
#if (PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)) || (defined(SDL) && !defined(MOBILE_DEVICE))
#define PPSSPP_HAS_MUTE_ON_LOST_FOCUS 1
#endif
void Audio_SetWindowFocused(bool focused);
