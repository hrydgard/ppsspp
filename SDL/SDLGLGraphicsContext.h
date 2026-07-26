#include "ppsspp_config.h"
#include <SDL3/SDL.h>

#include <string>

#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/GraphicsContext.h"

class SDLGLGraphicsContext : public GraphicsContext {
public:
	SDLGLGraphicsContext(int force_gl_version) : force_gl_version_(force_gl_version) {}

	bool InitAPI(void *wnd, std::string *deviceName, std::string *errorMessage) override;
	void ShutdownAPI() override;

	// Returns 0 on success.
	// data1 should be a *pointer* to the SDL_Window pointer, data2 is the SDL_WindowFlags.
	bool InitSurface(WindowSystem winsys, void *data1, void *data2, std::string *error_message) override;
	void ShutdownSurface() override;

	bool NeedsSeparateEmuThread() const override { return true; }

	void Resize() override {}

	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}

	void ThreadStart() override {
		renderManager_->ThreadStart(draw_);
	}

	bool ThreadFrame(bool waitIfEmpty) override {
		return renderManager_->ThreadFrame(waitIfEmpty);
	}

	void ThreadEnd() override {
		renderManager_->ThreadEnd();
	}

private:
	Draw::DrawContext *draw_ = nullptr;
	SDL_Window *window_ = nullptr;
	SDL_GLContext glContext_ = nullptr;
	int force_gl_version_ = 0;
	GLRenderManager *renderManager_ = nullptr;
};
