#include "sceFont.h"

#include "base/timeutil.h"

#include <cmath>
#include <vector>
#include <map>
#include <algorithm>

#include "Common/ChunkFile.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/FileSystems/FileSystem.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/HLE/sceKernel.h"
#include "Core/Font/PGF.h"
#include "Core/HLE/sceKernelThread.h"

enum {
	ERROR_FONT_OUT_OF_MEMORY        = 0x80460001,
	ERROR_FONT_INVALID_LIBID        = 0x80460002,
	ERROR_FONT_INVALID_PARAMETER    = 0x80460003,
	ERROR_FONT_HANDLER_OPEN_FAILED  = 0x80460005,
	ERROR_FONT_TOO_MANY_OPEN_FONTS  = 0x80460009,
	ERROR_FONT_INVALID_FONT_DATA    = 0x8046000a,
};

enum {
	FONT_IS_CLOSED = 0,
	FONT_IS_OPEN   = 1,
};

// Actions
static int actionPostAllocCallback;
static int actionPostOpenCallback;

// Monster Hunter sequence:
// 36:46:998 c:\dev\ppsspp\core\hle\scefont.cpp:469 E[HLE]: sceFontNewLib 89ad4a0, 9fff5cc
// 36:46:998 c:\dev\ppsspp\core\hle\scefont.cpp:699 E[HLE]: UNIMPL sceFontGetNumFontList 1, 9fff5cc
// 36:46:998 c:\dev\ppsspp\core\hle\scefont.cpp:526 E[HLE]: sceFontFindOptimumFont 1, 9fff524, 9fff5cc
// 36:46:999 c:\dev\ppsspp\core\hle\scefont.cpp:490 E[HLE]: sceFontOpenFont 1, 1, 0, 9fff5cc
// 36:46:999 c:\dev\ppsspp\core\hle\scefont.cpp:542 E[HLE]: sceFontGetFontInfo 1, 997140c

typedef u32 FontLibraryHandle;
typedef u32 FontHandle;

struct FontNewLibParams {
	u32_le userDataAddr;
	u32_le numFonts;
	u32_le cacheDataAddr;

	// Driver callbacks.
	u32_le allocFuncAddr;
	u32_le freeFuncAddr;
	u32_le openFuncAddr;
	u32_le closeFuncAddr;
	u32_le readFuncAddr;
	u32_le seekFuncAddr;
	u32_le errorFuncAddr;
	u32_le ioFinishFuncAddr;
};

struct FontRegistryEntry {
	int hSize;
	int vSize;
	int hResolution;
	int vResolution;
	int extraAttributes;
	int weight;
	int familyCode;
	int style;
	int styleSub;
	int languageCode;
	int regionCode;
	int countryCode;
	const char *fileName;
	const char *fontName;
	int expireDate;
	int shadow_option;
};

static const FontRegistryEntry fontRegistry[] = {
	{ 0x288, 0x288, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SANS_SERIF, FONT_STYLE_DB, 0, FONT_LANGUAGE_CHINESE, 0, 1, "zh_gb.pgf", "FTT-NewRodin Pro DB", 0, 0 },
	{ 0x288, 0x288, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SANS_SERIF, FONT_STYLE_DB, 0, FONT_LANGUAGE_JAPANESE, 0, 1, "jpn0.pgf", "FTT-NewRodin Pro DB", 0, 0 },
	{0x288, 0x288, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SANS_SERIF, FONT_STYLE_REGULAR, 0, FONT_LANGUAGE_LATIN, 0, 1, "ltn0.pgf", "FTT-NewRodin Pro Latin", 0, 0},
	{0x288, 0x288, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SERIF, FONT_STYLE_REGULAR, 0, FONT_LANGUAGE_LATIN, 0, 1, "ltn1.pgf", "FTT-Matisse Pro Latin", 0, 0},
	{0x288, 0x288, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SANS_SERIF, FONT_STYLE_ITALIC, 0, FONT_LANGUAGE_LATIN, 0, 1, "ltn2.pgf", "FTT-NewRodin Pro Latin", 0, 0},
	{0x288, 0x288, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SERIF, FONT_STYLE_ITALIC, 0, FONT_LANGUAGE_LATIN, 0, 1, "ltn3.pgf", "FTT-Matisse Pro Latin", 0, 0},
	{0x288, 0x288, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SANS_SERIF, FONT_STYLE_BOLD, 0, FONT_LANGUAGE_LATIN, 0, 1, "ltn4.pgf", "FTT-NewRodin Pro Latin", 0, 0},
	{0x288, 0x288, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SERIF, FONT_STYLE_BOLD, 0, FONT_LANGUAGE_LATIN, 0, 1, "ltn5.pgf", "FTT-Matisse Pro Latin", 0, 0},
	{0x288, 0x288, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SANS_SERIF, FONT_STYLE_BOLD_ITALIC, 0, FONT_LANGUAGE_LATIN, 0, 1, "ltn6.pgf", "FTT-NewRodin Pro Latin", 0, 0},
	{0x288, 0x288, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SERIF, FONT_STYLE_BOLD_ITALIC, 0, FONT_LANGUAGE_LATIN, 0, 1, "ltn7.pgf", "FTT-Matisse Pro Latin", 0, 0},
	{0x1c0, 0x1c0, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SANS_SERIF, FONT_STYLE_REGULAR, 0, FONT_LANGUAGE_LATIN, 0, 1, "ltn8.pgf", "FTT-NewRodin Pro Latin", 0, 0},
	{0x1c0, 0x1c0, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SERIF, FONT_STYLE_REGULAR, 0, FONT_LANGUAGE_LATIN, 0, 1, "ltn9.pgf", "FTT-Matisse Pro Latin", 0, 0},
	{0x1c0, 0x1c0, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SANS_SERIF, FONT_STYLE_ITALIC, 0, FONT_LANGUAGE_LATIN, 0, 1, "ltn10.pgf", "FTT-NewRodin Pro Latin", 0, 0},
	{0x1c0, 0x1c0, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SERIF, FONT_STYLE_ITALIC, 0, FONT_LANGUAGE_LATIN, 0, 1, "ltn11.pgf", "FTT-Matisse Pro Latin", 0, 0},
	{0x1c0, 0x1c0, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SANS_SERIF, FONT_STYLE_BOLD, 0, FONT_LANGUAGE_LATIN, 0, 1, "ltn12.pgf", "FTT-NewRodin Pro Latin", 0, 0},
	{0x1c0, 0x1c0, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SERIF, FONT_STYLE_BOLD, 0, FONT_LANGUAGE_LATIN, 0, 1, "ltn13.pgf", "FTT-Matisse Pro Latin", 0, 0},
	{0x1c0, 0x1c0, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SANS_SERIF, FONT_STYLE_BOLD_ITALIC, 0, FONT_LANGUAGE_LATIN, 0, 1, "ltn14.pgf", "FTT-NewRodin Pro Latin", 0, 0},
	{0x1c0, 0x1c0, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SERIF, FONT_STYLE_BOLD_ITALIC, 0, FONT_LANGUAGE_LATIN, 0, 1, "ltn15.pgf", "FTT-Matisse Pro Latin", 0, 0},
	{0x288, 0x288, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SANS_SERIF, FONT_STYLE_REGULAR, 0, FONT_LANGUAGE_KOREAN, 0, 3, "kr0.pgf", "AsiaNHH(512Johab)", 0, 0},
};

static const float pointDPI = 72.f;

class LoadedFont;
class FontLib;
class Font;
int GetInternalFontIndex(Font *font);

// These should not need to be state saved.
static std::vector<Font *> internalFonts;
// However, these we must save - but we could take a shortcut
// for LoadedFonts that point to internal fonts.
static std::map<u32, LoadedFont *> fontMap;
static std::map<u32, u32> fontLibMap;
// We keep this list to avoid ptr references, even before alloc is called.
static std::vector<FontLib *> fontLibList;

enum MatchQuality {
	MATCH_UNKNOWN,
	MATCH_NONE,
	MATCH_GOOD,
};

