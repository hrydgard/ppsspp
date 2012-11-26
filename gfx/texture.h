#ifndef _TEXTURE_H
#define _TEXTURE_H

// Load and manage OpenGL textures easily. Supports ETC1 compressed texture with mipmaps.


#include <string>

#include "gfx/gl_lost_manager.h"

class Texture : public GfxResourceHolder {
public:
	Texture();
	~Texture();

	bool LoadZIM(const char *filename);
#if !defined(USING_GLES2)
	bool LoadPNG(const char *filename);
#endif
	bool LoadXOR();	// Loads a placeholder texture.

	// Deduces format from the filename.
	// If loading fails, will load a 256x256 XOR texture.
	// If filename begins with "gen:", will defer to texture_gen.cpp/h.
	bool Load(const char *filename);

	void Bind(int stage = -1);

	void Destroy();

	unsigned int Handle() const {
		return id_;
	}

	virtual void GLLost();
	std::string filename() const { return filename_; }

private:
	std::string filename_;
#ifdef METRO
	ID3D11Texture2D *tex_;
#endif
	unsigned int id_;
	int width_, height_;
};

#endif
