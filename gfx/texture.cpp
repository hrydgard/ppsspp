#if defined(ANDROID) || defined(BLACKBERRY)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#else
#include <GL/glew.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if !defined(ANDROID) && !defined(BLACKBERRY)
#include "image/png_load.h"
#include "ext/etcpack/etcdec.h"
#endif

#include "image/zim_load.h"
#include "base/logging.h"
#include "texture.h"
#include "gfx/texture_gen.h"
#include "gfx/gl_debug_log.h"
#include "gfx/gl_lost_manager.h"

Texture::Texture() : id_(0) {
	register_gl_resource_holder(this);
}

void Texture::Destroy() {
	if (id_) {
		glDeleteTextures(1, &id_);
		id_ = 0;
	}
}

void Texture::GLLost() {
	ILOG("Reloading lost texture %s", filename_.c_str());
	Load(filename_.c_str());
}

Texture::~Texture() {
	unregister_gl_resource_holder(this);
	Destroy();
}

static void SetTextureParameters(int zim_flags) {
	GLenum wrap = GL_REPEAT;
	if (zim_flags & ZIM_CLAMP) wrap = GL_CLAMP_TO_EDGE;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
	GL_CHECK();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	if ((zim_flags & (ZIM_HAS_MIPS | ZIM_GEN_MIPS))) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
	} else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	}
	GL_CHECK();
}

bool Texture::Load(const char *filename) {
	// hook for generated textures
	if (!memcmp(filename, "gen:", 4)) {
		// TODO
		// return false;
		int bpp, w, h;
		bool clamp;
		uint8_t *data = generateTexture(filename, bpp, w, h, clamp);
		if (!data) 
			return false;
		glGenTextures(1, &id_);
		glBindTexture(GL_TEXTURE_2D, id_);
		if (bpp == 1) {

#if defined(ANDROID) || defined(BLACKBERRY)
			glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, data);
#else
			glTexImage2D(GL_TEXTURE_2D, 0, 1, w, h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, data);
#endif
		} else {
			FLOG("unsupported");
		}
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		delete [] data;
		return true;
	}

	filename_ = filename;

	// Currently here are a bunch of project-specific workarounds.
	// They shouldn't really hurt anything else very much though.

	int len = strlen(filename);
	char fn[256];
	strcpy(fn, filename);
	bool zim = false;
	if (!strcmp("dds", &filename[len-3])) {
		strcpy(&fn[len-3], "zim");
		zim = true;
	}
	if (!strcmp("6TX", &filename[len-3]) || !strcmp("6tx", &filename[len-3])) {
		ILOG("Detected 6TX %s", filename);
		strcpy(&fn[len-3], "zim");
		zim = true;
	}
	for (int i = 0; i < (int)strlen(fn); i++) {
		if (fn[i] == '\\') fn[i] = '/';
	}

	if (fn[0] == 'm') fn[0] = 'M';
	const char *name = fn;
	if (zim && 0==memcmp(name, "Media/textures/", strlen("Media/textures"))) name += strlen("Media/textures/");
	len = strlen(name);
#if !defined(ANDROID) && !defined(BLACKBERRY)
	if (!strcmp("png", &name[len-3]) ||
		!strcmp("PNG", &name[len-3])) {
			if (!LoadPNG(fn)) {
				LoadXOR();
				return false;
			} else {
				return true;
			}
	} else
#endif
	if (!strcmp("zim", &name[len-3])) {
		if (!LoadZIM(name)) {
			LoadXOR();
			return false;
		} else {
			return true;
		}
	}
	LoadXOR();
	return false;
}

#if !defined(ANDROID) && !defined(BLACKBERRY)
bool Texture::LoadPNG(const char *filename) {
	unsigned char *image_data;
	if (1 != pngLoad(filename, &width_, &height_, &image_data, false)) {
		return false;
	}
	GL_CHECK();
	glGenTextures(1, &id_);
	glBindTexture(GL_TEXTURE_2D, id_);
	SetTextureParameters(ZIM_GEN_MIPS);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, 
		GL_RGBA, GL_UNSIGNED_BYTE, image_data);
	glGenerateMipmap(GL_TEXTURE_2D);
	GL_CHECK();
	free(image_data);
	return true;
}
#endif

