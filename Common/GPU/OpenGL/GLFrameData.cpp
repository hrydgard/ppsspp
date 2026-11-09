#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLFrameData.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/Log.h"

void GLDeleter::Take(GLDeleter &other) {
	_assert_msg_(IsEmpty(), "Deleter already has stuff");
	shaders = std::move(other.shaders);
	programs = std::move(other.programs);
	buffers = std::move(other.buffers);
	textures = std::move(other.textures);
	inputLayouts = std::move(other.inputLayouts);
	framebuffers = std::move(other.framebuffers);
	pushBuffers = std::move(other.pushBuffers);
	other.shaders.clear();
	other.programs.clear();
	other.buffers.clear();
	other.textures.clear();
	other.inputLayouts.clear();
	other.framebuffers.clear();
	other.pushBuffers.clear();
}

// Runs on the GPU thread.
void GLDeleter::Perform(GLRenderManager *renderManager) {
	for (auto pushBuffer : pushBuffers) {
		renderManager->UnregisterPushBuffer(pushBuffer);
		delete pushBuffer;
	}
	pushBuffers.clear();
	for (auto shader : shaders) {
		delete shader;
	}
	shaders.clear();
	for (auto program : programs) {
		delete program;
	}
	programs.clear();
	for (auto buffer : buffers) {
		delete buffer;
	}
	buffers.clear();
	for (auto texture : textures) {
		delete texture;
	}
	textures.clear();
	for (auto inputLayout : inputLayouts) {
		// No GL objects in an inputLayout yet
		delete inputLayout;
	}
	inputLayouts.clear();
	for (auto framebuffer : framebuffers) {
		delete framebuffer;
	}
	if (!framebuffers.empty()) {
		// The destructor bound the default framebuffer. Without this, the next readback from the framebuffer
		// that was bound before could read the default one (zeros for Cars Race-O-Rama's luminance, #15914).
		renderManager->ForgetBoundFramebuffers();
	}
	framebuffers.clear();
}
