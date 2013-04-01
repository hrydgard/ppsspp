#pragma once

// Load and manage OpenGL textures easily. Supports ETC1 compressed texture with mipmaps
// in the custom ZIM format.

#include <string>

#include "base/basictypes.h"
#include "gfx/gl_lost_manager.h"

class Texture : public GfxResourceHolder {
public:
	Texture();
	~Texture();

	// Deduces format from the filename.
	// If loading fails, will load a 256x256 XOR texture.
	// If filename begins with "gen:", will defer to texture_gen.cpp/h.
	// When format is known, it's fine to use LoadZIM etc directly.
	// Those will NOT auto-fall back to xor texture however!
	bool Load(const char *filename);
	void Bind(int stage = -1);
	void Destroy();

	// PNG from memory buffer
	bool LoadPNG(const uint8_t *data, size_t size, bool genMips = true);
	bool LoadZIM(const char *filename);
	bool LoadPNG(const char *filename);

	unsigned int Handle() const {
		return id_;
	}

	virtual void GLLost();
	std::string filename() const { return filename_; }

	static void Unbind(int stage = -1);

	int Width() const { return width_; }
	int Height() const { return height_; }

private:
	bool LoadXOR();	// Loads a placeholder texture.

	std::string filename_;
#ifdef METRO
	ID3D11Texture2D *tex_;
#endif
	unsigned int id_;
	int width_, height_;
};