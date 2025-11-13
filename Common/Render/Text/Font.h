#pragma once

#include <string>
#include <string_view>

#include "Common/Common.h"
#include "Common/CommonTypes.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Data/Hash/Hash.h"

// NOTE: Make sure the java flags match.
enum class FontStyleFlags : u8 {
	Default = 0,
	Bold = 1,
	Light = 2,
	Italic = 16,
	Underline = 32,  // Future use.
	Strikethrough = 64,  // Future use.
};
ENUM_CLASS_BITOPS(FontStyleFlags);

enum class FontFamily : u8 {
	SansSerif = 1,
	Fixed = 2,
};

struct FontStyle {
	FontStyle() {}
	constexpr FontStyle(FontFamily _family, int size, FontStyleFlags _flags) : family(_family), sizePts(size), flags(_flags) {}

	u16 sizePts = 0;
	FontFamily family = FontFamily::SansSerif;
	FontStyleFlags flags = FontStyleFlags::Default;
};

inline constexpr bool operator<(const FontStyle &a, const FontStyle &b) {
	if (a.family != b.family)
		return a.family < b.family;
	if (a.sizePts != b.sizePts)
		return a.sizePts < b.sizePts;
	return a.flags < b.flags;
}
