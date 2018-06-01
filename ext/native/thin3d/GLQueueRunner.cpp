#include "Common/MemoryUtil.h"
#include "Core/Reporting.h"
#include "GLQueueRunner.h"
#include "GLRenderManager.h"
#include "DataFormatGL.h"
#include "base/logging.h"
#include "base/stringutil.h"
#include "gfx/gl_common.h"
#include "gfx/gl_debug_log.h"
#include "gfx_es2/gpu_features.h"
#include "math/dataconv.h"
#include "math/math_util.h"

#define TEXCACHE_NAME_CACHE_SIZE 16

#ifdef IOS
extern void bindDefaultFBO();
#endif

// Workaround for Retroarch. Simply declare
//   extern GLuint g_defaultFBO;
// and set is as appropriate. Can adjust the variables in ext/native/base/display.h as
// appropriate.
GLuint g_defaultFBO = 0;

void GLQueueRunner::CreateDeviceObjects() {
	CHECK_GL_ERROR_IF_DEBUG();
	if (gl_extensions.EXT_texture_filter_anisotropic) {
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropyLevel_);
	} else {
		maxAnisotropyLevel_ = 0.0f;
	}

	if (gl_extensions.ARB_vertex_array_object) {
		glGenVertexArrays(1, &globalVAO_);
	}

	// An eternal optimist.
	sawOutOfMemory_ = false;

	// Populate some strings from the GL thread.
	auto populate = [&](int name) {
		const GLubyte *value = glGetString(name);
		if (!value)
			glStrings_[name] = "?";
		else
			glStrings_[name] = (const char *)value;
	};
	populate(GL_VENDOR);
	populate(GL_RENDERER);
	populate(GL_VERSION);
	populate(GL_SHADING_LANGUAGE_VERSION);
	if (!gl_extensions.IsCoreContext) {  // Not OK to query this in core profile!
		populate(GL_EXTENSIONS);
	}
	CHECK_GL_ERROR_IF_DEBUG();
}

void GLQueueRunner::DestroyDeviceObjects() {
	CHECK_GL_ERROR_IF_DEBUG();
	if (!nameCache_.empty()) {
		glDeleteTextures((GLsizei)nameCache_.size(), &nameCache_[0]);
		nameCache_.clear();
	}
	if (gl_extensions.ARB_vertex_array_object) {
		glDeleteVertexArrays(1, &globalVAO_);
	}
	delete[] readbackBuffer_;
	readbackBufferSize_ = 0;
	delete[] tempBuffer_;
	tempBufferSize_ = 0;
	CHECK_GL_ERROR_IF_DEBUG();
}

