#pragma once

#include <string>
#include <string_view>

#include "Common/Common.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Data/Hash/Hash.h"

enum class FontStyleFlags {
	Default = 0,
	Bold = 1,
	Italic = 16,
};
ENUM_CLASS_BITOPS(FontStyleFlags);

struct FontStyle {
	FontStyle() {}
	FontStyle(FontID atlasFnt, std::string_view name, int size, FontStyleFlags _flags = FontStyleFlags::Default) : atlasFont(atlasFnt), fontName(name), sizePts(size), flags(_flags) {}

	u32 Hash() const {
		u32 hash = fontName.empty() ? 0 : hash::Adler32(fontName);
		hash ^= sizePts;
		hash ^= (int)flags << 10;
		return hash;
	}

	FontID atlasFont{nullptr};

	// For native fonts:
	std::string fontName;
	int sizePts = 0;
	FontStyleFlags flags = FontStyleFlags::Default;
};
