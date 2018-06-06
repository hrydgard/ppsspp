#if !defined(__APPLE__)
#include "SDL_syswm.h"
#endif
#include "SDL.h"

#include "thin3d/GLRenderManager.h"
#include "gfx/gl_common.h"
#include "Common/GraphicsContext.h"

// TODO: Move this to a better place.
#if defined(USING_EGL)
int8_t EGL_Open();
#endif

class SDLGLGraphicsContext : public DummyGraphicsContext {
public:
	SDLGLGraphicsContext() {
	}

	// Returns 0 on success.
	int Init(SDL_Window *&window, int x, int y, int mode, std::string *error_message);

	void Shutdown() override;
	void ShutdownFromRenderThread() override;

	void SwapBuffers() override {
		// Do nothing, the render thread takes care of this.
	}

	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}

	void ThreadStart() override {
		renderManager_->ThreadStart();
	}

	bool ThreadFrame() override {
		return renderManager_->ThreadFrame();
	}

	void ThreadEnd() override {
		renderManager_->ThreadEnd();
	}

	void StopThread() override {
		renderManager_->WaitUntilQueueIdle();
		renderManager_->StopThread();
	}

private:
	Draw::DrawContext *draw_ = nullptr;
	SDL_Window *window_;
	SDL_GLContext glContext = nullptr;
	GLRenderManager *renderManager_ = nullptr;
};


