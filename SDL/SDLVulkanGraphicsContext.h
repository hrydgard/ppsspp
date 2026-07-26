#include "ppsspp_config.h"
#include <SDL3/SDL.h>

#include "Common/GPU/GraphicsContext.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanDebug.h"

#include "Common/GPU/thin3d.h"

class VulkanRenderManager;

class SDLVulkanGraphicsContext : public GraphicsContext {
public:
	SDLVulkanGraphicsContext() {}

	bool InitAPI(void *wnd, std::string *deviceName, std::string *errorMessage) override;
	bool InitSurface(WindowSystem winsys, void *data1, void *data2, std::string *error_message) override;

	void ShutdownSurface() override;
	void ShutdownAPI() override;

	void Resize() override;

	void Poll() override;

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
