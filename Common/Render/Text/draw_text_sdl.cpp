#include "ppsspp_config.h"

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/System/Display.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Hash/Hash.h"
#include "Common/Data/Text/WrapText.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/Render/Text/draw_text_sdl.h"
#include <algorithm>
#include <clocale>

#if defined(USE_SDL3_TTF)

#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <SDL3_ttf/SDL_ttf.h>

#if defined(USE_SDL3_TTF_FRIBIDI)
#include <fribidi.h>
#endif

// SDL3_ttf shapes a whole string in one direction, guessed from its first character, so a run of the
// other direction comes out reversed ("PPSSPP Gold" inside Hebrew text). When a string mixes directions,
// reorder each line into visual order with FriBidi and have SDL3_ttf render it left-to-right. Strings in a
// single direction are left to SDL3_ttf, which shapes Arabic better than FriBidi's presentation forms.
// Without FriBidi, mixed strings stay partly reversed.
static bool ReorderBidi(std::string_view str, std::string *out) {
#if defined(USE_SDL3_TTF_FRIBIDI)
	bool hasRTL = false;
	UTF8 decoder(str);
	while (!decoder.end()) {
		if (FRIBIDI_IS_RTL(fribidi_get_bidi_type(decoder.next()))) {
			hasRTL = true;
			break;
		}
	}
	if (!hasRTL) {
		return false;
	}

	std::vector<std::string_view> lines;
	SplitString(str, '\n', lines);

	std::vector<FriBidiChar> logical;
	std::vector<FriBidiChar> visual;
	std::vector<FriBidiLevel> levels;
	std::vector<char> utf8;
	std::string result;
	bool mixed = false;
	int firstLevel = -1;
	for (size_t i = 0; i < lines.size(); i++) {
		if (i > 0) {
			result.push_back('\n');
		}
		std::string_view line = lines[i];
		FriBidiParType baseDir = FRIBIDI_PAR_ON;
		// TextDrawer::ApplyBaseDirection wraps RTL lines in RLE ... PDF. Take the direction from that instead,
		// so the marks don't count as a second level and send pure Arabic lines down the reordering path.
		const std::string_view rle = "\xE2\x80\xAB";
		const std::string_view pdf = "\xE2\x80\xAC";
		if (line.size() >= rle.size() + pdf.size() && startsWith(line, rle) && endsWith(line, pdf)) {
			line = line.substr(rle.size(), line.size() - rle.size() - pdf.size());
			baseDir = FRIBIDI_PAR_RTL;
		}
		logical.resize(line.size() + 1);
		FriBidiStrIndex len = fribidi_charset_to_unicode(FRIBIDI_CHAR_SET_UTF8, line.data(), (FriBidiStrIndex)line.size(), logical.data());
		if (len <= 0) {
			result.append(line);
			continue;
		}
		visual.resize(len + 1);
		levels.resize(len);
		if (!fribidi_log2vis(logical.data(), len, &baseDir, visual.data(), nullptr, nullptr, levels.data())) {
			return false;
		}
		for (FriBidiStrIndex j = 0; j < len; j++) {
			if (firstLevel < 0) {
				firstLevel = levels[j];
			} else if (levels[j] != firstLevel) {
				mixed = true;
			}
		}
		utf8.resize(len * 4 + 1);
		FriBidiStrIndex size = fribidi_unicode_to_charset(FRIBIDI_CHAR_SET_UTF8, visual.data(), len, utf8.data());
		result.append(utf8.data(), size);
	}
	if (!mixed) {
		return false;
	}
	*out = std::move(result);
	return true;
#else
	return false;
#endif
}

static void SetBidiDirection(TTF_Font *font, bool reordered) {
#if defined(USE_SDL3_TTF_FRIBIDI)
	// The font is shared, so set the direction every time, either way.
	TTF_SetFontDirection(font, reordered ? TTF_DIRECTION_LTR : TTF_DIRECTION_INVALID);
#endif
}

