#include "SDL.h"
#include "SDL_syswm.h"

#include "Common/GraphicsContext.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanDebug.h"

#include "Common/GPU/thin3d.h"

class VulkanRenderManager;

class SDLVulkanGraphicsContext : public GraphicsContext {
public:
	SDLVulkanGraphicsContext() {}
	~SDLVulkanGraphicsContext() {
		delete draw_;
	}

	bool Init(SDL_Window *&window, int x, int y, int mode, std::string *error_message);

	void Shutdown() override;

	void SwapBuffers() override {
		// We don't do it this way.
	}

	void Resize() override;

	void Poll() override;

	void SwapInterval(int interval) override {
	}
	void *GetAPIContext() override {
		return vulkan_;
	}

	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}
private:
	Draw::DrawContext *draw_ = nullptr;
	VulkanContext *vulkan_ = nullptr;
	VulkanRenderManager *renderManager_ = nullptr;
};
