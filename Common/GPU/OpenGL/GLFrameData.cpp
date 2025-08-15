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
void GLDeleter::Perform(GLRenderManager *renderManager, bool skipGLCalls) {
	for (auto pushBuffer : pushBuffers) {
		renderManager->UnregisterPushBuffer(pushBuffer);
		if (skipGLCalls) {
			pushBuffer->Destroy(false);
		}
		delete pushBuffer;
	}
	pushBuffers.clear();
	for (auto shader : shaders) {
		if (skipGLCalls && shader)
			shader->shader = 0;  // prevent the glDeleteShader
		delete shader;
	}
	shaders.clear();
	for (auto program : programs) {
		if (skipGLCalls && program)
			program->program = 0;  // prevent the glDeleteProgram
		delete program;
	}
	programs.clear();
	for (auto buffer : buffers) {
		if (skipGLCalls && buffer)
			buffer->buffer_ = 0;
		delete buffer;
	}
	buffers.clear();
	for (auto texture : textures) {
		if (skipGLCalls && texture)
			texture->texture = 0;
		delete texture;
	}
	textures.clear();
	for (auto inputLayout : inputLayouts) {
		// No GL objects in an inputLayout yet
		delete inputLayout;
	}
	inputLayouts.clear();
	for (auto framebuffer : framebuffers) {
		if (skipGLCalls) {
			framebuffer->handle = 0;
			framebuffer->color_texture.texture = 0;
			framebuffer->z_stencil_buffer = 0;
			framebuffer->z_stencil_texture.texture = 0;
			framebuffer->z_buffer = 0;
			framebuffer->stencil_buffer = 0;
		}
		delete framebuffer;
	}
	framebuffers.clear();
}