static TTF_Font *OpenFontFace(const std::string &fontPath, float ptSize, int faceIndex) {
	SDL_PropertiesID props = SDL_CreateProperties();
	if (!props) {
		return nullptr;
	}
	SDL_SetStringProperty(props, TTF_PROP_FONT_CREATE_FILENAME_STRING, fontPath.c_str());
	SDL_SetFloatProperty(props, TTF_PROP_FONT_CREATE_SIZE_FLOAT, ptSize);
	SDL_SetNumberProperty(props, TTF_PROP_FONT_CREATE_FACE_NUMBER, faceIndex);
	TTF_Font *font = TTF_OpenFontWithProperties(props);
	SDL_DestroyProperties(props);
	return font;
}

static std::string getlocale() {
	// setlocale is not an intuitive function...
	char *curlocale = setlocale(LC_CTYPE, nullptr);
	std::string loc = curlocale ? std::string(curlocale) : "en_US";
	size_t ptPos = loc.find('.');
	// Remove any secondary specifier.
	if (ptPos != std::string::npos) {
		loc.resize(ptPos);
	}
	return loc;
}

TextDrawerSDL::TextDrawerSDL(Draw::DrawContext *draw): TextDrawer(draw) {
	if (!TTF_Init()) {
		ERROR_LOG(Log::G3D, "Unable to initialize SDL3_ttf");
	}

	dpiScale_ = CalculateDPIScale();

#if defined(USE_SDL3_TTF_FONTCONFIG)
	config = FcInitLoadConfigAndFonts();
#endif

	PrepareFallbackFonts(getlocale());
}

TextDrawerSDL::~TextDrawerSDL() {
	ClearCache();
	ClearFonts();

	TTF_Quit();

	#if defined(USE_SDL3_TTF_FONTCONFIG)
	FcConfigDestroy(config);
	// Don't call this - it crashes, see https://github.com/openframeworks/openFrameworks/issues/5061.
	//FcFini();
#endif
}

