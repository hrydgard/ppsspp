// Frame timing
//
// A frame on the main thread should look a bit like this:
//
// 1. -- Wait for the right time to start the frame  (alternatively, see this is step 8).
// 2. Sample inputs (on some platforms, this is done continouously during step 3)
// 3. Run CPU
// 4. Submit GPU commands (there's no reason to ever wait before this).
// 5. -- Wait for the right time to present
// 6. Send Present command
// 7. Do other end-of-frame stuff
//
// To minimize latency, we should *maximize* 1 and *minimize* 5 (while still keeping some margin to soak up hitches).
// Additionally, if too many completed frames have been buffered up, we need a feedback mechanism, so we can temporarily
// artificially increase 1 in order to "catch the CPU up".
//
// There are some other things that can influence the frame timing:
// * Unthrottling. If vsync is off or the backend can change present mode dynamically, we can simply disable all waits during unthrottle.
// * Frame skipping. This gets complicated.
// * The game not actually asking for flips, like in static loading screens

#include "ppsspp_config.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Log.h"
#include "Common/System/Display.h"
#include "Common/TimeUtil.h"

#include "Core/RetroAchievements.h"
#include "Core/CoreParameter.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/HW/Display.h"
#include "Core/HLE/sceNet.h"
#include "Core/FrameTiming.h"

FrameTiming g_frameTiming;

void WaitUntil(double now, double timestamp, const char *reason) {
#if 1
	// Use precise timing.
	sleep_precise(timestamp - now, reason);
#else

#if PPSSPP_PLATFORM(WINDOWS)
	// Old method. TODO: Should we make an option?
	while (time_now_d() < timestamp) {
		sleep_ms(1, reason); // Sleep for 1ms on this thread
	}
#else
	const double left = timestamp - now;
	if (left > 0.0 && left < 3.0) {
		usleep((long)(left * 1000000));
	}
#endif

#endif
}

void FrameTiming::DeferWaitUntil(double until, double *curTimePtr) {
	_dbg_assert_(until > 0.0);
	waitUntil_ = until;
	curTimePtr_ = curTimePtr;
}

void FrameTiming::PostSubmit() {
	if (waitUntil_ != 0.0) {
		WaitUntil(time_now_d(), waitUntil_, "post-submit");
		if (curTimePtr_) {
			*curTimePtr_ = waitUntil_;
			curTimePtr_ = nullptr;
		}
		waitUntil_ = 0.0;
	}
}

void FrameTiming::ComputePresentMode(Draw::DrawContext *draw, bool fastForward) {
	if (!draw) {
		// This happens in headless mode.
		fastForwardSkipFlip_ = true;
		presentMode_ = Draw::PresentMode::FIFO;
		return;
	}

	_dbg_assert_(draw->GetDeviceCaps().presentModesSupported != (Draw::PresentMode)0);

	if (draw->GetDeviceCaps().presentModesSupported == Draw::PresentMode::FIFO) {
		// Only FIFO mode is supported (like on iOS and some GLES backends).
		fastForwardSkipFlip_ = true;
		presentMode_ = Draw::PresentMode::FIFO;
		return;
	}

	// More than one present mode is supported. Use careful logic.

	// The user has requested vsync off.
	if (!g_Config.bVSync) {
		if (draw->GetDeviceCaps().presentModesSupported & Draw::PresentMode::IMMEDIATE) {
			// Use immediate mode, whether fast-forwarding or not.
			presentMode_ = Draw::PresentMode::IMMEDIATE;
			fastForwardSkipFlip_ = false;
			return;
		}
		// Inconsistent state - vsync is off but immediate mode is not supported.
		// We will simply force on VSync.
		g_Config.bVSync = true;
	}

	// At this point, vsync is always on. What decides now is whether MAILBOX or IMMEDIATE is available,
	// and also if we need an unsynced mode.

	// OK, vsync is requested (or immediate is not available). If mailbox is supported, it works the same as IMMEDIATE above.

	if (g_Config.bLowLatencyPresent) {
		// Use mailbox if available. It works fine for both fast-forward and normal.
		if (draw->GetDeviceCaps().presentModesSupported & Draw::PresentMode::MAILBOX) {
			presentMode_ = Draw::PresentMode::MAILBOX;
			fastForwardSkipFlip_ = false;
			return;
		}
		// We could force off lowLatencyPresent here, but it's no good when changing between backends.
		// So let's leave it on in the background, maybe the user just went from Vulkan to OpenGL.
	}

	// At this point, low-latency mode is not available, and vsync is on. We see if we can use INSTANT
	// mode for fast-forwarding, or if we need to resort to frameskipping.
	if (draw->GetDeviceCaps().presentInstantModeChange) {
		if (fastForward) {
			presentMode_ = Draw::PresentMode::IMMEDIATE;
			fastForwardSkipFlip_ = false;
			return;
		}
	}

	// Finally, fallback to FIFO mode, with skip-flip in fast-forward.
	presentMode_ = Draw::PresentMode::FIFO;
	fastForwardSkipFlip_ = true;
}
