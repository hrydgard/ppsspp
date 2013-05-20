#include "sceFont.h"

#include "base/timeutil.h"

#include <cmath>
#include <vector>
#include <map>
#include <algorithm>

#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "ChunkFile.h"
#include "Core/FileSystems/FileSystem.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/HLE/sceKernel.h"
#include "Core/Font/PGF.h"
#include "Core/HLE/sceKernelThread.h"

enum {
	ERROR_FONT_INVALID_LIBID                            = 0x80460002,
	ERROR_FONT_INVALID_PARAMETER                        = 0x80460003,
	ERROR_FONT_TOO_MANY_OPEN_FONTS                      = 0x80460009,
};

enum {
	FONT_IS_CLOSED = 0,
	FONT_IS_OPEN = 1,
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
	u32 userDataAddr;
	u32 numFonts;
	u32 cacheDataAddr;

	// Driver callbacks.
	u32 allocFuncAddr;
	u32 freeFuncAddr;
	u32 openFuncAddr;
	u32 closeFuncAddr;
	u32 readFuncAddr;
	u32 seekFuncAddr;
	u32 errorFuncAddr;
	u32 ioFinishFuncAddr;
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
	{0x288, 0x288, 0x2000, 0x2000, 0, 0, FONT_FAMILY_SANS_SERIF, FONT_STYLE_DB, 0, FONT_LANGUAGE_JAPANESE, 0, 1, "jpn0.pgf", "FTT-NewRodin Pro DB", 0, 0},
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
	MATCH_NONE,
	MATCH_GOOD,
	MATCH_PERFECT,
};

// TODO: Merge this class with PGF? That'd make it harder to support .bwfon
// fonts though, unless that's added directly to PGF.
class Font {
public:
	// For savestates only.
	Font() {
	}

	Font(const u8 *data, size_t dataSize) {
		pgf_.ReadPtr(data, dataSize);
		style_.fontH = pgf_.header.hSize / 64.0f;
		style_.fontV = pgf_.header.vSize / 64.0f;
		style_.fontHRes = pgf_.header.hResolution / 64.0f;
		style_.fontVRes = pgf_.header.vResolution / 64.0f;
	}

