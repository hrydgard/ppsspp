#include "ppsspp_config.h"
#include <algorithm>

#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLDebugLog.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/OpenGL/DataFormatGL.h"
#include "Common/Math/math_util.h"
#include "Common/VR/PPSSPPVR.h"

#include "Common/Log.h"
#include "Common/LogReporting.h"
#include "Common/MemoryUtil.h"
#include "Common/StringUtils.h"
#include "Common/Data/Convert/SmallDataConvert.h"

#include "GLQueueRunner.h"
#include "GLRenderManager.h"
#include "DataFormatGL.h"

// These are the same value, alias for simplicity.
#if defined(GL_CLIP_DISTANCE0_EXT) && !defined(GL_CLIP_DISTANCE0)
#define GL_CLIP_DISTANCE0 GL_CLIP_DISTANCE0_EXT
#elif !defined(GL_CLIP_DISTANCE0)
#define GL_CLIP_DISTANCE0 0x3000
#endif
#ifndef GL_DEPTH_STENCIL_TEXTURE_MODE
#define GL_DEPTH_STENCIL_TEXTURE_MODE 0x90EA
#endif
#ifndef GL_STENCIL_INDEX
#define GL_STENCIL_INDEX 0x1901
#endif

static constexpr int TEXCACHE_NAME_CACHE_SIZE = 16;

#if PPSSPP_PLATFORM(IOS)
extern void bindDefaultFBO();
#endif

// Workaround for Retroarch. Simply declare
//   extern GLuint g_defaultFBO;
// and set is as appropriate. Can adjust the variables in ext/native/base/display.h as
// appropriate.
GLuint g_defaultFBO = 0;

void GLQueueRunner::CreateDeviceObjects() {
	CHECK_GL_ERROR_IF_DEBUG();
	if (caps_.anisoSupported) {
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropyLevel_);
	} else {
		maxAnisotropyLevel_ = 0.0f;
	}

	if (gl_extensions.ARB_vertex_array_object) {
		glGenVertexArrays(1, &globalVAO_);
	}

	// An eternal optimist.
	sawOutOfMemory_ = false;

	// Populate some strings from the GL thread so they can be queried from thin3d.
	// TODO: Merge with GLFeatures.cpp/h
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
	CHECK_GL_ERROR_IF_DEBUG();

#if !PPSSPP_ARCH(X86)  // Doesn't work on AMD for some reason. See issue #17787
	useDebugGroups_ = !gl_extensions.IsGLES && gl_extensions.VersionGEThan(4, 3);
#endif
}

void GLQueueRunner::DestroyDeviceObjects() {
	CHECK_GL_ERROR_IF_DEBUG();
	if (gl_extensions.ARB_vertex_array_object) {
		glDeleteVertexArrays(1, &globalVAO_);
	}
	delete[] readbackBuffer_;
	readbackBuffer_ = nullptr;
	readbackBufferSize_ = 0;
	CHECK_GL_ERROR_IF_DEBUG();
}

template <typename Getiv, typename GetLog>
static std::string GetInfoLog(GLuint name, Getiv getiv, GetLog getLog) {
	GLint bufLength = 0;
	getiv(name, GL_INFO_LOG_LENGTH, &bufLength);
	if (bufLength <= 0)
		bufLength = 2048;

	std::string infoLog;
	infoLog.resize(bufLength);
	GLsizei len = 0;
	getLog(name, (GLsizei)infoLog.size(), &len, &infoLog[0]);
	if (len <= 0)
		return "(unknown reason)";

	infoLog.resize(len);
	return infoLog;
}

void GLQueueRunner::RunInitSteps(const FastVec<GLRInitStep> &steps, bool skipGLCalls) {
	if (skipGLCalls) {
		// Some bookkeeping still needs to be done.
		for (size_t i = 0; i < steps.size(); i++) {
			const GLRInitStep &step = steps[i];
			switch (step.stepType) {
			case GLRInitStepType::BUFFER_SUBDATA:
			{
				if (step.buffer_subdata.deleteData)
					delete[] step.buffer_subdata.data;
				break;
			}
			case GLRInitStepType::TEXTURE_IMAGE:
			{
				if (step.texture_image.allocType == GLRAllocType::ALIGNED) {
					FreeAlignedMemory(step.texture_image.data);
				} else if (step.texture_image.allocType == GLRAllocType::NEW) {
					delete[] step.texture_image.data;
				}
				break;
			}
			case GLRInitStepType::CREATE_PROGRAM:
			{
				WARN_LOG(Log::G3D, "CREATE_PROGRAM found with skipGLCalls, not good");
				break;
			}
			case GLRInitStepType::CREATE_SHADER:
			{
				WARN_LOG(Log::G3D, "CREATE_SHADER found with skipGLCalls, not good");
				break;
			}
			default:
				break;
			}
		}
		return;
	}

#if !defined(USING_GLES2)
	if (useDebugGroups_)
		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 1, -1, "InitSteps");
