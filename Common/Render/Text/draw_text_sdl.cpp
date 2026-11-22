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
	fallbackFonts_.assign(fallbackFontPaths_.size(), nullptr);
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
	auto addFallbackUnique = [&](const std::pair<std::string, int> &candidate) {
		for (const auto &existing : fallbackFontPaths_) {
			if (existing.first == candidate.first && existing.second == candidate.second) {
				return;
			}
		}
		fallbackFontPaths_.push_back(candidate);
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
				addFallbackUnique(std::make_pair(std::string((const char *)path), fontIndex));
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
				addFallbackUnique(std::make_pair(std::string((const char *)path), fontIndex));
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
			addFallbackUnique(candidate);
		}
		for (const auto &candidate : otherFonts) {
			addFallbackUnique(candidate);
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
					addFallbackUnique(std::make_pair(fontPath.ToString(), k));
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
			addFallbackUnique(std::make_pair(std::string(standardFontPaths[i]), 0));
		}
	}
}

namespace {

struct TextRun {
	std::string text;
	bool isEmoji;
};

static bool CodepointIsEmoji(uint32_t c) {
	if (c > 0xFFFF) {
		return true;
	}
	if ((c >= 0x2600 && c <= 0x27BF) ||
		(c >= 0x2300 && c <= 0x23FF) ||
		(c >= 0x2B50 && c <= 0x2B55) ||
		c == 0x200D ||
		(c >= 0xFE0E && c <= 0xFE0F) ||
		c == 0x20E3) {
		return true;
	}
	return false;
}

static uint32_t GetFirstCodepoint(std::string_view str) {
	if (str.empty())
		return 0;
	const char *p = str.data();
	unsigned char c = (unsigned char)p[0];
	uint32_t cp = 0;
	int extra = 0;
	if (c < 0x80)
		cp = c;
	else if ((c & 0xE0) == 0xC0) {
		cp = c & 0x1F;
		extra = 1;
	} else if ((c & 0xF0) == 0xE0) {
		cp = c & 0x0F;
		extra = 2;
	} else if ((c & 0xF8) == 0xF0) {
		cp = c & 0x07;
		extra = 3;
	}
	for (int k = 1; k <= extra && (size_t)k < str.size(); ++k) {
		cp = (cp << 6) | ((unsigned char)p[k] & 0x3F);
	}
	return cp;
}

static bool LineHasEmoji(std::string_view line) {
	const char *p = line.data();
	size_t len = line.size();
	size_t i = 0;
	while (i < len) {
		unsigned char c = (unsigned char)p[i++];
		uint32_t cp = 0;
		int extra = 0;
		if (c < 0x80)
			cp = c;
		else if ((c & 0xE0) == 0xC0) {
			cp = c & 0x1F;
			extra = 1;
		} else if ((c & 0xF0) == 0xE0) {
			cp = c & 0x0F;
			extra = 2;
		} else if ((c & 0xF8) == 0xF0) {
			cp = c & 0x07;
			extra = 3;
		}
		for (int k = 0; k < extra && i < len; ++k) {
			cp = (cp << 6) | ((unsigned char)p[i++] & 0x3F);
		}
		if (CodepointIsEmoji(cp))
			return true;
	}
	return false;
}

static std::vector<TextRun> SegmentLine(std::string_view line) {
	std::vector<TextRun> runs;
	if (line.empty())
		return runs;
	const char *p = line.data();
	size_t len = line.size();
	size_t i = 0;
	TextRun currentRun;
	bool hasCurrent = false;

	while (i < len) {
		size_t start = i;
		unsigned char c = (unsigned char)p[i++];
		uint32_t cp = 0;
		int extra = 0;
		if (c < 0x80)
			cp = c;
		else if ((c & 0xE0) == 0xC0) {
			cp = c & 0x1F;
			extra = 1;
		} else if ((c & 0xF0) == 0xE0) {
			cp = c & 0x0F;
			extra = 2;
		} else if ((c & 0xF8) == 0xF0) {
			cp = c & 0x07;
			extra = 3;
		}
		for (int k = 0; k < extra && i < len; ++k) {
			cp = (cp << 6) | ((unsigned char)p[i++] & 0x3F);
		}
		bool isEmoji = CodepointIsEmoji(cp);
		std::string glyphStr(p + start, i - start);

		if (!hasCurrent) {
			currentRun.text = glyphStr;
			currentRun.isEmoji = isEmoji;
			hasCurrent = true;
		} else if (currentRun.isEmoji == isEmoji) {
			currentRun.text += glyphStr;
		} else {
			runs.push_back(currentRun);
			currentRun.text = glyphStr;
			currentRun.isEmoji = isEmoji;
		}
	}
	if (hasCurrent) {
		runs.push_back(currentRun);
	}
	return runs;
}

} // namespace