enum FontOpenMode {
	FONT_OPEN_INTERNAL_STINGY   = 0,
	FONT_OPEN_INTERNAL_FULL     = 1,
	// Calls open/seek/read/close handlers to read the file partially.
	FONT_OPEN_USERFILE_HANDLERS = 2,
	// Reads directly from filesystem.
	FONT_OPEN_USERFILE_FULL     = 3,
	FONT_OPEN_USERBUFFER        = 4,
};

// TODO: Merge this class with PGF? That'd make it harder to support .bwfon
// fonts though, unless that's added directly to PGF.
class Font {
public:
	// For savestates only.
	Font() {
	}

	Font(const u8 *data, size_t dataSize) {
		Init(data, dataSize);
	}

	Font(const u8 *data, size_t dataSize, const FontRegistryEntry &entry) {
		Init(data, dataSize, entry);
	}

	Font(const std::vector<u8> &data) {
		Init(&data[0], data.size());
	}

	Font(const std::vector<u8> &data, const FontRegistryEntry &entry) {
		Init(&data[0], data.size(), entry);
	}

	const PGFFontStyle &GetFontStyle() const { return style_; }

	MatchQuality MatchesStyle(const PGFFontStyle &style) const {
		// If no field matches, it doesn't match.
		MatchQuality match = MATCH_UNKNOWN;

#define CHECK_FIELD(f, m) \
		if (style.f != 0) { \
			if (style.f != style_.f) { \
				return MATCH_NONE; \
			} \
			if (match < m) { \
				match = m; \
			} \
		}
#define CHECK_FIELD_STR(f, m) \
		if (style.f[0] != '\0') { \
			if (strcmp(style.f, style_.f) != 0) { \
				return MATCH_NONE; \
			} \
			if (match < m) { \
				match = m; \
			} \
		}

		CHECK_FIELD(fontFamily, MATCH_GOOD);
		CHECK_FIELD(fontStyle, MATCH_GOOD);
		CHECK_FIELD(fontLanguage, MATCH_GOOD);
		CHECK_FIELD(fontCountry, MATCH_GOOD);

		CHECK_FIELD_STR(fontName, MATCH_GOOD);
		CHECK_FIELD_STR(fontFileName, MATCH_GOOD);

#undef CHECK_FIELD_STR
#undef CHECK_FIELD
		return match;
	}

	PGF *GetPGF() { return &pgf_; }
	const PGF *GetPGF() const { return &pgf_; }
	bool IsValid() const { return valid_; }

	void DoState(PointerWrap &p) {
		auto s = p.Section("Font", 1, 2);
		if (!s)
			return;

		p.Do(pgf_);
		p.Do(style_);
		if (s < 2) {
			valid_ = true;
		} else {
			p.Do(valid_);
		}
	}

private:
	void Init(const u8 *data, size_t dataSize) {
		valid_ = pgf_.ReadPtr(data, dataSize);
		memset(&style_, 0, sizeof(style_));
		style_.fontH = (float)pgf_.header.hSize / 64.0f;
		style_.fontV = (float)pgf_.header.vSize / 64.0f;
		style_.fontHRes = (float)pgf_.header.hResolution / 64.0f;
		style_.fontVRes = (float)pgf_.header.vResolution / 64.0f;
	}

	void Init(const u8 *data, size_t dataSize, const FontRegistryEntry &entry) {
		valid_ = pgf_.ReadPtr(data, dataSize);
		style_.fontH = entry.hSize / 64.f;
		style_.fontV = entry.vSize / 64.f;
		style_.fontHRes = entry.hResolution / 64.f;
		style_.fontVRes = entry.vResolution / 64.f;
		style_.fontWeight = (float)entry.weight;
		style_.fontFamily = (u16)entry.familyCode;
		style_.fontStyle = (u16)entry.style;
		style_.fontStyleSub = (u16)entry.styleSub;
		style_.fontLanguage = (u16)entry.languageCode;
		style_.fontRegion = (u16)entry.regionCode;
		style_.fontCountry = (u16)entry.countryCode;
		strncpy(style_.fontName, entry.fontName, sizeof(style_.fontName));
		strncpy(style_.fontFileName, entry.fileName, sizeof(style_.fontFileName));
		style_.fontAttributes = entry.extraAttributes;
		style_.fontExpire = entry.expireDate;
	}

	PGF pgf_;
	PGFFontStyle style_;
	bool valid_;
	DISALLOW_COPY_AND_ASSIGN(Font);
};

class LoadedFont {
public:
	// For savestates only.
	LoadedFont() : font_(NULL) {
	}

	LoadedFont(Font *font, FontOpenMode mode, u32 fontLibID, u32 handle)
		: fontLibID_(fontLibID), font_(font), handle_(handle), open_(true), mode_(mode) {}

	~LoadedFont() {
		switch (mode_) {
		case FONT_OPEN_USERBUFFER:
		case FONT_OPEN_USERFILE_FULL:
		case FONT_OPEN_USERFILE_HANDLERS:
			// For these types, it's our responsibility to delete.
			delete font_;
			font_ = NULL;
			break;
		default:
			// Otherwise, it's an internal font, we keep those.
			break;
		}
	}

	const Font *GetFont() const { return font_; }
	const PGF *GetPGF() const { return font_->GetPGF(); }
	const FontLib *GetFontLib() const { return fontLibList[fontLibID_]; }
	FontLib *GetFontLib() { return fontLibList[fontLibID_]; }
	u32 Handle() const { return handle_; }

	bool GetCharInfo(int charCode, PGFCharInfo *charInfo, int glyphType = FONT_PGF_CHARGLYPH) const;
	void DrawCharacter(const GlyphImage *image, int clipX, int clipY, int clipWidth, int clipHeight, int charCode, int glyphType) const;

	bool IsOpen() const { return open_; }
	void Close() {
		open_ = false;
		// We keep the rest around until deleted, as some queries are allowed
		// on closed fonts (which is rather strange).
	}

	void DoState(PointerWrap &p) {
		auto s = p.Section("LoadedFont", 1, 3);
		if (!s)
			return;

		int numInternalFonts = (int)internalFonts.size();
		p.Do(numInternalFonts);
		if (numInternalFonts != (int)internalFonts.size()) {
			ERROR_LOG(SCEFONT, "Unable to load state: different internal font count.");
			p.SetError(p.ERROR_FAILURE);
			return;
		}

		p.Do(fontLibID_);
		int internalFont = GetInternalFontIndex(font_);
		p.Do(internalFont);
		if (internalFont == -1) {
			p.Do(font_);
		} else if (p.mode == p.MODE_READ) {
			font_ = internalFonts[internalFont];
		}
		p.Do(handle_);
		if (s >= 2) {
			p.Do(open_);
		} else {
			open_ = fontLibID_ != (u32)-1;
		}
		if (s >= 3) {
			p.Do(mode_);
		} else {
			mode_ = FONT_OPEN_INTERNAL_FULL;
		}
	}

private:
	u32 fontLibID_;
	Font *font_;
	u32 handle_;
	FontOpenMode mode_;
	bool open_;
	DISALLOW_COPY_AND_ASSIGN(LoadedFont);
};

class PostAllocCallback : public Action {
public:
	PostAllocCallback() {}
	static Action *Create() { return new PostAllocCallback(); }
	void DoState(PointerWrap &p) {
		auto s = p.Section("PostAllocCallback", 1, 2);
		if (!s)
			return;

		p.Do(fontLibID_);
		if (s >= 2) {
			p.Do(errorCodePtr_);
		}
	}
	void run(MipsCall &call);
	void SetFontLib(u32 fontLibID, u32 errorCodePtr) { fontLibID_ = fontLibID; errorCodePtr_ = errorCodePtr; }

private:
	u32 fontLibID_;
	u32 errorCodePtr_;
};

class PostOpenCallback : public Action {
public:
	PostOpenCallback() {}
	static Action *Create() { return new PostOpenCallback(); }
	void DoState(PointerWrap &p) {
		auto s = p.Section("PostOpenCallback", 1);
		if (!s)
			return;

		p.Do(fontLibID_);
	}
	void run(MipsCall &call);
	void SetFontLib(u32 fontLibID) { fontLibID_ = fontLibID; }

private:
	u32 fontLibID_;
};

