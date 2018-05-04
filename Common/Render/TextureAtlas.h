#pragma once

#include <cstdint>
#include <cstring>
#include "Common/Swap.h"

#define ATLAS_MAGIC ('A' + ('T' << 8) + ('L' << 16) | ('A' << 24))

// Metadata file structure v0:
//
// AtlasHeader
// For each image:
//   AtlasImage
// For each font:
//   AtlasFontHeader
//   For each range:
//     AtlasRange
//   For each char:
//     AtlasChar

struct Atlas;

struct ImageID {
public:
	ImageID() : id(nullptr) {}
	explicit ImageID(const char *_id) : id(_id) {}

	static inline ImageID invalid() {
		return ImageID{ nullptr };
	}

	bool isValid() const {
		return id != nullptr;
	}

	bool isInvalid() const {
		return id == nullptr;
	}

	bool operator ==(const ImageID &other) {
		return (id == other.id) || !strcmp(id, other.id);
	}

	bool operator !=(const ImageID &other) {
		if (id == other.id) {
			return false;
		}
		return strcmp(id, other.id) != 0;
	}

private:
	const char *id;
	friend struct Atlas;
};

struct FontID {
public:
	explicit FontID(const char *_id) : id(_id) {}

	static inline FontID invalid() {
		return FontID{ nullptr };
	}

	bool isInvalid() const {
		return id == nullptr;
	}

private:
	const char *id;
	friend struct Atlas;
};

struct AtlasChar {
	// texcoords
	float_le sx, sy, ex, ey;
	// offset from the origin
	float_le ox, oy;
	// distance to move the origin forward
	float_le wx;
	// size in pixels
	u16_le pw, ph;
};

struct AtlasCharRange {
	s32_le start;
	s32_le end;
	s32_le result_index;
};

struct AtlasFontHeader {
	float_le padding;
	float_le height;
	float_le ascend;
	float_le distslope;
	s32_le numRanges;
	s32_le numChars;
	char name[32];
};

struct AtlasFont {
	~AtlasFont();

	float padding;
	float height;
	float ascend;
	float distslope;
	const AtlasChar *charData;
	const AtlasCharRange *ranges;
	int numRanges;
	int numChars;
	char name[32];

	// Returns 0 on no match.
	const AtlasChar *getChar(int utf32) const ;
};

struct AtlasImage {
	float_le u1, v1, u2, v2;
	s32_le w, h;
	char name[32];
};

struct AtlasHeader {
	s32_le magic;
	s32_le version;
	s32_le numFonts;
	s32_le numImages;
};

struct Atlas {
	~Atlas();
	bool Load(const uint8_t *data, size_t data_size);
	bool IsMetadataLoaded() {
		return images != nullptr;
	}

	AtlasFont *fonts = nullptr;
	int num_fonts = 0;
	AtlasImage *images = nullptr;
	int num_images = 0;

	// These are inefficient linear searches, try not to call every frame.
	const AtlasFont *getFont(FontID id) const;
	const AtlasImage *getImage(ImageID id) const;

	bool measureImage(ImageID id, float *w, float *h) const;
};