// If a user complains about missing characters on SDL, re-visit this!
void TextDrawerSDL::PrepareFallbackFonts(std::string_view locale) {
	auto addFallbackUnique = [&](const std::pair<std::string, int> &candidate, bool emoji) {
		for (const auto &existing : fallbackFontPaths_) {
			if (existing.path == candidate.first && existing.faceIndex == candidate.second) {
				return;
			}
		}
		fallbackFontPaths_.push_back({ candidate.first, candidate.second, emoji });
	};

#if defined(USE_SDL3_TTF_FONTCONFIG)
	FcObjectSet *os = FcObjectSetBuild (FC_FILE, FC_INDEX, (char *) 0);

	// Standard color and symbol emoji font names across Linux/FreeBSD/OpenBSD/NetBSD.
	const char *emojiNames[] = {
		"Noto Color Emoji",
		"Twemoji",
		"Twitter Color Emoji",
		"OpenMoji",
		"OpenMoji Color",
		"OpenMoji Black",
		"JoyPixels",
		"EmojiOne",
		"EmojiOne Color",
		"EmojiOne Mozilla",
		"Apple Color Emoji",
		"Segoe UI Emoji",
		"Noto Emoji",
		"Symbola",
	};

	// Query known emoji font families first with FcFontList so color emoji fonts are prioritized
	for (size_t i = 0; i < ARRAY_SIZE(emojiNames); i++) {
		FcPattern *name = FcNameParse((const FcChar8 *)emojiNames[i]);
		FcFontSet *foundFonts = FcFontList(config, name, os);
		for (int j = 0; foundFonts && j < foundFonts->nfont; ++j) {
			FcPattern *font = foundFonts->fonts[j];
			FcChar8 *path = nullptr;
			int fontIndex = 0;
			if (FcPatternGetInteger(font, FC_INDEX, 0, &fontIndex) != FcResultMatch) {
				fontIndex = 0;
			}
			if (FcPatternGetString(font, FC_FILE, 0, &path) == FcResultMatch && path) {
				addFallbackUnique(std::make_pair(std::string((const char *)path), fontIndex), true);
			}
		}
		if (foundFonts) {
			FcFontSetDestroy(foundFonts);
		}
		FcPatternDestroy(name);
	}

	// Also query fontconfig's "emoji" generic family to match any custom system-configured emoji font.
	FcPattern *emojiPattern = FcNameParse((const FcChar8 *)"emoji");
	if (emojiPattern) {
		FcConfigSubstitute(config, emojiPattern, FcMatchPattern);
		FcDefaultSubstitute(emojiPattern);
		FcResult res;
		FcPattern *matched = FcFontMatch(config, emojiPattern, &res);
		if (matched) {
			FcChar8 *path = nullptr;
			int fontIndex = 0;
			if (FcPatternGetInteger(matched, FC_INDEX, 0, &fontIndex) != FcResultMatch) {
				fontIndex = 0;
			}
			if (FcPatternGetString(matched, FC_FILE, 0, &path) == FcResultMatch && path) {
				addFallbackUnique(std::make_pair(std::string((const char *)path), fontIndex), true);
			}
			FcPatternDestroy(matched);
		}
		FcPatternDestroy(emojiPattern);
	}

	// To install the recommended Droid Sans fallback font in Ubuntu:
	// sudo apt install fonts-droid-fallback
	const char *hardcodedNames[] = {
		"Droid Sans",
		"Droid Sans Fallback",
		"Source Han Sans",
		"Noto Sans CJK",
		"Noto Sans Hebrew",
		"Noto Sans Lao",
		"Noto Sans Thai",
		"DejaVu Sans Condensed",
		"DejaVu Sans",
		"Meera Regular",
		"FreeSans",
		"Gargi",
		"KacstDigital",
		"KacstFarsi",
		"Khmer OS",
		"Paduak",
		"Paduak",
		"Jamrul",
	};

	std::vector<const char *> names;
	if (locale == "zh_CN") {
		names.push_back("Noto Sans CJK SC");
	} else if (locale == "zh_TW") {
		names.push_back("Noto Sans CJK TC");
		names.push_back("Noto Sans CJK HK");
	} else if (locale == "ja_JP") {
		names.push_back("Noto Sans CJK JP");
	} else if (locale == "ko_KR") {
		names.push_back("Noto Sans CJK KR");
	} else {
		// Let's just pick one.
		names.push_back("Noto Sans CJK JP");
	}

	// Then push all the hardcoded ones.
	for (size_t i = 0; i < ARRAY_SIZE(hardcodedNames); i++) {
		names.push_back(hardcodedNames[i]);
	}

	for (size_t i = 0; i < names.size(); i++) {
		FcPattern *name = FcNameParse((const FcChar8 *)names[i]);
		FcPatternAddInteger(name, FC_WEIGHT, FC_WEIGHT_NORMAL);
		FcPatternAddInteger(name, FC_SLANT, FC_SLANT_ROMAN);
		FcFontSet *foundFonts = FcFontList(config, name, os);

		std::vector<std::pair<std::string, int>> preferredFonts;
		std::vector<std::pair<std::string, int>> otherFonts;

		for (int j = 0; foundFonts && j < foundFonts->nfont; ++j) {
			FcPattern* font = foundFonts->fonts[j];
			FcChar8 *path;
			int fontIndex;
			int fontWeight = FC_WEIGHT_NORMAL;

			if (FcPatternGetInteger(font, FC_INDEX, 0, &fontIndex) != FcResultMatch) {
				fontIndex = 0; // The 0th face is guaranteed to exist
			}

			if (FcPatternGetInteger(font, FC_WEIGHT, 0, &fontWeight) != FcResultMatch) {
				fontWeight = FC_WEIGHT_NORMAL;
			}

			if (FcPatternGetString(font, FC_FILE, 0, &path) == FcResultMatch) {
				std::string path_str((const char*)path);
				if (fontWeight <= FC_WEIGHT_NORMAL) {
					preferredFonts.push_back(std::make_pair(path_str, fontIndex));
				} else {
					otherFonts.push_back(std::make_pair(path_str, fontIndex));
				}
			}
		}

		for (const auto &candidate : preferredFonts) {
			addFallbackUnique(candidate, false);
		}
		for (const auto &candidate : otherFonts) {
			addFallbackUnique(candidate, false);
		}

		if (foundFonts) {
			FcFontSetDestroy(foundFonts);
		}

		FcPatternDestroy(name);
	}

	if (os) {
		FcObjectSetDestroy(os);
	}
#elif PPSSPP_PLATFORM(MAC)
	const char *fontDirs[] = {
		"/System/Library/Fonts/",
		"/System/Library/Fonts/Supplemental/",
		"/Library/Fonts/"
	};

	const char *fallbackFonts[] = {
		"Apple Color Emoji.ttc",
		"Hiragino Sans GB.ttc",
		"PingFang.ttc",
		"PingFang SC.ttc",
		"PingFang TC.ttc",
		"ヒラギノ角ゴシック W4.ttc",
		"AppleGothic.ttf",
		"Arial Unicode.ttf",
	};

	for (int i = 0; i < ARRAY_SIZE(fontDirs); i++) {
		for (int j = 0; j < ARRAY_SIZE(fallbackFonts); j++) {
			Path fontPath = Path(fontDirs[i]) / fallbackFonts[j];

			if (File::Exists(fontPath)) {
				TTF_Font *openedFont = TTF_OpenFont(fontPath.ToString().c_str(), 24.0f);
				int numFaces = TTF_GetNumFontFaces(openedFont);
				for (int k = 0; k < numFaces; k++) {
					addFallbackUnique(std::make_pair(fontPath.ToString(), k), strstr(fallbackFonts[j], "Emoji") != nullptr);
				}
				TTF_CloseFont(openedFont);
			}
		}
	}
#endif

	// Direct filesystem fallback paths for Linux, FreeBSD, OpenBSD, NetBSD
	const char *standardFontPaths[] = {
		// Linux
		"/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
		"/usr/share/fonts/google-noto-color-emoji/NotoColorEmoji.ttf",
		"/usr/share/fonts/noto-color-emoji/NotoColorEmoji.ttf",
		"/usr/share/fonts/noto-emoji/NotoColorEmoji.ttf",
		"/usr/share/fonts/twemoji/Twemoji.ttf",
		"/usr/share/fonts/twemoji-color/Twemoji.ttf",
		"/usr/share/fonts/openmoji/OpenMoji-Color.ttf",
		"/usr/share/fonts/truetype/ancient-scripts/Symbola_hint.ttf",
		"/usr/share/fonts/truetype/ancient-scripts/Symbola.ttf",
		// FreeBSD
		"/usr/local/share/fonts/noto/NotoColorEmoji.ttf",
		"/usr/local/share/fonts/twemoji/Twemoji.ttf",
		"/usr/local/share/fonts/Symbola/Symbola.ttf",
		// OpenBSD
		"/usr/local/share/fonts/noto/NotoColorEmoji.ttf",
		"/usr/local/share/fonts/Symbola.ttf",
		// NetBSD
		"/usr/pkg/share/fonts/truetype/noto/NotoColorEmoji.ttf",
		"/usr/pkg/share/fonts/noto/NotoColorEmoji.ttf",
	};
	for (size_t i = 0; i < ARRAY_SIZE(standardFontPaths); i++) {
		if (File::Exists(Path(standardFontPaths[i]))) {
			addFallbackUnique(std::make_pair(std::string(standardFontPaths[i]), 0), true);
		}
	}

	const bool haveEmojiFont = std::any_of(fallbackFontPaths_.begin(), fallbackFontPaths_.end(), [](const FallbackFontPath &fallback) {
		return fallback.emoji;
	});
	if (!haveEmojiFont) {
		WARN_LOG(Log::G3D, "No emoji font found, emoji will show as boxes. Install one, like Noto Color Emoji: "
			"fonts-noto-color-emoji on Debian/Ubuntu, google-noto-color-emoji-fonts on Fedora, noto-fonts-emoji on Arch.");
	}
}