bool Texture::LoadXOR() {
	width_ = height_ = 256;
	unsigned char *buf = new unsigned char[width_*height_*4];
	for (int y = 0; y < 256; y++) {
		for (int x = 0; x < 256; x++) {
			buf[(y*width_ + x)*4 + 0] = x^y;
			buf[(y*width_ + x)*4 + 1] = x^y;
			buf[(y*width_ + x)*4 + 2] = x^y;
			buf[(y*width_ + x)*4 + 3] = 0xFF;
		}
	}
	GL_CHECK();
	glGenTextures(1, &id_);
	glBindTexture(GL_TEXTURE_2D, id_);
	SetTextureParameters(ZIM_GEN_MIPS);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, buf);
	glGenerateMipmap(GL_TEXTURE_2D);
	GL_CHECK();
	delete [] buf;
	return true;
}


#if !defined(ANDROID) && !defined(BLACKBERRY)

// Allocates using new[], doesn't free.
uint8_t *ETC1ToRGBA(uint8_t *etc1, int width, int height) {
	uint8_t *rgba = new uint8_t[width * height * 4];
	memset(rgba, 0xFF, width * height * 4);
	for (int y = 0; y < height; y += 4) {
		for (int x = 0; x < width; x += 4) {
			DecompressBlock(etc1 + ((y / 4) * width/4 + (x / 4)) * 8,
				rgba + (y * width + x) * 4, width, 255);
		}
	}
	return rgba;
}

#endif

bool Texture::LoadZIM(const char *filename) {
	uint8_t *image_data[ZIM_MAX_MIP_LEVELS];
	int width[ZIM_MAX_MIP_LEVELS];
	int height[ZIM_MAX_MIP_LEVELS];

	int flags;
	int num_levels = ::LoadZIM(filename, &width[0], &height[0], &flags, &image_data[0]);
	if (!num_levels)
		return false;
	width_ = width[0];
	height_ = height[0];
	int data_type = GL_UNSIGNED_BYTE;
	int colors = GL_RGBA;
	int storage = GL_RGBA;
	bool compressed = false;
	switch (flags & ZIM_FORMAT_MASK) {
	case ZIM_RGBA8888:
		data_type = GL_UNSIGNED_BYTE;
		break;
	case ZIM_RGBA4444:
		data_type = GL_UNSIGNED_SHORT_4_4_4_4;
		break;
	case ZIM_RGB565:
		data_type = GL_UNSIGNED_SHORT_5_6_5;
		colors = GL_RGB;
		storage = GL_RGB;
		break;
	case ZIM_ETC1:
		compressed = true;
		break;
	}

	GL_CHECK();
	glGenTextures(1, &id_);
	glBindTexture(GL_TEXTURE_2D, id_);
	SetTextureParameters(flags);

	if (compressed) {
		for (int l = 0; l < num_levels; l++) {
			int data_w = width[l];
			int data_h = height[l];
			if (data_w < 4) data_w = 4;
			if (data_h < 4) data_h = 4;
#if defined(ANDROID) || defined(BLACKBERRY)
			int compressed_image_bytes = data_w * data_h / 2;
			glCompressedTexImage2D(GL_TEXTURE_2D, l, GL_ETC1_RGB8_OES, width[l], height[l], 0, compressed_image_bytes, image_data[l]);
			GL_CHECK();
#else
			image_data[l] = ETC1ToRGBA(image_data[l], data_w, data_h);
			glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA, width[l], height[l], 0,
				GL_RGBA, GL_UNSIGNED_BYTE, image_data[l]);
#endif
		}
		GL_CHECK();
#if !defined(ANDROID) && !defined(BLACKBERRY)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, num_levels - 2);
#endif
	} else {
		for (int l = 0; l < num_levels; l++) {
			glTexImage2D(GL_TEXTURE_2D, l, storage, width[l], height[l], 0,
				colors, data_type, image_data[l]);
		}
		if (num_levels == 1 && (flags & ZIM_GEN_MIPS)) {
			glGenerateMipmap(GL_TEXTURE_2D);
		}
	}
	SetTextureParameters(flags);

	GL_CHECK();
	// Only free the top level, since the allocation is used for all of them.
	delete [] image_data[0];
	return true;
}

void Texture::Bind(int stage) {
	GL_CHECK();
	if (stage != -1)
		glActiveTexture(GL_TEXTURE0 + stage);
	glBindTexture(GL_TEXTURE_2D, id_);
	GL_CHECK();
}