struct NativeFontLib {
	FontNewLibParams params;
	// TODO
	u32_le fontInfo1;
	u32_le fontInfo2;
	u16_le unk1;
	u16_le unk2;
	float_le hRes;
	float_le vRes;
	u32_le internalFontCount;
	u32_le internalFontInfo;
	u16_le altCharCode;
	u16_le unk5;
};

struct FontImageRect {
	s16_le width;
	s16_le height;
};

// A "fontLib" is a container of loaded fonts.
// One can open either "internal" fonts or custom fonts into a fontlib.
class FontLib {
public:
	FontLib() {
		// For save states only.
	}

	FontLib(u32 paramPtr, u32 errorCodePtr) : fontHRes_(128.0f), fontVRes_(128.0f), altCharCode_(0x5F) {
		nfl_ = 0;
		Memory::ReadStruct(paramPtr, &params_);
		if (params_.numFonts > 9) {
			params_.numFonts = 9;
		}

		// Technically, this should be four separate allocations.
		u32 allocSize = 0x4C + params_.numFonts * 0x4C + params_.numFonts * 0x230 + (u32)internalFonts.size() * 0xA8;
		PostAllocCallback *action = (PostAllocCallback *) __KernelCreateAction(actionPostAllocCallback);
		action->SetFontLib(GetListID(), errorCodePtr);

		u32 args[2] = { params_.userDataAddr, allocSize };
		__KernelDirectMipsCall(params_.allocFuncAddr, action, args, 2, true);
	}

	u32 GetListID() {
		return (u32)(std::find(fontLibList.begin(), fontLibList.end(), this) - fontLibList.begin());
	}

	void Done() {
		for (size_t i = 0; i < fonts_.size(); i++) {
			if (isfontopen_[i] == FONT_IS_OPEN) {
				fontMap[fonts_[i]]->Close();
				delete fontMap[fonts_[i]];
				fontMap.erase(fonts_[i]);
			}
		}
		u32 args[2] = { params_.userDataAddr, (u32)handle_ };
		// TODO: The return value of this is leaking.
		__KernelDirectMipsCall(params_.freeFuncAddr, 0, args, 2, false);
		handle_ = 0;
		fonts_.clear();
		isfontopen_.clear();
	}

	void AllocDone(u32 allocatedAddr) {
		handle_ = allocatedAddr;
		fonts_.resize(params_.numFonts);
		isfontopen_.resize(params_.numFonts);
		for (size_t i = 0; i < fonts_.size(); i++) {
			u32 addr = allocatedAddr + 0x4C + (u32)i * 0x4C;
			isfontopen_[i] = 0;
			fonts_[i] = addr;
		}

		// Let's write out the native struct to make tests easier.
		// It's possible games may depend on this staying in ram, e.g. copying it, we may move to that.
		nfl_ = allocatedAddr;
		nfl_->params = params_;
		nfl_->fontInfo1 = allocatedAddr + 0x4C;
		nfl_->fontInfo2 = allocatedAddr + 0x4C + params_.numFonts * 0x4C;
		nfl_->unk1 = 0;
		nfl_->unk2 = 0;
		nfl_->hRes = fontHRes_;
		nfl_->vRes = fontVRes_;
		nfl_->internalFontCount = (u32)internalFonts.size();
		nfl_->internalFontInfo = allocatedAddr + 0x4C + params_.numFonts * 0x4C + params_.numFonts * 0x230;
		nfl_->altCharCode = altCharCode_;
	}

	u32 handle() const { return handle_; }
	int numFonts() const { return params_.numFonts; }

	void SetResolution(float hres, float vres) {
		fontHRes_ = hres;
		fontVRes_ = vres;
		if (nfl_.IsValid()) {
			nfl_->hRes = hres;
			nfl_->vRes = vres;
		}
	}

	float FontHRes() const { return fontHRes_; }
	float FontVRes() const { return fontVRes_; }

	void SetAltCharCode(int charCode) {
		altCharCode_ = charCode;
		if (nfl_.IsValid())
			nfl_->altCharCode = charCode;
	}

	int GetFontHandle(int index) {
		return fonts_[index];
	}

	// For FONT_OPEN_USER* modes, the font will automatically be freed.
	LoadedFont *OpenFont(Font *font, FontOpenMode mode, int &error) {
		// TODO: Do something with mode, possibly save it where the PSP does in the struct.
		// Maybe needed in Font, though?  Handlers seem... difficult to emulate.
		int freeFontIndex = -1;
		for (size_t i = 0; i < fonts_.size(); i++) {
			if (isfontopen_[i] == 0) {
				freeFontIndex = (int)i;
				break;
			}
		}
		if (freeFontIndex < 0) {
			ERROR_LOG(SCEFONT, "Too many fonts opened in FontLib");
			error = ERROR_FONT_TOO_MANY_OPEN_FONTS;
			return 0;
		}
		if (!font->IsValid()) {
			ERROR_LOG(SCEFONT, "Invalid font data");
			error = ERROR_FONT_INVALID_FONT_DATA;
			return 0;
		}
		LoadedFont *loadedFont = new LoadedFont(font, mode, GetListID(), fonts_[freeFontIndex]);
		isfontopen_[freeFontIndex] = 1;

		auto prevFont = fontMap.find(loadedFont->Handle());
		if (prevFont != fontMap.end()) {
			// Before replacing it and forgetting about it, let's free it.
			delete prevFont->second;
		}
		fontMap[loadedFont->Handle()] = loadedFont;
		return loadedFont;
	}

	void CloseFont(LoadedFont *font) {
		for (size_t i = 0; i < fonts_.size(); i++) {
			if (fonts_[i] == font->Handle()) {
				isfontopen_[i] = 0;

			}
		}
		font->Close();
	}

	void DoState(PointerWrap &p) {
		auto s = p.Section("FontLib", 1, 2);
		if (!s)
			return;

		p.Do(fonts_);
		p.Do(isfontopen_);
		p.Do(params_);
		p.Do(fontHRes_);
		p.Do(fontVRes_);
		p.Do(fileFontHandle_);
		p.Do(handle_);
		p.Do(altCharCode_);
		if (s >= 2) {
			p.Do(nfl_);
		} else {
			nfl_ = 0;
		}
	}

	void SetFileFontHandle(u32 handle) {
		fileFontHandle_ = handle;
	}

	u32 GetAltCharCode() const { return altCharCode_; }

private:
	std::vector<u32> fonts_;
	std::vector<u32> isfontopen_;

	FontNewLibParams params_;
	float fontHRes_;
	float fontVRes_;
	int fileFontHandle_;
	int handle_;
	int altCharCode_;
	PSPPointer<NativeFontLib> nfl_;

	DISALLOW_COPY_AND_ASSIGN(FontLib);
};


void PostAllocCallback::run(MipsCall &call) {
	INFO_LOG(SCEFONT, "Entering PostAllocCallback::run");
	u32 v0 = currentMIPS->r[MIPS_REG_V0];
	if (v0 == 0) {
		// TODO: Who deletes fontLib?
		Memory::Write_U32(ERROR_FONT_OUT_OF_MEMORY, errorCodePtr_);
		call.setReturnValue(0);
	} else {
		FontLib *fontLib = fontLibList[fontLibID_];
		fontLib->AllocDone(v0);
		fontLibMap[fontLib->handle()] = fontLibID_;
		call.setReturnValue(fontLib->handle());
	}
	INFO_LOG(SCEFONT, "Leaving PostAllocCallback::run");
}

void PostOpenCallback::run(MipsCall &call) {
	FontLib *fontLib = fontLibList[fontLibID_];
	u32 v0 = currentMIPS->r[MIPS_REG_V0];
	fontLib->SetFileFontHandle(v0);
}

