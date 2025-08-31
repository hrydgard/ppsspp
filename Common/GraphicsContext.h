#pragma once

#include <string>

#include "Common/Log.h"
#include "Common/GPU/thin3d.h"
#include "Common/TimeUtil.h"

// Init is done differently on each platform, and done close to the creation, so it's
// expected to be implemented by subclasses.
class GraphicsContext {
public:
	virtual ~GraphicsContext() = default;

	virtual bool InitFromRenderThread(std::string *errorMessage) { return true; }
	virtual void ShutdownFromRenderThread() {}

	virtual void Shutdown() = 0;

	// Used during window resize. Must be called from the window thread,
	// not the rendering thread or CPU thread.
	virtual void Pause() {}
	virtual void Resume() {}

	virtual void Resize() = 0;
	virtual void NotifyWindowRestored() {}

	// Needs casting to the appropriate type, unfortunately. Should find a better solution..
	virtual void *GetAPIContext() { return nullptr; }

	// Called from the render thread from threaded backends.
	virtual void ThreadStart() {}
	virtual bool ThreadFrame(bool waitIfEmpty) { return true; }   // waitIfEmpty should normally be true, except in exit scenarios.
	virtual void ThreadEnd() {}

	// Useful for checks that need to be performed every frame.
	// Should strive to get rid of these.
	virtual void Poll() {}

	virtual Draw::DrawContext *GetDrawContext() = 0;

	void ThreadFrameUntilCondition(std::function<bool()> conditionStopped) {
		bool exitOnEmpty = false;

		INFO_LOG(Log::System, "Executing graphicsContext->ThreadFrame to clear buffers");
		while (true) {
			// When EmuThread is done, we know there are no more frames coming. When that happens,
			// we'll bail.
			if (!exitOnEmpty && conditionStopped()) {
				INFO_LOG(Log::System, "Found out that the thread is done.");
				exitOnEmpty = true;
			}

			bool retval = ThreadFrame(false);
			if (!retval && exitOnEmpty) {
				INFO_LOG(Log::System, "ThreadFrame returned false and emu thread is done, breaking.");
				break;
			} else {
				sleep_ms(5, "exit poll");
			}
		}

	}
};