// Characters that stay in the emoji run they follow: the zero width joiner, variation selectors, the
// combining keycap, skin tone modifiers and tags.
static bool CodepointContinuesEmoji(uint32_t c) {
	return c == 0x200D || c == 0xFE0E || c == 0xFE0F || c == 0x20E3 || (c >= 0x1F3FB && c <= 0x1F3FF) || (c >= 0xE0020 && c <= 0xE007F);
}

// Premultiplies first, so the linear filter doesn't pull in the color of transparent pixels, and copies
// instead of blending, which would multiply by alpha a second time.
static void BlitPremultiplied(SDL_Surface *src, SDL_Surface *dst, const SDL_Rect &rect) {
	SDL_PremultiplySurfaceAlpha(src, false);
	SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
	if (rect.w == src->w && rect.h == src->h) {
		SDL_BlitSurface(src, nullptr, dst, &rect);
	} else {
		SDL_BlitSurfaceScaled(src, nullptr, dst, &rect, SDL_SCALEMODE_LINEAR);
	}
}

// Splits a line into runs of text, drawn with the base font and the fallbacks attached to it, and runs of
// emoji, each drawn with one emoji font and scaled down to the line height if needed.
std::vector<TextDrawerSDL::TextRun> TextDrawerSDL::LayoutLine(TTF_Font *font, std::string_view line, int *lineWidth) {
	const int ptSize = static_cast<int>(TTF_GetFontSize(font));
	std::vector<TextRun> runs;
	UTF8 decoder(line);
	bool afterJoiner = false;
	while (!decoder.end()) {
		const int start = decoder.byteIndex();
		const uint32_t c = decoder.next();
		// U+FE0F asks for emoji presentation of the character before it, U+FE0E for text.
		const uint32_t selector = decoder.end() ? 0 : decoder.peek();
		TTF_Font *emojiFont = nullptr;
		if (!runs.empty() && runs.back().emojiFont && (afterJoiner || CodepointContinuesEmoji(c))) {
			emojiFont = runs.back().emojiFont;
		} else if (selector == 0xFE0F || (selector != 0xFE0E && CodepointIsProbablyEmoji(c))) {
			emojiFont = FindEmojiFont(c, ptSize);
		} else if (!TTF_FontHasGlyph(font, c) && FindFallbackFont(c, false, ptSize) < 0) {
			// No font that can be attached has it, but an emoji font might.
			emojiFont = FindEmojiFont(c, ptSize);
		}
		afterJoiner = c == 0x200D;
		const size_t size = decoder.byteIndex() - start;
		if (!runs.empty() && runs.back().emojiFont == emojiFont) {
			runs.back().text = std::string_view(runs.back().text.data(), runs.back().text.size() + size);
		} else {
			runs.push_back({ line.substr(start, size), emojiFont, 0 });
		}
	}

	const int baseHeight = TTF_GetFontHeight(font);
	*lineWidth = 0;
	std::string temp;
	for (TextRun &run : runs) {
		temp = run.text;  // zero-terminate.
		int height = 0;
		TTF_GetStringSize(run.emojiFont ? run.emojiFont : font, temp.c_str(), temp.size(), &run.width, &height);
		if (run.emojiFont) {
			const int emojiHeight = TTF_GetFontHeight(run.emojiFont);
			if (emojiHeight > baseHeight && baseHeight > 0) {
				run.width = (run.width * baseHeight + emojiHeight / 2) / emojiHeight;
			}
		}
		*lineWidth += run.width;
	}
	return runs;
}

