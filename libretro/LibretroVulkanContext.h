#pragma once

#include "libretro/LibretroGraphicsContext.h"

class LibretroVulkanContext : public LibretroHWRenderContext {
public:
	LibretroVulkanContext();
   ~LibretroVulkanContext() override { Shutdown(); }
	bool Init() override;
	void Shutdown() override;
	void SwapBuffers() override;

	void *GetAPIContext() override;
	void CreateDrawContext() override;

	void ContextReset() override;
	void ContextDestroy() override;

	GPUCore GetGPUCore() override { return GPUCORE_VULKAN; }
	const char *Ident() override { return "Vulkan"; }
};