	Font(const u8 *data, size_t dataSize, const FontRegistryEntry &entry) {
		pgf_.ReadPtr(data, dataSize);
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

	const PGFFontStyle &GetFontStyle() const { return style_; }

	MatchQuality MatchesStyle(const PGFFontStyle &style, bool optimum) const {
		// If no field matches, it doesn't match.
		MatchQuality match = MATCH_NONE;

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

		// H and V take the first match, most other fields take the last match.
		CHECK_FIELD(fontH, MATCH_PERFECT);
		CHECK_FIELD(fontV, MATCH_PERFECT);

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

	void DoState(PointerWrap &p) {
		p.Do(pgf_);
		p.Do(style_);
		p.DoMarker("Font");
	}

private:
	PGF pgf_;
	PGFFontStyle style_;
	DISALLOW_COPY_AND_ASSIGN(Font);
};

class LoadedFont {
public:
	// For savestates only.
	LoadedFont() : font_(NULL) {
	}

	LoadedFont(Font *font, u32 fontLibID, u32 handle)
		: fontLibID_(fontLibID), font_(font), handle_(handle) {}

	Font *GetFont() { return font_; }
	PGF *GetPGF() { return font_->GetPGF(); }
	FontLib *GetFontLib() { if (!IsOpen()) return NULL; return fontLibList[fontLibID_]; }
	u32 Handle() const { return handle_; }

	bool IsOpen() const { return fontLibID_ != (u32)-1; }
	void Close() {
		fontLibID_ = (u32)-1;
		// We keep the rest around until deleted, as some queries are allowed
		// on closed fonts (which is rather strange).
	}

	void DoState(PointerWrap &p) {
		int numInternalFonts = (int)internalFonts.size();
		p.Do(numInternalFonts);
		if (numInternalFonts != (int)internalFonts.size()) {
			ERROR_LOG(HLE, "Unable to load state: different internal font count.");
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
		p.DoMarker("LoadedFont");
	}

private:
	u32 fontLibID_;
	Font *font_;
	u32 handle_;
	DISALLOW_COPY_AND_ASSIGN(LoadedFont);
};

class PostAllocCallback : public Action {
public:
	PostAllocCallback() {}
	static Action *Create() { return new PostAllocCallback(); }
	void DoState(PointerWrap &p) { p.Do(fontLibID_); p.DoMarker("PostAllocCallback"); }
	void run(MipsCall &call);
	void SetFontLib(u32 fontLibID) { fontLibID_ = fontLibID; }

private:
	u32 fontLibID_;
};

class PostOpenCallback : public Action {
public:
	PostOpenCallback() {}
	static Action *Create() { return new PostOpenCallback(); }
	void DoState(PointerWrap &p) { p.Do(fontLibID_); p.DoMarker("PostOpenCallback"); }
	void run(MipsCall &call);
	void SetFontLib(u32 fontLibID) { fontLibID_ = fontLibID; }

private:
	u32 fontLibID_;
};

// A "fontLib" is a container of loaded fonts.
// One can open either "internal" fonts or custom fonts into a fontlib.
class FontLib {
public:
	FontLib() {
		// For save states only.
	}

	FontLib(u32 paramPtr) :	fontHRes_(128.0f), fontVRes_(128.0f) {
		Memory::ReadStruct(paramPtr, &params_);
		// We use the same strange scheme that JPCSP uses.
		u32 allocSize = 4 + 4 * params_.numFonts;
		PostAllocCallback *action = (PostAllocCallback *) __KernelCreateAction(actionPostAllocCallback);
		action->SetFontLib(GetListID());

		u32 args[2] = { params_.userDataAddr, allocSize };
		__KernelDirectMipsCall(params_.allocFuncAddr, action, args, 2, true);
	}

	u32 GetListID() {
		return (u32)(std::find(fontLibList.begin(), fontLibList.end(), this) - fontLibList.begin());
	}

	void Close() {
		__KernelDirectMipsCall(params_.closeFuncAddr, 0, 0, 0, false);
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
			u32 addr = allocatedAddr + 4 + (u32)i * 4;
			isfontopen_[i] = 0;
			fonts_[i] = addr;
		}
	}

	u32 handle() const { return handle_; }
	int numFonts() const { return params_.numFonts; }

	void SetResolution(float hres, float vres) {
		fontHRes_ = hres;
		fontVRes_ = vres;
	}

	float FontHRes() const { return fontHRes_; }
	float FontVRes() const { return fontVRes_; }

	void SetAltCharCode(int charCode) { altCharCode_ = charCode; }

	int GetFontHandle(int index) {
		return fonts_[index];
	}

	LoadedFont *OpenFont(Font *font) {
		int freeFontIndex = -1;
		for (size_t i = 0; i < fonts_.size(); i++) {
			if (isfontopen_[i] == 0) {
				freeFontIndex = (int)i;
				break;
			}
		}
		if (freeFontIndex < 0) {
			ERROR_LOG(HLE, "Too many fonts opened in FontLib");
			return 0;
		}
		LoadedFont *loadedFont = new LoadedFont(font, GetListID(), fonts_[freeFontIndex]);
		isfontopen_[freeFontIndex] = 1;
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
		p.Do(fonts_);
		p.Do(isfontopen_);
		p.Do(params_);
		p.Do(fontHRes_);
		p.Do(fontVRes_);
		p.Do(fileFontHandle_);
		p.Do(handle_);
		p.Do(altCharCode_);
		p.DoMarker("FontLib");
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

	DISALLOW_COPY_AND_ASSIGN(FontLib);
};


void PostAllocCallback::run(MipsCall &call) {
	INFO_LOG(HLE, "Entering PostAllocCallback::run");
	u32 v0 = currentMIPS->r[MIPS_REG_V0];
	FontLib *fontLib = fontLibList[fontLibID_];
	fontLib->AllocDone(v0);
	fontLibMap[fontLib->handle()] = fontLibID_;
	call.setReturnValue(fontLib->handle());
	INFO_LOG(HLE, "Leaving PostAllocCallback::run");
}

void PostOpenCallback::run(MipsCall &call) {
	FontLib *fontLib = fontLibList[fontLibID_];
	u32 v0 = currentMIPS->r[MIPS_REG_V0];
	fontLib->SetFileFontHandle(v0);
}

FontLib *GetFontLib(u32 handle) {
	if (fontLibMap.find(handle) != fontLibMap.end()) {
		return fontLibList[fontLibMap[handle]];
	} else {
		ERROR_LOG(HLE, "No fontlib with handle %08x", handle);
		return 0;
	}
}

LoadedFont *GetLoadedFont(u32 handle, bool allowClosed) {
	auto iter = fontMap.find(handle);
	if (iter != fontMap.end()) {
		if (iter->second->IsOpen() || allowClosed) {
			return fontMap[handle];
		} else {
			ERROR_LOG(HLE, "Font exists but is closed, which was not allowed in this call.");
			return 0;
		}
	} else {
		ERROR_LOG(HLE, "No font with handle %08x", handle);
		return 0;
	}
}

void __LoadInternalFonts() {
	if (internalFonts.size()) {
		// Fonts already loaded.
		return;
	}
	std::string fontPath = "flash0:/font/";
	if (!pspFileSystem.GetFileInfo(fontPath).exists) {
		pspFileSystem.MkDir(fontPath);
	}
	for (size_t i = 0; i < ARRAY_SIZE(fontRegistry); i++) {
		const FontRegistryEntry &entry = fontRegistry[i];
		std::string fontFilename = fontPath + entry.fileName;
		PSPFileInfo info = pspFileSystem.GetFileInfo(fontFilename);
		if (info.exists) {
			INFO_LOG(HLE, "Loading font %s (%i bytes)", fontFilename.c_str(), (int)info.size);
			u8 *buffer = new u8[(size_t)info.size];
			u32 handle = pspFileSystem.OpenFile(fontFilename, FILEACCESS_READ);
			if (!handle) {
				ERROR_LOG(HLE, "Failed opening font");
				delete [] buffer;
				continue;
			}
			pspFileSystem.ReadFile(handle, buffer, info.size);
			pspFileSystem.CloseFile(handle);
			
			internalFonts.push_back(new Font(buffer, (size_t)info.size, entry));

			delete [] buffer;
			INFO_LOG(HLE, "Loaded font %s", fontFilename.c_str());
		} else {
			INFO_LOG(HLE, "Font file not found: %s", fontFilename.c_str());
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

Font *GetOptimumFont(const PGFFontStyle &requestedStyle, Font *optimumFont, Font *candidateFont) {
	if (!optimumFont)
		return candidateFont;
	PGFFontStyle optimumStyle = optimumFont->GetFontStyle();
	PGFFontStyle candidateStyle = candidateFont->GetFontStyle();

	bool testH = requestedStyle.fontH != 0.0f || requestedStyle.fontV == 0.0f;
	if (testH && fabsf(requestedStyle.fontH - optimumStyle.fontH) > fabsf(requestedStyle.fontH - candidateStyle.fontH)) {
		return candidateFont;
	}

	// Check the fontV if it is specified or both fontH and fontV are unspecified
	bool testV = requestedStyle.fontV != 0.f || requestedStyle.fontH == 0.f;
	if (testV && fabsf(requestedStyle.fontV - optimumStyle.fontV) > fabsf(requestedStyle.fontV - candidateStyle.fontV)) {
		return candidateFont;
	}

	return optimumFont;
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
	__LoadInternalFonts();

	p.Do(fontLibList);
	p.Do(fontLibMap);
	p.Do(fontMap);

	p.Do(actionPostAllocCallback);
	__KernelRestoreActionType(actionPostAllocCallback, PostAllocCallback::Create);
	p.Do(actionPostOpenCallback);
	__KernelRestoreActionType(actionPostOpenCallback, PostOpenCallback::Create);
	p.DoMarker("sceFont");
}

u32 sceFontNewLib(u32 paramPtr, u32 errorCodePtr) {
	// Lazy load internal fonts, only when font library first inited.
	__LoadInternalFonts();
	INFO_LOG(HLE, "sceFontNewLib(%08x, %08x)", paramPtr, errorCodePtr);

	if (Memory::IsValidAddress(paramPtr) && Memory::IsValidAddress(errorCodePtr)) {
		Memory::Write_U32(0, errorCodePtr);
		
		FontLib *newLib = new FontLib(paramPtr);
		fontLibList.push_back(newLib);
		// The game should never see this value, the return value is replaced
		// by the action. Except if we disable the alloc, in this case we return
		// the handle correctly here.
		return newLib->handle();
	}

	return 0;
}

int sceFontDoneLib(u32 fontLibHandle) {
	INFO_LOG(HLE, "sceFontDoneLib(%08x)", fontLibHandle);
	FontLib *fl = GetFontLib(fontLibHandle);
	if (fl) {
		fl->Done();
	}
	return 0;
}

// Open internal font into a FontLib
u32 sceFontOpen(u32 libHandle, u32 index, u32 mode, u32 errorCodePtr) {
	if (!Memory::IsValidAddress(errorCodePtr)) {
		// Would crash on the PSP.
		ERROR_LOG(HLE, "sceFontOpen(%x, %x, %x, %x): invalid pointer", libHandle, index, mode, errorCodePtr);
		return 0;
	}

	INFO_LOG(HLE, "sceFontOpen(%x, %x, %x, %x)", libHandle, index, mode, errorCodePtr);
	FontLib *fontLib = GetFontLib(libHandle);
	if (fontLib == NULL) {
		Memory::Write_U32(ERROR_FONT_INVALID_LIBID, errorCodePtr);
		return 0;
	}
	if (index >= internalFonts.size()) {
		Memory::Write_U32(ERROR_FONT_INVALID_PARAMETER, errorCodePtr);
		return 0;
	}

	LoadedFont *font = fontLib->OpenFont(internalFonts[index]);
	if (font) {
		fontMap[font->Handle()] = font;
		Memory::Write_U32(0, errorCodePtr);
		return font->Handle();
	} else {
		Memory::Write_U32(ERROR_FONT_TOO_MANY_OPEN_FONTS, errorCodePtr);
		return 0;
	}
}

// Open a user font in RAM into a FontLib
u32 sceFontOpenUserMemory(u32 libHandle, u32 memoryFontAddrPtr, u32 memoryFontLength, u32 errorCodePtr) {
	ERROR_LOG(HLE, "sceFontOpenUserMemory %x, %x, %x, %x", libHandle, memoryFontAddrPtr, memoryFontLength, errorCodePtr);
	if (!Memory::IsValidAddress(errorCodePtr) || !Memory::IsValidAddress(memoryFontAddrPtr)) {
		Memory::Write_U32(ERROR_FONT_INVALID_PARAMETER, errorCodePtr);
		return 0;
	}

	FontLib *fontLib = GetFontLib(libHandle);
	if (!fontLib) {
		Memory::Write_U32(ERROR_FONT_INVALID_PARAMETER, errorCodePtr);
		return 0;
	}

	const u8 *fontData = Memory::GetPointer(memoryFontAddrPtr);
	LoadedFont *font = fontLib->OpenFont(new Font(fontData, memoryFontLength));
	if (font) {
		fontMap[font->Handle()] = font;
		Memory::Write_U32(0, errorCodePtr);
		return font->Handle();
	} else {
		Memory::Write_U32(ERROR_FONT_TOO_MANY_OPEN_FONTS, errorCodePtr);
		return 0;
	}
}

// Open a user font in a file into a FontLib
u32 sceFontOpenUserFile(u32 libHandle, const char *fileName, u32 mode, u32 errorCodePtr) {
	ERROR_LOG(HLE, "sceFontOpenUserFile(%08x, %s, %08x, %08x)", libHandle, fileName, mode, errorCodePtr);
	if (!Memory::IsValidAddress(errorCodePtr))
		return ERROR_FONT_INVALID_PARAMETER;

	PSPFileInfo info = pspFileSystem.GetFileInfo(fileName);
	if (!info.exists) {
		Memory::Write_U32(ERROR_FONT_INVALID_PARAMETER, errorCodePtr);
		return 0;
	}

	FontLib *fontLib = GetFontLib(libHandle);
	if (!fontLib) {
		Memory::Write_U32(ERROR_FONT_INVALID_PARAMETER, errorCodePtr);
		return 0;
	}

	u8 *buffer = new u8[(size_t)info.size];

	u32 fileHandle = pspFileSystem.OpenFile(fileName, FILEACCESS_READ);
	pspFileSystem.ReadFile(fileHandle, buffer, info.size);
	pspFileSystem.CloseFile(fileHandle);

	LoadedFont *font = fontLib->OpenFont(new Font(buffer, (size_t)info.size));
	if (font) {
		fontMap[font->Handle()] = font;
		Memory::Write_U32(0, errorCodePtr);
		return font->Handle();
	} else {
		Memory::Write_U32(ERROR_FONT_TOO_MANY_OPEN_FONTS, errorCodePtr);
		return 0;
	}
}

int sceFontClose(u32 fontHandle) {
	LoadedFont *font = GetLoadedFont(fontHandle, false);
	if (font)
	{
		INFO_LOG(HLE, "sceFontClose(%x)", fontHandle);
		FontLib *fontLib = font->GetFontLib();
		if (fontLib)
			fontLib->CloseFont(font);
	}
	else
		ERROR_LOG(HLE, "sceFontClose(%x) - font not open?", fontHandle);
	return 0;
}

int sceFontFindOptimumFont(u32 libHandlePtr, u32 fontStylePtr, u32 errorCodePtr) {
	ERROR_LOG(HLE, "sceFontFindOptimumFont(%08x, %08x, %08x)", libHandlePtr, fontStylePtr, errorCodePtr);
	if (!fontStylePtr)
		return 0;

	if (!Memory::IsValidAddress(errorCodePtr))
		return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
	
	auto requestedStyle = Memory::GetStruct<const PGFFontStyle>(fontStylePtr);

	Font *optimumFont = 0;
	for (size_t i = 0; i < internalFonts.size(); i++) {
		MatchQuality q = internalFonts[i]->MatchesStyle(*requestedStyle, true);
		if (q != MATCH_NONE) {
			optimumFont = internalFonts[i];
			if (q == MATCH_PERFECT) {
				break;
			}
		}
	}
	if (optimumFont) {
		Memory::Write_U32(0, errorCodePtr);
		return GetInternalFontIndex(optimumFont);
	} else {
		Memory::Write_U32(0, errorCodePtr);
		return 0;
	}
}

// Returns the font index, not handle
int sceFontFindFont(u32 libHandlePtr, u32 fontStylePtr, u32 errorCodePtr) {
	ERROR_LOG(HLE, "sceFontFindFont(%x, %x, %x)", libHandlePtr, fontStylePtr, errorCodePtr);
	if (!Memory::IsValidAddress(errorCodePtr)) {
		Memory::Write_U32(ERROR_FONT_INVALID_PARAMETER, errorCodePtr);
		return 0;
	}

	PGFFontStyle style;
	Memory::ReadStruct(fontStylePtr, &style);

	for (size_t i = 0; i < internalFonts.size(); i++) {
		if (internalFonts[i]->MatchesStyle(style, false)) {
			Memory::Write_U32(0, errorCodePtr);
			return (int)i;
		}
	}
	return -1;
}

int sceFontGetFontInfo(u32 fontHandle, u32 fontInfoPtr) {
	if (!Memory::IsValidAddress(fontInfoPtr)) {
		ERROR_LOG(HLE, "sceFontGetFontInfo(%x, %x): bad fontInfo pointer", fontHandle, fontInfoPtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}
	LoadedFont *font = GetLoadedFont(fontHandle, true);
	if (!font) {
		ERROR_LOG(HLE, "sceFontGetFontInfo(%x, %x): bad font", fontHandle, fontInfoPtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}

	INFO_LOG(HLE, "sceFontGetFontInfo(%x, %x)", fontHandle, fontInfoPtr);
	auto fi = Memory::GetStruct<PGFFontInfo>(fontInfoPtr);
	font->GetPGF()->GetFontInfo(fi);
	fi->fontStyle = font->GetFont()->GetFontStyle();

	return 0;
}

int sceFontGetFontInfoByIndexNumber(u32 libHandle, u32 fontInfoPtr, u32 unknown, u32 fontIndex) {
	INFO_LOG(HLE, "sceFontGetFontInfoByIndexNumber(%x, %x, %i, %i)", libHandle, fontInfoPtr, unknown, fontIndex);
	FontLib *fl = GetFontLib(libHandle);
	u32 fontHandle = fl->GetFontHandle(fontIndex);
	return sceFontGetFontInfo(fontHandle, fontInfoPtr);
}

int sceFontGetCharInfo(u32 fontHandle, u32 charCode, u32 charInfoPtr) {
	if (!Memory::IsValidAddress(charInfoPtr)) {
		ERROR_LOG(HLE, "sceFontGetCharInfo(%08x, %i, %08x): bad charInfo pointer", fontHandle, charCode, charInfoPtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}
	LoadedFont *font = GetLoadedFont(fontHandle, false);
	if (!font) {
		// The PSP crashes, but we assume it'd work like sceFontGetFontInfo(), and not touch charInfo.
		ERROR_LOG(HLE, "sceFontGetCharInfo(%08x, %i, %08x): bad font", fontHandle, charCode, charInfoPtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}

	DEBUG_LOG(HLE, "sceFontGetCharInfo(%08x, %i, %08x)", fontHandle, charCode, charInfoPtr);
	auto charInfo = Memory::GetStruct<PGFCharInfo>(charInfoPtr);
	font->GetPGF()->GetCharInfo(charCode, charInfo);

	return 0;
}

// Not sure about the arguments.
int sceFontGetShadowInfo(u32 fontHandle, u32 charCode, u32 shadowCharInfoPtr) {
	ERROR_LOG_REPORT(HLE, "UNIMPL sceFontGetShadowInfo(%08x, %i, %08x)", fontHandle, charCode, shadowCharInfoPtr);
	// TODO
	return 0;
}

int sceFontGetCharImageRect(u32 fontHandle, u32 charCode, u32 charRectPtr) {
	INFO_LOG(HLE, "sceFontGetCharImageRect(%08x, %i, %08x)", fontHandle, charCode, charRectPtr);
	if (!Memory::IsValidAddress(charRectPtr))
		return -1;

	PGFCharInfo charInfo;
	LoadedFont *font = GetLoadedFont(fontHandle, false);
	if (font) {
		font->GetPGF()->GetCharInfo(charCode, &charInfo);
		Memory::Write_U16(charInfo.bitmapWidth, charRectPtr);      // character bitmap width in pixels
		Memory::Write_U16(charInfo.bitmapHeight, charRectPtr + 2);  // character bitmap height in pixels
	} else {
		ERROR_LOG(HLE, "sceFontGetCharImageRect - invalid font");
	}
	return 0;
}

int sceFontGetShadowImageRect(u32 fontHandle, u32 charCode, u32 charRectPtr) {
	ERROR_LOG_REPORT(HLE, "UNIMPL sceFontGetShadowImageRect()");
	return 0;
}

int sceFontGetCharGlyphImage(u32 fontHandle, u32 charCode, u32 glyphImagePtr) {
	if (!Memory::IsValidAddress(glyphImagePtr)) {
		ERROR_LOG(HLE, "sceFontGetCharGlyphImage(%x, %x, %x): bad glyphImage pointer", fontHandle, charCode, glyphImagePtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}
	LoadedFont *font = GetLoadedFont(fontHandle, false);
	if (!font) {
		ERROR_LOG(HLE, "sceFontGetCharGlyphImage(%x, %x, %x): bad font", fontHandle, charCode, glyphImagePtr);
		return ERROR_FONT_INVALID_PARAMETER;
	}

	INFO_LOG(HLE, "sceFontGetCharGlyphImage(%x, %x, %x)", fontHandle, charCode, glyphImagePtr);
	auto glyph = Memory::GetStruct<const GlyphImage>(glyphImagePtr);
	int altCharCode = font->GetFontLib()->GetAltCharCode();
	font->GetPGF()->DrawCharacter(glyph, 0, 0, 8192, 8192, charCode, altCharCode, FONT_PGF_CHARGLYPH);
	return 0;
}

int sceFontGetCharGlyphImage_Clip(u32 fontHandle, u32 charCode, u32 glyphImagePtr, int clipXPos, int clipYPos, int clipWidth, int clipHeight) {
	if (!Memory::IsValidAddress(glyphImagePtr)) {
		ERROR_LOG(HLE, "sceFontGetCharGlyphImage_Clip(%08x, %i, %08x, %i, %i, %i, %i): bad glyphImage pointer", fontHandle, charCode, glyphImagePtr, clipXPos, clipYPos, clipWidth, clipHeight);
		return ERROR_FONT_INVALID_PARAMETER;
	}
	LoadedFont *font = GetLoadedFont(fontHandle, false);
	if (!font) {
		ERROR_LOG(HLE, "sceFontGetCharGlyphImage_Clip(%08x, %i, %08x, %i, %i, %i, %i): bad font", fontHandle, charCode, glyphImagePtr, clipXPos, clipYPos, clipWidth, clipHeight);
		return ERROR_FONT_INVALID_PARAMETER;
	}

	INFO_LOG(HLE, "sceFontGetCharGlyphImage_Clip(%08x, %i, %08x, %i, %i, %i, %i)", fontHandle, charCode, glyphImagePtr, clipXPos, clipYPos, clipWidth, clipHeight);
	auto glyph = Memory::GetStruct<const GlyphImage>(glyphImagePtr);
	int altCharCode = font->GetFontLib()->GetAltCharCode();
	font->GetPGF()->DrawCharacter(glyph, clipXPos, clipYPos, clipXPos + clipWidth, clipYPos + clipHeight, charCode, altCharCode, FONT_PGF_CHARGLYPH);
	return 0;
}

int sceFontSetAltCharacterCode(u32 fontLibHandle, u32 charCode) {
	INFO_LOG(HLE, "sceFontSetAltCharacterCode(%08x) (%08x)", fontLibHandle, charCode);
	FontLib *fl = GetFontLib(fontLibHandle);
	if (fl) {
		fl->SetAltCharCode(charCode);
	}
	return 0;
}

int sceFontFlush(u32 fontHandle) {
	INFO_LOG(HLE, "sceFontFlush(%i)", fontHandle);
	// Probably don't need to do anything here.
	return 0;
}

// One would think that this should loop through the fonts loaded in the fontLibHandle,
// but it seems not.
int sceFontGetFontList(u32 fontLibHandle, u32 fontStylePtr, u32 numFonts) {
	INFO_LOG(HLE, "sceFontGetFontList(%08x, %08x, %i)", fontLibHandle, fontStylePtr, numFonts);
	numFonts = std::min(numFonts, (u32)internalFonts.size());
	for (u32 i = 0; i < numFonts; i++)
	{
		PGFFontStyle style = internalFonts[i]->GetFontStyle();
		Memory::WriteStruct(fontStylePtr, &style);
		fontStylePtr += sizeof(style);
	}
	return 0;
}

int sceFontGetNumFontList(u32 fontLibHandle, u32 errorCodePtr) {	
	INFO_LOG(HLE, "sceFontGetNumFontList(%08x, %08x)", fontLibHandle, errorCodePtr);
	if (Memory::IsValidAddress(errorCodePtr))
		Memory::Write_U32(0, errorCodePtr);
	return (int)internalFonts.size();
}

int sceFontSetResolution(u32 fontLibHandle, float hRes, float vRes) {
	INFO_LOG(HLE, "sceFontSetResolution(%08x, %f, %f)", fontLibHandle, hRes, vRes);
	FontLib *fl = GetFontLib(fontLibHandle);
	if (fl) {
		fl->SetResolution(hRes, vRes);
	}
	return 0;
}

float sceFontPixelToPointH(int fontLibHandle, float fontPixelsH, u32 errorCodePtr) {
	INFO_LOG(HLE, "sceFontPixelToPointH(%08x, %f, %08x)", fontLibHandle, fontPixelsH, errorCodePtr);
	if (Memory::IsValidAddress(errorCodePtr))
		Memory::Write_U32(0, errorCodePtr);
	FontLib *fl = GetFontLib(fontLibHandle);
	if (fl) {
		return fontPixelsH * pointDPI / fl->FontHRes();
	}
	return 0;
}

float sceFontPixelToPointV(int fontLibHandle, float fontPixelsV, u32 errorCodePtr) {
	INFO_LOG(HLE, "UNIMPL sceFontPixelToPointV(%08x, %f, %08x)", fontLibHandle, fontPixelsV, errorCodePtr);
	if (Memory::IsValidAddress(errorCodePtr))
		Memory::Write_U32(0, errorCodePtr);
	FontLib *fl = GetFontLib(fontLibHandle);
	if (fl) {
		return fontPixelsV * pointDPI / fl->FontVRes();
	}
	return 0;
}

float sceFontPointToPixelH(int fontLibHandle, float fontPointsH, u32 errorCodePtr) {
	INFO_LOG(HLE, "UNIMPL sceFontPointToPixelH(%08x, %f, %08x)", fontLibHandle, fontPointsH, errorCodePtr);
	if (Memory::IsValidAddress(errorCodePtr))
		Memory::Write_U32(0, errorCodePtr);
	FontLib *fl = GetFontLib(fontLibHandle);
	if (fl) {
		return fontPointsH * fl->FontHRes() / pointDPI;
	}
	return 0;
}

float sceFontPointToPixelV(int fontLibHandle, float fontPointsV, u32 errorCodePtr) {
	INFO_LOG(HLE, "UNIMPL sceFontPointToPixelV(%08x, %f, %08x)", fontLibHandle, fontPointsV, errorCodePtr);
	if (Memory::IsValidAddress(errorCodePtr))
		Memory::Write_U32(0, errorCodePtr);
	FontLib *fl = GetFontLib(fontLibHandle);
	if (fl) {
		return fontPointsV * fl->FontVRes() / pointDPI;
	}
	return 0;
}

int sceFontCalcMemorySize() {
	ERROR_LOG_REPORT(HLE, "UNIMPL sceFontCalcMemorySize()");
	return 0;
}

int sceFontGetShadowGlyphImage() {
	ERROR_LOG_REPORT(HLE, "UNIMPL sceFontGetShadowGlyphImage()");
	return 0;
}

int sceFontGetShadowGlyphImage_Clip() {
	ERROR_LOG_REPORT(HLE, "UNIMPL sceFontGetShadowGlyphImage_Clip()");
	return 0;
}

const HLEFunction sceLibFont[] = {
	{0x67f17ed7, WrapU_UU<sceFontNewLib>, "sceFontNewLib"},	
	{0x574b6fbc, WrapI_U<sceFontDoneLib>, "sceFontDoneLib"},
	{0x48293280, WrapI_UFF<sceFontSetResolution>, "sceFontSetResolution"},	
	{0x27f6e642, WrapI_UU<sceFontGetNumFontList>, "sceFontGetNumFontList"},
	{0xbc75d85b, WrapI_UUU<sceFontGetFontList>, "sceFontGetFontList"},	
	{0x099ef33c, WrapI_UUU<sceFontFindOptimumFont>, "sceFontFindOptimumFont"},	
	{0x681e61a7, WrapI_UUU<sceFontFindFont>, "sceFontFindFont"},	
	{0x2f67356a, WrapI_V<sceFontCalcMemorySize>, "sceFontCalcMemorySize"},	
	{0x5333322d, WrapI_UUUU<sceFontGetFontInfoByIndexNumber>, "sceFontGetFontInfoByIndexNumber"},
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
	{0x568be516, WrapI_V<sceFontGetShadowGlyphImage>, "sceFontGetShadowGlyphImage"},
	{0x5dcf6858, WrapI_V<sceFontGetShadowGlyphImage_Clip>, "sceFontGetShadowGlyphImage_Clip"},
	{0x02d7f94b, WrapI_U<sceFontFlush>, "sceFontFlush"},
};

void Register_sceFont() {
	RegisterModule("sceLibFont", ARRAY_SIZE(sceLibFont), sceLibFont);
}

