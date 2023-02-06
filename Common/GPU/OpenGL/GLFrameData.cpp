#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLFrameData.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/GPU/DataFormat.h"
#include "Common/Log.h"

void GLCachedReadback::Destroy(bool skipGLCalls) {
	if (buffer && !skipGLCalls) {
		glDeleteBuffers(1, &buffer);
	}
	buffer = 0;
}

void GLFrameData::PerformReadbacks() {
	// TODO: Shorten the lock by doing some queueing tricks here.
	std::lock_guard<std::mutex> guard(readbackMutex);
	readbacks_.IterateMut([=](const GLReadbackKey &key, GLCachedReadback *cached) {
		if (!cached->pending) {
			return;
		}

		glBindBuffer(GL_PIXEL_PACK_BUFFER, cached->buffer);
		GLubyte *ptr = (GLubyte *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
		if (!ptr) {
			int error = glGetError();
			ERROR_LOG(G3D, "mapbuffer error: error %d buffer %d (%dx%d)", error, cached->buffer, key.width, key.height);
			cached->pending = false;
			return;
		}
		int bpp = (int)Draw::DataFormatSizeInBytes(key.dstFormat);
		int dataSize = key.width * key.height * bpp;
		_dbg_assert_(dataSize != 0);
		if (cached->dataSize < dataSize) {
			delete[] cached->data;
			cached->data = new uint8_t[dataSize];
			cached->dataSize = dataSize;
		}
		int pixelStride = key.width;
		if (cached->convert) {
			Draw::ConvertFromRGBA8888(cached->data, ptr, pixelStride, pixelStride, key.width, key.height, key.dstFormat);
		} else {
			for (int y = 0; y < key.height; y++) {
				memcpy(cached->data + y * pixelStride * bpp, ptr + y * key.width * bpp, key.width * bpp);
			}
		}
		glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
		cached->pending = false;
	});
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

void GLFrameData::EndFrame() {
	for (auto &rb : queuedReadbacks_) {
		// What should we do here? cleanup?
	}
	queuedReadbacks_.clear();
}

void GLFrameData::Destroy(bool skipGLCalls) {
	std::lock_guard<std::mutex> guard(readbackMutex);
	readbacks_.IterateMut([=](const GLReadbackKey &key, GLCachedReadback *cached) {
		cached->Destroy(skipGLCalls);
		delete cached;
	});
	readbacks_.Clear();
}

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
		if (skipGLCalls)
			shader->shader = 0;  // prevent the glDeleteShader
		delete shader;
	}
	shaders.clear();
	for (auto program : programs) {
		if (skipGLCalls)
			program->program = 0;  // prevent the glDeleteProgram
		delete program;
	}
	programs.clear();
	for (auto buffer : buffers) {
		if (skipGLCalls)
			buffer->buffer_ = 0;
		delete buffer;
	}
	buffers.clear();
	for (auto texture : textures) {
		if (skipGLCalls)
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
