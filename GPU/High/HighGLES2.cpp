///


#include "Core/Reporting.h"
#include "GPU/GLES/TextureCache.h"
#include "GPU/High/HighGpu.h"
#include "GPU/High/HighGLES2.h"

namespace HighGpu {

HighGPU_GLES::HighGPU_GLES() {
}
HighGPU_GLES::~HighGPU_GLES() {
}

void HighGPU_GLES::Execute(CommandPacket *packet, int start, int end) {
	PrintCommandPacket(packet);
	// Pass 1: Decode all the textures. This is done first so that the GL driver can
	// upload them in the background (if it's that advanced)  while we are decoding vertex
	// data and preparing the draw calls.
	for (int i = start; i < end; i++) {
		
	}

	// Pass 2: Decode all the vertex data.
	while (i != end) {
		const Command *cmd = &packet->commands[i];
		switch (cmd->type) {
		case CMD_DRAWTRI:
			break;

		case CMD_DRAWLINE:
			break;

		case CMD_SYNC:
			break;
		}
	}

	// Pass 3: Fetch shaders, perform the draws.
	

}

// Let's avoid passing nulls into snprintf().
static const char *GetGLStringAlways(GLenum name) {
	const GLubyte *value = glGetString(name);
	if (!value)
		return "?";
	return (const char *)value;
}

// Needs to be called on GPU thread, not reporting thread.
void HighGPU_GLES::BuildReportingInfo() {
	const char *glVendor = GetGLStringAlways(GL_VENDOR);
	const char *glRenderer = GetGLStringAlways(GL_RENDERER);
	const char *glVersion = GetGLStringAlways(GL_VERSION);
	const char *glSlVersion = GetGLStringAlways(GL_SHADING_LANGUAGE_VERSION);
	const char *glExtensions = GetGLStringAlways(GL_EXTENSIONS);

	char temp[16384];
	snprintf(temp, sizeof(temp), "%s (%s %s), %s (extensions: %s)", glVersion, glVendor, glRenderer, glSlVersion, glExtensions);
	reportingPrimaryInfo_ = glVendor;
	reportingFullInfo_ = temp;

	Reporting::UpdateConfig();
}

void HighGPU_GLES::GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) override {
	backend_->GetReportingInfo(primaryInfo, fullInfo);
	primaryInfo = reportingPrimaryInfo_;
	fullInfo = reportingFullInfo_;
}

void HighGPU_GLES::DeviceLost() override {
}

void HighGPU_GLES::ProcessEvent(GPUEvent ev) override {
	switch (ev.type) {
	case GPU_EVENT_INIT_CLEAR:
		InitClearInternal();
		break;

	case GPU_EVENT_BEGIN_FRAME:
		BeginFrameInternal();
		break;

	case GPU_EVENT_COPY_DISPLAY_TO_OUTPUT:
		CopyDisplayToOutputInternal();
		break;

	case GPU_EVENT_INVALIDATE_CACHE:
		InvalidateCacheInternal(ev.invalidate_cache.addr, ev.invalidate_cache.size, ev.invalidate_cache.type);
		break;

	case GPU_EVENT_FB_MEMCPY:
		PerformMemoryCopyInternal(ev.fb_memcpy.dst, ev.fb_memcpy.src, ev.fb_memcpy.size);
		break;

	case GPU_EVENT_FB_MEMSET:
		PerformMemorySetInternal(ev.fb_memset.dst, ev.fb_memset.v, ev.fb_memset.size);
		break;

	case GPU_EVENT_FB_STENCIL_UPLOAD:
		PerformStencilUploadInternal(ev.fb_stencil_upload.dst, ev.fb_stencil_upload.size);
		break;

	case GPU_EVENT_REINITIALIZE:
		ReinitializeInternal();
		break;

	default:
		GPUCommon::ProcessEvent(ev);
	}
}

void HighGPU_GLES::ReinitializeInternal() {
	textureCache_.Clear(true);
	depalShaderCache_.Clear();
	framebufferManager_.DestroyAllFBOs();
	framebufferManager_.Resized();
}

void HighGPU_GLES::InitClearInternal() {
	bool useNonBufferedRendering = g_Config.iRenderingMode == FB_NON_BUFFERED_MODE;
	if (useNonBufferedRendering) {
		glstate.depthWrite.set(GL_TRUE);
		glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(0,0,0,1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}
	glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
}

void HighGPU_GLES::InvalidateCacheInternal(u32 addr, int size, GPUInvalidationType type) {
	if (size > 0)
		textureCache_.Invalidate(addr, size, type);
	else
		textureCache_.InvalidateAll(type);

	if (type != GPU_INVALIDATE_ALL && framebufferManager_.MayIntersectFramebuffer(addr)) {
		// If we're doing block transfers, we shouldn't need this, and it'll only confuse us.
		// Vempire invalidates (with writeback) after drawing, but before blitting.
		if (!g_Config.bBlockTransferGPU || type == GPU_INVALIDATE_SAFE) {
			framebufferManager_.UpdateFromMemory(addr, size, type == GPU_INVALIDATE_SAFE);
		}
	}
}

void HighGPU_GLES::PerformMemoryCopyInternal(u32 dest, u32 src, int size) {
	if (!framebufferManager_.NotifyFramebufferCopy(src, dest, size)) {
		// We use a little hack for Download/Upload using a VRAM mirror.
		// Since they're identical we don't need to copy.
		if (!Memory::IsVRAMAddress(dest) || (dest ^ 0x00400000) != src) {
			Memory::Memcpy(dest, src, size);
		}
	}
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
}

void HighGPU_GLES::PerformMemorySetInternal(u32 dest, u8 v, int size) {
	if (!framebufferManager_.NotifyFramebufferCopy(dest, dest, size, true)) {
		InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	}
}

void HighGPU_GLES::PerformStencilUploadInternal(u32 dest, int size) {
	framebufferManager_.NotifyStencilUpload(dest, size);
}

void HighGPU_GLES::CopyDisplayToOutputInternal() {
	// Flush anything left over.
	framebufferManager_.RebindFramebuffer();
	transformDraw_.Flush();

	shaderManager_->DirtyLastShader();

	glstate.depthWrite.set(GL_TRUE);
	glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	framebufferManager_.CopyDisplayToOutput();
	framebufferManager_.EndFrame();

	// If buffered, discard the depth buffer of the backbuffer. Don't even know if we need one.
#if 0
#ifdef USING_GLES2
	if (gl_extensions.EXT_discard_framebuffer && g_Config.iRenderingMode != 0) {
		GLenum attachments[] = {GL_DEPTH_EXT, GL_STENCIL_EXT};
		glDiscardFramebufferEXT(GL_FRAMEBUFFER, 2, attachments);
	}
#endif
#endif

	gstate_c.textureChanged = TEXCHANGE_UPDATED;
}


}  // namespace
