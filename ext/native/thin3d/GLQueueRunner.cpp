#include "GLQueueRunner.h"
#include "GLRenderManager.h"
#include "base/logging.h"
#include "gfx/gl_common.h"
#include "gfx/gl_debug_log.h"
#include "gfx_es2/gpu_features.h"
#include "math/dataconv.h"

void GLQueueRunner::CreateDeviceObjects() {

}

void GLQueueRunner::DestroyDeviceObjects() {

}

void GLQueueRunner::RunInitSteps(const std::vector<GLRInitStep> &steps) {
	for (int i = 0; i < steps.size(); i++) {
		const GLRInitStep &step = steps[i];
		switch (step.stepType) {
		case GLRInitStepType::CREATE_TEXTURE:
		{
			GLRTexture *tex = step.create_texture.texture;
			glGenTextures(1, &tex->texture);
			glBindTexture(tex->target, tex->texture);
			break;
		}
		case GLRInitStepType::CREATE_BUFFER:
		{
			GLRBuffer *buffer = step.create_buffer.buffer;
			glGenBuffers(1, &buffer->buffer);
			glBindBuffer(buffer->target_, buffer->buffer);
			glBufferData(buffer->target_, step.create_buffer.size, nullptr, step.create_buffer.usage);
			break;
		}
		case GLRInitStepType::BUFFER_SUBDATA:
		{
			GLRBuffer *buffer = step.buffer_subdata.buffer;
			glBufferSubData(buffer->buffer, step.buffer_subdata.offset, step.buffer_subdata.size, step.buffer_subdata.data);
			delete[] step.buffer_subdata.data;
			break;
		}
		case GLRInitStepType::CREATE_PROGRAM:
		{
			GLRProgram *program = step.create_program.program;
			program->program = glCreateProgram();
			for (int i = 0; i < step.create_program.num_shaders; i++) {
				glAttachShader(program->program, step.create_program.shaders[i]->shader);
			}

			for (auto iter : program->semantics_) {
				glBindAttribLocation(program->program, iter.location, iter.attrib);
			}
			glLinkProgram(program->program);

			GLint linkStatus = GL_FALSE;
			glGetProgramiv(program->program, GL_LINK_STATUS, &linkStatus);
			if (linkStatus != GL_TRUE) {
				GLint bufLength = 0;
				glGetProgramiv(program->program, GL_INFO_LOG_LENGTH, &bufLength);
				if (bufLength) {
					char* buf = new char[bufLength];
					glGetProgramInfoLog(program->program, bufLength, NULL, buf);
					ELOG("Could not link program:\n %s", buf);
					// We've thrown out the source at this point. Might want to do something about that.
#ifdef _WIN32
					OutputDebugStringUTF8(buf);
#endif
					delete[] buf;
				} else {
					ELOG("Could not link program with %d shaders for unknown reason:", step.create_program.num_shaders);
				}
				break;
			}

			// Auto-initialize samplers.
			glUseProgram(program->program);
			for (int i = 0; i < 4; i++) {
				char temp[256];
				sprintf(temp, "Sampler%i", i);
				int samplerLoc = glGetUniformLocation(program->program, temp);
				if (samplerLoc != -1) {
					glUniform1i(samplerLoc, i);
				}
			}

			// Here we could (using glGetAttribLocation) save a bitmask about which pieces of vertex data are used in the shader
			// and then AND it with the vertex format bitmask later...
		}
			break;
		case GLRInitStepType::CREATE_SHADER:
		{
			GLuint shader = glCreateShader(step.create_shader.stage);
			step.create_shader.shader->shader = shader;
			// language_ = language;
			const char *code = step.create_shader.code;
			glShaderSource(shader, 1, &code, nullptr);
			delete[] code;
			glCompileShader(shader);
			GLint success = 0;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
			if (!success) {
#define MAX_INFO_LOG_SIZE 2048
				GLchar infoLog[MAX_INFO_LOG_SIZE];
				GLsizei len = 0;
				glGetShaderInfoLog(shader, MAX_INFO_LOG_SIZE, &len, infoLog);
				infoLog[len] = '\0';
				glDeleteShader(shader);
				shader = 0;
				ILOG("%s Shader compile error:\n%s", step.create_shader.stage == GL_FRAGMENT_SHADER ? "Fragment" : "Vertex", infoLog);
			}
			break;
		}

		case GLRInitStepType::CREATE_INPUT_LAYOUT:
		{
			GLRInputLayout *layout = step.create_input_layout.inputLayout;
			// TODO
			break;
		}
		case GLRInitStepType::TEXTURE_SUBDATA:
			break;
		case GLRInitStepType::TEXTURE_IMAGE:
		{
			GLRTexture *tex = step.texture_image.texture;
			CHECK_GL_ERROR_IF_DEBUG();
			glTexImage2D(tex->target, step.texture_image.level, step.texture_image.internalFormat, step.texture_image.width, step.texture_image.height, 0, step.texture_image.format, step.texture_image.type, step.texture_image.data);
			delete[] step.texture_image.data;
			CHECK_GL_ERROR_IF_DEBUG();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			break;
		}
		default:
			Crash();
		}
	}
}