uint32_t TextDrawerSDL::CheckMissingGlyph(TTF_Font *font, std::string_view text) {
	if (!font) {
		return 0;
	}
	UTF8 utf8Decoded(text);

	auto attachedIter = attachedFallbackIndices_.find(font);

	while (!utf8Decoded.end()) {
		uint32_t glyph = utf8Decoded.next();
		if (TTF_FontHasGlyph(font, glyph)) {
			continue;
		}

		// Don't search fallbacks to attach to the base font for emoji glyphs,
		// as oversized emoji fonts are scaled and rendered separately.
		if (CodepointIsEmoji(glyph)) {
			continue;
		}

		auto cacheIter = glyphFallbackFontIndex_.find(glyph);
		if (cacheIter != glyphFallbackFontIndex_.end() && cacheIter->second == -1) {
			// Already known that no fallback font can satisfy this glyph.
			continue;
		}

		bool foundInAttached = false;
		if (attachedIter != attachedFallbackIndices_.end()) {
			for (int idx : attachedIter->second) {
				if (idx >= 0 && idx < (int)fallbackFonts_.size() && fallbackFonts_[idx]) {
					if (TTF_FontHasGlyph(fallbackFonts_[idx], glyph)) {
						foundInAttached = true;
						break;
					}
				}
			}
		}
		if (!foundInAttached) {
			return glyph;
		}
	}

	return 0;
}

// If this returns >= 0, the nth font in fallbackFonts_ can be used as a fallback.
int TextDrawerSDL::FindFallbackFonts(uint32_t missingGlyph, int ptSize) {
	auto iter = glyphFallbackFontIndex_.find(missingGlyph);
	if (iter != glyphFallbackFontIndex_.end()) {
		return iter->second;
	}

	// First, check already opened fallback fonts.
	for (size_t i = 0; i < fallbackFonts_.size(); i++) {
		TTF_Font *fallbackFont = fallbackFonts_[i];
		if (fallbackFont && TTF_FontHasGlyph(fallbackFont, missingGlyph)) {
			glyphFallbackFontIndex_[missingGlyph] = (int)i;
			return (int)i;
		}
	}

	// If none of the loaded fonts can handle it, load more fonts.
	for (size_t i = 0; i < fallbackFontPaths_.size(); i++) {
		if (fallbackFonts_[i] == nullptr) {
			const std::string &fontPath = fallbackFontPaths_[i].first;
			int faceIndex = fallbackFontPaths_[i].second;

			TTF_Font *font = OpenFontFace(fontPath, static_cast<float>(ptSize), faceIndex);
			if (font) {
				fallbackFonts_[i] = font;
				if (TTF_FontHasGlyph(font, missingGlyph)) {
					glyphFallbackFontIndex_[missingGlyph] = (int)i;
					return (int)i;
				}
			}
		}
	}

	// Not found at all? Let's remember that for this glyph.
	glyphFallbackFontIndex_[missingGlyph] = -1;
	return -1;
}

TTF_Font *TextDrawerSDL::FindEmojiFont(uint32_t emojiGlyph, int ptSize) {
	int idx = FindFallbackFonts(emojiGlyph, ptSize);
	if (idx >= 0 && idx < (int)fallbackFonts_.size()) {
		TTF_Font *f = fallbackFonts_[idx];
		if (f && (int)TTF_GetFontSize(f) != ptSize) {
			TTF_SetFontSize(f, (float)ptSize);
		}
		return f;
	}
	return nullptr;
}

