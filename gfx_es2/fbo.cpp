#include <string.h>

#include "base/logging.h"
#include "gfx_es2/fbo.h"
#include "gfx/gl_common.h"
#include "gfx_es2/gl_state.h"

#if defined(USING_GLES2)
#define GL_READ_FRAMEBUFFER GL_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER GL_FRAMEBUFFER
#define GL_RGBA8 GL_RGBA
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 GL_DEPTH_COMPONENT24_OES
#endif
#ifndef GL_DEPTH24_STENCIL8_OES
#define GL_DEPTH24_STENCIL8_OES 0x88F0
#endif
#endif

struct FBO {
	GLuint handle;
	GLuint color_texture;
	GLuint z_stencil_buffer;  // Either this is set, or the two below.
	GLuint z_buffer;
	GLuint stencil_buffer;

	int width;
	int height;
};


// On PC, we always use GL_DEPTH24_STENCIL8. 
// On Android, we try to use what's available.

FBO *fbo_create(int width, int height, int num_color_textures, bool z_stencil) {
	CheckGLExtensions();

	FBO *fbo = new FBO();
	fbo->width = width;
	fbo->height = height;

	// Color texture is same everywhere
	glGenFramebuffers(1, &fbo->handle);
	glGenTextures(1, &fbo->color_texture);

	// Create the surfaces.
	glBindTexture(GL_TEXTURE_2D, fbo->color_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// TODO: We could opt to only create 16-bit render targets on slow devices. For later.
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

#ifdef USING_GLES2
	if (gl_extensions.OES_packed_depth_stencil) {
		ILOG("Creating FBO using DEPTH24_STENCIL8");
		// Standard method
		fbo->stencil_buffer = 0;
		fbo->z_buffer = 0;
		// 24-bit Z, 8-bit stencil combined
		glGenRenderbuffers(1, &fbo->z_stencil_buffer);
		glBindRenderbuffer(GL_RENDERBUFFER, fbo->z_stencil_buffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, width, height);

		// Bind it all together
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo->handle);
		glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo->color_texture, 0);
		glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo->z_stencil_buffer);
		glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fbo->z_stencil_buffer);
	} else {
		ILOG("Creating FBO using separate stencil");
		// TEGRA
		fbo->z_stencil_buffer = 0;
		// 16/24-bit Z, separate 8-bit stencil
		glGenRenderbuffers(1, &fbo->z_buffer);
		glBindRenderbuffer(GL_RENDERBUFFER, fbo->z_buffer);
		glRenderbufferStorage(GL_RENDERBUFFER, gl_extensions.OES_depth24 ? GL_DEPTH_COMPONENT24 : GL_DEPTH_COMPONENT16, width, height);

		// 8-bit stencil buffer
		glGenRenderbuffers(1, &fbo->stencil_buffer);
		glBindRenderbuffer(GL_RENDERBUFFER, fbo->stencil_buffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, width, height);

		// Bind it all together
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo->handle);
		glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo->color_texture, 0);
		glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo->z_buffer);
		glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fbo->stencil_buffer);
	}
#else
	fbo->stencil_buffer = 0;
	fbo->z_buffer = 0;
	// 24-bit Z, 8-bit stencil
	glGenRenderbuffers(1, &fbo->z_stencil_buffer);
	glBindRenderbuffer(GL_RENDERBUFFER, fbo->z_stencil_buffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

	// Bind it all together
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo->handle);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo->color_texture, 0);
	glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo->z_stencil_buffer);
	glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fbo->z_stencil_buffer);
#endif

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	switch(status) {
	case GL_FRAMEBUFFER_COMPLETE:
		ILOG("Framebuffer verified complete.");
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
	return fbo;
}

void fbo_unbind() {
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

void fbo_bind_as_render_target(FBO *fbo) {
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo->handle);
}

void fbo_bind_for_read(FBO *fbo) {
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo->handle);
}

void fbo_bind_color_as_texture(FBO *fbo, int color) {
	glBindTexture(GL_TEXTURE_2D, fbo->color_texture);
}

void fbo_destroy(FBO *fbo) {
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo->handle);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
	glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo->handle);
	glDeleteTextures(1, &fbo->color_texture);
	glDeleteRenderbuffers(1, &fbo->z_stencil_buffer);
}