void GLQueueRunner::RunSteps(const std::vector<GLRStep *> &steps) {
	for (int i = 0; i < steps.size(); i++) {
		const GLRStep &step = *steps[i];
		switch (step.stepType) {
		case GLRStepType::RENDER:
			PerformRenderPass(step);
			break;
		case GLRStepType::COPY:
			PerformCopy(step);
			break;
		case GLRStepType::BLIT:
			PerformBlit(step);
			break;
		case GLRStepType::READBACK:
			PerformReadback(step);
			break;
		case GLRStepType::READBACK_IMAGE:
			PerformReadbackImage(step);
			break;
		default:
			Crash();
			break;
		}
		delete steps[i];
	}
}

void GLQueueRunner::LogSteps(const std::vector<GLRStep *> &steps) {

}


void GLQueueRunner::PerformBlit(const GLRStep &step) {
}

void GLQueueRunner::PerformRenderPass(const GLRStep &step) {
	// Don't execute empty renderpasses.
	if (step.commands.empty()) {
		// Nothing to do.
		return;
	}

	PerformBindFramebufferAsRenderTarget(step);

	glEnable(GL_SCISSOR_TEST);

	GLRFramebuffer *fb = step.render.framebuffer;
	GLRProgram *curProgram = nullptr;
	GLint activeTexture = GL_TEXTURE0;

	auto &commands = step.commands;
	for (const auto &c : commands) {
		switch (c.cmd) {
		case GLRRenderCommand::DEPTH:
			if (c.depth.enabled) {
				glEnable(GL_DEPTH_TEST);
				glDepthMask(c.depth.write);
				glDepthFunc(c.depth.func);
			} else {
				glDisable(GL_DEPTH_TEST);
			}
			break;
		case GLRRenderCommand::BLEND:
			if (c.blend.enabled) {
				glEnable(GL_BLEND);
				glBlendEquationSeparate(c.blend.funcColor, c.blend.funcAlpha);
				glBlendFuncSeparate(c.blend.srcColor, c.blend.dstColor, c.blend.srcAlpha, c.blend.dstAlpha);
			} else {
				glDisable(GL_BLEND);
			}
			break;
		case GLRRenderCommand::CLEAR:
			if (c.clear.clearMask & GL_COLOR_BUFFER_BIT) {
				float color[4];
				Uint8x4ToFloat4(color, c.clear.clearColor);
				glClearColor(color[0], color[1], color[2], color[3]);
			}
			if (c.clear.clearMask & GL_DEPTH_BUFFER_BIT) {
#if defined(USING_GLES2)
				glClearDepthf(c.clear.clearZ);
#else
				glClearDepth(c.clear.clearZ);
#endif
			}
			if (c.clear.clearMask & GL_STENCIL_BUFFER_BIT) {
				glClearStencil(c.clear.clearStencil);
			}
			glClear(c.clear.clearMask);
			break;
		case GLRRenderCommand::BLENDCOLOR:
			glBlendColor(c.blendColor.color[0], c.blendColor.color[1], c.blendColor.color[2], c.blendColor.color[3]);
			break;
		case GLRRenderCommand::VIEWPORT:
			// TODO: Support FP viewports through glViewportArrays
			glViewport((GLint)c.viewport.vp.x, (GLint)c.viewport.vp.y, (GLsizei)c.viewport.vp.w, (GLsizei)c.viewport.vp.h);
			glDepthRange(c.viewport.vp.minZ, c.viewport.vp.maxZ);
			break;
		case GLRRenderCommand::SCISSOR:
			glScissor(c.scissor.rc.x, c.scissor.rc.y, c.scissor.rc.w, c.scissor.rc.h);
			break;
		case GLRRenderCommand::UNIFORM4F:
		{
			int loc = c.uniform4.loc;
			if (c.uniform4.name) {
				loc = curProgram->GetUniformLoc(c.uniform4.name);
			}
			if (loc >= 0) {
				switch (c.uniform4.count) {
				case 1:
					glUniform1f(loc, c.uniform4.v[0]);
					break;
				case 2:
					glUniform2fv(loc, 1, c.uniform4.v);
					break;
				case 3:
					glUniform3fv(loc, 1, c.uniform4.v);
					break;
				case 4:
					glUniform4fv(loc, 1, c.uniform4.v);
					break;
				}
			}
			break;
		}
		case GLRRenderCommand::UNIFORMMATRIX:
		{
			int loc = c.uniform4.loc;
			if (c.uniform4.name) {
				loc = curProgram->GetUniformLoc(c.uniform4.name);
			}
			if (loc >= 0) {
				glUniformMatrix4fv(loc, 1, false, c.uniformMatrix4.m);
			}
			break;
		}
		case GLRRenderCommand::STENCIL:
			glStencilFunc(c.stencil.func, c.stencil.ref, c.stencil.compareMask);
			glStencilOp(c.stencil.sFail, c.stencil.zFail, c.stencil.pass);
			glStencilMask(c.stencil.writeMask);
			break;
		case GLRRenderCommand::BINDTEXTURE:
		{
			GLint target = c.texture.slot;
			if (target != activeTexture) {
				glActiveTexture(target);
				activeTexture = target;
			}
			glBindTexture(c.texture.texture->target, c.texture.texture->texture);
			break;
		}
		case GLRRenderCommand::BINDPROGRAM:
		{
			glUseProgram(c.program.program->program);
			curProgram = c.program.program;
			break;
		}
		case GLRRenderCommand::BIND_INPUT_LAYOUT:
		{
			GLRInputLayout *layout = c.inputLayout.inputLayout;
			for (int i = 0; i < 7; i++) {  // SEM_MAX
				if (layout->semanticsMask_ & (1 << i)) {
					glEnableVertexAttribArray(i);
				}
			}
			for (int i = 0; i < layout->entries.size(); i++) {
				auto &entry = layout->entries[i];
				glVertexAttribPointer(entry.location, entry.count, entry.type, entry.normalized, entry.stride, (const void *)(c.inputLayout.offset + entry.offset));
			}
			break;
		}
		case GLRRenderCommand::UNBIND_INPUT_LAYOUT:
		{
			GLRInputLayout *layout = c.inputLayout.inputLayout;
			for (int i = 0; i < 7; i++) {  // SEM_MAX
				if (layout->semanticsMask_ & (1 << i)) {
					glDisableVertexAttribArray(i);
				}
			}
			break;
		}
		case GLRRenderCommand::GENMIPS:
			glGenerateMipmap(GL_TEXTURE_2D);
			break;
		case GLRRenderCommand::DRAW:
			glDrawArrays(c.draw.mode, c.draw.first, c.draw.count);
			break;
		case GLRRenderCommand::DRAW_INDEXED:
			if (c.drawIndexed.instances == 1) {
				glDrawElements(c.drawIndexed.mode, c.drawIndexed.count, c.drawIndexed.indexType, c.drawIndexed.indices);
			}
			break;
		case GLRRenderCommand::TEXTURESAMPLER:
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, c.textureSampler.wrapS);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, c.textureSampler.wrapT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, c.textureSampler.magFilter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, c.textureSampler.minFilter);
			break;
		case GLRRenderCommand::RASTER:
			if (c.raster.cullEnable) {
				glEnable(GL_CULL_FACE);
				glFrontFace(c.raster.frontFace);
				glCullFace(c.raster.cullFace);
			} else {
				glDisable(GL_CULL_FACE);
			}
			break;
		default:
			Crash();
			break;
		}
	}

	if (activeTexture != GL_TEXTURE0)
		glActiveTexture(GL_TEXTURE0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glDisable(GL_SCISSOR_TEST);
}