#endif

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
			glGenBuffers(1, &buffer->buffer_);
			glBindBuffer(buffer->target_, buffer->buffer_);
			glBufferData(buffer->target_, step.create_buffer.size, nullptr, step.create_buffer.usage);
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRInitStepType::BUFFER_SUBDATA:
		{
			GLRBuffer *buffer = step.buffer_subdata.buffer;
			glBindBuffer(buffer->target_, buffer->buffer_);
			glBufferSubData(buffer->target_, step.buffer_subdata.offset, step.buffer_subdata.size, step.buffer_subdata.data);
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
			_assert_msg_(step.create_program.num_shaders > 0, "Can't create a program with zero shaders");
			bool anyFailed = false;
			for (int j = 0; j < step.create_program.num_shaders; j++) {
				_dbg_assert_msg_(step.create_program.shaders[j]->shader, "Can't create a program with a null shader");
				anyFailed = anyFailed || step.create_program.shaders[j]->failed;
				glAttachShader(program->program, step.create_program.shaders[j]->shader);
			}

			for (auto iter : program->semantics_) {
				glBindAttribLocation(program->program, iter.location, iter.attrib);
			}

#if !defined(USING_GLES2)
			if (step.create_program.support_dual_source) {
				_dbg_assert_msg_(caps_.dualSourceBlend, "ARB/EXT_blend_func_extended required for dual src blend");
				// Dual source alpha
				glBindFragDataLocationIndexed(program->program, 0, 0, "fragColor0");
				glBindFragDataLocationIndexed(program->program, 0, 1, "fragColor1");
			} else if (gl_extensions.VersionGEThan(3, 0, 0)) {
				glBindFragDataLocation(program->program, 0, "fragColor0");
			}
#elif !PPSSPP_PLATFORM(IOS)
			if (gl_extensions.GLES3 && step.create_program.support_dual_source) {
				// For GLES2, we use gl_SecondaryFragColorEXT as fragColor1.
				_dbg_assert_msg_(gl_extensions.EXT_blend_func_extended, "EXT_blend_func_extended required for dual src");
				glBindFragDataLocationIndexedEXT(program->program, 0, 0, "fragColor0");
				glBindFragDataLocationIndexedEXT(program->program, 0, 1, "fragColor1");
			}
#endif
			glLinkProgram(program->program);

			GLint linkStatus = GL_FALSE;
			glGetProgramiv(program->program, GL_LINK_STATUS, &linkStatus);
			if (linkStatus != GL_TRUE) {
				std::string infoLog = GetInfoLog(program->program, glGetProgramiv, glGetProgramInfoLog);

				// TODO: Could be other than vs/fs.  Also, we're assuming order here...
				GLRShader *vs = step.create_program.shaders[0];
				GLRShader *fs = step.create_program.num_shaders > 1 ? step.create_program.shaders[1] : nullptr;
				std::string vsDesc = vs->desc + (vs->failed ? " (failed)" : "");
				std::string fsDesc = fs ? (fs->desc + (fs->failed ? " (failed)" : "")) : "(none)";
				const char *vsCode = vs->code.c_str();
				const char *fsCode = fs ? fs->code.c_str() : "(none)";
				if (!anyFailed)
					Reporting::ReportMessage("Error in shader program link: info: %s\nfs: %s\n%s\nvs: %s\n%s", infoLog.c_str(), fsDesc.c_str(), fsCode, vsDesc.c_str(), vsCode);

				ERROR_LOG(Log::G3D, "Could not link program:\n %s", infoLog.c_str());
				ERROR_LOG(Log::G3D, "VS desc:\n%s", vsDesc.c_str());
				ERROR_LOG(Log::G3D, "FS desc:\n%s", fsDesc.c_str());
				ERROR_LOG(Log::G3D, "VS:\n%s\n", LineNumberString(vsCode).c_str());
				ERROR_LOG(Log::G3D, "FS:\n%s\n", LineNumberString(fsCode).c_str());

#ifdef _WIN32
				OutputDebugStringUTF8(infoLog.c_str());
				if (vsCode)
					OutputDebugStringUTF8(LineNumberString(vsCode).c_str());
				if (fsCode)
					OutputDebugStringUTF8(LineNumberString(fsCode).c_str());
#endif
				CHECK_GL_ERROR_IF_DEBUG();
				break;
			}

			glUseProgram(program->program);

			// Query all the uniforms.
			for (size_t j = 0; j < program->queries_.size(); j++) {
				auto &query = program->queries_[j];
				_dbg_assert_(query.name);

				int location = glGetUniformLocation(program->program, query.name);

				if (location < 0 && query.required) {
					WARN_LOG(Log::G3D, "Required uniform query for '%s' failed", query.name);
				}
				*query.dest = location;
			}

			// Run initializers.
			for (size_t j = 0; j < program->initialize_.size(); j++) {
				auto &init = program->initialize_[j];
				GLint uniform = *init.uniform;
				if (uniform != -1) {
					switch (init.type) {
					case 0:
						glUniform1i(uniform, init.value);
						break;
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
				std::string infoLog = GetInfoLog(shader, glGetShaderiv, glGetShaderInfoLog);
				std::string errorString = StringFromFormat(
					"Error in shader compilation for: %s\n"
					"Info log: %s\n"
					"Shader source:\n%s\n//END\n\n",
					step.create_shader.shader->desc.c_str(),
					infoLog.c_str(),
					LineNumberString(code).c_str());
				std::vector<std::string_view> lines;
				SplitString(errorString, '\n', lines);
				for (auto line : lines) {
					ERROR_LOG(Log::G3D, "%.*s", (int)line.size(), line.data());
				}
				if (errorCallback_) {
					std::string desc = StringFromFormat("Shader compilation failed: %s", step.create_shader.stage == GL_VERTEX_SHADER ? "vertex" : "fragment");
					errorCallback_(desc.c_str(), errorString.c_str(), errorCallbackUserData_);
				}
				Reporting::ReportMessage("Error in shader compilation: info: %s\n%s\n%s", infoLog.c_str(), step.create_shader.shader->desc.c_str(), (const char *)code);
#ifdef SHADERLOG
				OutputDebugStringUTF8(infoLog.c_str());
#endif
				step.create_shader.shader->failed = true;
				step.create_shader.shader->error = infoLog;  // Hm, we never use this.
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
			// GLRInputLayout *layout = step.create_input_layout.inputLayout;
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
			if (!step.texture_image.data && step.texture_image.allocType != GLRAllocType::NONE)
				_assert_msg_(false, "missing texture data");

			// For things to show in RenderDoc, need to split into glTexImage2D(..., nullptr) and glTexSubImage.

			int blockSize = 0;
			bool bc = Draw::DataFormatIsBlockCompressed(step.texture_image.format, &blockSize);

			GLenum internalFormat, format, type;
			int alignment;
			Thin3DFormatToGLFormatAndType(step.texture_image.format, internalFormat, format, type, alignment);
			if (step.texture_image.depth == 1) {
				if (bc) {
					int dataSize = ((step.texture_image.width + 3) & ~3) * ((step.texture_image.height + 3) & ~3) * blockSize / 16;
					glCompressedTexImage2D(tex->target, step.texture_image.level, internalFormat,
						step.texture_image.width, step.texture_image.height, 0, dataSize, step.texture_image.data);
				} else {
					glTexImage2D(tex->target,
						step.texture_image.level, internalFormat,
						step.texture_image.width, step.texture_image.height, 0,
						format, type, step.texture_image.data);
				}
			} else {
				glTexImage3D(tex->target,
					step.texture_image.level, internalFormat,
					step.texture_image.width, step.texture_image.height, step.texture_image.depth, 0,
					format, type, step.texture_image.data);
			}
			allocatedTextures = true;
			if (step.texture_image.allocType == GLRAllocType::ALIGNED) {
				FreeAlignedMemory(step.texture_image.data);
			} else if (step.texture_image.allocType == GLRAllocType::NEW) {
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
			if (step.texture_image.depth > 1) {
				glTexParameteri(tex->target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
			}
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
			if ((!gl_extensions.IsGLES || gl_extensions.GLES3) && step.texture_finalize.loadedLevels > 1) {
				glTexParameteri(tex->target, GL_TEXTURE_MAX_LEVEL, step.texture_finalize.loadedLevels - 1);
			}
			tex->maxLod = (float)step.texture_finalize.loadedLevels - 1;
			if (step.texture_finalize.genMips) {
				glGenerateMipmap(tex->target);
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		default:
			CHECK_GL_ERROR_IF_DEBUG();
			_assert_msg_(false, "Bad GLRInitStepType: %d", (int)step.stepType);
			break;
		}
	}
	CHECK_GL_ERROR_IF_DEBUG();

	// TODO: Use GL_KHR_no_error or a debug callback, where supported?
	if (false && allocatedTextures) {
		// Users may use replacements or scaling, with high render resolutions, and run out of VRAM.
		// This detects that, rather than looking like PPSSPP is broken.
		// Calling glGetError() isn't great, but at the end of init, only after creating textures, shouldn't be too bad...
		GLenum err = glGetError();
		if (err == GL_OUT_OF_MEMORY) {
			WARN_LOG_REPORT(Log::G3D, "GL ran out of GPU memory; switching to low memory mode");
			sawOutOfMemory_ = true;
		} else if (err != GL_NO_ERROR) {
			// We checked the err anyway, might as well log if there is one.
			std::string errorString = GLEnumToString(err);
			WARN_LOG(Log::G3D, "Got an error after init: %08x (%s)", err, errorString.c_str());
			if (errorCallback_) {
				errorCallback_("GL frame init error", errorString.c_str(), errorCallbackUserData_);
			}
		}
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

#if !defined(USING_GLES2)
	if (useDebugGroups_)
		glPopDebugGroup();
#endif
}

void GLQueueRunner::InitCreateFramebuffer(const GLRInitStep &step) {
	GLRFramebuffer *fbo = step.create_framebuffer.framebuffer;

#ifndef USING_GLES2
	if (!gl_extensions.ARB_framebuffer_object && gl_extensions.EXT_framebuffer_object) {
		fbo_ext_create(step);
	} else if (!gl_extensions.ARB_framebuffer_object && !gl_extensions.IsGLES) {
		return;
	}
	// If GLES2, we have basic FBO support and can just proceed.
#endif
	CHECK_GL_ERROR_IF_DEBUG();

	auto initFBOTexture = [&](GLRTexture &tex, GLint internalFormat, GLenum format, GLenum type, bool linear) {
		glGenTextures(1, &tex.texture);
		tex.target = GL_TEXTURE_2D;
		tex.maxLod = 0.0f;

		// Create the surfaces.
		glBindTexture(GL_TEXTURE_2D, tex.texture);
		glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, fbo->width, fbo->height, 0, format, type, nullptr);

		tex.wrapS = GL_CLAMP_TO_EDGE;
		tex.wrapT = GL_CLAMP_TO_EDGE;
		tex.magFilter = linear ? GL_LINEAR : GL_NEAREST;
		tex.minFilter = linear ? GL_LINEAR : GL_NEAREST;

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, tex.wrapS);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tex.wrapT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, tex.magFilter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, tex.minFilter);
		if (!gl_extensions.IsGLES || gl_extensions.GLES3) {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		}
	};

	// Color texture is same everywhere
	glGenFramebuffers(1, &fbo->handle);
	initFBOTexture(fbo->color_texture, GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, true);

retry_depth:
	if (!fbo->z_stencil_) {
		INFO_LOG(Log::G3D, "Creating %d x %d FBO using no depth", fbo->width, fbo->height);

		fbo->z_stencil_buffer = 0;
		fbo->stencil_buffer = 0;
		fbo->z_buffer = 0;

		// Bind it all together
		glBindFramebuffer(GL_FRAMEBUFFER, fbo->handle);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo->color_texture.texture, 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
	} else if (gl_extensions.IsGLES) {
		if (gl_extensions.OES_packed_depth_stencil && (gl_extensions.OES_depth_texture || gl_extensions.GLES3)) {
			INFO_LOG(Log::G3D, "Creating %d x %d FBO using DEPTH24_STENCIL8 texture", fbo->width, fbo->height);
			fbo->z_stencil_buffer = 0;
			fbo->stencil_buffer = 0;
			fbo->z_buffer = 0;

			if (gl_extensions.GLES3) {
				initFBOTexture(fbo->z_stencil_texture, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, false);
			} else {
				initFBOTexture(fbo->z_stencil_texture, GL_DEPTH_STENCIL, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, false);
			}

			// Bind it all together
			glBindFramebuffer(GL_FRAMEBUFFER, fbo->handle);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo->color_texture.texture, 0);
			if (gl_extensions.GLES3) {
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, fbo->z_stencil_texture.texture, 0);
			} else {
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fbo->z_stencil_texture.texture, 0);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, fbo->z_stencil_texture.texture, 0);
			}
		} else if (gl_extensions.OES_packed_depth_stencil) {
			INFO_LOG(Log::G3D, "Creating %d x %d FBO using DEPTH24_STENCIL8", fbo->width, fbo->height);
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
			INFO_LOG(Log::G3D, "Creating %d x %d FBO using separate stencil", fbo->width, fbo->height);
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
	} else if (gl_extensions.VersionGEThan(3, 0)) {
		INFO_LOG(Log::G3D, "Creating %d x %d FBO using DEPTH24_STENCIL8 texture", fbo->width, fbo->height);
		fbo->z_stencil_buffer = 0;
		fbo->stencil_buffer = 0;
		fbo->z_buffer = 0;

		initFBOTexture(fbo->z_stencil_texture, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, false);

		// Bind it all together
		glBindFramebuffer(GL_FRAMEBUFFER, fbo->handle);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo->color_texture.texture, 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, fbo->z_stencil_texture.texture, 0);
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
	if (status != GL_FRAMEBUFFER_COMPLETE && !fbo->z_buffer) {
		CHECK_GL_ERROR_IF_DEBUG();
		// Uh oh, maybe we need a z/stencil.  Platforms sometimes, right?
		fbo->z_stencil_ = true;
		goto retry_depth;
	}

	switch (status) {
	case GL_FRAMEBUFFER_COMPLETE:
		// INFO_LOG(Log::G3D, "Framebuffer verified complete.");
		break;
	case GL_FRAMEBUFFER_UNSUPPORTED:
		ERROR_LOG(Log::G3D, "GL_FRAMEBUFFER_UNSUPPORTED");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
		ERROR_LOG(Log::G3D, "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT");
		break;
	default:
		_assert_msg_(false, "Other framebuffer error: %d", status);
		break;
	}

	// Unbind state we don't need
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	CHECK_GL_ERROR_IF_DEBUG();

	currentDrawHandle_ = fbo->handle;
	currentReadHandle_ = fbo->handle;
}