inline bool LoadedFont::GetCharInfo(int charCode, PGFCharInfo *charInfo, int glyphType) const {
	auto fontLib = GetFontLib();
	int altCharCode = fontLib == NULL ? -1 : fontLib->GetAltCharCode();
	return GetPGF()->GetCharInfo(charCode, charInfo, altCharCode, glyphType);
}

inline void LoadedFont::DrawCharacter(const GlyphImage *image, int clipX, int clipY, int clipWidth, int clipHeight, int charCode, int glyphType) const {
	auto fontLib = GetFontLib();
	int altCharCode = fontLib == NULL ? -1 : fontLib->GetAltCharCode();
	GetPGF()->DrawCharacter(image, clipX, clipY, clipWidth, clipHeight, charCode, altCharCode, glyphType);
}

FontLib *GetFontLib(u32 handle) {
	if (fontLibMap.find(handle) != fontLibMap.end()) {
		return fontLibList[fontLibMap[handle]];
	} else {
		ERROR_LOG(SCEFONT, "No fontlib with handle %08x", handle);
		return 0;
	}
}

LoadedFont *GetLoadedFont(u32 handle, bool allowClosed) {
	auto iter = fontMap.find(handle);
	if (iter != fontMap.end()) {
		if (iter->second->IsOpen() || allowClosed) {
			return fontMap[handle];
		} else {
			ERROR_LOG(SCEFONT, "Font exists but is closed, which was not allowed in this call.");
			return 0;
		}
	} else {
		ERROR_LOG(SCEFONT, "No font with handle %08x", handle);
		return 0;
	}
}

void __LoadInternalFonts() {
	if (internalFonts.size()) {
		// Fonts already loaded.
		return;
	}
	const std::string fontPath = "flash0:/font/";
	const std::string fontOverridePath = "ms0:/PSP/flash0/font/";
	const std::string userfontPath = "disc0:/PSP_GAME/USRDIR/";
	
	if (!pspFileSystem.GetFileInfo(fontPath).exists) {
		pspFileSystem.MkDir(fontPath);
	}
	for (size_t i = 0; i < ARRAY_SIZE(fontRegistry); i++) {
		const FontRegistryEntry &entry = fontRegistry[i];
		std::string fontFilename = userfontPath + entry.fileName; 
		PSPFileInfo info = pspFileSystem.GetFileInfo(fontFilename);

		if (!info.exists) {
			// No user font, let's try override path.
			fontFilename = fontOverridePath + entry.fileName;
			info = pspFileSystem.GetFileInfo(fontFilename);
		}

		if (!info.exists) {
			// No override, let's use the default path.
			fontFilename = fontPath + entry.fileName;
			info = pspFileSystem.GetFileInfo(fontFilename);
		}

		if (info.exists) {
			DEBUG_LOG(SCEFONT, "Loading internal font %s (%i bytes)", fontFilename.c_str(), (int)info.size);
			std::vector<u8> buffer;
			if (pspFileSystem.ReadEntireFile(fontFilename, buffer) < 0) {
				ERROR_LOG(SCEFONT, "Failed opening font");
				continue;
			}
			
			internalFonts.push_back(new Font(buffer, entry));

			DEBUG_LOG(SCEFONT, "Loaded font %s", fontFilename.c_str());
		} else {
			WARN_LOG(SCEFONT, "Font file not found: %s", fontFilename.c_str());
		}
	}
}

Style FontStyleFromString(const std::string &str) {
	if (str == "Regular")
		return FONT_STYLE_REGULAR;
	else if (str == "Italic")
		return FONT_STYLE_ITALIC;
	else if (str == "Bold")
		return FONT_STYLE_BOLD;
	else if (str == "Bold Italic")
		return FONT_STYLE_BOLD_ITALIC;
	return FONT_STYLE_REGULAR;
}

int GetInternalFontIndex(Font *font) {
	for (size_t i = 0; i < internalFonts.size(); i++) {
		if (internalFonts[i] == font)
			return (int)i;
	}
	return -1;
}

void __FontInit() {
	actionPostAllocCallback = __KernelRegisterActionType(PostAllocCallback::Create);
	actionPostOpenCallback = __KernelRegisterActionType(PostOpenCallback::Create);
}

void __FontShutdown() {
	for (auto iter = fontMap.begin(); iter != fontMap.end(); iter++) {
		FontLib *fontLib = iter->second->GetFontLib();
		if (fontLib)
			fontLib->CloseFont(iter->second);
		delete iter->second;
	}
	fontMap.clear();
	for (auto iter = fontLibList.begin(); iter != fontLibList.end(); iter++) {
		delete *iter;
	}
	fontLibList.clear();
	fontLibMap.clear();
	for (auto iter = internalFonts.begin(); iter != internalFonts.end(); ++iter) {
		delete *iter;
	}
	internalFonts.clear();
}

void __FontDoState(PointerWrap &p) {
	auto s = p.Section("sceFont", 1);
	if (!s)
		return;

	__LoadInternalFonts();

	p.Do(fontLibList);
	p.Do(fontLibMap);
	p.Do(fontMap);

	p.Do(actionPostAllocCallback);
	__KernelRestoreActionType(actionPostAllocCallback, PostAllocCallback::Create);
	p.Do(actionPostOpenCallback);
	__KernelRestoreActionType(actionPostOpenCallback, PostOpenCallback::Create);
}

u32 sceFontNewLib(u32 paramPtr, u32 errorCodePtr) {
	// Lazy load internal fonts, only when font library first inited.
	__LoadInternalFonts();

	auto params = PSPPointer<FontNewLibParams>::Create(paramPtr);
	auto errorCode = PSPPointer<u32>::Create(errorCodePtr);

	if (!params.IsValid() || !errorCode.IsValid()) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontNewLib(%08x, %08x): invalid addresses", paramPtr, errorCodePtr);
		// The PSP would crash in this situation, not a real error code.
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
	}
	if (!Memory::IsValidAddress(params->allocFuncAddr) || !Memory::IsValidAddress(params->freeFuncAddr)) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontNewLib(%08x, %08x): missing alloc func", paramPtr, errorCodePtr);
		*errorCode = ERROR_FONT_INVALID_PARAMETER;
		return 0;
	}

	INFO_LOG(SCEFONT, "sceFontNewLib(%08x, %08x)", paramPtr, errorCodePtr);
	*errorCode = 0;

	FontLib *newLib = new FontLib(paramPtr, errorCodePtr);
	fontLibList.push_back(newLib);
	// The game should never see this value, the return value is replaced
	// by the action. Except if we disable the alloc, in this case we return
	// the handle correctly here.
	return newLib->handle();
}

int sceFontDoneLib(u32 fontLibHandle) {
	INFO_LOG(SCEFONT, "sceFontDoneLib(%08x)", fontLibHandle);
	FontLib *fl = GetFontLib(fontLibHandle);
	if (fl) {
		fl->Done();
	}
	return 0;
}

// Open internal font into a FontLib
u32 sceFontOpen(u32 libHandle, u32 index, u32 mode, u32 errorCodePtr) {
	auto errorCode = PSPPointer<int>::Create(errorCodePtr);
	if (!errorCode.IsValid()) {
		// Would crash on the PSP.
		ERROR_LOG(SCEFONT, "sceFontOpen(%x, %x, %x, %x): invalid pointer", libHandle, index, mode, errorCodePtr);
		return -1;
	}

	DEBUG_LOG(SCEFONT, "sceFontOpen(%x, %x, %x, %x)", libHandle, index, mode, errorCodePtr);
	FontLib *fontLib = GetFontLib(libHandle);
	if (fontLib == NULL) {
		*errorCode = ERROR_FONT_INVALID_LIBID;
		return 0;
	}
	if (index >= internalFonts.size()) {
		*errorCode = ERROR_FONT_INVALID_PARAMETER;
		return 0;
	}

	FontOpenMode openMode = mode == 0 ? FONT_OPEN_INTERNAL_STINGY : FONT_OPEN_INTERNAL_FULL;
	LoadedFont *font = fontLib->OpenFont(internalFonts[index], openMode, *errorCode);
	if (font) {
		*errorCode = 0;
		return font->Handle();
	} else {
		return 0;
	}
}

