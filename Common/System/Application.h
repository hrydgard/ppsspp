#pragma once

#include "Common/GPU/MiscTypes.h"

class GraphicsContext;

class Application {
public:
	WindowSystem windowSystem;
	void *windowData1 = nullptr;
	void *windowData2 = nullptr;  // This should be the window, if there is one.

	void *Window() const { return windowData2; }
	virtual ~Application() = default;
	virtual bool InitGraphics(GraphicsContext *graphicsContext) = 0;
	virtual void ShutdownGraphics(GraphicsContext *graphicsContext) = 0;
	virtual void Frame(GraphicsContext *graphicsContext) = 0;
};