void TextDrawerSDL::AttachFallbackFontsForString(TTF_Font *font, std::string_view text, int ptSize) {
	if (!font || text.empty()) {
		return;
	}

	const int baseHeight = TTF_GetFontHeight(font);
	std::set<uint32_t> triedGlyphs;
	while (uint32_t missingGlyph = CheckMissingGlyph(font, text)) {
		if (!triedGlyphs.insert(missingGlyph).second) {
			break;
		}
		int fallbackIndex = FindFallbackFonts(missingGlyph, ptSize);
		if (fallbackIndex < 0 || fallbackIndex >= (int)fallbackFonts_.size()) {
			continue;
		}
		TTF_Font *fallbackFont = fallbackFonts_[fallbackIndex];
		if (!fallbackFont) {
			continue;
		}
		if ((int)TTF_GetFontSize(fallbackFont) != ptSize) {
			TTF_SetFontSize(fallbackFont, (float)ptSize);
		}
		int fallbackHeight = TTF_GetFontHeight(fallbackFont);
		// If the fallback font is oversized (e.g. fixed-size bitmap strikes like Noto Color Emoji with height 128),
		// do NOT attach it directly to the base font. Attaching causes SDL3_ttf to expand the entire line and
		// render 128px unscaled glyphs. Instead, oversized emoji fonts are scaled and rendered separately.
		if (fallbackHeight > 0 && baseHeight > 0 && fallbackHeight > baseHeight * 1.5f) {
			continue;
		}
		if (attachedFallbackIndices_[font].insert(fallbackIndex).second) {
			TTF_AddFallbackFont(font, fallbackFont);
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

	const int ptSize = static_cast<int>(style.sizePts / dpiScale_ * 1.25f);
	AttachFallbackFontsForString(font, str, ptSize);
	SetBidiDirection(font, reordered);

	std::vector<std::string_view> lines;
	SplitString(str, '\n', lines);

	const int baseHeight = TTF_GetFontHeight(font);
	int extW = 0, extH = 0;
	std::string temp;

	for (auto line : lines) {
		if (line.empty()) {
			line = " ";
		}

		if (LineHasEmoji(line)) {
			auto runs = SegmentLine(line);
			int lineWidth = 0;
			for (const auto &run : runs) {
				int rw = 0, rh = 0;
				if (!run.isEmoji) {
					temp = run.text;
					TTF_GetStringSize(font, temp.c_str(), temp.size(), &rw, &rh);
					lineWidth += rw;
				} else {
					uint32_t firstCp = GetFirstCodepoint(run.text);
					TTF_Font *emojiFont = FindEmojiFont(firstCp, ptSize);
					if (emojiFont) {
						temp = run.text;
						TTF_GetStringSize(emojiFont, temp.c_str(), temp.size(), &rw, &rh);
						int emojiH = TTF_GetFontHeight(emojiFont);
						if (emojiH > baseHeight && baseHeight > 0) {
							rw = (rw * baseHeight + emojiH / 2) / emojiH;
						}
					} else {
						temp = run.text;
						TTF_GetStringSize(font, temp.c_str(), temp.size(), &rw, &rh);
					}
					lineWidth += rw;
				}
			}
			if (lineWidth > extW)
				extW = lineWidth;
			extH += baseHeight;
		} else {
			temp = line;
			int width = 0;
			int height = 0;
			TTF_GetStringSize(font, temp.c_str(), temp.size(), &width, &height);
			if (width > extW)
				extW = width;
			extH += height;
		}
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

	const int ptSize = static_cast<int>(TTF_GetFontSize(font));
	AttachFallbackFontsForString(font, processedStr, ptSize);
	SetBidiDirection(font, reordered);

	const int baseHeight = TTF_GetFontHeight(font);
	SDL_Color fgColor = { 0xFF, 0xFF, 0xFF, 0xFF };
	SDL_Surface *text = nullptr;

	if (LineHasEmoji(processedStr)) {
		std::vector<std::string_view> lines;
		SplitString(processedStr, '\n', lines);

		struct MeasuredLine {
			std::vector<TextRun> runs;
			std::vector<int> runWidths;
			int totalWidth = 0;
		};
		std::vector<MeasuredLine> measuredLines(lines.size());
		int maxLineWidth = 0;

		for (size_t li = 0; li < lines.size(); ++li) {
			auto line = lines[li];
			if (line.empty()) {
				line = " ";
			}
			auto runs = SegmentLine(line);
			measuredLines[li].runs = runs;
			for (const auto &run : runs) {
				int rw = 0, rh = 0;
				if (!run.isEmoji) {
					std::string temp(run.text);
					TTF_GetStringSize(font, temp.c_str(), temp.size(), &rw, &rh);
				} else {
					uint32_t firstCp = GetFirstCodepoint(run.text);
					TTF_Font *emojiFont = FindEmojiFont(firstCp, ptSize);
					if (emojiFont) {
						std::string temp(run.text);
						TTF_GetStringSize(emojiFont, temp.c_str(), temp.size(), &rw, &rh);
						int emojiH = TTF_GetFontHeight(emojiFont);
						if (emojiH > baseHeight && baseHeight > 0) {
							rw = (rw * baseHeight + emojiH / 2) / emojiH;
						}
					} else {
						std::string temp(run.text);
						TTF_GetStringSize(font, temp.c_str(), temp.size(), &rw, &rh);
					}
				}
				measuredLines[li].runWidths.push_back(rw);
				measuredLines[li].totalWidth += rw;
			}
			if (measuredLines[li].totalWidth > maxLineWidth) {
				maxLineWidth = measuredLines[li].totalWidth;
			}
		}

		if (maxLineWidth <= 0)
			maxLineWidth = 1;
		int totalHeight = (int)lines.size() * baseHeight;
		if (totalHeight <= 0)
			totalHeight = 1;

		text = SDL_CreateSurface(maxLineWidth, totalHeight, SDL_PIXELFORMAT_ARGB8888);
		if (text) {
			SDL_ClearSurface(text, 0.0f, 0.0f, 0.0f, 0.0f);
			for (size_t li = 0; li < lines.size(); ++li) {
				const auto &mline = measuredLines[li];
				int startX = 0;
				if (align & ALIGN_HCENTER) {
					startX = (maxLineWidth - mline.totalWidth) / 2;
				} else if (align & ALIGN_RIGHT) {
					startX = maxLineWidth - mline.totalWidth;
				}
				int curX = startX;
				int curY = (int)li * baseHeight;

				for (size_t ri = 0; ri < mline.runs.size(); ++ri) {
					const auto &run = mline.runs[ri];
					int rw = mline.runWidths[ri];
					if (!run.isEmoji) {
						std::string temp(run.text);
						SDL_Surface *s = TTF_RenderText_Blended(font, temp.c_str(), temp.size(), fgColor);
						if (s) {
							SDL_Rect dst = { curX, curY, s->w, s->h };
							SDL_BlitSurface(s, NULL, text, &dst);
							curX += s->w;
							SDL_DestroySurface(s);
						} else {
							curX += rw;
						}
					} else {
						uint32_t firstCp = GetFirstCodepoint(run.text);
						TTF_Font *emojiFont = FindEmojiFont(firstCp, ptSize);
						if (emojiFont) {
							std::string temp(run.text);
							SDL_Surface *s = TTF_RenderText_Blended(emojiFont, temp.c_str(), temp.size(), fgColor);
							if (s) {
								SDL_Rect dst = { curX, curY, rw, baseHeight };
								SDL_BlitSurfaceScaled(s, NULL, text, &dst, SDL_SCALEMODE_LINEAR);
								curX += rw;
								SDL_DestroySurface(s);
							} else {
								curX += rw;
							}
						} else {
							std::string temp(run.text);
							SDL_Surface *s = TTF_RenderText_Blended(font, temp.c_str(), temp.size(), fgColor);
							if (s) {
								SDL_Rect dst = { curX, curY, s->w, s->h };
								SDL_BlitSurface(s, NULL, text, &dst);
								curX += s->w;
								SDL_DestroySurface(s);
							} else {
								curX += rw;
							}
						}
					}
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
						if (swap)
							v = (v & 0xFF00FF00) | ((v >> 16) & 0xFF) | ((v << 16) & 0xFF0000);
						bitmapData32[entry.bmWidth * y + x] = RGBAToPremul8888(v);
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
		if (iter) {
			TTF_CloseFont(iter);
		}
	}

	// We wipe all the maps, including fontMap_.
	fontMap_.clear();
	fallbackFonts_.assign(fallbackFontPaths_.size(), nullptr);
	glyphFallbackFontIndex_.clear();
	attachedFallbackIndices_.clear();
}

#endif