TTF_Font *TextDrawerSDL::GetFallbackFont(int index, int ptSize) {
	auto iter = fallbackFonts_.find(std::make_pair(index, ptSize));
	if (iter != fallbackFonts_.end()) {
		return iter->second;
	}
	const FallbackFontPath &fallback = fallbackFontPaths_[index];
	TTF_Font *font = OpenFontFace(fallback.path, static_cast<float>(ptSize), fallback.faceIndex);
	fallbackFonts_[std::make_pair(index, ptSize)] = font;
	return font;
}

// Fixed-size bitmap fonts (like Noto Color Emoji's 128px strikes) aren't attachable: SDL3_ttf would draw
// their glyphs unscaled and make the whole line that tall. They're only used for emoji runs, scaled down.
bool TextDrawerSDL::FallbackHasGlyph(int index, uint32_t glyph, bool attachable, int ptSize) {
	// Which glyphs a face has doesn't depend on the size, so any open copy can answer.
	auto iter = fallbackFonts_.lower_bound(std::make_pair(index, 0));
	if (iter != fallbackFonts_.end() && iter->first.first == index) {
		TTF_Font *font = iter->second;
		return font && TTF_FontHasGlyph(font, glyph) && (!attachable || TTF_FontIsScalable(font));
	}

	const FallbackFontPath &fallback = fallbackFontPaths_[index];
	TTF_Font *font = OpenFontFace(fallback.path, static_cast<float>(ptSize), fallback.faceIndex);
	if (!font) {
		return false;
	}
	if (TTF_FontHasGlyph(font, glyph) && (!attachable || TTF_FontIsScalable(font))) {
		fallbackFonts_[std::make_pair(index, ptSize)] = font;
		return true;
	}
	// Don't keep faces open that we have no use for yet, there can be dozens.
	TTF_CloseFont(font);
	return false;
}

