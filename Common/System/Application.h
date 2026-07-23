#pragma once

class GraphicsContext;

class Application {
public:
	virtual ~Application() = default;
	virtual bool InitGraphics(GraphicsContext *graphicsContext) = 0;
	virtual void ShutdownGraphics(GraphicsContext *graphicsContext) = 0;
	virtual void Frame(GraphicsContext *graphicsContext) = 0;
};
