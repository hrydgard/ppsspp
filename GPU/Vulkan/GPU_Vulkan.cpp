// Copyright (c) 2015- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "GPU/Vulkan/GPU_Vulkan.h"

GPU_Vulkan::GPU_Vulkan(VulkanContext *vulkan) : transformDraw_(vulkan) {

}

GPU_Vulkan::~GPU_Vulkan() {

}

void GPU_Vulkan::CheckGPUFeatures() {

}

void GPU_Vulkan::ReapplyGfxStateInternal() {}
void GPU_Vulkan::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {}
void GPU_Vulkan::CopyDisplayToOutput() {}
void GPU_Vulkan::BeginFrame() {}
void GPU_Vulkan::UpdateStats() {}
void GPU_Vulkan::InvalidateCache(u32 addr, int size, GPUInvalidationType type) {}
bool GPU_Vulkan::PerformMemoryCopy(u32 dest, u32 src, int size) { return false; }
bool GPU_Vulkan::PerformMemorySet(u32 dest, u8 v, int size) { return false; }
bool GPU_Vulkan::PerformMemoryDownload(u32 dest, int size) { return false; }
bool GPU_Vulkan::PerformMemoryUpload(u32 dest, int size) { return false; }
bool GPU_Vulkan::PerformStencilUpload(u32 dest, int size) { return false; }
void GPU_Vulkan::ClearCacheNextFrame() {}
void GPU_Vulkan::DeviceLost() {}  // Only happens on Android. Drop all textures and shaders.

void GPU_Vulkan::DumpNextFrame() {}

void GPU_Vulkan::DoState(PointerWrap &p) {
	GPUCommon::DoState(p);
}


void GPU_Vulkan::DoBlockTransfer(u32 skipDrawReason) {

}

void GPU_Vulkan::Resized() {}

void GPU_Vulkan::ClearShaderCache() {}
bool GPU_Vulkan::FramebufferDirty() { return false; }
bool GPU_Vulkan::FramebufferReallyDirty() { return false; }

void GPU_Vulkan::FastRunLoop(DisplayList &list) {

}

void GPU_Vulkan::ProcessEvent(GPUEvent ev) {

}

void GPU_Vulkan::FastLoadBoneMatrix(u32 target) {

}

void GPU_Vulkan::FinishDeferred() {

}

void GPU_Vulkan::UpdateCmdInfo() {

}

void GPU_Vulkan::InitClear() {

}

void GPU_Vulkan::Reinitialize() {

}

void GPU_Vulkan::PreExecuteOp(u32 op, u32 diff) {

}

void GPU_Vulkan::Execute_Generic(u32 op, u32 diff) {

}

void GPU_Vulkan::ExecuteOp(u32 op, u32 diff) {

}



std::vector<std::string> GPU_Vulkan::DebugGetShaderIDs(DebugShaderType shader) {
	std::vector<std::string> ids;
	return ids;
}

std::string GPU_Vulkan::DebugGetShaderString(std::string id, DebugShaderType shader, DebugShaderStringType stringType) {
	return "N/A";
}

void GPU_Vulkan::NotifyVideoUpload(u32 addr, int size, int width, int format) {

}