// Returns an index in fallbackFontPaths_, or -1. Emoji fonts are tried first for emoji, and last for
// everything else, so that symbols they also have come from a font that can be attached.
int TextDrawerSDL::FindFallbackFont(uint32_t glyph, bool emoji, int ptSize) {
	const uint32_t key = glyph * 2 + (emoji ? 1 : 0);
	auto iter = glyphFallbackFontIndex_.find(key);
	if (iter != glyphFallbackFontIndex_.end()) {
		return iter->second;
	}

	int found = -1;
	for (int pass = 0; pass < 2 && found < 0; pass++) {
		for (int i = 0; i < (int)fallbackFontPaths_.size(); i++) {
			if ((fallbackFontPaths_[i].emoji == emoji) != (pass == 0)) {
				continue;
			}
			if (FallbackHasGlyph(i, glyph, !emoji, ptSize)) {
				found = i;
				break;
			}
		}
	}

	glyphFallbackFontIndex_[key] = found;
	return found;
}

TTF_Font *TextDrawerSDL::FindEmojiFont(uint32_t emojiGlyph, int ptSize) {
	int index = FindFallbackFont(emojiGlyph, true, ptSize);
	return index >= 0 ? GetFallbackFont(index, ptSize) : nullptr;
}

void TextDrawerSDL::AttachFallbackFontsForString(TTF_Font *font, std::string_view text) {
	if (!font || text.empty()) {
		return;
	}

	const int ptSize = static_cast<int>(TTF_GetFontSize(font));
	std::set<int> &attached = attachedFallbackIndices_[font];
	UTF8 decoder(text);
	while (!decoder.end()) {
		const uint32_t glyph = decoder.next();
		// Emoji are mostly drawn in runs of their own (see LayoutLine), but U+FE0E can put them in a text run.
		if (TTF_FontHasGlyph(font, glyph)) {
			continue;
		}
		bool covered = false;
		for (int index : attached) {
			TTF_Font *fallback = GetFallbackFont(index, ptSize);
			if (fallback && TTF_FontHasGlyph(fallback, glyph)) {
				covered = true;
				break;
			}
		}
		if (covered) {
			continue;
		}
		const int index = FindFallbackFont(glyph, false, ptSize);
		if (index < 0) {
			continue;
		}
		TTF_Font *fallback = GetFallbackFont(index, ptSize);
		if (fallback && attached.insert(index).second) {
			TTF_AddFallbackFont(font, fallback);
		}
	}
}

void TextDrawerSDL::EnsureFont(const FontStyle &style) {
	if (fontMap_.find(style) != fontMap_.end()) {
		return;
	}

	TTF_Font *font = nullptr;
	uint8_t *fileData = nullptr;
	std::string useFont = GetFilenameForFontStyle(style) + ".ttf";
	const int ptSize = static_cast<int>(style.sizePts / dpiScale_ * 1.25f);
	DEBUG_LOG(Log::G3D, "Loading SDL font '%s' from VFS at size %d pts", useFont.c_str(), ptSize);

	size_t fileSz;
	fileData = g_VFS.ReadFile(useFont.c_str(), &fileSz);
	if (fileData) {
		SDL_IOStream *rw = SDL_IOFromConstMem(fileData, fileSz);
		DEBUG_LOG(Log::G3D, "Opened font from RW: '%p' '%d'", fileData, (int)fileSz);
		font = TTF_OpenFontIO(rw, true, static_cast<float>(ptSize));
		if (!font) {
			ERROR_LOG(Log::G3D, "Failed to load font from asset file: '%s'", useFont.c_str());
		}
	} else {
		ERROR_LOG(Log::G3D, "Failed to load font file %s from VFS", useFont.c_str());
	}

	fontMap_[style] = std::make_pair(font, fileData);
}