void GLQueueRunner::RunSteps(const std::vector<GLRStep *> &steps, GLFrameData &frameData, bool skipGLCalls, bool keepSteps, bool useVR) {
	if (skipGLCalls) {
		if (keepSteps) {
			return;
		}
		// Dry run
		for (size_t i = 0; i < steps.size(); i++) {
			const GLRStep &step = *steps[i];
			switch (step.stepType) {
			case GLRStepType::RENDER:
				for (const auto &c : step.commands) {
					switch (c.cmd) {
					case GLRRenderCommand::TEXTURE_SUBIMAGE:
						if (c.texture_subimage.data) {
							if (c.texture_subimage.allocType == GLRAllocType::ALIGNED) {
								FreeAlignedMemory(c.texture_subimage.data);
							} else if (c.texture_subimage.allocType == GLRAllocType::NEW) {
								delete[] c.texture_subimage.data;
							}
						}
						break;
					default:
						break;
					}
				}
				break;
			default:
				break;
			}
			delete steps[i];
		}
		return;
	}

	size_t totalRenderCount = 0;
	for (auto &step : steps) {
		if (step->stepType == GLRStepType::RENDER) {
			// Skip empty render steps.
			if (step->commands.empty()) {
				step->stepType = GLRStepType::RENDER_SKIP;
				continue;
			}
			totalRenderCount++;
		}
	}

	CHECK_GL_ERROR_IF_DEBUG();
	size_t renderCount = 0;
	for (size_t i = 0; i < steps.size(); i++) {
		GLRStep &step = *steps[i];

#if !defined(USING_GLES2)
		if (useDebugGroups_)
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, (GLuint)i + 10000, -1, step.tag);
#endif

		switch (step.stepType) {
		case GLRStepType::RENDER:
			renderCount++;
			if (IsVREnabled()) {
				PreprocessStepVR(&step);
				PerformRenderPass(step, renderCount == 1, renderCount == totalRenderCount, frameData.profile);
			} else {
				PerformRenderPass(step, renderCount == 1, renderCount == totalRenderCount, frameData.profile);
			}
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
		case GLRStepType::RENDER_SKIP:
			break;
		default:
			Crash();
			break;
		}

#if !defined(USING_GLES2)
		if (useDebugGroups_)
			glPopDebugGroup();
#endif
		if (frameData.profile.enabled) {
			frameData.profile.passesString += StepToString(step);
		}
		if (!keepSteps) {
			delete steps[i];
		}
	}

	CHECK_GL_ERROR_IF_DEBUG();
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
		ERROR_LOG(Log::G3D, "GLQueueRunner: Tried to blit without the capability");
	}
}