void GLQueueRunner::RunInitSteps(const std::vector<GLRInitStep> &steps) {
	CHECK_GL_ERROR_IF_DEBUG();
	glActiveTexture(GL_TEXTURE0);
	GLuint boundTexture = (GLuint)-1;
	bool allocatedTextures = false;

	for (size_t i = 0; i < steps.size(); i++) {
		const GLRInitStep &step = steps[i];
		switch (step.stepType) {
		case GLRInitStepType::CREATE_TEXTURE:
		{
			GLRTexture *tex = step.create_texture.texture;
			glGenTextures(1, &tex->texture);
			glBindTexture(tex->target, tex->texture);
			boundTexture = tex->texture;
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRInitStepType::CREATE_BUFFER:
		{
			GLRBuffer *buffer = step.create_buffer.buffer;
			glGenBuffers(1, &buffer->buffer);
			glBindBuffer(buffer->target_, buffer->buffer);
			glBufferData(buffer->target_, step.create_buffer.size, nullptr, step.create_buffer.usage);
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRInitStepType::BUFFER_SUBDATA:
		{
			GLRBuffer *buffer = step.buffer_subdata.buffer;
			glBindBuffer(GL_ARRAY_BUFFER, buffer->buffer);
			glBufferSubData(GL_ARRAY_BUFFER, step.buffer_subdata.offset, step.buffer_subdata.size, step.buffer_subdata.data);
			if (step.buffer_subdata.deleteData)
				delete[] step.buffer_subdata.data;
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRInitStepType::CREATE_PROGRAM:
		{
			CHECK_GL_ERROR_IF_DEBUG();
			GLRProgram *program = step.create_program.program;
			program->program = glCreateProgram();
			_assert_msg_(G3D, step.create_program.num_shaders > 0, "Can't create a program with zero shaders");
			for (int j = 0; j < step.create_program.num_shaders; j++) {
				_dbg_assert_msg_(G3D, step.create_program.shaders[j]->shader, "Can't create a program with a null shader");
				glAttachShader(program->program, step.create_program.shaders[j]->shader);
			}

			for (auto iter : program->semantics_) {
				glBindAttribLocation(program->program, iter.location, iter.attrib);
			}

#if !defined(USING_GLES2)
			if (step.create_program.support_dual_source) {
				// Dual source alpha
				glBindFragDataLocationIndexed(program->program, 0, 0, "fragColor0");
				glBindFragDataLocationIndexed(program->program, 0, 1, "fragColor1");
			} else if (gl_extensions.VersionGEThan(3, 3, 0)) {
				glBindFragDataLocation(program->program, 0, "fragColor0");
			}
#elif !defined(IOS)
			if (gl_extensions.GLES3 && step.create_program.support_dual_source) {
				glBindFragDataLocationIndexedEXT(program->program, 0, 0, "fragColor0");
				glBindFragDataLocationIndexedEXT(program->program, 0, 1, "fragColor1");
			}
#endif
			glLinkProgram(program->program);

			GLint linkStatus = GL_FALSE;
			glGetProgramiv(program->program, GL_LINK_STATUS, &linkStatus);
			if (linkStatus != GL_TRUE) {
				GLint bufLength = 0;
				glGetProgramiv(program->program, GL_INFO_LOG_LENGTH, &bufLength);
				if (bufLength) {
					char *buf = new char[bufLength];
					glGetProgramInfoLog(program->program, bufLength, nullptr, buf);

					// TODO: Could be other than vs/fs.  Also, we're assuming order here...
					const char *vsDesc = step.create_program.shaders[0]->desc.c_str();
					const char *fsDesc = step.create_program.num_shaders > 1 ? step.create_program.shaders[1]->desc.c_str() : nullptr;
					const char *vsCode = step.create_program.shaders[0]->code.c_str();
					const char *fsCode = step.create_program.num_shaders > 1 ? step.create_program.shaders[1]->code.c_str() : nullptr;
					Reporting::ReportMessage("Error in shader program link: info: %s\nfs: %s\n%s\nvs: %s\n%s", buf, fsDesc, fsCode, vsDesc, vsCode);

					ELOG("Could not link program:\n %s", buf);
					ERROR_LOG(G3D, "VS desc:\n%s", vsDesc);
					ERROR_LOG(G3D, "FS desc:\n%s", fsDesc);
					ERROR_LOG(G3D, "VS:\n%s\n", vsCode);
					ERROR_LOG(G3D, "FS:\n%s\n", fsCode);

#ifdef _WIN32
					OutputDebugStringUTF8(buf);
					if (vsCode)
						OutputDebugStringUTF8(LineNumberString(vsCode).c_str());
					if (fsCode)
						OutputDebugStringUTF8(LineNumberString(fsCode).c_str());
#endif
					delete[] buf;
				} else {
					ELOG("Could not link program with %d shaders for unknown reason:", step.create_program.num_shaders);
				}
				CHECK_GL_ERROR_IF_DEBUG();
				break;
			}

			glUseProgram(program->program);

			// Query all the uniforms.
			for (size_t j = 0; j < program->queries_.size(); j++) {
				auto &x = program->queries_[j];
				assert(x.name);
				*x.dest = glGetUniformLocation(program->program, x.name);
			}

			// Run initializers.
			for (size_t j = 0; j < program->initialize_.size(); j++) {
				auto &init = program->initialize_[j];
				GLint uniform = *init.uniform;
				if (uniform != -1) {
					switch (init.type) {
					case 0:
						glUniform1i(uniform, init.value);
					}
				}
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRInitStepType::CREATE_SHADER:
		{
			CHECK_GL_ERROR_IF_DEBUG();
			GLuint shader = glCreateShader(step.create_shader.stage);
			step.create_shader.shader->shader = shader;
			const char *code = step.create_shader.code;
			glShaderSource(shader, 1, &code, nullptr);
			glCompileShader(shader);
			GLint success = 0;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
			if (!success) {
#define MAX_INFO_LOG_SIZE 2048
				GLchar infoLog[MAX_INFO_LOG_SIZE];
				GLsizei len = 0;
				glGetShaderInfoLog(shader, MAX_INFO_LOG_SIZE, &len, infoLog);
				infoLog[len] = '\0';
#ifdef __ANDROID__
				ELOG("Error in shader compilation! %s\n", infoLog);
				ELOG("Shader source:\n%s\n", (const char *)code);
#endif
				ERROR_LOG(G3D, "Error in shader compilation for: %s", step.create_shader.shader->desc.c_str());
				ERROR_LOG(G3D, "Info log: %s", infoLog);
				ERROR_LOG(G3D, "Shader source:\n%s\n", (const char *)code);
				Reporting::ReportMessage("Error in shader compilation: info: %s\n%s\n%s", infoLog, step.create_shader.shader->desc.c_str(), (const char *)code);
#ifdef SHADERLOG
				OutputDebugStringUTF8(infoLog);
#endif
				step.create_shader.shader->valid = false;
				step.create_shader.shader->failed = true;
				step.create_shader.shader->error = infoLog;
			}
			// Before we throw away the code, attach it to the shader for debugging.
			step.create_shader.shader->code = code;
			delete[] step.create_shader.code;
			step.create_shader.shader->valid = true;
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRInitStepType::CREATE_INPUT_LAYOUT:
		{
			GLRInputLayout *layout = step.create_input_layout.inputLayout;
			// Nothing to do unless we want to create vertexbuffer objects (GL 4.5)
			break;
		}
		case GLRInitStepType::CREATE_FRAMEBUFFER:
		{
			CHECK_GL_ERROR_IF_DEBUG();
			boundTexture = (GLuint)-1;
			InitCreateFramebuffer(step);
			allocatedTextures = true;
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRInitStepType::TEXTURE_IMAGE:
		{
			GLRTexture *tex = step.texture_image.texture;
			CHECK_GL_ERROR_IF_DEBUG();
			if (boundTexture != tex->texture) {
				glBindTexture(tex->target, tex->texture);
				boundTexture = tex->texture;
			}
			if (!step.texture_image.data)
				Crash();
			// For things to show in RenderDoc, need to split into glTexImage2D(..., nullptr) and glTexSubImage.
			glTexImage2D(tex->target, step.texture_image.level, step.texture_image.internalFormat, step.texture_image.width, step.texture_image.height, 0, step.texture_image.format, step.texture_image.type, step.texture_image.data);
			allocatedTextures = true;
			if (step.texture_image.allocType == GLRAllocType::ALIGNED) {
				FreeAlignedMemory(step.texture_image.data);
			} else {
				delete[] step.texture_image.data;
			}
			CHECK_GL_ERROR_IF_DEBUG();
			tex->wrapS = GL_CLAMP_TO_EDGE;
			tex->wrapT = GL_CLAMP_TO_EDGE;
			tex->magFilter = step.texture_image.linearFilter ? GL_LINEAR : GL_NEAREST;
			tex->minFilter = step.texture_image.linearFilter ? GL_LINEAR : GL_NEAREST;
			glTexParameteri(tex->target, GL_TEXTURE_WRAP_S, tex->wrapS);
			glTexParameteri(tex->target, GL_TEXTURE_WRAP_T, tex->wrapT);
			glTexParameteri(tex->target, GL_TEXTURE_MAG_FILTER, tex->magFilter);
			glTexParameteri(tex->target, GL_TEXTURE_MIN_FILTER, tex->minFilter);
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRInitStepType::TEXTURE_FINALIZE:
		{
			CHECK_GL_ERROR_IF_DEBUG();
			GLRTexture *tex = step.texture_finalize.texture;
			if (boundTexture != tex->texture) {
				glBindTexture(tex->target, tex->texture);
				boundTexture = tex->texture;
			}
			if (!gl_extensions.IsGLES || gl_extensions.GLES3) {
				glTexParameteri(tex->target, GL_TEXTURE_MAX_LEVEL, step.texture_finalize.maxLevel);
			}
			tex->maxLod = (float)step.texture_finalize.maxLevel;
			if (step.texture_finalize.genMips) {
				glGenerateMipmap(tex->target);
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		default:
			CHECK_GL_ERROR_IF_DEBUG();
			Crash();
			break;
		}
	}
	CHECK_GL_ERROR_IF_DEBUG();

	// TODO: Use GL_KHR_no_error or a debug callback, where supported?
	if (allocatedTextures) {
		// Users may use replacements or scaling, with high render resolutions, and run out of VRAM.
		// This detects that, rather than looking like PPSSPP is broken.
		// Calling glGetError() isn't great, but at the end of init shouldn't be too bad...
		GLenum err = glGetError();
		if (err == GL_OUT_OF_MEMORY) {
			WARN_LOG_REPORT(G3D, "GL ran out of GPU memory; switching to low memory mode");
			sawOutOfMemory_ = true;
		} else if (err != GL_NO_ERROR) {
			// We checked the err anyway, might as well log if there is one.
			WARN_LOG(G3D, "Got an error after init: %08x (%s)", err, GLEnumToString(err).c_str());
		}
	}
}

void GLQueueRunner::InitCreateFramebuffer(const GLRInitStep &step) {
	GLRFramebuffer *fbo = step.create_framebuffer.framebuffer;

#ifndef USING_GLES2
	if (!gl_extensions.ARB_framebuffer_object && gl_extensions.EXT_framebuffer_object) {
		fbo_ext_create(step);
	} else if (!gl_extensions.ARB_framebuffer_object) {
		return;
	}
	// If GLES2, we have basic FBO support and can just proceed.
#endif
	CHECK_GL_ERROR_IF_DEBUG();

	// Color texture is same everywhere
	glGenFramebuffers(1, &fbo->handle);
	glGenTextures(1, &fbo->color_texture.texture);
	fbo->color_texture.target = GL_TEXTURE_2D;
	fbo->color_texture.maxLod = 0.0f;

	// Create the surfaces.
	glBindTexture(GL_TEXTURE_2D, fbo->color_texture.texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fbo->width, fbo->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	fbo->color_texture.wrapS = GL_CLAMP_TO_EDGE;
	fbo->color_texture.wrapT = GL_CLAMP_TO_EDGE;
	fbo->color_texture.magFilter = GL_LINEAR;
	fbo->color_texture.minFilter = GL_LINEAR;
	fbo->color_texture.canWrap = isPowerOf2(fbo->width) && isPowerOf2(fbo->height);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, fbo->color_texture.wrapS);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, fbo->color_texture.wrapT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, fbo->color_texture.magFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, fbo->color_texture.minFilter);
	if (!gl_extensions.IsGLES || gl_extensions.GLES3) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	}

	if (gl_extensions.IsGLES) {
		if (gl_extensions.OES_packed_depth_stencil) {
			ILOG("Creating %i x %i FBO using DEPTH24_STENCIL8", fbo->width, fbo->height);
			// Standard method
			fbo->stencil_buffer = 0;
			fbo->z_buffer = 0;
			// 24-bit Z, 8-bit stencil combined
			glGenRenderbuffers(1, &fbo->z_stencil_buffer);
			glBindRenderbuffer(GL_RENDERBUFFER, fbo->z_stencil_buffer);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, fbo->width, fbo->height);

			// Bind it all together
			glBindFramebuffer(GL_FRAMEBUFFER, fbo->handle);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo->color_texture.texture, 0);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo->z_stencil_buffer);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fbo->z_stencil_buffer);
		} else {
			ILOG("Creating %i x %i FBO using separate stencil", fbo->width, fbo->height);
			// TEGRA
			fbo->z_stencil_buffer = 0;
			// 16/24-bit Z, separate 8-bit stencil
			glGenRenderbuffers(1, &fbo->z_buffer);
			glBindRenderbuffer(GL_RENDERBUFFER, fbo->z_buffer);
			// Don't forget to make sure fbo_standard_z_depth() matches.
			glRenderbufferStorage(GL_RENDERBUFFER, gl_extensions.OES_depth24 ? GL_DEPTH_COMPONENT24 : GL_DEPTH_COMPONENT16, fbo->width, fbo->height);

			// 8-bit stencil buffer
			glGenRenderbuffers(1, &fbo->stencil_buffer);
			glBindRenderbuffer(GL_RENDERBUFFER, fbo->stencil_buffer);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, fbo->width, fbo->height);

			// Bind it all together
			glBindFramebuffer(GL_FRAMEBUFFER, fbo->handle);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo->color_texture.texture, 0);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo->z_buffer);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fbo->stencil_buffer);
		}
	} else {
		fbo->stencil_buffer = 0;
		fbo->z_buffer = 0;
		// 24-bit Z, 8-bit stencil
		glGenRenderbuffers(1, &fbo->z_stencil_buffer);
		glBindRenderbuffer(GL_RENDERBUFFER, fbo->z_stencil_buffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, fbo->width, fbo->height);

		// Bind it all together
		glBindFramebuffer(GL_FRAMEBUFFER, fbo->handle);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo->color_texture.texture, 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo->z_stencil_buffer);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fbo->z_stencil_buffer);
	}

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	switch (status) {
	case GL_FRAMEBUFFER_COMPLETE:
		// ILOG("Framebuffer verified complete.");
		break;
	case GL_FRAMEBUFFER_UNSUPPORTED:
		ELOG("GL_FRAMEBUFFER_UNSUPPORTED");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
		ELOG("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT ");
		break;
	default:
		FLOG("Other framebuffer error: %i", status);
		break;
	}

	// Unbind state we don't need
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	CHECK_GL_ERROR_IF_DEBUG();

	currentDrawHandle_ = fbo->handle;
	currentReadHandle_ = fbo->handle;
}