void TextDrawerSDL::MeasureStringInternal(const FontStyle &style, std::string_view str, float *w, float *h) {
	*w = 1.0f;
	*h = 1.0f;
	auto iter = fontMap_.find(style);
	if (iter == fontMap_.end()) {
		return;
	}
	TTF_Font *font = iter->second.first;
	if (!font) {
		return;
	}

	std::string reorderedStr;
	const bool reordered = ReorderBidi(str, &reorderedStr);
	if (reordered) {
		str = reorderedStr;
	}

	AttachFallbackFontsForString(font, str);
	SetBidiDirection(font, reordered);

	std::vector<std::string_view> lines;
	SplitString(str, '\n', lines);

	// DrawStringBitmap lays out every line of a string with emoji in it as runs, a line height apart.
	const bool hasEmoji = AnyEmojiInString(str, str.size());
	const int baseHeight = TTF_GetFontHeight(font);
	int extW = 0, extH = 0;
	std::string temp;
	for (auto line : lines) {
		int width = 0;
		int height = 0;
		if (line.empty()) {
			// Measure empty lines as if it was a space.
			line = " ";
		}
		if (hasEmoji) {
			LayoutLine(font, line, &width);
			height = baseHeight;
		} else {
			temp = line;  // zero-terminate, ugh.
			TTF_GetStringSize(font, temp.c_str(), temp.size(), &width, &height);
		}
		extW = std::max(extW, width);
		extH += height;
	}

	*w = (float)extW;
	*h = (float)extH;
}