// Open a user font in RAM into a FontLib
u32 sceFontOpenUserMemory(u32 libHandle, u32 memoryFontAddrPtr, u32 memoryFontLength, u32 errorCodePtr) {
	auto errorCode = PSPPointer<int>::Create(errorCodePtr);
	if (!errorCode.IsValid()) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontOpenUserMemory(%08x, %08x, %08x, %08x): invalid error address", libHandle, memoryFontAddrPtr, memoryFontLength, errorCodePtr);
		return -1;
	}
	if (!Memory::IsValidAddress(memoryFontAddrPtr)) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontOpenUserMemory(%08x, %08x, %08x, %08x): invalid address", libHandle, memoryFontAddrPtr, memoryFontLength, errorCodePtr);
		*errorCode = ERROR_FONT_INVALID_PARAMETER;
		return 0;
	}

	FontLib *fontLib = GetFontLib(libHandle);
	if (!fontLib) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontOpenUserMemory(%08x, %08x, %08x, %08x): bad font lib", libHandle, memoryFontAddrPtr, memoryFontLength, errorCodePtr);
		*errorCode = ERROR_FONT_INVALID_PARAMETER;
		return 0;
	}

	DEBUG_LOG(SCEFONT, "sceFontOpenUserMemory(%08x, %08x, %08x, %08x)", libHandle, memoryFontAddrPtr, memoryFontLength, errorCodePtr);
	const u8 *fontData = Memory::GetPointer(memoryFontAddrPtr);
	// Games are able to overstate the size of a font.  Let's avoid crashing when we memcpy() it.
	// Unsigned 0xFFFFFFFF is treated as max, but that's impossible, so let's clamp to 64MB.
	if (memoryFontLength > 0x03FFFFFF)
		memoryFontLength = 0x03FFFFFF;
	while (!Memory::IsValidAddress(memoryFontAddrPtr + memoryFontLength - 1)) {
		--memoryFontLength;
	}
	Font *f = new Font(fontData, memoryFontLength);
	LoadedFont *font = fontLib->OpenFont(f, FONT_OPEN_USERBUFFER, *errorCode);
	if (font) {
		*errorCode = 0;
		return font->Handle();
	} else {
		delete f;
		return 0;
	}
}

// Open a user font in a file into a FontLib
u32 sceFontOpenUserFile(u32 libHandle, const char *fileName, u32 mode, u32 errorCodePtr) {
	auto errorCode = PSPPointer<int>::Create(errorCodePtr);

	if (!errorCode.IsValid()) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontOpenUserFile(%08x, %s, %08x, %08x): invalid error address", libHandle, fileName, mode, errorCodePtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}

	if (fileName == NULL) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontOpenUserFile(%08x, %s, %08x, %08x): invalid filename", libHandle, fileName, mode, errorCodePtr);
		*errorCode = ERROR_FONT_INVALID_PARAMETER;
		return 0;
	}

	FontLib *fontLib = GetFontLib(libHandle);
	if (!fontLib) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontOpenUserFile(%08x, %s, %08x, %08x): invalid font lib", libHandle, fileName, mode, errorCodePtr);
		*errorCode = ERROR_FONT_INVALID_LIBID;
		return 0;
	}

	// TODO: Technically, we only do this if mode = 1.  Mode 0 uses the handlers.
	if (mode != 1) {
		WARN_LOG_REPORT(SCEFONT, "Loading file directly instead of using handlers: %s", fileName);
	}
	PSPFileInfo info = pspFileSystem.GetFileInfo(fileName);
	if (!info.exists) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontOpenUserFile(%08x, %s, %08x, %08x): file does not exist", libHandle, fileName, mode, errorCodePtr);
		*errorCode = ERROR_FONT_HANDLER_OPEN_FAILED;
		return 0;
	}

	INFO_LOG(SCEFONT, "sceFontOpenUserFile(%08x, %s, %08x, %08x)", libHandle, fileName, mode, errorCodePtr);
	std::vector<u8> buffer;
	pspFileSystem.ReadEntireFile(fileName, buffer);
	Font *f = new Font(buffer);
	FontOpenMode openMode = mode == 0 ? FONT_OPEN_USERFILE_HANDLERS : FONT_OPEN_USERFILE_FULL;
	LoadedFont *font = fontLib->OpenFont(f, openMode, *errorCode);
	if (font) {
		*errorCode = 0;
		return font->Handle();
	} else {
		delete f;
		return 0;
	}
}

int sceFontClose(u32 fontHandle) {
	LoadedFont *font = GetLoadedFont(fontHandle, false);
	if (font)
	{
		DEBUG_LOG(SCEFONT, "sceFontClose(%x)", fontHandle);
		FontLib *fontLib = font->GetFontLib();
		if (fontLib)
			fontLib->CloseFont(font);
	}
	else
		ERROR_LOG(SCEFONT, "sceFontClose(%x) - font not open?", fontHandle);
	return 0;
}

int sceFontFindOptimumFont(u32 libHandle, u32 fontStylePtr, u32 errorCodePtr) {
	auto errorCode = PSPPointer<int>::Create(errorCodePtr);
	if (!errorCode.IsValid()) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontFindOptimumFont(%08x, %08x, %08x): invalid error address", libHandle, fontStylePtr, errorCodePtr);
		return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
	}

	FontLib *fontLib = GetFontLib(libHandle);
	if (!fontLib) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontFindOptimumFont(%08x, %08x, %08x): invalid font lib", libHandle, fontStylePtr, errorCodePtr);
		*errorCode = ERROR_FONT_INVALID_LIBID;
		return 0;
	}

	if (!Memory::IsValidAddress(fontStylePtr)) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontFindOptimumFont(%08x, %08x, %08x): invalid style address", libHandle, fontStylePtr, errorCodePtr);
		// Yes, actually.  Must've been a typo in the library.
		*errorCode = ERROR_FONT_INVALID_LIBID;
		return 0;
	}

	DEBUG_LOG(SCEFONT, "sceFontFindOptimumFont(%08x, %08x, %08x)", libHandle, fontStylePtr, errorCodePtr);

	auto requestedStyle = PSPPointer<const PGFFontStyle>::Create(fontStylePtr);

	// Find the first nearest match for H/V, OR the last exact match for others.
	float hRes = requestedStyle->fontHRes > 0.0f ? requestedStyle->fontHRes : fontLib->FontHRes();
	float vRes = requestedStyle->fontVRes > 0.0f ? requestedStyle->fontVRes : fontLib->FontVRes();
	Font *optimumFont = 0;
	Font *nearestFont = 0;
	float nearestDist = std::numeric_limits<float>::infinity();
	for (size_t i = 0; i < internalFonts.size(); i++) {
		MatchQuality q = internalFonts[i]->MatchesStyle(*requestedStyle);
		if (q != MATCH_NONE) {
			auto matchStyle = internalFonts[i]->GetFontStyle();
			if (requestedStyle->fontH > 0.0f) {
				float hDist = abs(matchStyle.fontHRes * matchStyle.fontH - hRes * requestedStyle->fontH);
				if (hDist < nearestDist) {
					nearestDist = hDist;
					nearestFont = internalFonts[i];
				}
			}
			if (requestedStyle->fontV > 0.0f) {
				// Appears to be a bug?  It seems to match H instead of V.
				float vDist = abs(matchStyle.fontVRes * matchStyle.fontV - vRes * requestedStyle->fontH);
				if (vDist < nearestDist) {
					nearestDist = vDist;
					nearestFont = internalFonts[i];
				}
			}
		}
		if (q == MATCH_GOOD) {
			optimumFont = internalFonts[i];
		}
	}
	if (nearestFont) {
		optimumFont = nearestFont;
	}
	if (optimumFont) {
		*errorCode = 0;
		return GetInternalFontIndex(optimumFont);
	} else {
		*errorCode = 0;
		return 0;
	}
}

