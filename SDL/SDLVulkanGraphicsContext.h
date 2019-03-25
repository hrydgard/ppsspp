#include "SDL.h"
#if !defined(__APPLE__)
#include "SDL_syswm.h"
#endif

#include "Common/GraphicsContext.h"
#include "Common/Vulkan/VulkanContext.h"
#include "Common/Vulkan/VulkanDebug.h"

#include "thin3d/thin3d.h"

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

	void Resize() override {
		draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
		vulkan_->DestroyObjects();
		vulkan_->ReinitSurface();
		vulkan_->InitObjects();
		draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
	}

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
	VulkanLogOptions g_LogOptions;
};
