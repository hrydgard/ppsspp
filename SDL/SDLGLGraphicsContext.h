#include "ppsspp_config.h"
#include <SDL3/SDL.h>

#include <string>

#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GraphicsContext.h"

class SDLGLGraphicsContext : public GraphicsContext {
public:
	// Returns 0 on success.
	int Init(SDL_Window *&window, int x, int y, int w, int h, int mode, std::string *error_message, int force_gl_version);

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

	bool ThreadFrame(bool waitIfEmpty) override {
		return renderManager_->ThreadFrame(waitIfEmpty);
	}

	void ThreadEnd() override {
		renderManager_->ThreadEnd();
	}

private:
	Draw::DrawContext *draw_ = nullptr;
	SDL_Window *window_ = nullptr;
	SDL_GLContext glContext = nullptr;
	GLRenderManager *renderManager_ = nullptr;
};