// Returns the font index, not handle
int sceFontFindFont(u32 libHandle, u32 fontStylePtr, u32 errorCodePtr) {
	auto errorCode = PSPPointer<int>::Create(errorCodePtr);
	if (!errorCode.IsValid()) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontFindFont(%x, %x, %x): invalid error address", libHandle, fontStylePtr, errorCodePtr);
		return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
	}

	FontLib *fontLib = GetFontLib(libHandle);
	if (!fontLib) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontFindFont(%08x, %08x, %08x): invalid font lib", libHandle, fontStylePtr, errorCodePtr);
		*errorCode = ERROR_FONT_INVALID_LIBID;
		return 0;
	}

	if (!Memory::IsValidAddress(fontStylePtr)) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontFindFont(%08x, %08x, %08x): invalid style address", libHandle, fontStylePtr, errorCodePtr);
		*errorCode = ERROR_FONT_INVALID_PARAMETER;
		return 0;
	}

	DEBUG_LOG(SCEFONT, "sceFontFindFont(%x, %x, %x)", libHandle, fontStylePtr, errorCodePtr);

	auto requestedStyle = PSPPointer<const PGFFontStyle>::Create(fontStylePtr);

	// Find the closest exact match for the fields specified.
	float hRes = requestedStyle->fontHRes > 0.0f ? requestedStyle->fontHRes : fontLib->FontHRes();
	float vRes = requestedStyle->fontVRes > 0.0f ? requestedStyle->fontVRes : fontLib->FontVRes();
	for (size_t i = 0; i < internalFonts.size(); i++) {
		if (internalFonts[i]->MatchesStyle(*requestedStyle) != MATCH_NONE) {
			auto matchStyle = internalFonts[i]->GetFontStyle();
			if (requestedStyle->fontH > 0.0f) {
				float hDist = abs(matchStyle.fontHRes * matchStyle.fontH - hRes * requestedStyle->fontH);
				if (hDist > 0.001f) {
					continue;
				}
			} else if (requestedStyle->fontV > 0.0f) {
				// V seems to be ignored, unless H isn't specified.
				// If V is specified alone, the match always fails.
				continue;
			}
			*errorCode = 0;
			return (int)i;
		}
	}
	*errorCode = 0;
	return -1;
}

int sceFontGetFontInfo(u32 fontHandle, u32 fontInfoPtr) {
	if (!Memory::IsValidAddress(fontInfoPtr)) {
		ERROR_LOG(SCEFONT, "sceFontGetFontInfo(%x, %x): bad fontInfo pointer", fontHandle, fontInfoPtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}
	LoadedFont *font = GetLoadedFont(fontHandle, true);
	if (!font) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetFontInfo(%x, %x): bad font", fontHandle, fontInfoPtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}

	DEBUG_LOG(SCEFONT, "sceFontGetFontInfo(%x, %x)", fontHandle, fontInfoPtr);
	auto fi = PSPPointer<PGFFontInfo>::Create(fontInfoPtr);
	font->GetPGF()->GetFontInfo(fi);
	fi->fontStyle = font->GetFont()->GetFontStyle();

	return 0;
}

// It says FontInfo but it means Style - this is like sceFontGetFontList().
int sceFontGetFontInfoByIndexNumber(u32 libHandle, u32 fontInfoPtr, u32 index) {
	auto fontStyle = PSPPointer<PGFFontStyle>::Create(fontInfoPtr);
	FontLib *fl = GetFontLib(libHandle);
	if (!fl || fl->handle() == 0) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetFontInfoByIndexNumber(%08x, %08x, %i): invalid font lib", libHandle, fontInfoPtr, index);
		return !fl ? ERROR_FONT_INVALID_LIBID : ERROR_FONT_INVALID_PARAMETER;
	}
	if (index >= internalFonts.size()) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetFontInfoByIndexNumber(%08x, %08x, %i): invalid font index", libHandle, fontInfoPtr, index);
		return ERROR_FONT_INVALID_PARAMETER;
	}
	if (!fontStyle.IsValid()) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetFontInfoByIndexNumber(%08x, %08x, %i): invalid info pointer", libHandle, fontInfoPtr, index);
		return ERROR_FONT_INVALID_PARAMETER;
	}

	DEBUG_LOG(SCEFONT, "sceFontGetFontInfoByIndexNumber(%08x, %08x, %i)", libHandle, fontInfoPtr, index);
	auto font = internalFonts[index];
	*fontStyle = font->GetFontStyle();

	return 0;
}