static void EnableDisableVertexArrays(uint32_t prevAttr, uint32_t newAttr) {
	int enable = (~prevAttr) & newAttr;
	int disable = prevAttr & (~newAttr);
	for (int i = 0; i < 7; i++) {  // SEM_MAX
		if (enable & (1 << i)) {
			glEnableVertexAttribArray(i);
		}
		if (disable & (1 << i)) {
			glDisableVertexAttribArray(i);
		}
	}
}

void GLQueueRunner::PerformRenderPass(const GLRStep &step, bool first, bool last, GLQueueProfileContext &profile) {
	CHECK_GL_ERROR_IF_DEBUG();

	PerformBindFramebufferAsRenderTarget(step);

	if (first) {
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_BLEND);
		glDisable(GL_CULL_FACE);
		glDisable(GL_DITHER);
		glEnable(GL_SCISSOR_TEST);
#ifndef USING_GLES2
		if (!gl_extensions.IsGLES) {
			glDisable(GL_COLOR_LOGIC_OP);
		}
#endif
		if (gl_extensions.ARB_vertex_array_object) {
			glBindVertexArray(globalVAO_);
		}
	}

	GLRProgram *curProgram = nullptr;
	int activeSlot = -1;

	// State filtering tracking.
	int attrMask = 0;
	int colorMask = -1;
	int depthMask = -1;
	int depthFunc = -1;
	GLuint curArrayBuffer = (GLuint)0;
	GLuint curElemArrayBuffer = (GLuint)0;
	bool depthEnabled = false;
	bool stencilEnabled = false;
	bool blendEnabled = false;
	bool cullEnabled = false;
	bool ditherEnabled = false;
	bool depthClampEnabled = false;
#ifndef USING_GLES2
	int logicOp = -1;
	bool logicEnabled = false;