void GLQueueRunner::RunSteps(const std::vector<GLRStep *> &steps) {
	CHECK_GL_ERROR_IF_DEBUG();
	for (size_t i = 0; i < steps.size(); i++) {
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
	CHECK_GL_ERROR_IF_DEBUG();
}

void GLQueueRunner::LogSteps(const std::vector<GLRStep *> &steps) {

}


void GLQueueRunner::PerformBlit(const GLRStep &step) {
	CHECK_GL_ERROR_IF_DEBUG();
	// Without FBO_ARB / GLES3, this will collide with bind_for_read, but there's nothing
	// in ES 2.0 that actually separate them anyway of course, so doesn't matter.
	fbo_bind_fb_target(false, step.blit.dst->handle);
	fbo_bind_fb_target(true, step.blit.src->handle);

	int srcX1 = step.blit.srcRect.x;
	int srcY1 = step.blit.srcRect.y;
	int srcX2 = step.blit.srcRect.x + step.blit.srcRect.w;
	int srcY2 = step.blit.srcRect.y + step.blit.srcRect.h;
	int dstX1 = step.blit.dstRect.x;
	int dstY1 = step.blit.dstRect.y;
	int dstX2 = step.blit.dstRect.x + step.blit.dstRect.w;
	int dstY2 = step.blit.dstRect.y + step.blit.dstRect.h;

	if (gl_extensions.GLES3 || gl_extensions.ARB_framebuffer_object) {
		glBlitFramebuffer(srcX1, srcY1, srcX2, srcY2, dstX1, dstY1, dstX2, dstY2, step.blit.aspectMask, step.blit.filter ? GL_LINEAR : GL_NEAREST);
		CHECK_GL_ERROR_IF_DEBUG();
#if defined(USING_GLES2) && defined(__ANDROID__)  // We only support this extension on Android, it's not even available on PC.
	} else if (gl_extensions.NV_framebuffer_blit) {
		glBlitFramebufferNV(srcX1, srcY1, srcX2, srcY2, dstX1, dstY1, dstX2, dstY2, step.blit.aspectMask, step.blit.filter ? GL_LINEAR : GL_NEAREST);
		CHECK_GL_ERROR_IF_DEBUG();
#endif // defined(USING_GLES2) && defined(__ANDROID__)
	} else {
		ERROR_LOG(G3D, "GLQueueRunner: Tried to blit without the capability");
	}
}

void GLQueueRunner::PerformRenderPass(const GLRStep &step) {
	CHECK_GL_ERROR_IF_DEBUG();
	// Don't execute empty renderpasses.
	if (step.commands.empty()) {
		// Nothing to do.
		return;
	}

	PerformBindFramebufferAsRenderTarget(step);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DITHER);
	glEnable(GL_SCISSOR_TEST);
#ifndef USING_GLES2
	glDisable(GL_COLOR_LOGIC_OP);
#endif

	/*
#ifndef USING_GLES2
	if (g_Config.iInternalResolution == 0) {
		glLineWidth(std::max(1, (int)(renderWidth_ / 480)));
		glPointSize(std::max(1.0f, (float)(renderWidth_ / 480.f)));
	} else {
		glLineWidth(g_Config.iInternalResolution);
		glPointSize((float)g_Config.iInternalResolution);
	}
#endif
	*/

	if (gl_extensions.ARB_vertex_array_object) {
		glBindVertexArray(globalVAO_);
	}

	GLRFramebuffer *fb = step.render.framebuffer;
	GLRProgram *curProgram = nullptr;
	int activeSlot = 0;
	glActiveTexture(GL_TEXTURE0 + activeSlot);

	// State filtering tracking.
	int attrMask = 0;
	int colorMask = -1;
	int depthMask = -1;
	int depthFunc = -1;
	int logicOp = -1;
	GLuint curArrayBuffer = (GLuint)-1;
	GLuint curElemArrayBuffer = (GLuint)-1;
	bool depthEnabled = false;
	bool stencilEnabled = false;
	bool blendEnabled = false;
	bool cullEnabled = false;
	bool ditherEnabled = false;
	bool logicEnabled = false;
	GLuint blendEqColor = (GLuint)-1;
	GLuint blendEqAlpha = (GLuint)-1;

	GLRTexture *curTex[8]{};

	auto &commands = step.commands;
	for (const auto &c : commands) {
		switch (c.cmd) {
		case GLRRenderCommand::DEPTH:
			if (c.depth.enabled) {
				if (!depthEnabled) {
					glEnable(GL_DEPTH_TEST);
					depthEnabled = true;
				}
				if (c.depth.write != depthMask) {
					glDepthMask(c.depth.write);
					depthMask = c.depth.write;
				}
				if (c.depth.func != depthFunc) {
					glDepthFunc(c.depth.func);
					depthFunc = c.depth.func;
				}
			} else if (!c.depth.enabled && depthEnabled) {
				glDisable(GL_DEPTH_TEST);
				depthEnabled = false;
			}
			break;
		case GLRRenderCommand::STENCILFUNC:
			if (c.stencilFunc.enabled) {
				if (!stencilEnabled) {
					glEnable(GL_STENCIL_TEST);
					stencilEnabled = true;
				}
				glStencilFunc(c.stencilFunc.func, c.stencilFunc.ref, c.stencilFunc.compareMask);
			} else if (stencilEnabled) {
				glDisable(GL_STENCIL_TEST);
				stencilEnabled = false;
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		case GLRRenderCommand::STENCILOP:
			glStencilOp(c.stencilOp.sFail, c.stencilOp.zFail, c.stencilOp.pass);
			glStencilMask(c.stencilOp.writeMask);
			break;
		case GLRRenderCommand::BLEND:
			if (c.blend.enabled) {
				if (!blendEnabled) {
					glEnable(GL_BLEND);
					blendEnabled = true;
				}
				if (blendEqColor != c.blend.funcColor || blendEqAlpha != c.blend.funcAlpha) {
					glBlendEquationSeparate(c.blend.funcColor, c.blend.funcAlpha);
					blendEqColor = c.blend.funcColor;
					blendEqAlpha = c.blend.funcAlpha;
				}
				glBlendFuncSeparate(c.blend.srcColor, c.blend.dstColor, c.blend.srcAlpha, c.blend.dstAlpha);
			} else if (!c.blend.enabled && blendEnabled) {
				glDisable(GL_BLEND);
				blendEnabled = false;
			}
			if (c.blend.mask != colorMask) {
				glColorMask(c.blend.mask & 1, (c.blend.mask >> 1) & 1, (c.blend.mask >> 2) & 1, (c.blend.mask >> 3) & 1);
				colorMask = c.blend.mask;
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		case GLRRenderCommand::LOGICOP:
#ifndef USING_GLES2
			if (c.logic.enabled) {
				if (!logicEnabled) {
					glEnable(GL_COLOR_LOGIC_OP);
					logicEnabled = true;
				}
				if (logicOp != c.logic.logicOp) {
					glLogicOp(c.logic.logicOp);
				}
			} else if (!c.logic.enabled && logicEnabled) {
				glDisable(GL_COLOR_LOGIC_OP);
				logicEnabled = false;
			}
#endif
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		case GLRRenderCommand::CLEAR:
			// Scissor test is on, and should be on after leaving this case. If we disable it,
			// we re-enable it at the end.
			if (c.clear.scissorW == 0) {
				glDisable(GL_SCISSOR_TEST);
			} else {
				glScissor(c.clear.scissorX, c.clear.scissorY, c.clear.scissorW, c.clear.scissorH);
			}
			if (c.clear.colorMask != colorMask) {
				glColorMask(c.clear.colorMask & 1, (c.clear.colorMask >> 1) & 1, (c.clear.colorMask >> 2) & 1, (c.clear.colorMask >> 3) & 1);
				colorMask = c.clear.colorMask;
			}
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
			if (c.clear.scissorW == 0) {
				glEnable(GL_SCISSOR_TEST);
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		case GLRRenderCommand::INVALIDATE:
		{
			GLenum attachments[3];
			int count = 0;
			if (c.clear.clearMask & GL_COLOR_BUFFER_BIT)
				attachments[count++] = GL_COLOR_ATTACHMENT0;
			if (c.clear.clearMask & GL_DEPTH_BUFFER_BIT)
				attachments[count++] = GL_DEPTH_ATTACHMENT;
			if (c.clear.clearMask & GL_STENCIL_BUFFER_BIT)
				attachments[count++] = GL_STENCIL_BUFFER_BIT;
			glInvalidateFramebuffer(GL_FRAMEBUFFER, count, attachments);
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::BLENDCOLOR:
			glBlendColor(c.blendColor.color[0], c.blendColor.color[1], c.blendColor.color[2], c.blendColor.color[3]);
			break;
		case GLRRenderCommand::VIEWPORT:
		{
			float y = c.viewport.vp.y;
			if (!curFB_)
				y = curFBHeight_ - y - c.viewport.vp.h;

			// TODO: Support FP viewports through glViewportArrays
			glViewport((GLint)c.viewport.vp.x, (GLint)y, (GLsizei)c.viewport.vp.w, (GLsizei)c.viewport.vp.h);
#if !defined(USING_GLES2)
			glDepthRange(c.viewport.vp.minZ, c.viewport.vp.maxZ);
#else
			glDepthRangef(c.viewport.vp.minZ, c.viewport.vp.maxZ);
#endif
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::SCISSOR:
		{
			int y = c.scissor.rc.y;
			if (!curFB_)
				y = curFBHeight_ - y - c.scissor.rc.h;
			glScissor(c.scissor.rc.x, y, c.scissor.rc.w, c.scissor.rc.h);
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::UNIFORM4F:
		{
			int loc = c.uniform4.loc ? *c.uniform4.loc : -1;
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
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::UNIFORM4I:
		{
			int loc = c.uniform4.loc ? *c.uniform4.loc : -1;
			if (c.uniform4.name) {
				loc = curProgram->GetUniformLoc(c.uniform4.name);
			}
			if (loc >= 0) {
				switch (c.uniform4.count) {
				case 1:
					glUniform1iv(loc, 1, (GLint *)&c.uniform4.v[0]);
					break;
				case 2:
					glUniform2iv(loc, 1, (GLint *)c.uniform4.v);
					break;
				case 3:
					glUniform3iv(loc, 1, (GLint *)c.uniform4.v);
					break;
				case 4:
					glUniform4iv(loc, 1, (GLint *)c.uniform4.v);
					break;
				}
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::UNIFORMMATRIX:
		{
			int loc = c.uniform4.loc ? *c.uniform4.loc : -1;
			if (c.uniform4.name) {
				loc = curProgram->GetUniformLoc(c.uniform4.name);
			}
			if (loc >= 0) {
				glUniformMatrix4fv(loc, 1, false, c.uniformMatrix4.m);
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::BINDTEXTURE:
		{
			GLint slot = c.texture.slot;
			if (slot != activeSlot) {
				glActiveTexture(GL_TEXTURE0 + slot);
				activeSlot = slot;
			}
			if (c.texture.texture) {
				if (curTex[slot] != c.texture.texture) {
					glBindTexture(c.texture.texture->target, c.texture.texture->texture);
					curTex[slot] = c.texture.texture;
				}
			} else {
				glBindTexture(GL_TEXTURE_2D, 0);  // Which target? Well we only use this one anyway...
				curTex[slot] = nullptr;
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::BIND_FB_TEXTURE:
		{
			GLint slot = c.bind_fb_texture.slot;
			if (slot != activeSlot) {
				glActiveTexture(GL_TEXTURE0 + slot);
				activeSlot = slot;
			}
			if (c.bind_fb_texture.aspect == GL_COLOR_BUFFER_BIT) {
				if (curTex[slot] != &c.bind_fb_texture.framebuffer->color_texture)
				glBindTexture(GL_TEXTURE_2D, c.bind_fb_texture.framebuffer->color_texture.texture);
				curTex[slot] = &c.bind_fb_texture.framebuffer->color_texture;
			} else {
				// TODO: Depth texturing?
				curTex[slot] = nullptr;
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::BINDPROGRAM:
		{
			if (curProgram != c.program.program) {
				glUseProgram(c.program.program->program);
				curProgram = c.program.program;
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::BIND_VERTEX_BUFFER:
		{
			// TODO: Add fast path for glBindVertexBuffer
			GLRInputLayout *layout = c.bindVertexBuffer.inputLayout;
			GLuint buf = c.bindVertexBuffer.buffer ? c.bindVertexBuffer.buffer->buffer : 0;
			assert(!c.bindVertexBuffer.buffer->Mapped());
			if (buf != curArrayBuffer) {
				glBindBuffer(GL_ARRAY_BUFFER, buf);
				curArrayBuffer = buf;
			}
			int enable = layout->semanticsMask_ & ~attrMask;
			int disable = (~layout->semanticsMask_) & attrMask;
			for (int i = 0; i < 7; i++) {  // SEM_MAX
				if (enable & (1 << i)) {
					glEnableVertexAttribArray(i);
				}
				if (disable & (1 << i)) {
					glDisableVertexAttribArray(i);
				}
			}
			attrMask = layout->semanticsMask_;
			for (size_t i = 0; i < layout->entries.size(); i++) {
				auto &entry = layout->entries[i];
				glVertexAttribPointer(entry.location, entry.count, entry.type, entry.normalized, entry.stride, (const void *)(c.bindVertexBuffer.offset + entry.offset));
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::BIND_BUFFER:
		{
			if (c.bind_buffer.target == GL_ARRAY_BUFFER) {
				Crash();
			} else if (c.bind_buffer.target == GL_ELEMENT_ARRAY_BUFFER) {
				GLuint buf = c.bind_buffer.buffer ? c.bind_buffer.buffer->buffer : 0;
				assert(!c.bind_buffer.buffer->Mapped());
				if (buf != curElemArrayBuffer) {
					glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf);
					curElemArrayBuffer = buf;
				}
			} else {
				GLuint buf = c.bind_buffer.buffer ? c.bind_buffer.buffer->buffer : 0;
				assert(!c.bind_buffer.buffer->Mapped());
				glBindBuffer(c.bind_buffer.target, buf);
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::GENMIPS:
			// TODO: Should we include the texture handle in the command?
			// Also, should this not be an init command?
			glGenerateMipmap(GL_TEXTURE_2D);
			break;
		case GLRRenderCommand::DRAW:
			glDrawArrays(c.draw.mode, c.draw.first, c.draw.count);
			break;
		case GLRRenderCommand::DRAW_INDEXED:
			if (c.drawIndexed.instances == 1) {
				glDrawElements(c.drawIndexed.mode, c.drawIndexed.count, c.drawIndexed.indexType, c.drawIndexed.indices);
			} else {
				glDrawElementsInstanced(c.drawIndexed.mode, c.drawIndexed.count, c.drawIndexed.indexType, c.drawIndexed.indices, c.drawIndexed.instances);
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		case GLRRenderCommand::TEXTURESAMPLER:
		{
			CHECK_GL_ERROR_IF_DEBUG();
			GLint slot = c.textureSampler.slot;
			if (slot != activeSlot) {
				glActiveTexture(GL_TEXTURE0 + slot);
				activeSlot = slot;
			}
			GLRTexture *tex = curTex[slot];
			if (!tex) {
				break;
			}
			if (tex->canWrap) {
				if (tex->wrapS != c.textureSampler.wrapS) {
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, c.textureSampler.wrapS);
					tex->wrapS = c.textureSampler.wrapS;
				}
				if (tex->wrapT != c.textureSampler.wrapT) {
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, c.textureSampler.wrapT);
					tex->wrapT = c.textureSampler.wrapT;
				}
			}
			if (tex->magFilter != c.textureSampler.magFilter) {
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, c.textureSampler.magFilter);
				tex->magFilter = c.textureSampler.magFilter;
			}
			if (tex->minFilter != c.textureSampler.minFilter) {
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, c.textureSampler.minFilter);
				tex->minFilter = c.textureSampler.minFilter;
			}
			if (tex->anisotropy != c.textureSampler.anisotropy) {
				if (c.textureSampler.anisotropy != 0.0f) {
					glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, c.textureSampler.anisotropy);
				}
				tex->anisotropy = c.textureSampler.anisotropy;
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::TEXTURELOD:
		{
			GLint slot = c.textureSampler.slot;
			if (slot != activeSlot) {
				glActiveTexture(GL_TEXTURE0 + slot);
				activeSlot = slot;
			}
			GLRTexture *tex = curTex[slot];
			if (!tex) {
				break;
			}
#ifndef USING_GLES2
			if (tex->lodBias != c.textureLod.lodBias) {
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, c.textureLod.lodBias);
				tex->lodBias = c.textureLod.lodBias;
			}
#endif
			if (tex->minLod != c.textureLod.minLod) {
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, c.textureLod.minLod);
				tex->minLod = c.textureLod.minLod;
			}
			if (tex->maxLod != c.textureLod.maxLod) {
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, c.textureLod.maxLod);
				tex->maxLod = c.textureLod.maxLod;
			}
			break;
		}
		case GLRRenderCommand::RASTER:
			if (c.raster.cullEnable) {
				if (!cullEnabled) {
					glEnable(GL_CULL_FACE);
					cullEnabled = true;
				}
				glFrontFace(c.raster.frontFace);
				glCullFace(c.raster.cullFace);
			} else if (!c.raster.cullEnable && cullEnabled) {
				glDisable(GL_CULL_FACE);
				cullEnabled = false;
			}
			if (c.raster.ditherEnable) {
				if (!ditherEnabled) {
					glEnable(GL_DITHER);
					ditherEnabled = true;
				}
			} else if (!c.raster.ditherEnable && ditherEnabled) {
				glDisable(GL_DITHER);
				ditherEnabled = false;
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		default:
			Crash();
			break;
		}
	}

	for (int i = 0; i < 7; i++) {
		if (attrMask & (1 << i)) {
			glDisableVertexAttribArray(i);
		}
	}

	if (activeSlot != 0) {
		glActiveTexture(GL_TEXTURE0);
		activeSlot = 0;  // doesn't matter, just nice.
	}
	CHECK_GL_ERROR_IF_DEBUG();

	// Wipe out the current state.
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	if (gl_extensions.ARB_vertex_array_object) {
		glBindVertexArray(0);
	}
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
#ifndef USING_GLES2
	glDisable(GL_COLOR_LOGIC_OP);
#endif
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	CHECK_GL_ERROR_IF_DEBUG();
}

void GLQueueRunner::PerformCopy(const GLRStep &step) {
	CHECK_GL_ERROR_IF_DEBUG();
	GLuint srcTex = 0;
	GLuint dstTex = 0;
	GLuint target = GL_TEXTURE_2D;

	const GLRect2D &srcRect = step.copy.srcRect;
	const GLOffset2D &dstPos = step.copy.dstPos;

	GLRFramebuffer *src = step.copy.src;
	GLRFramebuffer *dst = step.copy.dst;

	int srcLevel = 0;
	int dstLevel = 0;
	int srcZ = 0;
	int dstZ = 0;
	int depth = 1;

	switch (step.copy.aspectMask) {
	case GL_COLOR_BUFFER_BIT:
		srcTex = src->color_texture.texture;
		dstTex = dst->color_texture.texture;
		break;
	case GL_DEPTH_BUFFER_BIT:
		// TODO: Support depth copies.
		_assert_msg_(G3D, false, "Depth copies not yet supported - soon");
		target = GL_RENDERBUFFER;
		/*
		srcTex = src->depth.texture;
		dstTex = src->depth.texture;
		*/
		break;
	}

	_dbg_assert_(G3D, srcTex);
	_dbg_assert_(G3D, dstTex);

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
	CHECK_GL_ERROR_IF_DEBUG();
}

void GLQueueRunner::PerformReadback(const GLRStep &pass) {
	using namespace Draw;
	CHECK_GL_ERROR_IF_DEBUG();

	GLRFramebuffer *fb = pass.readback.src;

	fbo_bind_fb_target(true, fb ? fb->handle : 0);

	// Reads from the "bound for read" framebuffer. Note that if there's no fb, it's not valid to call this.
	if (fb && (gl_extensions.GLES3 || !gl_extensions.IsGLES))
		glReadBuffer(GL_COLOR_ATTACHMENT0);

	CHECK_GL_ERROR_IF_DEBUG();

	// Always read back in 8888 format.
	const GLuint internalFormat = GL_RGBA;
	const GLuint format = GL_RGBA;
	const GLuint type = GL_UNSIGNED_BYTE;
	const int srcAlignment = 4;
	int dstAlignment = (int)DataFormatSizeInBytes(pass.readback.dstFormat);

	int pixelStride = pass.readback.srcRect.w;
	// Apply the correct alignment.
	glPixelStorei(GL_PACK_ALIGNMENT, srcAlignment);
	if (!gl_extensions.IsGLES || gl_extensions.GLES3) {
		// Some drivers seem to require we specify this.  See #8254.
		glPixelStorei(GL_PACK_ROW_LENGTH, pixelStride);
	}

	GLRect2D rect = pass.readback.srcRect;

	bool convert = pass.readback.dstFormat != DataFormat::R8G8B8A8_UNORM;

	int tempSize = srcAlignment * rect.w * rect.h;
	int readbackSize = dstAlignment * rect.w * rect.h;
	if (convert && tempSize > tempBufferSize_) {
		delete[] tempBuffer_;
		tempBuffer_ = new uint8_t[tempSize];
		tempBufferSize_ = tempSize;
	}
	if (readbackSize > readbackBufferSize_) {
		delete[] readbackBuffer_;
		readbackBuffer_ = new uint8_t[readbackSize];
		readbackBufferSize_ = readbackSize;
	}

	glReadPixels(rect.x, rect.y, rect.w, rect.h, format, type, convert ? tempBuffer_ : readbackBuffer_);
	#ifdef DEBUG_READ_PIXELS
	LogReadPixelsError(glGetError());
	#endif
	if (!gl_extensions.IsGLES || gl_extensions.GLES3) {
		glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	}
	if (convert) {
		ConvertFromRGBA8888(readbackBuffer_, tempBuffer_, pixelStride, pixelStride, rect.w, rect.h, pass.readback.dstFormat);
	}

	CHECK_GL_ERROR_IF_DEBUG();
}

void GLQueueRunner::PerformReadbackImage(const GLRStep &pass) {
	GLRTexture *tex = pass.readback_image.texture;

	glBindTexture(GL_TEXTURE_2D, tex->texture);

	CHECK_GL_ERROR_IF_DEBUG();

#ifndef USING_GLES2
	int pixelStride = pass.readback_image.srcRect.w;
	glPixelStorei(GL_PACK_ALIGNMENT, 4);

	GLRect2D rect = pass.readback.srcRect;

	int size = 4 * rect.w * rect.h;
	if (size > readbackBufferSize_) {
		delete[] readbackBuffer_;
		readbackBuffer_ = new uint8_t[size];
		readbackBufferSize_ = size;
	}

	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glGetTexImage(GL_TEXTURE_2D, pass.readback_image.mipLevel, GL_RGBA, GL_UNSIGNED_BYTE, readbackBuffer_);
#endif

	CHECK_GL_ERROR_IF_DEBUG();
}

void GLQueueRunner::PerformBindFramebufferAsRenderTarget(const GLRStep &pass) {
	if (pass.render.framebuffer) {
		curFBWidth_ = pass.render.framebuffer->width;
		curFBHeight_ = pass.render.framebuffer->height;
	} else {
		curFBWidth_ = targetWidth_;
		curFBHeight_ = targetHeight_;
	}

	curFB_ = pass.render.framebuffer;
	if (curFB_) {
		// Without FBO_ARB / GLES3, this will collide with bind_for_read, but there's nothing
		// in ES 2.0 that actually separate them anyway of course, so doesn't matter.
		fbo_bind_fb_target(false, curFB_->handle);
	} else {
		fbo_unbind();
		// Backbuffer is now bound.
	}
	CHECK_GL_ERROR_IF_DEBUG();
}

void GLQueueRunner::CopyReadbackBuffer(int width, int height, Draw::DataFormat srcFormat, Draw::DataFormat destFormat, int pixelStride, uint8_t *pixels) {
	// TODO: Maybe move data format conversion here, and always read back 8888. Drivers
	// don't usually provide very optimized conversion implementations, though some do.
	// Just need to be careful about dithering, which may break Danganronpa.
	int bpp = (int)Draw::DataFormatSizeInBytes(destFormat);
	for (int y = 0; y < height; y++) {
		memcpy(pixels + y * pixelStride * bpp, readbackBuffer_ + y * width * bpp, width * bpp);
	}
}

GLuint GLQueueRunner::AllocTextureName() {
	if (nameCache_.empty()) {
		nameCache_.resize(TEXCACHE_NAME_CACHE_SIZE);
		glGenTextures(TEXCACHE_NAME_CACHE_SIZE, &nameCache_[0]);
	}
	u32 name = nameCache_.back();
	nameCache_.pop_back();
	CHECK_GL_ERROR_IF_DEBUG();
	return name;
}

// On PC, we always use GL_DEPTH24_STENCIL8. 
// On Android, we try to use what's available.

#ifndef USING_GLES2
void GLQueueRunner::fbo_ext_create(const GLRInitStep &step) {
	GLRFramebuffer *fbo = step.create_framebuffer.framebuffer;

	CHECK_GL_ERROR_IF_DEBUG();

	// Color texture is same everywhere
	glGenFramebuffersEXT(1, &fbo->handle);
	glGenTextures(1, &fbo->color_texture.texture);

	// Create the surfaces.
	glBindTexture(GL_TEXTURE_2D, fbo->color_texture.texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fbo->width, fbo->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	fbo->color_texture.target = GL_TEXTURE_2D;
	fbo->color_texture.wrapS = GL_CLAMP_TO_EDGE;
	fbo->color_texture.wrapT = GL_CLAMP_TO_EDGE;
	fbo->color_texture.magFilter = GL_LINEAR;
	fbo->color_texture.minFilter = GL_LINEAR;
	fbo->color_texture.maxLod = 0.0f;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, fbo->color_texture.wrapS);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, fbo->color_texture.wrapT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, fbo->color_texture.magFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, fbo->color_texture.minFilter);

	fbo->stencil_buffer = 0;
	fbo->z_buffer = 0;
	// 24-bit Z, 8-bit stencil
	glGenRenderbuffersEXT(1, &fbo->z_stencil_buffer);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, fbo->z_stencil_buffer);
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_STENCIL_EXT, fbo->width, fbo->height);
	// glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH24_STENCIL8, width, height);

	// Bind it all together
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo->handle);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, fbo->color_texture.texture, 0);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, fbo->z_stencil_buffer);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, fbo->z_stencil_buffer);

	GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
	switch (status) {
	case GL_FRAMEBUFFER_COMPLETE_EXT:
		// ILOG("Framebuffer verified complete.");
		break;
	case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
		ELOG("GL_FRAMEBUFFER_UNSUPPORTED");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
		ELOG("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT ");
		break;
	default:
		FLOG("Other framebuffer error: %i", status);
		break;
	}
	// Unbind state we don't need
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	CHECK_GL_ERROR_IF_DEBUG();

	currentDrawHandle_ = fbo->handle;
	currentReadHandle_ = fbo->handle;
}
#endif

GLenum GLQueueRunner::fbo_get_fb_target(bool read, GLuint **cached) {
	bool supportsBlit = gl_extensions.ARB_framebuffer_object;
	if (gl_extensions.IsGLES) {
		supportsBlit = (gl_extensions.GLES3 || gl_extensions.NV_framebuffer_blit);
	}

	// Note: GL_FRAMEBUFFER_EXT and GL_FRAMEBUFFER have the same value, same with _NV.
	if (supportsBlit) {
		if (read) {
			*cached = &currentReadHandle_;
			return GL_READ_FRAMEBUFFER;
		} else {
			*cached = &currentDrawHandle_;
			return GL_DRAW_FRAMEBUFFER;
		}
	} else {
		*cached = &currentDrawHandle_;
		return GL_FRAMEBUFFER;
	}
}

void GLQueueRunner::fbo_bind_fb_target(bool read, GLuint name) {
	CHECK_GL_ERROR_IF_DEBUG();
	GLuint *cached;
	GLenum target = fbo_get_fb_target(read, &cached);
	if (*cached != name) {
		if (gl_extensions.ARB_framebuffer_object || gl_extensions.IsGLES) {
			glBindFramebuffer(target, name);
		} else {
#ifndef USING_GLES2
			glBindFramebufferEXT(target, name);
#endif
		}
		*cached = name;
	}
	CHECK_GL_ERROR_IF_DEBUG();
}

void GLQueueRunner::fbo_unbind() {
	CHECK_GL_ERROR_IF_DEBUG();
#ifndef USING_GLES2
	if (gl_extensions.ARB_framebuffer_object || gl_extensions.IsGLES) {
		glBindFramebuffer(GL_FRAMEBUFFER, g_defaultFBO);
	} else if (gl_extensions.EXT_framebuffer_object) {
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, g_defaultFBO);
	}
#else
	glBindFramebuffer(GL_FRAMEBUFFER, g_defaultFBO);
#endif

#ifdef IOS
	bindDefaultFBO();
#endif

	currentDrawHandle_ = 0;
	currentReadHandle_ = 0;
	CHECK_GL_ERROR_IF_DEBUG();
}

GLRFramebuffer::~GLRFramebuffer() {
	CHECK_GL_ERROR_IF_DEBUG();
	if (gl_extensions.ARB_framebuffer_object || gl_extensions.IsGLES) {
		if (handle) {
			glBindFramebuffer(GL_FRAMEBUFFER, handle);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
			glBindFramebuffer(GL_FRAMEBUFFER, g_defaultFBO);
			glDeleteFramebuffers(1, &handle);
		}
		if (z_stencil_buffer)
			glDeleteRenderbuffers(1, &z_stencil_buffer);
		if (z_buffer)
			glDeleteRenderbuffers(1, &z_buffer);
		if (stencil_buffer)
			glDeleteRenderbuffers(1, &stencil_buffer);
	} else if (gl_extensions.EXT_framebuffer_object) {
#ifndef USING_GLES2
		if (handle) {
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, handle);
			glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
			glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER_EXT, 0);
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, g_defaultFBO);
			glDeleteFramebuffersEXT(1, &handle);
		}
		if (z_stencil_buffer)
			glDeleteRenderbuffers(1, &z_stencil_buffer);
		if (z_buffer)
			glDeleteRenderbuffers(1, &z_buffer);
		if (stencil_buffer)
			glDeleteRenderbuffers(1, &stencil_buffer);
#endif
	}
	CHECK_GL_ERROR_IF_DEBUG();
}
