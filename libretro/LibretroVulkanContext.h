#pragma once

#include "Common/Vulkan/VulkanLoader.h"
#include "libretro/LibretroGraphicsContext.h"

class LibretroVulkanContext : public LibretroHWRenderContext {
	public:
	LibretroVulkanContext() : LibretroHWRenderContext(RETRO_HW_CONTEXT_VULKAN, VK_MAKE_VERSION(1, 0, 18))
	{
#if 0
		hw_render_.cache_context = true;
#endif
	}
	~LibretroVulkanContext() override {}
	bool Init() override;
	void Shutdown() override;
	void SwapBuffers() override;
	void ContextDestroy() override;

	void *GetAPIContext() override;
	void CreateDrawContext() override;
	GPUCore GetGPUCore() override { return GPUCORE_VULKAN; }
	const char *Ident() override { return "Vulkan"; }
};
