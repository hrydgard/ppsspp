#include "ppsspp_config.h"
#if PPSSPP_PLATFORM(MAC)
#include "SDL2/SDL.h"
#include "SDL2/SDL_syswm.h"
#else
#include "SDL.h"
#include "SDL_syswm.h"
#endif

#include <string>

#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GraphicsContext.h"

class SDLGLGraphicsContext : public GraphicsContext {
public:
	// Returns 0 on success.
	int Init(SDL_Window *&window, int x, int y, int w, int h, int mode, std::string *error_message);

	bool InitFromRenderThread(std::string *errorMessage) override;

	void Shutdown() override {}
	void ShutdownFromRenderThread() override;

	void Resize() override {}

	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}

	void ThreadStart() override {
		renderManager_->ThreadStart(draw_);
	}

	bool ThreadFrame() override {
		return renderManager_->ThreadFrame();
	}

	void ThreadEnd() override {
		renderManager_->ThreadEnd();
	}

	void StopThread() override {
		renderManager_->StopThread();
	}

private:
	Draw::DrawContext *draw_ = nullptr;
	SDL_Window *window_ = nullptr;
	SDL_GLContext glContext = nullptr;
	GLRenderManager *renderManager_ = nullptr;
};