void GLQueueRunner::PerformCopy(const GLRStep &step) {
	GLuint srcTex = 0;
	GLuint dstTex = 0;
	GLuint target = GL_TEXTURE_2D;

	const GLRect2D &srcRect = step.copy.srcRect;
	const GLOffset2D &dstPos = step.copy.dstPos;

	GLRFramebuffer *src = step.copy.src;
	GLRFramebuffer *dst = step.copy.src;

	int srcLevel = 0;
	int dstLevel = 0;
	int srcZ = 0;
	int dstZ = 0;
	int depth = 1;

	switch (step.copy.aspectMask) {
	case GL_COLOR_BUFFER_BIT:
		srcTex = src->color.texture;
		dstTex = dst->color.texture;
		break;
	case GL_DEPTH_BUFFER_BIT:
		target = GL_RENDERBUFFER;
		srcTex = src->depth.texture;
		dstTex = src->depth.texture;
		break;
	}
#if defined(USING_GLES2)
#ifndef IOS
	glCopyImageSubDataOES(
		srcTex, target, srcLevel, srcRect.x, srcRect.y, srcZ,
		dstTex, target, dstLevel, dstPos.x, dstPos.y, dstZ,
		srcRect.w, srcRect.h, depth);
#endif
#else
	if (gl_extensions.ARB_copy_image) {
		glCopyImageSubData(
			srcTex, target, srcLevel, srcRect.x, srcRect.y, srcZ,
			dstTex, target, dstLevel, dstPos.x, dstPos.y, dstZ,
			srcRect.w, srcRect.h, depth);
	} else if (gl_extensions.NV_copy_image) {
		// Older, pre GL 4.x NVIDIA cards.
		glCopyImageSubDataNV(
			srcTex, target, srcLevel, srcRect.x, srcRect.y, srcZ,
			dstTex, target, dstLevel, dstPos.x, dstPos.y, dstZ,
			srcRect.w, srcRect.h, depth);
	}
#endif
}

void GLQueueRunner::PerformReadback(const GLRStep &pass) {

}

void GLQueueRunner::PerformReadbackImage(const GLRStep &pass) {

}

void GLQueueRunner::PerformBindFramebufferAsRenderTarget(const GLRStep &pass) {
	
}

void GLQueueRunner::CopyReadbackBuffer(int width, int height, Draw::DataFormat srcFormat, Draw::DataFormat destFormat, int pixelStride, uint8_t *pixels) {

}
