#pragma once

#include "GPU/Common/FramebufferCommon.h"

namespace HighGpu {

class FramebufferManagerGLES : public FramebufferManagerCommon {
public:
	void Init();
	void BeginFrame();
	void EndFrame();
	void DestroyAllFBOs();
	void DeviceLost();
	void Resized();
	void CopyDisplayToOutput();
};

}  // namespace
