#include <cstring>
#include <cstdint>
#include <zstd.h>

#include "Common/Log.h"
#include "Common/Render/TextureAtlas.h"

class ByteReader {
public:
	ByteReader(const uint8_t *data, size_t size) : data_(data), offset_(0), size_(size) {}

	template<class T>
	T Read() {
		_dbg_assert_(offset_ + sizeof(T) <= size_);
		T x;
		memcpy(&x, data_ + offset_, sizeof(T));
		offset_ += sizeof(T);
		return x;
	}

	template<class T>
	void ReadInto(T *t) {
		_dbg_assert_(offset_ + sizeof(T) <= size_);
		memcpy(t, data_ + offset_, sizeof(T));
		offset_ += sizeof(T);
	}

	template<class T>
	T *ReadMultipleAlloc(size_t count, bool compressed) {
		T *t = new T[count];
		if (!compressed) {
			_dbg_assert_(offset_ + sizeof(T) * count <= size_);
			memcpy(t, data_ + offset_, sizeof(T) * count);
			offset_ += sizeof(T) * count;
		} else {
			_dbg_assert_(offset_ + sizeof(uint32_t) <= size_);
			uint32_t compressed_size = 0;
			memcpy(&compressed_size, data_ + offset_, sizeof(uint32_t));
			offset_ += sizeof(uint32_t);

			_dbg_assert_(offset_ + compressed_size <= size_);
			ZSTD_decompress(t, sizeof(T) * count, data_ + offset_, compressed_size);
			offset_ += compressed_size;
		}
		return t;
	}

private:
	const uint8_t *data_;
	size_t offset_;
	size_t size_;
};

bool Atlas::Load(const uint8_t *data, size_t data_size) {
	ByteReader reader(data, data_size);

	AtlasHeader header = reader.Read<AtlasHeader>();
	num_images = header.numImages;
	num_fonts = header.numFonts;
	if (header.magic != ATLAS_MAGIC) {
		return false;
	}

	images = reader.ReadMultipleAlloc<AtlasImage>(num_images, header.version >= 1);

	fonts = new AtlasFont[num_fonts];
	for (int i = 0; i < num_fonts; i++) {
		AtlasFontHeader font_header = reader.Read<AtlasFontHeader>();
		fonts[i].padding = font_header.padding;
		fonts[i].height = font_header.height;
		fonts[i].ascend = font_header.ascend;
		fonts[i].distslope = font_header.distslope;
		fonts[i].numRanges = font_header.numRanges;
		fonts[i].numChars = font_header.numChars;
		fonts[i].ranges = reader.ReadMultipleAlloc<AtlasCharRange>(font_header.numRanges, header.version >= 1);
		fonts[i].charData = reader.ReadMultipleAlloc<AtlasChar>(font_header.numChars, header.version >= 1);
		memcpy(fonts[i].name, font_header.name, sizeof(font_header.name));
	}
	return true;
}

const AtlasFont *Atlas::getFont(FontID id) const {
	if (id.isInvalid())
		return nullptr;

	for (int i = 0; i < num_fonts; i++) {
		if (!strcmp(id.id, fonts[i].name))
			return &fonts[i];
	}
	return nullptr;
}

const AtlasImage *Atlas::getImage(ImageID name) const {
	if (name.isInvalid())
		return nullptr;

	for (int i = 0; i < num_images; i++) {
		if (!strcmp(name.id, images[i].name))
			return &images[i];
	}
	return nullptr;
}

bool Atlas::measureImage(ImageID id, float *w, float *h) const {
	const AtlasImage *image = getImage(id);
	if (image) {
		*w = (float)image->w;
		*h = (float)image->h;
		return true;
	} else {
		*w = 0.0f;
		*h = 0.0f;
		return false;
	}
}

const AtlasChar *AtlasFont::getChar(int utf32) const {
	for (int i = 0; i < numRanges; i++) {
		if (utf32 >= ranges[i].start && utf32 < ranges[i].end) {
			const AtlasChar *c = &charData[ranges[i].result_index + utf32 - ranges[i].start];
			if (c->ex == 0 && c->ey == 0)
				return nullptr;
			else
				return c;
		}
	}
	return nullptr;
}

Atlas::~Atlas() {
	delete[] images;
	delete[] fonts;
}

AtlasFont::~AtlasFont() {
	delete[] ranges;
	delete[] charData;
}