int sceFontGetCharInfo(u32 fontHandle, u32 charCode, u32 charInfoPtr) {
	if (!Memory::IsValidAddress(charInfoPtr)) {
		ERROR_LOG(SCEFONT, "sceFontGetCharInfo(%08x, %i, %08x): bad charInfo pointer", fontHandle, charCode, charInfoPtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}
	LoadedFont *font = GetLoadedFont(fontHandle, true);
	if (!font) {
		// The PSP crashes, but we assume it'd work like sceFontGetFontInfo(), and not touch charInfo.
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetCharInfo(%08x, %i, %08x): bad font", fontHandle, charCode, charInfoPtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}

	DEBUG_LOG(SCEFONT, "sceFontGetCharInfo(%08x, %i, %08x)", fontHandle, charCode, charInfoPtr);
	auto charInfo = PSPPointer<PGFCharInfo>::Create(charInfoPtr);
	font->GetCharInfo(charCode, charInfo);

	return 0;
}

int sceFontGetShadowInfo(u32 fontHandle, u32 charCode, u32 charInfoPtr) {
	if (!Memory::IsValidAddress(charInfoPtr)) {
		ERROR_LOG(SCEFONT, "sceFontGetShadowInfo(%08x, %i, %08x): bad charInfo pointer", fontHandle, charCode, charInfoPtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}
	LoadedFont *font = GetLoadedFont(fontHandle, true);
	if (!font) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetShadowInfo(%08x, %i, %08x): bad font", fontHandle, charCode, charInfoPtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}

	DEBUG_LOG(SCEFONT, "sceFontGetShadowInfo(%08x, %i, %08x)", fontHandle, charCode, charInfoPtr);
	auto charInfo = PSPPointer<PGFCharInfo>::Create(charInfoPtr);
	font->GetCharInfo(charCode, charInfo, FONT_PGF_SHADOWGLYPH);

	return 0;
}

int sceFontGetCharImageRect(u32 fontHandle, u32 charCode, u32 charRectPtr) {
	auto charRect = PSPPointer<FontImageRect>::Create(charRectPtr);
	LoadedFont *font = GetLoadedFont(fontHandle, true);
	if (!font) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetCharImageRect(%08x, %i, %08x): bad font", fontHandle, charCode, charRectPtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}
	if (!charRect.IsValid()) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetCharImageRect(%08x, %i, %08x): invalid rect pointer", fontHandle, charCode, charRectPtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}

	DEBUG_LOG(SCEFONT, "sceFontGetCharImageRect(%08x, %i, %08x)", fontHandle, charCode, charRectPtr);
	PGFCharInfo charInfo;
	font->GetCharInfo(charCode, &charInfo);
	charRect->width = charInfo.bitmapWidth;
	charRect->height = charInfo.bitmapHeight;
	return 0;
}

int sceFontGetShadowImageRect(u32 fontHandle, u32 charCode, u32 charRectPtr) {
	auto charRect = PSPPointer<FontImageRect>::Create(charRectPtr);
	LoadedFont *font = GetLoadedFont(fontHandle, true);
	if (!font) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetShadowImageRect(%08x, %i, %08x): bad font", fontHandle, charCode, charRectPtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}
	if (!charRect.IsValid()) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetShadowImageRect(%08x, %i, %08x): invalid rect pointer", fontHandle, charCode, charRectPtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}

	DEBUG_LOG(SCEFONT, "sceFontGetShadowImageRect(%08x, %i, %08x)", fontHandle, charCode, charRectPtr);
	PGFCharInfo charInfo;
	font->GetCharInfo(charCode, &charInfo, FONT_PGF_SHADOWGLYPH);
	charRect->width = charInfo.bitmapWidth;
	charRect->height = charInfo.bitmapHeight;
	return 0;
}

int sceFontGetCharGlyphImage(u32 fontHandle, u32 charCode, u32 glyphImagePtr) {
	if (!Memory::IsValidAddress(glyphImagePtr)) {
		ERROR_LOG(SCEFONT, "sceFontGetCharGlyphImage(%x, %x, %x): bad glyphImage pointer", fontHandle, charCode, glyphImagePtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}
	LoadedFont *font = GetLoadedFont(fontHandle, true);
	if (!font) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetCharGlyphImage(%x, %x, %x): bad font", fontHandle, charCode, glyphImagePtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}

	DEBUG_LOG(SCEFONT, "sceFontGetCharGlyphImage(%x, %x, %x)", fontHandle, charCode, glyphImagePtr);
	auto glyph = PSPPointer<const GlyphImage>::Create(glyphImagePtr);
	font->DrawCharacter(glyph, -1, -1, -1, -1, charCode, FONT_PGF_CHARGLYPH);
	return 0;
}

int sceFontGetCharGlyphImage_Clip(u32 fontHandle, u32 charCode, u32 glyphImagePtr, int clipXPos, int clipYPos, int clipWidth, int clipHeight) {
	if (!Memory::IsValidAddress(glyphImagePtr)) {
		ERROR_LOG(SCEFONT, "sceFontGetCharGlyphImage_Clip(%08x, %i, %08x, %i, %i, %i, %i): bad glyphImage pointer", fontHandle, charCode, glyphImagePtr, clipXPos, clipYPos, clipWidth, clipHeight);
		return ERROR_FONT_INVALID_PARAMETER;
	}
	LoadedFont *font = GetLoadedFont(fontHandle, true);
	if (!font) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetCharGlyphImage_Clip(%08x, %i, %08x, %i, %i, %i, %i): bad font", fontHandle, charCode, glyphImagePtr, clipXPos, clipYPos, clipWidth, clipHeight);
		return ERROR_FONT_INVALID_PARAMETER;
	}

	DEBUG_LOG(SCEFONT, "sceFontGetCharGlyphImage_Clip(%08x, %i, %08x, %i, %i, %i, %i)", fontHandle, charCode, glyphImagePtr, clipXPos, clipYPos, clipWidth, clipHeight);
	auto glyph = PSPPointer<const GlyphImage>::Create(glyphImagePtr);
	font->DrawCharacter(glyph, clipXPos, clipYPos, clipWidth, clipHeight, charCode, FONT_PGF_CHARGLYPH);
	return 0;
}

int sceFontSetAltCharacterCode(u32 fontLibHandle, u32 charCode) {
	FontLib *fl = GetFontLib(fontLibHandle);
	if (!fl) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontSetAltCharacterCode(%08x, %08x): invalid font lib", fontLibHandle, charCode);
		return ERROR_FONT_INVALID_LIBID;
	}

	INFO_LOG(SCEFONT, "sceFontSetAltCharacterCode(%08x, %08x)", fontLibHandle, charCode);
	fl->SetAltCharCode(charCode & 0xFFFF);
	return 0;
}

int sceFontFlush(u32 fontHandle) {
	INFO_LOG(SCEFONT, "sceFontFlush(%i)", fontHandle);
	// Probably don't need to do anything here.
	return 0;
}

// One would think that this should loop through the fonts loaded in the fontLibHandle,
// but it seems not.
int sceFontGetFontList(u32 fontLibHandle, u32 fontStylePtr, int numFonts) {
	auto fontStyles = PSPPointer<PGFFontStyle>::Create(fontStylePtr);
	FontLib *fl = GetFontLib(fontLibHandle);
	if (!fl) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetFontList(%08x, %08x, %i): invalid font lib", fontLibHandle, fontStylePtr, numFonts);
		return ERROR_FONT_INVALID_LIBID;
	}
	if (!fontStyles.IsValid()) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetFontList(%08x, %08x, %i): invalid style pointer", fontLibHandle, fontStylePtr, numFonts);
		return ERROR_FONT_INVALID_PARAMETER;
	}

	DEBUG_LOG(SCEFONT, "sceFontGetFontList(%08x, %08x, %i)", fontLibHandle, fontStylePtr, numFonts);
	if (fl->handle() != 0) {
		numFonts = std::min(numFonts, (int)internalFonts.size());
		for (int i = 0; i < numFonts; i++)
			fontStyles[i] = internalFonts[i]->GetFontStyle();
	}

	return hleDelayResult(0, "font list read", 100);
}

int sceFontGetNumFontList(u32 fontLibHandle, u32 errorCodePtr) {	
	auto errorCode = PSPPointer<int>::Create(errorCodePtr);
	if (!errorCode.IsValid()) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetNumFontList(%08x, %08x): invalid error address", fontLibHandle, errorCodePtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}
	FontLib *fl = GetFontLib(fontLibHandle);
	if (!fl) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetNumFontList(%08x, %08x): invalid font lib", fontLibHandle, errorCodePtr);
		*errorCode = ERROR_FONT_INVALID_LIBID;
		return 0;
	}
	DEBUG_LOG(SCEFONT, "sceFontGetNumFontList(%08x, %08x)", fontLibHandle, errorCodePtr);
	*errorCode = 0;
	return fl->handle() == 0 ? 0 : (int)internalFonts.size();
}

int sceFontSetResolution(u32 fontLibHandle, float hRes, float vRes) {
	FontLib *fl = GetFontLib(fontLibHandle);
	if (!fl) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontSetResolution(%08x, %f, %f): invalid font lib", fontLibHandle, hRes, vRes);
		return ERROR_FONT_INVALID_LIBID;
	}
	if (hRes <= 0.0f || vRes <= 0.0f) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontSetResolution(%08x, %f, %f): negative value", fontLibHandle, hRes, vRes);
		return ERROR_FONT_INVALID_PARAMETER;
	}
	INFO_LOG(SCEFONT, "sceFontSetResolution(%08x, %f, %f)", fontLibHandle, hRes, vRes);
	fl->SetResolution(hRes, vRes);
	return 0;
}

float sceFontPixelToPointH(int fontLibHandle, float fontPixelsH, u32 errorCodePtr) {
	auto errorCode = PSPPointer<int>::Create(errorCodePtr);
	if (!errorCode.IsValid()) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontPixelToPointH(%08x, %f, %08x): invalid error address", fontLibHandle, fontPixelsH, errorCodePtr);
		return 0.0f;
	}
	FontLib *fl = GetFontLib(fontLibHandle);
	if (!fl) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontPixelToPointH(%08x, %f, %08x): invalid font lib", fontLibHandle, fontPixelsH, errorCodePtr);
		*errorCode = ERROR_FONT_INVALID_LIBID;
		return 0.0f;
	}
	DEBUG_LOG(SCEFONT, "sceFontPixelToPointH(%08x, %f, %08x)", fontLibHandle, fontPixelsH, errorCodePtr);
	*errorCode = 0;
	return fontPixelsH * pointDPI / fl->FontHRes();
}

float sceFontPixelToPointV(int fontLibHandle, float fontPixelsV, u32 errorCodePtr) {
	auto errorCode = PSPPointer<int>::Create(errorCodePtr);
	if (!errorCode.IsValid()) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontPixelToPointV(%08x, %f, %08x): invalid error address", fontLibHandle, fontPixelsV, errorCodePtr);
		return 0.0f;
	}
	FontLib *fl = GetFontLib(fontLibHandle);
	if (!fl) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontPixelToPointV(%08x, %f, %08x): invalid font lib", fontLibHandle, fontPixelsV, errorCodePtr);
		*errorCode = ERROR_FONT_INVALID_LIBID;
		return 0.0f;
	}
	DEBUG_LOG(SCEFONT, "sceFontPixelToPointV(%08x, %f, %08x)", fontLibHandle, fontPixelsV, errorCodePtr);
	*errorCode = 0;
	return fontPixelsV * pointDPI / fl->FontVRes();
}

