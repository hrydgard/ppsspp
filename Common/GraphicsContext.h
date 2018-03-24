#pragma once

#include <string>

#include "thin3d/thin3d.h"

// Init is done differently on each platform, and done close to the creation, so it's
// expected to be implemented by subclasses.
class GraphicsContext {
public:
	virtual ~GraphicsContext() {}

	virtual bool InitFromRenderThread(std::string *errorMessage) { return true; }
	virtual void ShutdownFromRenderThread() {}

	virtual void Shutdown() = 0;
	virtual void SwapInterval(int interval) = 0;

	virtual void SwapBuffers() = 0;

	// Used during window resize. Must be called from the window thread,
	// not the rendering thread or CPU thread.
	virtual void Pause() {}
	virtual void Resume() {}

	virtual void Resize() = 0;

	// Needs casting to the appropriate type, unfortunately. Should find a better solution..
	virtual void *GetAPIContext() { return nullptr; }

	// Called from the render thread from threaded backends.
	virtual void ThreadStart() {}
	virtual bool ThreadFrame() { return true; }
	virtual void ThreadEnd() {}
	virtual void StopThread() {}

	virtual Draw::DrawContext *GetDrawContext() = 0;
};

class DummyGraphicsContext : public GraphicsContext {
public:
	void Shutdown() override {}
	void SwapInterval(int interval) override {}
	void SwapBuffers() override {}
	void Resize() override {}

	Draw::DrawContext *GetDrawContext() override { return nullptr; }
};