bool TextDrawerSDL::DrawStringBitmap(const FontStyle &style, std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) {
	if (str.empty()) {
		bitmapData.clear();
		return false;
	}

	auto fontIter = fontMap_.find(style);
	if (fontIter == fontMap_.end()) {
		ERROR_LOG(Log::G3D, "Font style not in map");
		return false;
	}

	TTF_Font *font = fontIter->second.first;
	if (!font) {
		return false;
	}

	std::string processedStr;
	const bool reordered = ReorderBidi(str, &processedStr);
	if (!reordered) {
		processedStr = str;
	}

	// If a string includes only newlines, SDL3_ttf will refuse to render it
	// thinking it is empty. Add a space to avoid that.
	bool isAllNewline = processedStr.find_first_not_of('\n') == std::string::npos;
	if (isAllNewline) {
		processedStr.push_back(' ');
	}

	AttachFallbackFontsForString(font, processedStr);
	SetBidiDirection(font, reordered);

	const int baseHeight = TTF_GetFontHeight(font);
	SDL_Color fgColor = { 0xFF, 0xFF, 0xFF, 0xFF };
	SDL_Surface *text = nullptr;
	bool premultiplied = false;

	if (AnyEmojiInString(processedStr, processedStr.size())) {
		std::vector<std::string_view> lines;
		SplitString(processedStr, '\n', lines);

		std::vector<std::vector<TextRun>> lineRuns(lines.size());
		std::vector<int> lineWidths(lines.size());
		int maxLineWidth = 1;
		for (size_t i = 0; i < lines.size(); i++) {
			lineRuns[i] = LayoutLine(font, lines[i].empty() ? " " : lines[i], &lineWidths[i]);
			maxLineWidth = std::max(maxLineWidth, lineWidths[i]);
		}
		const int totalHeight = std::max(1, (int)lines.size() * baseHeight);

		text = SDL_CreateSurface(maxLineWidth, totalHeight, SDL_PIXELFORMAT_ARGB8888);
		if (text) {
			SDL_ClearSurface(text, 0.0f, 0.0f, 0.0f, 0.0f);
			premultiplied = true;
			for (size_t i = 0; i < lines.size(); i++) {
				int x = 0;
				if (align & ALIGN_HCENTER) {
					x = (maxLineWidth - lineWidths[i]) / 2;
				} else if (align & ALIGN_RIGHT) {
					x = maxLineWidth - lineWidths[i];
				}
				const int y = (int)i * baseHeight;
				for (const TextRun &run : lineRuns[i]) {
					std::string temp(run.text);
					SDL_Surface *s = TTF_RenderText_Blended(run.emojiFont ? run.emojiFont : font, temp.c_str(), temp.size(), fgColor);
					if (s) {
						SDL_Rect rect = { x, y, s->w, s->h };
						if (run.emojiFont && s->h > baseHeight) {
							// LayoutLine scaled the width the same way.
							rect.w = run.width;
							rect.h = baseHeight;
						}
						BlitPremultiplied(s, text, rect);
						SDL_DestroySurface(s);
					}
					x += run.width;
				}
			}
		}
	} else {
		if (align & ALIGN_HCENTER)
			TTF_SetFontWrapAlignment(font, TTF_HORIZONTAL_ALIGN_CENTER);
		else if (align & ALIGN_RIGHT)
			TTF_SetFontWrapAlignment(font, TTF_HORIZONTAL_ALIGN_RIGHT);
		else
			TTF_SetFontWrapAlignment(font, TTF_HORIZONTAL_ALIGN_LEFT);

		text = TTF_RenderText_Blended_Wrapped(font, processedStr.c_str(), processedStr.size(), fgColor, 0);
	}

	if (!text) {
		ERROR_LOG(Log::G3D, "Failed to render SDL3_ttf text: %s", SDL_GetError());
		return false;
	}
	SDL_LockSurface(text);

	entry.texture = nullptr;
	// Each row of pixel needs to be aligned to 8 bytes (= 2 pixels), or else
	// graphics corruption occurs. Made it 16-byte aligned just to be sure.
	entry.bmWidth = entry.width = (text->w + 3) & ~3;
	entry.bmHeight = entry.height = text->h;
	entry.lastUsedFrame = frameCount_;

	uint32_t *imageData = (uint32_t *)text->pixels;
	uint32_t pitch = text->pitch / sizeof(uint32_t);

	if (texFormat == Draw::DataFormat::R8G8B8A8_UNORM || texFormat == Draw::DataFormat::B8G8R8A8_UNORM) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint32_t));
		bool swap = texFormat == Draw::DataFormat::R8G8B8A8_UNORM;
		uint32_t *bitmapData32 = (uint32_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				if (x < text->w) {
					uint32_t v = imageData[pitch * y + x];
					if (fullColor) {
						if (swap) {
							v = (v & 0xFF00FF00) | ((v >> 16) & 0xFF) | ((v << 16) & 0xFF0000);
						}
						bitmapData32[entry.bmWidth * y + x] = premultiplied ? v : RGBAToPremul8888(v);
					} else {
						bitmapData32[entry.bmWidth * y + x] = AlphaToPremul8888(v >> 24);
					}
				} else {
					bitmapData32[entry.bmWidth * y + x] = 0;
				}
			}
		}
	} else if (texFormat == Draw::DataFormat::B4G4R4A4_UNORM_PACK16 || texFormat == Draw::DataFormat::R4G4B4A4_UNORM_PACK16) {
		_dbg_assert_(!fullColor);
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];

		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				const uint64_t index = entry.bmWidth * y + x;
				const u16 alpha = (x < text->w) ? AlphaToPremul4444(imageData[pitch * y + x] >> 24) : 0;
				bitmapData16[index] = alpha;
			}
		}
	} else if (texFormat == Draw::DataFormat::R8_UNORM) {
		_dbg_assert_(!fullColor);
		bitmapData.resize(entry.bmWidth * entry.bmHeight);
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint64_t index = entry.bmWidth * y + x;
				bitmapData[index] = (x < text->w) ? (imageData[pitch * y + x] >> 24) : 0;
			}
		}
	} else {
		_assert_msg_(false, "Bad TextDrawer format");
	}

	SDL_UnlockSurface(text);
	SDL_DestroySurface(text);
	return true;
}

void TextDrawerSDL::ClearFonts() {
	for (auto iter : fontMap_) {
		if (iter.second.first) {
			TTF_ClearFallbackFonts(iter.second.first);
			TTF_CloseFont(iter.second.first);
		}
		delete[] iter.second.second;
	}
	for (auto iter : fallbackFonts_) {
		if (iter.second) {
			TTF_CloseFont(iter.second);
		}
	}

	// We wipe all the maps, including fontMap_.
	fontMap_.clear();
	fallbackFonts_.clear();
	glyphFallbackFontIndex_.clear();
	attachedFallbackIndices_.clear();
}

#endif