#endif
	bool clipDistanceEnabled[8]{};
	GLuint blendEqColor = (GLuint)-1;
	GLuint blendEqAlpha = (GLuint)-1;
	GLenum blendSrcColor = (GLenum)-1;
	GLenum blendDstColor = (GLenum)-1;
	GLenum blendSrcAlpha = (GLenum)-1;
	GLenum blendDstAlpha = (GLenum)-1;

	GLuint stencilWriteMask = (GLuint)-1;
	GLenum stencilFunc = (GLenum)-1;
	GLuint stencilRef = (GLuint)-1;
	GLuint stencilCompareMask = (GLuint)-1;
	GLenum stencilSFail = (GLenum)-1;
	GLenum stencilZFail = (GLenum)-1;
	GLenum stencilPass = (GLenum)-1;
	GLenum frontFace = (GLenum)-1;
	GLenum cullFace = (GLenum)-1;
	GLRTexture *curTex[MAX_GL_TEXTURE_SLOTS]{};

	GLRViewport viewport = {
		-1000000000.0f,
		-1000000000.0f,
		-1000000000.0f,
		-1000000000.0f,
		-1000000000.0f,
		-1000000000.0f,
	};

	GLRect2D scissorRc = { -1, -1, -1, -1 };

	CHECK_GL_ERROR_IF_DEBUG();
	auto &commands = step.commands;
	for (const auto &c : commands) {
#ifdef _DEBUG
		if (profile.enabled) {
			if ((size_t)c.cmd < ARRAY_SIZE(profile.commandCounts)) {
				profile.commandCounts[(size_t)c.cmd]++;
			}
		}
#endif
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
			} else if (/* !c.depth.enabled && */ depthEnabled) {
				glDisable(GL_DEPTH_TEST);
				depthEnabled = false;
			}
			break;
		case GLRRenderCommand::STENCIL:
			if (c.stencil.enabled) {
				if (!stencilEnabled) {
					glEnable(GL_STENCIL_TEST);
					stencilEnabled = true;
				}
				if (c.stencil.func != stencilFunc || c.stencil.ref != stencilRef || c.stencil.compareMask != stencilCompareMask) {
					glStencilFunc(c.stencil.func, c.stencil.ref, c.stencil.compareMask);
					stencilFunc = c.stencil.func;
					stencilRef = c.stencil.ref;
					stencilCompareMask = c.stencil.compareMask;
				}
				if (c.stencil.sFail != stencilSFail || c.stencil.zFail != stencilZFail || c.stencil.pass != stencilPass) {
					glStencilOp(c.stencil.sFail, c.stencil.zFail, c.stencil.pass);
					stencilSFail = c.stencil.sFail;
					stencilZFail = c.stencil.zFail;
					stencilPass = c.stencil.pass;
				}
				if (c.stencil.writeMask != stencilWriteMask) {
					glStencilMask(c.stencil.writeMask);
					stencilWriteMask = c.stencil.writeMask;
				}
			} else if (/* !c.stencilFunc.enabled && */stencilEnabled) {
				glDisable(GL_STENCIL_TEST);
				stencilEnabled = false;
			}
			CHECK_GL_ERROR_IF_DEBUG();
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
				if (blendSrcColor != c.blend.srcColor || blendDstColor != c.blend.dstColor || blendSrcAlpha != c.blend.srcAlpha || blendDstAlpha != c.blend.dstAlpha) {
					glBlendFuncSeparate(c.blend.srcColor, c.blend.dstColor, c.blend.srcAlpha, c.blend.dstAlpha);
					blendSrcColor = c.blend.srcColor;
					blendDstColor = c.blend.dstColor;
					blendSrcAlpha = c.blend.srcAlpha;
					blendDstAlpha = c.blend.dstAlpha;
				}
			} else if (/* !c.blend.enabled && */ blendEnabled) {
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
			} else if (/* !c.logic.enabled && */ logicEnabled) {
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
				if (gl_extensions.IsGLES) {
					glClearDepthf(c.clear.clearZ);
				} else {
					glClearDepth(c.clear.clearZ);
				}
#endif
			}
			if (c.clear.clearMask & GL_STENCIL_BUFFER_BIT) {
				glClearStencil(c.clear.clearStencil);
			}
			glClear(c.clear.clearMask);
			// Restore the color mask if it was different.
			if (c.clear.colorMask != colorMask) {
				glColorMask(colorMask & 1, (colorMask >> 1) & 1, (colorMask >> 2) & 1, (colorMask >> 3) & 1);
			}
			if (c.clear.scissorW == 0) {
				glEnable(GL_SCISSOR_TEST);
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		case GLRRenderCommand::BLENDCOLOR:
			glBlendColor(c.blendColor.color[0], c.blendColor.color[1], c.blendColor.color[2], c.blendColor.color[3]);
			break;
		case GLRRenderCommand::VIEWPORT:
		{
			float y = c.viewport.vp.y;
			if (!curFB_)
				y = curFBHeight_ - y - c.viewport.vp.h;

			// TODO: Support FP viewports through glViewportArrays
			if (viewport.x != c.viewport.vp.x || viewport.y != y || viewport.w != c.viewport.vp.w || viewport.h != c.viewport.vp.h) {
				glViewport((GLint)c.viewport.vp.x, (GLint)y, (GLsizei)c.viewport.vp.w, (GLsizei)c.viewport.vp.h);
				viewport.x = c.viewport.vp.x;
				viewport.y = y;
				viewport.w = c.viewport.vp.w;
				viewport.h = c.viewport.vp.h;
			}

			if (viewport.minZ != c.viewport.vp.minZ || viewport.maxZ != c.viewport.vp.maxZ) {
				viewport.minZ = c.viewport.vp.minZ;
				viewport.maxZ = c.viewport.vp.maxZ;
#if !defined(USING_GLES2)
				if (gl_extensions.IsGLES) {
					glDepthRangef(c.viewport.vp.minZ, c.viewport.vp.maxZ);
				} else {
					glDepthRange(c.viewport.vp.minZ, c.viewport.vp.maxZ);
				}
#else
				glDepthRangef(c.viewport.vp.minZ, c.viewport.vp.maxZ);
#endif
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::SCISSOR:
		{
			int y = c.scissor.rc.y;
			if (!curFB_)
				y = curFBHeight_ - y - c.scissor.rc.h;
			if (scissorRc.x != c.scissor.rc.x || scissorRc.y != y || scissorRc.w != c.scissor.rc.w || scissorRc.h != c.scissor.rc.h) {
				glScissor(c.scissor.rc.x, y, c.scissor.rc.w, c.scissor.rc.h);
				scissorRc.x = c.scissor.rc.x;
				scissorRc.y = y;
				scissorRc.w = c.scissor.rc.w;
				scissorRc.h = c.scissor.rc.h;
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::UNIFORM4F:
		{
			_dbg_assert_(curProgram);
			int loc = c.uniform4.loc ? *c.uniform4.loc : -1;
			if (c.uniform4.name) {
				loc = curProgram->GetUniformLoc(c.uniform4.name);
			}
			if (loc >= 0) {
				_dbg_assert_(c.uniform4.count >=1 && c.uniform4.count <=4);
				switch (c.uniform4.count) {
				case 1: glUniform1f(loc, c.uniform4.v[0]); break;
				case 2: glUniform2fv(loc, 1, c.uniform4.v); break;
				case 3: glUniform3fv(loc, 1, c.uniform4.v); break;
				case 4: glUniform4fv(loc, 1, c.uniform4.v); break;
				}
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::UNIFORM4UI:
		{
			_dbg_assert_(curProgram);
			int loc = c.uniform4.loc ? *c.uniform4.loc : -1;
			if (c.uniform4.name) {
				loc = curProgram->GetUniformLoc(c.uniform4.name);
			}
			if (loc >= 0) {
				_dbg_assert_(c.uniform4.count >=1 && c.uniform4.count <=4);
				switch (c.uniform4.count) {
				case 1: glUniform1uiv(loc, 1, (GLuint *)c.uniform4.v); break;
				case 2: glUniform2uiv(loc, 1, (GLuint *)c.uniform4.v); break;
				case 3: glUniform3uiv(loc, 1, (GLuint *)c.uniform4.v); break;
				case 4: glUniform4uiv(loc, 1, (GLuint *)c.uniform4.v); break;
				}
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::UNIFORM4I:
		{
			_dbg_assert_(curProgram);
			int loc = c.uniform4.loc ? *c.uniform4.loc : -1;
			if (c.uniform4.name) {
				loc = curProgram->GetUniformLoc(c.uniform4.name);
			}
			if (loc >= 0) {
				_dbg_assert_(c.uniform4.count >=1 && c.uniform4.count <=4);
				switch (c.uniform4.count) {
				case 1: glUniform1iv(loc, 1, (GLint *)c.uniform4.v); break;
				case 2: glUniform2iv(loc, 1, (GLint *)c.uniform4.v); break;
				case 3: glUniform3iv(loc, 1, (GLint *)c.uniform4.v); break;
				case 4: glUniform4iv(loc, 1, (GLint *)c.uniform4.v); break;
				}
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::UNIFORMSTEREOMATRIX:
		{
			_dbg_assert_(curProgram);
			int loc = c.uniformStereoMatrix4.loc ? *c.uniformStereoMatrix4.loc : -1;
			if (c.uniformStereoMatrix4.name) {
				loc = curProgram->GetUniformLoc(c.uniformStereoMatrix4.name);
			}
			if (loc >= 0) {
				if (GetVRFBOIndex() == 0) {
					glUniformMatrix4fv(loc, 1, false, c.uniformStereoMatrix4.mData);
				} else {
					glUniformMatrix4fv(loc, 1, false, c.uniformStereoMatrix4.mData + 16);
				}
			}
			if (GetVRFBOIndex() == 1 || GetVRPassesCount() == 1) {
				// Only delete the data if we're rendering the only or the second eye.
				// If we delete during the first eye, we get a use-after-free or double delete.
				delete[] c.uniformStereoMatrix4.mData;
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::UNIFORMMATRIX:
		{
			_dbg_assert_(curProgram);
			int loc = c.uniformMatrix4.loc ? *c.uniformMatrix4.loc : -1;
			if (c.uniformMatrix4.name) {
				loc = curProgram->GetUniformLoc(c.uniformMatrix4.name);
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
				if (curTex[slot] != &c.bind_fb_texture.framebuffer->color_texture) {
					glBindTexture(GL_TEXTURE_2D, c.bind_fb_texture.framebuffer->color_texture.texture);
					curTex[slot] = &c.bind_fb_texture.framebuffer->color_texture;
				}
			} else if (c.bind_fb_texture.aspect == GL_DEPTH_BUFFER_BIT) {
				if (curTex[slot] != &c.bind_fb_texture.framebuffer->z_stencil_texture) {
					glBindTexture(GL_TEXTURE_2D, c.bind_fb_texture.framebuffer->z_stencil_texture.texture);
					curTex[slot] = &c.bind_fb_texture.framebuffer->z_stencil_texture;
				}
				// This should be uncommon, so always set the mode.
				glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_STENCIL_TEXTURE_MODE, GL_DEPTH_COMPONENT);
			} else if (c.bind_fb_texture.aspect == GL_STENCIL_BUFFER_BIT) {
				if (curTex[slot] != &c.bind_fb_texture.framebuffer->z_stencil_texture) {
					glBindTexture(GL_TEXTURE_2D, c.bind_fb_texture.framebuffer->z_stencil_texture.texture);
					curTex[slot] = &c.bind_fb_texture.framebuffer->z_stencil_texture;
				}
				// This should be uncommon, so always set the mode.
				glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_STENCIL_TEXTURE_MODE, GL_STENCIL_INDEX);
			} else {
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

				for (size_t i = 0; i < ARRAY_SIZE(clipDistanceEnabled); ++i) {
					if (c.program.program->use_clip_distance[i] == clipDistanceEnabled[i])
						continue;

					if (c.program.program->use_clip_distance[i])
						glEnable(GL_CLIP_DISTANCE0 + (GLenum)i);
					else
						glDisable(GL_CLIP_DISTANCE0 + (GLenum)i);
					clipDistanceEnabled[i] = c.program.program->use_clip_distance[i];
				}
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::DRAW:
		{
			GLRInputLayout *layout = c.draw.inputLayout;
			GLuint buf = c.draw.vertexBuffer->buffer_;
			_dbg_assert_(!c.draw.vertexBuffer->Mapped());
			if (buf != curArrayBuffer) {
				glBindBuffer(GL_ARRAY_BUFFER, buf);
				curArrayBuffer = buf;
			}
			if (attrMask != layout->semanticsMask_) {
				EnableDisableVertexArrays(attrMask, layout->semanticsMask_);
				attrMask = layout->semanticsMask_;
			}
			for (size_t i = 0; i < layout->entries.size(); i++) {
				auto &entry = layout->entries[i];
				glVertexAttribPointer(entry.location, entry.count, entry.type, entry.normalized, layout->stride, (const void *)(c.draw.vertexOffset + entry.offset));
			}
			if (c.draw.indexBuffer) {
				GLuint buf = c.draw.indexBuffer->buffer_;
				_dbg_assert_(!c.draw.indexBuffer->Mapped());
				if (buf != curElemArrayBuffer) {
					glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf);
					curElemArrayBuffer = buf;
				}
				if (c.draw.instances == 1) {
					glDrawElements(c.draw.mode, c.draw.count, c.draw.indexType, (void *)(intptr_t)c.draw.indexOffset);
				} else {
					glDrawElementsInstanced(c.draw.mode, c.draw.count, c.draw.indexType, (void *)(intptr_t)c.draw.indexOffset, c.draw.instances);
				}
			} else {
				glDrawArrays(c.draw.mode, c.draw.first, c.draw.count);
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::GENMIPS:
			// TODO: Should we include the texture handle in the command?
			// Also, should this not be an init command?
			glGenerateMipmap(GL_TEXTURE_2D);
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
			CHECK_GL_ERROR_IF_DEBUG();
			if (tex->canWrap) {
				if (tex->wrapS != c.textureSampler.wrapS) {
					glTexParameteri(tex->target, GL_TEXTURE_WRAP_S, c.textureSampler.wrapS);
					tex->wrapS = c.textureSampler.wrapS;
				}
				if (tex->wrapT != c.textureSampler.wrapT) {
					glTexParameteri(tex->target, GL_TEXTURE_WRAP_T, c.textureSampler.wrapT);
					tex->wrapT = c.textureSampler.wrapT;
				}
			}
			CHECK_GL_ERROR_IF_DEBUG();
			if (tex->magFilter != c.textureSampler.magFilter) {
				glTexParameteri(tex->target, GL_TEXTURE_MAG_FILTER, c.textureSampler.magFilter);
				tex->magFilter = c.textureSampler.magFilter;
			}
			CHECK_GL_ERROR_IF_DEBUG();
			if (tex->minFilter != c.textureSampler.minFilter) {
				glTexParameteri(tex->target, GL_TEXTURE_MIN_FILTER, c.textureSampler.minFilter);
				tex->minFilter = c.textureSampler.minFilter;
			}
			CHECK_GL_ERROR_IF_DEBUG();
			if (tex->anisotropy != c.textureSampler.anisotropy) {
				if (c.textureSampler.anisotropy != 0.0f) {
					glTexParameterf(tex->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, c.textureSampler.anisotropy);
				}
				tex->anisotropy = c.textureSampler.anisotropy;
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::TEXTURELOD:
		{
			GLint slot = c.textureLod.slot;
			if (slot != activeSlot) {
				glActiveTexture(GL_TEXTURE0 + slot);
				activeSlot = slot;
			}
			GLRTexture *tex = curTex[slot];
			if (!tex) {
				break;
			}
#ifndef USING_GLES2
			if (tex->lodBias != c.textureLod.lodBias && !gl_extensions.IsGLES) {
				glTexParameterf(tex->target, GL_TEXTURE_LOD_BIAS, c.textureLod.lodBias);
				tex->lodBias = c.textureLod.lodBias;
			}
#endif
			if (tex->minLod != c.textureLod.minLod) {
				glTexParameterf(tex->target, GL_TEXTURE_MIN_LOD, c.textureLod.minLod);
				tex->minLod = c.textureLod.minLod;
			}
			if (tex->maxLod != c.textureLod.maxLod) {
				glTexParameterf(tex->target, GL_TEXTURE_MAX_LOD, c.textureLod.maxLod);
				tex->maxLod = c.textureLod.maxLod;
			}
			break;
		}
		case GLRRenderCommand::TEXTURE_SUBIMAGE:
		{
			GLint slot = c.texture_subimage.slot;
			if (slot != activeSlot) {
				glActiveTexture(GL_TEXTURE0 + slot);
				activeSlot = slot;
			}
			// TODO: Need bind?
			GLRTexture *tex = c.texture_subimage.texture;
			if (!c.texture_subimage.data)
				Crash();
			_assert_(tex->target == GL_TEXTURE_2D);
			// For things to show in RenderDoc, need to split into glTexImage2D(..., nullptr) and glTexSubImage.
			GLuint internalFormat, format, type;
			int alignment;
			Thin3DFormatToGLFormatAndType(c.texture_subimage.format, internalFormat, format, type, alignment);
			glTexSubImage2D(tex->target, c.texture_subimage.level, c.texture_subimage.x, c.texture_subimage.y, c.texture_subimage.width, c.texture_subimage.height, format, type, c.texture_subimage.data);
			if (c.texture_subimage.allocType == GLRAllocType::ALIGNED) {
				FreeAlignedMemory(c.texture_subimage.data);
			} else if (c.texture_subimage.allocType == GLRAllocType::NEW) {
				delete[] c.texture_subimage.data;
			}
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		}
		case GLRRenderCommand::RASTER:
			if (c.raster.cullEnable) {
				if (!cullEnabled) {
					glEnable(GL_CULL_FACE);
					cullEnabled = true;
				}
				if (frontFace != c.raster.frontFace) {
					glFrontFace(c.raster.frontFace);
					frontFace = c.raster.frontFace;
				}
				if (cullFace != c.raster.cullFace) {
					glCullFace(c.raster.cullFace);
					cullFace = c.raster.cullFace;
				}
			} else if (/* !c.raster.cullEnable && */ cullEnabled) {
				glDisable(GL_CULL_FACE);
				cullEnabled = false;
			}
			if (c.raster.ditherEnable) {
				if (!ditherEnabled) {
					glEnable(GL_DITHER);
					ditherEnabled = true;
				}
			} else if (/* !c.raster.ditherEnable && */ ditherEnabled) {
				glDisable(GL_DITHER);
				ditherEnabled = false;
			}
#ifndef USING_GLES2
			if (c.raster.depthClampEnable) {
				if (!depthClampEnabled) {
					glEnable(GL_DEPTH_CLAMP);
					depthClampEnabled = true;
				}
			} else if (/* !c.raster.depthClampEnable && */ depthClampEnabled) {
				glDisable(GL_DEPTH_CLAMP);
				depthClampEnabled = false;
			}
#endif
			CHECK_GL_ERROR_IF_DEBUG();
			break;
		default:
			_assert_msg_(false, "Bad GLRRenderCommand: %d", (int)c.cmd);
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
	if (curArrayBuffer != 0)
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	if (curElemArrayBuffer != 0)
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	if (last && gl_extensions.ARB_vertex_array_object) {
		glBindVertexArray(0);
	}
	if (last)
		glDisable(GL_SCISSOR_TEST);
	if (depthEnabled)
		glDisable(GL_DEPTH_TEST);
	if (stencilEnabled)
		glDisable(GL_STENCIL_TEST);
	if (blendEnabled)
		glDisable(GL_BLEND);
	if (cullEnabled)
		glDisable(GL_CULL_FACE);
#ifndef USING_GLES2
	if (depthClampEnabled)
		glDisable(GL_DEPTH_CLAMP);
	if (!gl_extensions.IsGLES && logicEnabled) {
		glDisable(GL_COLOR_LOGIC_OP);
	}
#endif
	for (size_t i = 0; i < ARRAY_SIZE(clipDistanceEnabled); ++i) {
		if (clipDistanceEnabled[i])
			glDisable(GL_CLIP_DISTANCE0 + (GLenum)i);
	}
	if ((colorMask & 15) != 15)
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
		_assert_msg_(false, "Depth copies not yet supported - soon");
		target = GL_RENDERBUFFER;
		/*
		srcTex = src->depth.texture;
		dstTex = src->depth.texture;
		*/
		break;
	}

	_dbg_assert_(srcTex);
	_dbg_assert_(dstTex);

	_assert_msg_(caps_.framebufferCopySupported, "Image copy extension expected");

#if defined(USING_GLES2)
#if !PPSSPP_PLATFORM(IOS)
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

	// Always read back in 8888 format for the color aspect.
	GLuint format = GL_RGBA;
	GLuint type = GL_UNSIGNED_BYTE;
	int srcAlignment = 4;

#ifndef USING_GLES2
	if (pass.readback.aspectMask & GL_DEPTH_BUFFER_BIT) {
		format = GL_DEPTH_COMPONENT;
		type = GL_FLOAT;
		srcAlignment = 4;
	} else if (pass.readback.aspectMask & GL_STENCIL_BUFFER_BIT) {
		format = GL_STENCIL_INDEX;
		type = GL_UNSIGNED_BYTE;
		srcAlignment = 1;
	}
#endif

	readbackAspectMask_ = pass.readback.aspectMask;

	int pixelStride = pass.readback.srcRect.w;
	// Apply the correct alignment.
	glPixelStorei(GL_PACK_ALIGNMENT, srcAlignment);
	if (!gl_extensions.IsGLES || gl_extensions.GLES3) {
		// Some drivers seem to require we specify this.  See #8254.
		glPixelStorei(GL_PACK_ROW_LENGTH, pixelStride);
	}

	GLRect2D rect = pass.readback.srcRect;

	int readbackSize = srcAlignment * rect.w * rect.h;
	if (readbackSize > readbackBufferSize_) {
		delete[] readbackBuffer_;
		readbackBuffer_ = new uint8_t[readbackSize];
		readbackBufferSize_ = readbackSize;
	}

	glReadPixels(rect.x, rect.y, rect.w, rect.h, format, type, readbackBuffer_);
	#ifdef DEBUG_READ_PIXELS
	LogReadPixelsError(glGetError());
	#endif
	if (!gl_extensions.IsGLES || gl_extensions.GLES3) {
		glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	}
	CHECK_GL_ERROR_IF_DEBUG();
}

void GLQueueRunner::PerformReadbackImage(const GLRStep &pass) {
#ifndef USING_GLES2
	GLRTexture *tex = pass.readback_image.texture;
	GLRect2D rect = pass.readback_image.srcRect;

	if (gl_extensions.VersionGEThan(4, 5)) {
		int size = 4 * rect.w * rect.h;
		if (size > readbackBufferSize_) {
			delete[] readbackBuffer_;
			readbackBuffer_ = new uint8_t[size];
			readbackBufferSize_ = size;
		}

		glPixelStorei(GL_PACK_ALIGNMENT, 4);
		glGetTextureSubImage(tex->texture, pass.readback_image.mipLevel, rect.x, rect.y, 0, rect.w, rect.h, 1, GL_RGBA, GL_UNSIGNED_BYTE, readbackBufferSize_, readbackBuffer_);
	} else {
		glBindTexture(GL_TEXTURE_2D, tex->texture);

		CHECK_GL_ERROR_IF_DEBUG();

		GLint w, h;
		// This is only used for debugging (currently), and GL doesn't support a subrectangle.
		glGetTexLevelParameteriv(GL_TEXTURE_2D, pass.readback_image.mipLevel, GL_TEXTURE_WIDTH, &w);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, pass.readback_image.mipLevel, GL_TEXTURE_HEIGHT, &h);

		int size = 4 * std::max((int)w, rect.x + rect.w) * std::max((int)h, rect.y + rect.h);
		if (size > readbackBufferSize_) {
			delete[] readbackBuffer_;
			readbackBuffer_ = new uint8_t[size];
			readbackBufferSize_ = size;
		}

		glPixelStorei(GL_PACK_ALIGNMENT, 4);
		glPixelStorei(GL_PACK_ROW_LENGTH, rect.x + rect.w);
		glGetTexImage(GL_TEXTURE_2D, pass.readback_image.mipLevel, GL_RGBA, GL_UNSIGNED_BYTE, readbackBuffer_);
		glPixelStorei(GL_PACK_ROW_LENGTH, 0);

		if (rect.x != 0 || rect.y != 0) {
			int dstStride = 4 * rect.w;
			int srcStride = 4 * (rect.x + rect.w);
			int xoff = 4 * rect.x;
			int yoff = rect.y * srcStride;
			for (int y = 0; y < rect.h; ++y) {
				memmove(readbackBuffer_ + h * dstStride, readbackBuffer_ + yoff + h * srcStride + xoff, dstStride);
			}
		}
	}
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
		if (IsVREnabled()) {
			BindVRFramebuffer();
		}
		// Backbuffer is now bound.
	}
	CHECK_GL_ERROR_IF_DEBUG();
}

void GLQueueRunner::CopyFromReadbackBuffer(GLRFramebuffer *framebuffer, int width, int height, Draw::DataFormat srcFormat, Draw::DataFormat destFormat, int pixelStride, uint8_t *pixels) {
	// TODO: Maybe move data format conversion here, and always read back 8888. Drivers
	// don't usually provide very optimized conversion implementations, though some do.
	// Just need to be careful about dithering, which may break Danganronpa.
	int bpp = (int)Draw::DataFormatSizeInBytes(destFormat);
	if (!readbackBuffer_ || bpp <= 0 || !pixels) {
		// Something went wrong during the read and no readback buffer was allocated, probably.
		return;
	}

	// Always read back in 8888 format for the color aspect.
	GLuint internalFormat = GL_RGBA;
#ifndef USING_GLES2
	if (readbackAspectMask_ & GL_DEPTH_BUFFER_BIT) {
		internalFormat = GL_DEPTH_COMPONENT;
	} else if (readbackAspectMask_ & GL_STENCIL_BUFFER_BIT) {
		internalFormat = GL_STENCIL_INDEX;
	}
#endif

	bool convert = internalFormat == GL_RGBA && destFormat != Draw::DataFormat::R8G8B8A8_UNORM;
	if (convert) {
		// srcStride is width because we read back "packed" (with no gaps) from GL.
		ConvertFromRGBA8888(pixels, readbackBuffer_, pixelStride, width, width, height, destFormat);
	} else {
		for (int y = 0; y < height; y++) {
			memcpy(pixels + y * pixelStride * bpp, readbackBuffer_ + y * width * bpp, width * bpp);
		}
	}
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
		// INFO_LOG(Log::G3D, "Framebuffer verified complete.");
		break;
	case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
		ERROR_LOG(Log::G3D, "GL_FRAMEBUFFER_UNSUPPORTED");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
		ERROR_LOG(Log::G3D, "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT ");
		break;
	default:
		_assert_msg_(false, "Other framebuffer error: %d", status);
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

#if PPSSPP_PLATFORM(IOS) && !defined(__LIBRETRO__)
	bindDefaultFBO();
#endif

	currentDrawHandle_ = 0;
	currentReadHandle_ = 0;
	CHECK_GL_ERROR_IF_DEBUG();
}

GLRFramebuffer::~GLRFramebuffer() {
	if (handle == 0 && z_stencil_buffer == 0 && z_buffer == 0 && stencil_buffer == 0)
		return;

	CHECK_GL_ERROR_IF_DEBUG();
	if (handle) {
		if (gl_extensions.ARB_framebuffer_object || gl_extensions.IsGLES) {
			glBindFramebuffer(GL_FRAMEBUFFER, handle);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
			glBindFramebuffer(GL_FRAMEBUFFER, g_defaultFBO);
			glDeleteFramebuffers(1, &handle);
#ifndef USING_GLES2
		} else if (gl_extensions.EXT_framebuffer_object) {
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, handle);
			glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
			glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER_EXT, 0);
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, g_defaultFBO);
			glDeleteFramebuffersEXT(1, &handle);
#endif
		}
	}

	// These can only be set when supported.
	if (z_stencil_buffer)
		glDeleteRenderbuffers(1, &z_stencil_buffer);
	if (z_buffer)
		glDeleteRenderbuffers(1, &z_buffer);
	if (stencil_buffer)
		glDeleteRenderbuffers(1, &stencil_buffer);
	CHECK_GL_ERROR_IF_DEBUG();
}

std::string GLQueueRunner::StepToString(const GLRStep &step) const {
	char buffer[256];
	switch (step.stepType) {
	case GLRStepType::RENDER:
	{
		int w = step.render.framebuffer ? step.render.framebuffer->width : targetWidth_;
		int h = step.render.framebuffer ? step.render.framebuffer->height : targetHeight_;
		snprintf(buffer, sizeof(buffer), "RENDER %s %s (commands: %d, %dx%d)\n", step.tag, step.render.framebuffer ? step.render.framebuffer->Tag() : "", (int)step.commands.size(), w, h);
		break;
	}
	case GLRStepType::COPY:
		snprintf(buffer, sizeof(buffer), "COPY '%s' %s -> %s (%dx%d, %s)\n", step.tag, step.copy.src->Tag(), step.copy.dst->Tag(), step.copy.srcRect.w, step.copy.srcRect.h, GLRAspectToString((GLRAspect)step.copy.aspectMask));
		break;
	case GLRStepType::BLIT:
		snprintf(buffer, sizeof(buffer), "BLIT '%s' %s -> %s (%dx%d->%dx%d, %s)\n", step.tag, step.copy.src->Tag(), step.copy.dst->Tag(), step.blit.srcRect.w, step.blit.srcRect.h, step.blit.dstRect.w, step.blit.dstRect.h, GLRAspectToString((GLRAspect)step.blit.aspectMask));
		break;
	case GLRStepType::READBACK:
		snprintf(buffer, sizeof(buffer), "READBACK '%s' %s (%dx%d, %s)\n", step.tag, step.readback.src ? step.readback.src->Tag() : "(backbuffer)", step.readback.srcRect.w, step.readback.srcRect.h, GLRAspectToString((GLRAspect)step.readback.aspectMask));
		break;
	case GLRStepType::READBACK_IMAGE:
		snprintf(buffer, sizeof(buffer), "READBACK_IMAGE '%s' (%dx%d)\n", step.tag, step.readback_image.srcRect.w, step.readback_image.srcRect.h);
		break;
	case GLRStepType::RENDER_SKIP:
		snprintf(buffer, sizeof(buffer), "(RENDER_SKIP) %s\n", step.tag);
		break;
	default:
		buffer[0] = 0;
		break;
	}
	return std::string(buffer);
}

const char *GLRAspectToString(GLRAspect aspect) {
	switch (aspect) {
	case GLR_ASPECT_COLOR: return "COLOR";
	case GLR_ASPECT_DEPTH: return "DEPTH";
	case GLR_ASPECT_STENCIL: return "STENCIL";
	default: return "N/A";
	}
}

const char *RenderCommandToString(GLRRenderCommand cmd) {
	switch (cmd) {
	case GLRRenderCommand::DEPTH: return "DEPTH";
	case GLRRenderCommand::STENCIL: return "STENCIL";
	case GLRRenderCommand::BLEND: return "BLEND";
	case GLRRenderCommand::BLENDCOLOR: return "BLENDCOLOR";
	case GLRRenderCommand::LOGICOP: return "LOGICOP";
	case GLRRenderCommand::UNIFORM4I: return "UNIFORM4I";
	case GLRRenderCommand::UNIFORM4UI: return "UNIFORM4UI";
	case GLRRenderCommand::UNIFORM4F: return "UNIFORM4F";
	case GLRRenderCommand::UNIFORMMATRIX: return "UNIFORMMATRIX";
	case GLRRenderCommand::UNIFORMSTEREOMATRIX: return "UNIFORMSTEREOMATRIX";
	case GLRRenderCommand::TEXTURESAMPLER: return "TEXTURESAMPLER";
	case GLRRenderCommand::TEXTURELOD: return "TEXTURELOD";
	case GLRRenderCommand::VIEWPORT: return "VIEWPORT";
	case GLRRenderCommand::SCISSOR: return "SCISSOR";
	case GLRRenderCommand::RASTER: return "RASTER";
	case GLRRenderCommand::CLEAR: return "CLEAR";
	case GLRRenderCommand::INVALIDATE: return "INVALIDATE";
	case GLRRenderCommand::BINDPROGRAM: return "BINDPROGRAM";
	case GLRRenderCommand::BINDTEXTURE: return "BINDTEXTURE";
	case GLRRenderCommand::BIND_FB_TEXTURE: return "BIND_FB_TEXTURE";
	case GLRRenderCommand::BIND_VERTEX_BUFFER: return "BIND_VERTEX_BUFFER";
	case GLRRenderCommand::GENMIPS: return "GENMIPS";
	case GLRRenderCommand::DRAW: return "DRAW";
	case GLRRenderCommand::TEXTURE_SUBIMAGE: return "TEXTURE_SUBIMAGE";
	default: return "N/A";
	}
}