float sceFontPointToPixelH(int fontLibHandle, float fontPointsH, u32 errorCodePtr) {
	auto errorCode = PSPPointer<int>::Create(errorCodePtr);
	if (!errorCode.IsValid()) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontPointToPixelH(%08x, %f, %08x): invalid error address", fontLibHandle, fontPointsH, errorCodePtr);
		return 0.0f;
	}
	FontLib *fl = GetFontLib(fontLibHandle);
	if (!fl) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontPointToPixelH(%08x, %f, %08x): invalid font lib", fontLibHandle, fontPointsH, errorCodePtr);
		*errorCode = ERROR_FONT_INVALID_LIBID;
		return 0.0f;
	}
	DEBUG_LOG(SCEFONT, "sceFontPointToPixelH(%08x, %f, %08x)", fontLibHandle, fontPointsH, errorCodePtr);
	*errorCode = 0;
	return fontPointsH * fl->FontHRes() / pointDPI;
}

float sceFontPointToPixelV(int fontLibHandle, float fontPointsV, u32 errorCodePtr) {
	auto errorCode = PSPPointer<int>::Create(errorCodePtr);
	if (!errorCode.IsValid()) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontPointToPixelV(%08x, %f, %08x): invalid error address", fontLibHandle, fontPointsV, errorCodePtr);
		return 0.0f;
	}
	FontLib *fl = GetFontLib(fontLibHandle);
	if (!fl) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontPointToPixelV(%08x, %f, %08x): invalid font lib", fontLibHandle, fontPointsV, errorCodePtr);
		*errorCode = ERROR_FONT_INVALID_LIBID;
		return 0.0f;
	}
	DEBUG_LOG(SCEFONT, "sceFontPointToPixelV(%08x, %f, %08x)", fontLibHandle, fontPointsV, errorCodePtr);
	*errorCode = 0;
	return fontPointsV * fl->FontVRes() / pointDPI;
}

int sceFontCalcMemorySize() {
	ERROR_LOG_REPORT(SCEFONT, "UNIMPL sceFontCalcMemorySize()");
	return 0;
}

int sceFontGetShadowGlyphImage(u32 fontHandle, u32 charCode, u32 glyphImagePtr) {
	if (!Memory::IsValidAddress(glyphImagePtr)) {
		ERROR_LOG(SCEFONT, "sceFontGetShadowGlyphImage(%x, %x, %x): bad glyphImage pointer", fontHandle, charCode, glyphImagePtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}
	LoadedFont *font = GetLoadedFont(fontHandle, true);
	if (!font) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetShadowGlyphImage(%x, %x, %x): bad font", fontHandle, charCode, glyphImagePtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}

	DEBUG_LOG(SCEFONT, "sceFontGetShadowGlyphImage(%x, %x, %x)", fontHandle, charCode, glyphImagePtr);
	auto glyph = PSPPointer<const GlyphImage>::Create(glyphImagePtr);
	font->DrawCharacter(glyph, -1, -1, -1, -1, charCode, FONT_PGF_SHADOWGLYPH);
	return 0;
}

int sceFontGetShadowGlyphImage_Clip(u32 fontHandle, u32 charCode, u32 glyphImagePtr, int clipXPos, int clipYPos, int clipWidth, int clipHeight) {
	if (!Memory::IsValidAddress(glyphImagePtr)) {
		ERROR_LOG(SCEFONT, "sceFontGetShadowGlyphImage_Clip(%08x, %i, %08x, %i, %i, %i, %i): bad glyphImage pointer", fontHandle, charCode, glyphImagePtr, clipXPos, clipYPos, clipWidth, clipHeight);
		return ERROR_FONT_INVALID_PARAMETER;
	}
	LoadedFont *font = GetLoadedFont(fontHandle, true);
	if (!font) {
		ERROR_LOG_REPORT(SCEFONT, "sceFontGetShadowGlyphImage_Clip(%08x, %i, %08x, %i, %i, %i, %i): bad font", fontHandle, charCode, glyphImagePtr, clipXPos, clipYPos, clipWidth, clipHeight);
		return ERROR_FONT_INVALID_PARAMETER;
	}

	DEBUG_LOG(SCEFONT, "sceFontGetShadowGlyphImage_Clip(%08x, %i, %08x, %i, %i, %i, %i)", fontHandle, charCode, glyphImagePtr, clipXPos, clipYPos, clipWidth, clipHeight);
	auto glyph = PSPPointer<const GlyphImage>::Create(glyphImagePtr);
	font->DrawCharacter(glyph, clipXPos, clipYPos, clipWidth, clipHeight, charCode, FONT_PGF_SHADOWGLYPH);
	return 0;
}

const HLEFunction sceLibFont[] = {
	{0x67f17ed7, WrapU_UU<sceFontNewLib>, "sceFontNewLib"},
	{0x574b6fbc, WrapI_U<sceFontDoneLib>, "sceFontDoneLib"},
	{0x48293280, WrapI_UFF<sceFontSetResolution>, "sceFontSetResolution"},
	{0x27f6e642, WrapI_UU<sceFontGetNumFontList>, "sceFontGetNumFontList"},
	{0xbc75d85b, WrapI_UUI<sceFontGetFontList>, "sceFontGetFontList"},
	{0x099ef33c, WrapI_UUU<sceFontFindOptimumFont>, "sceFontFindOptimumFont"},
	{0x681e61a7, WrapI_UUU<sceFontFindFont>, "sceFontFindFont"},
	{0x2f67356a, WrapI_V<sceFontCalcMemorySize>, "sceFontCalcMemorySize"},
	{0x5333322d, WrapI_UUU<sceFontGetFontInfoByIndexNumber>, "sceFontGetFontInfoByIndexNumber"},
	{0xa834319d, WrapU_UUUU<sceFontOpen>, "sceFontOpen"},
	{0x57fcb733, WrapU_UCUU<sceFontOpenUserFile>, "sceFontOpenUserFile"},
	{0xbb8e7fe6, WrapU_UUUU<sceFontOpenUserMemory>, "sceFontOpenUserMemory"},
	{0x3aea8cb6, WrapI_U<sceFontClose>, "sceFontClose"},
	{0x0da7535e, WrapI_UU<sceFontGetFontInfo>, "sceFontGetFontInfo"},
	{0xdcc80c2f, WrapI_UUU<sceFontGetCharInfo>, "sceFontGetCharInfo"},
	{0xaa3de7b5, WrapI_UUU<sceFontGetShadowInfo>, "sceFontGetShadowInfo"},
	{0x5c3e4a9e, WrapI_UUU<sceFontGetCharImageRect>, "sceFontGetCharImageRect"},
	{0x48b06520, WrapI_UUU<sceFontGetShadowImageRect>, "sceFontGetShadowImageRect"},
	{0x980f4895, WrapI_UUU<sceFontGetCharGlyphImage>, "sceFontGetCharGlyphImage"},
	{0xca1e6945, WrapI_UUUIIII<sceFontGetCharGlyphImage_Clip>, "sceFontGetCharGlyphImage_Clip"},
	{0x74b21701, WrapF_IFU<sceFontPixelToPointH>, "sceFontPixelToPointH"},
	{0xf8f0752e, WrapF_IFU<sceFontPixelToPointV>, "sceFontPixelToPointV"},
	{0x472694cd, WrapF_IFU<sceFontPointToPixelH>, "sceFontPointToPixelH"},
	{0x3c4b7e82, WrapF_IFU<sceFontPointToPixelV>, "sceFontPointToPixelV"},
	{0xee232411, WrapI_UU<sceFontSetAltCharacterCode>, "sceFontSetAltCharacterCode"},
	{0x568be516, WrapI_UUU<sceFontGetShadowGlyphImage>, "sceFontGetShadowGlyphImage"},
	{0x5dcf6858, WrapI_UUUIIII<sceFontGetShadowGlyphImage_Clip>, "sceFontGetShadowGlyphImage_Clip"},
	{0x02d7f94b, WrapI_U<sceFontFlush>, "sceFontFlush"},
};

void Register_sceFont() {
	RegisterModule("sceLibFont", ARRAY_SIZE(sceLibFont), sceLibFont);
}

