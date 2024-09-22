// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <algorithm>

#include "ext/xxhash.h"

#include "Common/System/System.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Data/Format/ZIMLoad.h"
#include "Common/Data/Format/PNGLoad.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Common/BitScan.h"
#include "Core/HDRemaster.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/Util/PPGeDraw.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceGe.h"
#include "Core/MemMapHelpers.h"
#include "Core/System.h"

static Atlas g_ppge_atlas;
static Draw::DrawContext *g_draw = nullptr;

static u32 atlasPtr;
static int atlasWidth;
static int atlasHeight;
static uint64_t atlasHash;
static bool atlasRequiresReset;

struct PPGeVertex {
	u16_le u, v;
	u32_le color;
	float_le x, y, z;
};

struct PPGeRemasterVertex {
	float_le u, v;
	u32_le color;
	float_le x, y, z;
};

static PSPPointer<PspGeListArgs> listArgs;
static u32 listArgsSize = sizeof(PspGeListArgs);
static u32 savedContextPtr;
static u32 savedContextSize = 512 * 4;

// Display list writer
static u32 dlPtr;
static u32 dlWritePtr;
static u32 dlSize = 0x10000; // should be enough for a frame of gui...

static u32 dataPtr;
static u32 dataWritePtr;
static u32 dataSize = 0x10000; // should be enough for a frame of gui...

static PSPPointer<u16_le> palette;
static u32 paletteSize = sizeof(u16) * 16;

// Vertex collector
static u32 vertexStart;
static u32 vertexCount;

// Used for formatting text
struct AtlasCharVertex
{
	float x;
	float y;
	const AtlasChar *c;
};

struct AtlasTextMetrics
{
	float x;
	float y;
	float maxWidth;
	float lineHeight;
	float scale;
	int numLines;

};

typedef std::vector<AtlasCharVertex> AtlasCharLine;
typedef std::vector<AtlasCharLine> AtlasLineArray;

static AtlasCharLine char_one_line;
static AtlasLineArray char_lines;
static AtlasTextMetrics char_lines_metrics;

static bool textDrawerInited = false;
static TextDrawer *textDrawer = nullptr;
struct PPGeTextDrawerCacheKey {
	bool operator < (const PPGeTextDrawerCacheKey &other) const {
		if (align != other.align)
			return align < other.align;
		if (wrapWidth != other.wrapWidth)
			return wrapWidth < other.wrapWidth;
		return text < other.text;
	}
	std::string text;
	int align;
	float wrapWidth;
};

struct PPGeTextDrawerImage {
	PPGeTextDrawerImage() : entry(0) {}
	TextStringEntry entry;
	u32 ptr;
};

static std::map<PPGeTextDrawerCacheKey, PPGeTextDrawerImage> textDrawerImages;

void PPGeSetDrawContext(Draw::DrawContext *draw) {
	g_draw = draw;
}

// Overwrite the current text lines buffer so it can be drawn later.
void PPGePrepareText(std::string_view text, float x, float y, PPGeAlign align, float scale, float lineHeightScale,
	int WrapType = PPGE_LINE_NONE, int wrapWidth = 0);

// These functions must be called between PPGeBegin and PPGeEnd.

// Draw currently buffered text using the state from PPGeGetTextBoundingBox() call.
// Clears the buffer and state when done.
void PPGeDrawCurrentText(u32 color = 0xFFFFFFFF);

static void PPGeDecimateTextImages(int age = 97);

void PPGeSetTexture(u32 dataAddr, int width, int height);

//only 0xFFFFFF of data is used
static void WriteCmd(u8 cmd, u32 data) {
	Memory::Write_U32((cmd << 24) | (data & 0xFFFFFF), dlWritePtr);
	dlWritePtr += 4;
	_dbg_assert_(dlWritePtr <= dlPtr + dlSize);
}

static void WriteCmdAddrWithBase(u8 cmd, u32 addr) {
	WriteCmd(GE_CMD_BASE, (addr >> 8) & 0xFF0000);
	WriteCmd(cmd, addr & 0xFFFFFF);
}

/*
static void WriteCmdFloat(u8 cmd, float f) {
	union {
		float fl;
		u32 u;
	} conv;
	conv.fl = f;
	WriteCmd(cmd, conv.u >> 8);
}*/

static void BeginVertexData() {
	vertexCount = 0;
	vertexStart = dataWritePtr;
}

static void Vertex(float x, float y, float u, float v, int tw, int th, u32 color = 0xFFFFFFFF) {
	if (g_RemasterMode) {
		auto vtx = PSPPointer<PPGeRemasterVertex>::Create(dataWritePtr);
		vtx->x = x; vtx->y = y; vtx->z = 0;
		vtx->u = u * tw; vtx->v = v * th;
		vtx->color = color;
		dataWritePtr += (u32)vtx.ElementSize();
	} else {
		auto vtx = PSPPointer<PPGeVertex>::Create(dataWritePtr);
		vtx->x = x; vtx->y = y; vtx->z = 0;
		vtx->u = u * tw; vtx->v = v * th;
		vtx->color = color;
		dataWritePtr += (u32)vtx.ElementSize();
	}
	_dbg_assert_(dataWritePtr <= dataPtr + dataSize);
	vertexCount++;
}

static void EndVertexDataAndDraw(int prim) {
	_assert_msg_(vertexStart != 0, "Missing matching call to BeginVertexData()");
	if (vertexCount != 0) {
		NotifyMemInfo(MemBlockFlags::WRITE, vertexStart, dataWritePtr - vertexStart, "PPGe Vertex");
		WriteCmdAddrWithBase(GE_CMD_VADDR, vertexStart);
		WriteCmd(GE_CMD_PRIM, (prim << 16) | vertexCount);
	}
	vertexStart = 0;
}

bool PPGeIsFontTextureAddress(u32 addr) {
	return addr == atlasPtr;
}

static u32 __PPGeDoAlloc(u32 &size, bool fromTop, const char *name) {
	u32 ptr = kernelMemory.Alloc(size, fromTop, name);
	// Didn't get it, try again after decimating images.
	if (ptr == (u32)-1) {
		PPGeDecimateTextImages(4);
		PPGeImage::Decimate(4);

		ptr = kernelMemory.Alloc(size, fromTop, name);
		if (ptr == (u32)-1) {
			return 0;
		}
	}
	return ptr;
}

void __PPGeSetupListArgs()
{
	if (listArgs.IsValid())
		return;

	listArgs = __PPGeDoAlloc(listArgsSize, false, "PPGe List Args");
	if (listArgs.IsValid()) {
		listArgs->size = 8;
		if (savedContextPtr == 0)
			savedContextPtr = __PPGeDoAlloc(savedContextSize, false, "PPGe Saved Context");
		listArgs->context = savedContextPtr;
	}
}

void __PPGeInit() {
	// PPGe isn't really important for headless, and LoadZIM takes a long time.
	bool skipZIM = System_GetPropertyBool(SYSPROP_SKIP_UI);

	u8 *imageData[12]{};
	int width[12]{};
	int height[12]{};
	int flags = 0;

	bool loadedZIM = !skipZIM && LoadZIM("ppge_atlas.zim", width, height, &flags, imageData);
	if (!skipZIM && !loadedZIM) {
		ERROR_LOG(Log::sceGe, "Failed to load ppge_atlas.zim.\n\nPlace it in the directory \"assets\" under your PPSSPP directory.\n\nPPGe stuff will not be drawn.");
	}

	if (loadedZIM) {
		size_t atlas_data_size;
		if (!g_ppge_atlas.IsMetadataLoaded()) {
			uint8_t *atlas_data = g_VFS.ReadFile("ppge_atlas.meta", &atlas_data_size);
			if (atlas_data)
				g_ppge_atlas.Load(atlas_data, atlas_data_size);
			delete[] atlas_data;
		}
	}

	u32 atlasSize = height[0] * width[0] / 2;  // it's a 4-bit paletted texture in ram
	atlasWidth = width[0];
	atlasHeight = height[0];
	dlPtr = __PPGeDoAlloc(dlSize, false, "PPGe Display List");
	dataPtr = __PPGeDoAlloc(dataSize, false, "PPGe Vertex Data");
	__PPGeSetupListArgs();
	atlasPtr = atlasSize == 0 ? 0 : __PPGeDoAlloc(atlasSize, false, "PPGe Atlas Texture");
	palette = __PPGeDoAlloc(paletteSize, false, "PPGe Texture Palette");

	// Generate 16-greyscale palette. All PPGe graphics are greyscale so we can use a tiny paletted texture.
	for (int i = 0; i < 16; i++) {
		int val = i;
		palette[i] = (val << 12) | 0xFFF;
	}
	NotifyMemInfo(MemBlockFlags::WRITE, palette.ptr, 16 * sizeof(u16_le), "PPGe Palette");

	const u32_le *imagePtr = (u32_le *)imageData[0];
	u8 *ramPtr = atlasPtr == 0 ? nullptr : (u8 *)Memory::GetPointerRange(atlasPtr, atlasSize);

	// Palettize to 4-bit, the easy way.
	for (int i = 0; i < width[0] * height[0] / 2; i++) {
		// Each pixel is 16 bits, so this loads two pixels.
		u32 c = imagePtr[i];
		// It's white anyway, so we only look at one channel of each pixel.
		int a1 = (c & 0x0000000F) >> 0;
		int a2 = (c & 0x000F0000) >> 16;
		u8 cval = (a2 << 4) | a1;
		ramPtr[i] = cval;
	}
	if (atlasPtr != 0) {
		atlasHash = XXH3_64bits(ramPtr, atlasSize);
		NotifyMemInfo(MemBlockFlags::WRITE, atlasPtr, atlasSize, "PPGe Atlas");
	}

	free(imageData[0]);

	// We can't create it here, because Android needs it on the right thread.
	// Avoid creating ever on headless just to be safe.
	textDrawerInited = PSP_CoreParameter().headLess;
	textDrawer = nullptr;
	textDrawerImages.clear();

	atlasRequiresReset = false;

	INFO_LOG(Log::sceGe, "PPGe drawing library initialized. DL: %08x Data: %08x Atlas: %08x (%i) Args: %08x",
		dlPtr, dataPtr, atlasPtr, atlasSize, listArgs.ptr);
}

void __PPGeDoState(PointerWrap &p)
{
	auto s = p.Section("PPGeDraw", 1, 4);
	if (!s)
		return;

	Do(p, atlasPtr);
	Do(p, atlasWidth);
	Do(p, atlasHeight);
	Do(p, palette);

	// If the atlas the save state was created with differs from the current one, reload.
	uint64_t savedHash = atlasHash;
	if (s >= 4) {
		Do(p, savedHash);
	} else {
		// Memory was already updated by this point, so check directly.
		if (atlasPtr != 0) {
			savedHash = XXH3_64bits(Memory::GetPointerRange(atlasPtr, atlasWidth * atlasHeight / 2), atlasWidth * atlasHeight / 2);
		} else {
			savedHash ^= 1;
		}
	}
	atlasRequiresReset = savedHash != atlasHash;

	Do(p, savedContextPtr);
	Do(p, savedContextSize);

	if (s == 1) {
		listArgs = 0;
	} else {
		Do(p, listArgs);
	}

	if (s >= 3) {
		uint32_t sz = (uint32_t)textDrawerImages.size();
		Do(p, sz);

		switch (p.mode) {
		case PointerWrap::MODE_READ:
			textDrawerImages.clear();
			for (uint32_t i = 0; i < sz; ++i) {
				// We only care about the pointers, so we can free them.  We'll decimate right away.
				PPGeTextDrawerCacheKey key{ StringFromFormat("__savestate__%d", i), -1, -1 };
				textDrawerImages[key] = PPGeTextDrawerImage{};
				Do(p, textDrawerImages[key].ptr);
			}
			break;
		default:
			for (const auto &im : textDrawerImages) {
				Do(p, im.second.ptr);
			}
			break;
		}
	} else {
		textDrawerImages.clear();
	}

	Do(p, dlPtr);
	Do(p, dlWritePtr);
	Do(p, dlSize);

	Do(p, dataPtr);
	Do(p, dataWritePtr);
	Do(p, dataSize);

	Do(p, vertexStart);
	Do(p, vertexCount);

	Do(p, char_lines);
	Do(p, char_lines_metrics);
}

void __PPGeShutdown()
{
	if (atlasPtr)
		kernelMemory.Free(atlasPtr);
	if (dataPtr)
		kernelMemory.Free(dataPtr);
	if (dlPtr)
		kernelMemory.Free(dlPtr);
	if (listArgs.IsValid())
		kernelMemory.Free(listArgs.ptr);
	if (savedContextPtr)
		kernelMemory.Free(savedContextPtr);
	if (palette)
		kernelMemory.Free(palette.ptr);

	atlasPtr = 0;
	dataPtr = 0;
	dlPtr = 0;
	savedContextPtr = 0;
	listArgs = 0;

	delete textDrawer;
	textDrawer = nullptr;

	for (auto im : textDrawerImages)
		kernelMemory.Free(im.second.ptr);
	textDrawerImages.clear();
}

void PPGeBegin()
{
	if (!dlPtr)
		return;

	// Reset write pointers to start of command and data buffers.
	dlWritePtr = dlPtr;
	dataWritePtr = dataPtr;

	// Set up the correct states for UI drawing
	WriteCmd(GE_CMD_OFFSETADDR, 0);
	WriteCmd(GE_CMD_ALPHABLENDENABLE, 1);
	WriteCmd(GE_CMD_BLENDMODE, 2 | (3 << 4));
	WriteCmd(GE_CMD_ALPHATESTENABLE, 0);
	WriteCmd(GE_CMD_COLORTESTENABLE, 0); 
	WriteCmd(GE_CMD_ZTESTENABLE, 0);
	WriteCmd(GE_CMD_LIGHTINGENABLE, 0);
	WriteCmd(GE_CMD_FOGENABLE, 0);
	WriteCmd(GE_CMD_STENCILTESTENABLE, 0);
	WriteCmd(GE_CMD_CULLFACEENABLE, 0);
	WriteCmd(GE_CMD_CLEARMODE, 0);  // Normal mode
	WriteCmd(GE_CMD_MASKRGB, 0);
	WriteCmd(GE_CMD_MASKALPHA, 0);
	WriteCmd(GE_CMD_DITHERENABLE, 0);

	PPGeSetDefaultTexture();

	PPGeScissor(0, 0, 480, 272);
	WriteCmd(GE_CMD_MINZ, 0);
	WriteCmd(GE_CMD_MAXZ, 0xFFFF);

	// Through mode, so we don't have to bother with matrices
	if (g_RemasterMode) {
		WriteCmd(GE_CMD_VERTEXTYPE, GE_VTYPE_TC_FLOAT | GE_VTYPE_COL_8888 | GE_VTYPE_POS_FLOAT | GE_VTYPE_THROUGH);
	} else {
		WriteCmd(GE_CMD_VERTEXTYPE, GE_VTYPE_TC_16BIT | GE_VTYPE_COL_8888 | GE_VTYPE_POS_FLOAT | GE_VTYPE_THROUGH);
	}
}

void PPGeEnd()
{
	if (!dlPtr)
		return;

	WriteCmd(GE_CMD_FINISH, 0);
	WriteCmd(GE_CMD_END, 0);

	// Might've come from an old savestate.
	__PPGeSetupListArgs();

	if (dataWritePtr > dataPtr) {
		// We actually drew something
		gpu->EnableInterrupts(false);
		NotifyMemInfo(MemBlockFlags::WRITE, dlPtr, dlWritePtr - dlPtr, "PPGe ListCmds");
		u32 list = sceGeListEnQueue(dlPtr, dlWritePtr, -1, listArgs.ptr);
		DEBUG_LOG(Log::sceGe, "PPGe enqueued display list %i", list);
		gpu->EnableInterrupts(true);
	}
}

void PPGeScissor(int x1, int y1, int x2, int y2) {
	_dbg_assert_(x1 >= 0 && x1 <= 480 && x2 >= 0 && x2 <= 480);
	_dbg_assert_(y1 >= 0 && y1 <= 272 && y2 >= 0 && y2 <= 272);
	WriteCmd(GE_CMD_SCISSOR1, (y1 << 10) | x1);
	WriteCmd(GE_CMD_SCISSOR2, ((y2 - 1) << 10) | (x2 - 1));
}

void PPGeScissorReset() {
	PPGeScissor(0, 0, 480, 272);
}

static const AtlasChar *PPGeGetChar(const AtlasFont &atlasfont, unsigned int cval)
{
	const AtlasChar *c = atlasfont.getChar(cval);
	if (c == NULL) {
		// Try to use a replacement character, these come from the below table.
		// http://unicode.org/cldr/charts/supplemental/character_fallback_substitutions.html
		switch (cval) {
		case 0x00A0: // NO-BREAK SPACE
		case 0x2000: // EN QUAD
		case 0x2001: // EM QUAD
		case 0x2002: // EN SPACE
		case 0x2003: // EM SPACE
		case 0x2004: // THREE-PER-EM SPACE
		case 0x2005: // FOUR-PER-EM SPACE
		case 0x2006: // SIX-PER-EM SPACE
		case 0x2007: // FIGURE SPACE
		case 0x2008: // PUNCTUATION SPACE
		case 0x2009: // THIN SPACE
		case 0x200A: // HAIR SPACE
		case 0x202F: // NARROW NO-BREAK SPACE
		case 0x205F: // MEDIUM MATHEMATICAL
		case 0x3000: // IDEOGRAPHIC SPACE
			c = atlasfont.getChar(0x0020);
			break;

		default:
			c = atlasfont.getChar(0xFFFD);
			break;
		}
		if (c == NULL)
			c = atlasfont.getChar('?');
	}
	return c;
}

// Break a single text string into mutiple lines.
static AtlasTextMetrics BreakLines(std::string_view text, const AtlasFont &atlasfont, float x, float y, 
									PPGeAlign align, float scale, float lineHeightScale, int wrapType, float wrapWidth, bool dryRun)
{
	y += atlasfont.ascend * scale;
	float sx = x, sy = y;

	// TODO: Can we wrap it smartly on the screen edge?
	if (wrapWidth <= 0) {
		wrapWidth = 480.f;
	}

	// used for replacing with ellipses
	float wrapCutoff = 8.0f;
	const AtlasChar *dot = PPGeGetChar(atlasfont, '.');
	if (dot) {
		wrapCutoff = dot->wx * scale * 3.0f;
	}
	float threshold = sx + wrapWidth - wrapCutoff;

	//const float wrapGreyZone = 2.0f; // Grey zone for punctuations at line ends

	int numLines = 1;
	float maxw = 0;
	float lineHeight = atlasfont.height * lineHeightScale;
	for (UTF8 utf(text); !utf.end(); )
	{
		float lineWidth = 0;
		bool skipRest = false;
		while (!utf.end())
		{
			UTF8 utfWord(utf);
			float nextWidth = 0;
			float spaceWidth = 0;
			int numChars = 0;
			bool finished = false;
			while (!utfWord.end() && !finished)
			{
				UTF8 utfPrev = utfWord;
				u32 cval = utfWord.next();
				const AtlasChar *ch = PPGeGetChar(atlasfont, cval);
				if (!ch) {
					continue;
				}

				switch (cval) {
				// TODO: This list of punctuation is very incomplete.
				case ',':
				case '.':
				case ':':
				case '!':
				case ')':
				case '?':
				case 0x3001: // IDEOGRAPHIC COMMA
				case 0x3002: // IDEOGRAPHIC FULL STOP
				case 0x06D4: // ARABIC FULL STOP
				case 0xFF01: // FULLWIDTH EXCLAMATION MARK
				case 0xFF09: // FULLWIDTH RIGHT PARENTHESIS
				case 0xFF1F: // FULLWIDTH QUESTION MARK
					// Count this character (punctuation is so clingy), but then we're done.
					++numChars;
					nextWidth += ch->wx * scale;
					finished = true;
					break;

				case ' ':
				case 0x3000: // IDEOGRAPHIC SPACE
					spaceWidth += ch->wx * scale;
					finished = true;
					break;

				case '\t':
				case '\r':
				case '\n':
					// Ignore this character and we're done.
					finished = true;
					break;

				default:
					{
						// CJK characters can be wrapped more freely.
						bool isCJK = (cval >= 0x1100 && cval <= 0x11FF); // Hangul Jamo.
						isCJK = isCJK || (cval >= 0x2E80 && cval <= 0x2FFF); // Kangxi Radicals etc.
#if 0
						isCJK = isCJK || (cval >= 0x3040 && cval <= 0x31FF); // Hiragana, Katakana, Hangul Compatibility Jamo etc.
						isCJK = isCJK || (cval >= 0x3200 && cval <= 0x32FF); // CJK Enclosed
						isCJK = isCJK || (cval >= 0x3300 && cval <= 0x33FF); // CJK Compatibility
						isCJK = isCJK || (cval >= 0x3400 && cval <= 0x4DB5); // CJK Unified Ideographs Extension A
#else
						isCJK = isCJK || (cval >= 0x3040 && cval <= 0x4DB5); // Above collapsed
#endif
						isCJK = isCJK || (cval >= 0x4E00 && cval <= 0x9FBB); // CJK Unified Ideographs
						isCJK = isCJK || (cval >= 0xAC00 && cval <= 0xD7AF); // Hangul Syllables
						isCJK = isCJK || (cval >= 0xF900 && cval <= 0xFAD9); // CJK Compatibility Ideographs
						isCJK = isCJK || (cval >= 0x20000 && cval <= 0x2A6D6); // CJK Unified Ideographs Extension B
						isCJK = isCJK || (cval >= 0x2F800 && cval <= 0x2FA1D); // CJK Compatibility Supplement
						if (isCJK) {
							if (numChars > 0) {
								utfWord = utfPrev;
								finished = true;
								break;
							}
						}
					}
					++numChars;
					nextWidth += ch->wx * scale;
					break;
				}
			}

			bool useEllipsis = false;
			if (wrapType > 0)
			{
				if (lineWidth + nextWidth > wrapWidth || skipRest)
				{
					if (wrapType & PPGE_LINE_WRAP_WORD) {
						// TODO: Should check if we have had at least one other word instead.
						if (lineWidth > 0) {
							++numLines;
							break;
						}
					}
					if (wrapType & PPGE_LINE_USE_ELLIPSIS) {
						useEllipsis = true;
						if (skipRest) {
							numChars = 0;
						} else if (nextWidth < wrapCutoff) {
							// The word is too short, so just backspace!
							x = threshold;
						}
						nextWidth = 0;
						spaceWidth = 0;
						lineWidth = wrapWidth;
					}
				}
			}
			for (int i = 0; i < numChars; ++i)
			{
				u32 cval = utf.next();
				const AtlasChar *c = PPGeGetChar(atlasfont, cval);
				if (c)
				{
					if (useEllipsis && x >= threshold && dot)
					{
						if (!dryRun)
						{
							AtlasCharVertex cv;
							// Replace the following part with an ellipsis.
							cv.x = x + dot->ox * scale;
							cv.y = y + dot->oy * scale;
							cv.c = dot;
							char_one_line.push_back(cv);
							cv.x += dot->wx * scale;
							char_one_line.push_back(cv);
							cv.x += dot->wx * scale;
							char_one_line.push_back(cv);
						}
						skipRest = true;
						break;
					}
					if (!dryRun)
					{
						AtlasCharVertex cv;
						cv.x = x + c->ox * scale;
						cv.y = y + c->oy * scale;
						cv.c = c;
						char_one_line.push_back(cv);
					}
					x += c->wx * scale;
				}
			}
			lineWidth += nextWidth;

			u32 cval = utf.end() ? 0 : utf.next();
			if (spaceWidth > 0)
			{
				if (!dryRun)
				{
					// No need to check c.
					const AtlasChar *c = PPGeGetChar(atlasfont, cval);
					AtlasCharVertex cv;
					cv.x = x + c->ox * scale;
					cv.y = y + c->oy * scale;
					cv.c = c;
					char_one_line.push_back(cv);
				}
				x += spaceWidth;
				lineWidth += spaceWidth;
				if (wrapType > 0 && lineWidth > wrapWidth) {
					lineWidth = wrapWidth;
				}
			}
			else if (cval == '\n') {
				++numLines;
				break;
			}
			utf = utfWord;
		}
		y += lineHeight;
		x = sx;
		if (lineWidth > maxw) {
			maxw = lineWidth;
		}
		if (!dryRun)
		{
			char_lines.push_back(char_one_line);
			char_one_line.clear();
		}
	}

	const float w = maxw;
	const float h = (float)numLines * lineHeight;
	if (align & PPGeAlign::ANY) {
		if (!dryRun) {
			for (auto i = char_lines.begin(); i != char_lines.end(); ++i) {
				for (auto j = i->begin(); j != i->end(); ++j) {
					if (align & PPGeAlign::BOX_HCENTER) j->x -= w / 2.0f;
					else if (align & PPGeAlign::BOX_RIGHT) j->x -= w;

					if (align & PPGeAlign::BOX_VCENTER) j->y -= h / 2.0f;
					else if (align & PPGeAlign::BOX_BOTTOM) j->y -= h;
				}
			}
		}
		if (align & PPGeAlign::BOX_HCENTER) sx -= w / 2.0f;
		else if (align & PPGeAlign::BOX_RIGHT) sx -= w;
		if (align & PPGeAlign::BOX_VCENTER) sy -= h / 2.0f;
		else if (align & PPGeAlign::BOX_BOTTOM) sy -= h;
	}

	AtlasTextMetrics metrics = { sx, sy, w, lineHeight, scale, numLines };
	return metrics;
}

static bool HasTextDrawer() {
	// We create this on first use so it's on the correct thread.
	if (textDrawerInited) {
		return textDrawer != nullptr;
	}

	// Should we pass a draw_? Yes! UWP requires it.
	textDrawer = TextDrawer::Create(g_draw);
	if (textDrawer) {
		textDrawer->SetFontScale(1.0f, 1.0f);
		textDrawer->SetForcedDPIScale(1.0f);
		textDrawer->SetFont(g_Config.sFont.c_str(), 18, 0);
	}
	textDrawerInited = true;
	return textDrawer != nullptr;
}

static std::string PPGeSanitizeText(std::string_view text) {
	return SanitizeUTF8(text);
}

void PPGeMeasureText(float *w, float *h, std::string_view text, float scale, int WrapType, int wrapWidth) {
	std::string s = PPGeSanitizeText(text);

	if (HasTextDrawer()) {
		float mw, mh;
		textDrawer->SetFontScale(scale, scale);
		int dtalign = (WrapType & PPGE_LINE_WRAP_WORD) ? FLAG_WRAP_TEXT : 0;
		if (WrapType & PPGE_LINE_USE_ELLIPSIS)
			dtalign |= FLAG_ELLIPSIZE_TEXT;
		Bounds b(0, 0, wrapWidth <= 0 ? 480.0f : wrapWidth, 272.0f);
		textDrawer->MeasureStringRect(s, b, &mw, &mh, dtalign);

		if (w)
			*w = mw;
		if (h)
			*h = mh;
		return;
	}

	if (!g_ppge_atlas.IsMetadataLoaded() || g_ppge_atlas.num_fonts < 1) {
		if (w)
			*w = 0;
		if (h)
			*h = 0;
		return;
	}

	const AtlasFont &atlasfont = g_ppge_atlas.fonts[0];
	AtlasTextMetrics metrics = BreakLines(s.c_str(), atlasfont, 0, 0, PPGeAlign::BOX_TOP, scale, scale, WrapType, wrapWidth, true);
	if (w) *w = metrics.maxWidth;
	if (h) *h = metrics.lineHeight * metrics.numLines;
}

void PPGePrepareText(std::string_view text, float x, float y, PPGeAlign align, float scale, float lineHeightScale, int WrapType, int wrapWidth)
{
	const AtlasFont &atlasfont = g_ppge_atlas.fonts[0];
	if (!g_ppge_atlas.IsMetadataLoaded() || g_ppge_atlas.num_fonts < 1) {
		return;
	}
	char_lines_metrics = BreakLines(text, atlasfont, x, y, align, scale, lineHeightScale, WrapType, wrapWidth, false);
}

static void PPGeResetCurrentText() {
	char_one_line.clear();
	char_lines.clear();
	AtlasTextMetrics zeroBox = { 0 };
	char_lines_metrics = zeroBox;
}

// Draws some text using the one font we have in the atlas.
void PPGeDrawCurrentText(u32 color) {
	// If the atlas is larger than 512x512, need to use windows into it.
	bool useTextureWindow = g_Config.bSoftwareRendering && (atlasWidth > 512 || atlasHeight > 512);
	uint32_t texturePosX = 0;
	uint32_t texturePosY = 0;

	// Use half the available size just in case a character straddles a boundary.
	const float textureMaxPosX = !useTextureWindow ? 1.0f : atlasWidth / 256.0f;
	const float textureMaxPosY = !useTextureWindow ? 1.0f : atlasHeight / 256.0f;
	// These are the actual scale used.
	const float textureWindowW = !useTextureWindow ? atlasWidth : 512.0f;
	const float textureWindowH = !useTextureWindow ? atlasHeight : 512.0f;
	const float textureScaleX = !useTextureWindow ? 1.0f : atlasWidth / 512.0f;
	const float textureScaleY = !useTextureWindow ? 1.0f : atlasWidth / 512.0f;

	if (dlPtr) {
		float scale = char_lines_metrics.scale;

		if (useTextureWindow) {
			WriteCmd(GE_CMD_TEXWRAP, 0);
			WriteCmd(GE_CMD_TEXSIZE0, 9 | (9 << 8));
		}

		BeginVertexData();
		for (auto i = char_lines.begin(); i != char_lines.end(); ++i) {
			for (auto j = i->begin(); j != i->end(); ++j) {
				const AtlasChar &c = *j->c;

				int wantedPosX = (int)floorf(c.sx * textureMaxPosX);
				int wantedPosY = (int)floorf(c.sy * textureMaxPosY);
				if (useTextureWindow && (wantedPosX != texturePosX || wantedPosY != texturePosY)) {
					EndVertexDataAndDraw(GE_PRIM_RECTANGLES);

					uint32_t offset = atlasWidth * wantedPosY * 256 + wantedPosX * 256;
					WriteCmd(GE_CMD_TEXADDR0, (atlasPtr & 0xFFFFF0) + offset / 2);
					texturePosX = wantedPosX;
					texturePosY = wantedPosY;

					BeginVertexData();
				}

				float sx = (c.sx - texturePosX / textureMaxPosX) * textureScaleX;
				float sy = (c.sy - texturePosY / textureMaxPosY) * textureScaleY;
				float ex = (c.ex - texturePosX / textureMaxPosX) * textureScaleX;
				float ey = (c.ey - texturePosY / textureMaxPosY) * textureScaleY;

				float cx1 = j->x;
				float cy1 = j->y;
				float cx2 = cx1 + c.pw * scale;
				float cy2 = cy1 + c.ph * scale;
				Vertex(cx1, cy1, sx, sy, textureWindowW, textureWindowH, color);
				Vertex(cx2, cy2, ex, ey, textureWindowW, textureWindowH, color);
			}
		}
		EndVertexDataAndDraw(GE_PRIM_RECTANGLES);
	}
	PPGeResetCurrentText();
	if (useTextureWindow) {
		PPGeSetDefaultTexture();
	}
}

// Return a value such that (1 << value) >= x
inline int GetPow2(int x) {
	int ret = 31 - clz32_nonzero(x | 1);
	if ((1 << ret) < x)
		ret++;
	return ret;
}

static PPGeTextDrawerImage PPGeGetTextImage(std::string_view text, const PPGeStyle &style, float maxWidth, bool wrap) {
	int tdalign = 0;
	tdalign |= FLAG_ELLIPSIZE_TEXT;
	if (wrap) {
		tdalign |= FLAG_WRAP_TEXT;
	}

	PPGeTextDrawerCacheKey key{ std::string(text), tdalign, maxWidth / style.scale };
	PPGeTextDrawerImage im{};

	auto cacheItem = textDrawerImages.find(key);
	if (cacheItem != textDrawerImages.end()) {
		im = cacheItem->second;
		cacheItem->second.entry.lastUsedFrame = gpuStats.numFlips;
	} else {
		std::vector<uint8_t> bitmapData;
		textDrawer->SetFontScale(style.scale, style.scale);
		Bounds b(0, 0, maxWidth, 272.0f);
		std::string cleaned = ReplaceAll(text, "\r", "");
		textDrawer->DrawStringBitmapRect(bitmapData, im.entry, Draw::DataFormat::R8_UNORM, cleaned.c_str(), b, tdalign, false);

		int bufwBytes = ((im.entry.bmWidth + 31) / 32) * 16;
		u32 sz = bufwBytes * (im.entry.bmHeight + 1);
		u32 origSz = sz;
		im.ptr = __PPGeDoAlloc(sz, true, "PPGeText");

		if (bitmapData.size() & 1)
			bitmapData.resize(bitmapData.size() + 1);

		if (im.ptr) {
			int wBytes = (im.entry.bmWidth + 1) / 2;
			u8 *ramPtr = Memory::GetPointerWriteRange(im.ptr, sz);
			for (int y = 0; y < im.entry.bmHeight; ++y) {
				for (int x = 0; x < wBytes; ++x) {
					uint8_t c1 = bitmapData[y * im.entry.bmWidth + x * 2];
					uint8_t c2 = bitmapData[y * im.entry.bmWidth + x * 2 + 1];
					// Convert this to 4-bit palette values.
					ramPtr[y * bufwBytes + x] = (c2 & 0xF0) | (c1 >> 4);
				}
				if (bufwBytes != wBytes) {
					memset(ramPtr + y * bufwBytes + wBytes, 0, bufwBytes - wBytes);
				}
			}
			memset(ramPtr + im.entry.bmHeight * bufwBytes, 0, bufwBytes + sz - origSz);
		}

		im.entry.lastUsedFrame = gpuStats.numFlips;
		textDrawerImages[key] = im;
	}

	return im;
}

static void PPGeDrawTextImage(const PPGeTextDrawerImage &im, float x, float y, const PPGeStyle &style) {
	if (!im.ptr) {
		return;
	}

	// Sometimes our texture can be larger than 512 wide, which means we need to tile it.
	int bufw = ((im.entry.bmWidth + 31) / 32) * 32;
	int tile1bmWidth = std::min(512, im.entry.bmWidth);
	int tile2bmWidth = tile1bmWidth == 512 ? im.entry.bmWidth - 512 : 0;
	bool hasTile2 = tile2bmWidth != 0;

	// Now figure the tile shift value.
	int tile1wp2 = GetPow2(tile1bmWidth);
	int tile2wp2 = hasTile2 ? GetPow2(tile2bmWidth) : 0;
	int hp2 = GetPow2(im.entry.bmHeight);

	WriteCmd(GE_CMD_TEXADDR0, im.ptr & 0xFFFFF0);
	WriteCmd(GE_CMD_TEXBUFWIDTH0, bufw | ((im.ptr & 0xFF000000) >> 8));
	WriteCmd(GE_CMD_TEXSIZE0, tile1wp2 | (hp2 << 8));
	WriteCmd(GE_CMD_TEXFLUSH, 0);

	float w = im.entry.width * style.scale;
	float h = im.entry.height * style.scale;

	if (style.align & PPGeAlign::BOX_HCENTER)
		x -= w / 2.0f;
	else if (style.align & PPGeAlign::BOX_RIGHT)
		x -= w;
	if (style.align & PPGeAlign::BOX_VCENTER)
		y -= h / 2.0f;
	else if (style.align & PPGeAlign::BOX_BOTTOM)
		y -= h;

	BeginVertexData();
	float tile2x = 512 * style.scale;
	float tile1w = hasTile2 ? 512.0f * style.scale : w;
	float tile2w = hasTile2 ? w - tile1w : 0.0f;
	float tile1u1 = hasTile2 ? 1.0f : (float)im.entry.width / (1 << tile1wp2);
	float tile2u1 = hasTile2 ? (float)(im.entry.width - 512) / (1 << tile2wp2) : 0.0f;
	float v1 = (float)im.entry.height / (1 << hp2);
	if (style.hasShadow) {
		// Draw more shadows for a blurrier shadow.
		u32 shadowColor = alphaMul(style.shadowColor, 0.35f);
		for (float dy = 0.0f; dy <= 2.0f; dy += 1.0f) {
			for (float dx = 0.0f; dx <= 1.0f; dx += 0.5f) {
				if (dx == 0.0f && dy == 0.0f)
					continue;

				Vertex(x + dx, y + dy, 0, 0, 1 << tile1wp2, 1 << hp2, shadowColor);
				Vertex(x + dx + tile1w, y + dy + h, tile1u1, v1, 1 << tile1wp2, 1 << hp2, shadowColor);
			}
		}

		// Did we need another tile?
		if (hasTile2) {
			// Flush to change to tile2.
			EndVertexDataAndDraw(GE_PRIM_RECTANGLES);
			BeginVertexData();
			WriteCmd(GE_CMD_TEXADDR0, (im.ptr & 0xFFFFF0) + 256);
			WriteCmd(GE_CMD_TEXSIZE0, tile2wp2 | (hp2 << 8));

			for (float dy = 0.0f; dy <= 2.0f; dy += 1.0f) {
				for (float dx = 0.0f; dx <= 1.0f; dx += 0.5f) {
					if (dx == 0.0f && dy == 0.0f)
						continue;

					Vertex(x + tile2x + dx, y + dy, 0, 0, 1 << tile2wp2, 1 << hp2, shadowColor);
					Vertex(x + tile2x + dx + tile2w, y + dy + h, tile2u1, v1, 1 << tile2wp2, 1 << hp2, shadowColor);
				}
			}

			// Return to tile1 for the text.
			EndVertexDataAndDraw(GE_PRIM_RECTANGLES);
			BeginVertexData();
			WriteCmd(GE_CMD_TEXADDR0, im.ptr & 0xFFFFF0);
			WriteCmd(GE_CMD_TEXSIZE0, tile1wp2 | (hp2 << 8));
		}
	}

	Vertex(x, y, 0, 0, 1 << tile1wp2, 1 << hp2, style.color);
	Vertex(x + tile1w, y + h, tile1u1, v1, 1 << tile1wp2, 1 << hp2, style.color);
	if (hasTile2) {
		EndVertexDataAndDraw(GE_PRIM_RECTANGLES);
		BeginVertexData();
		WriteCmd(GE_CMD_TEXADDR0, (im.ptr & 0xFFFFF0) + 256);
		WriteCmd(GE_CMD_TEXSIZE0, tile2wp2 | (hp2 << 8));

		Vertex(x + tile2x, y, 0, 0, 1 << tile2wp2, 1 << hp2, style.color);
		Vertex(x + tile2x + tile2w, y + h, tile2u1, v1, 1 << tile2wp2, 1 << hp2, style.color);
	}
	EndVertexDataAndDraw(GE_PRIM_RECTANGLES);

	PPGeSetDefaultTexture();
}

static void PPGeDecimateTextImages(int age) {
	// Do this always, in case the platform has no TextDrawer but save state did.
	for (auto it = textDrawerImages.begin(); it != textDrawerImages.end(); ) {
		if (gpuStats.numFlips - it->second.entry.lastUsedFrame >= age) {
			kernelMemory.Free(it->second.ptr);
			it = textDrawerImages.erase(it);
		} else {
			++it;
		}
	}
}

void PPGeDrawText(std::string_view text, float x, float y, const PPGeStyle &style) {
	std::string str = PPGeSanitizeText(text);
	if (str.empty()) {
		return;
	}

	if (HasTextDrawer()) {
		PPGeTextDrawerImage im = PPGeGetTextImage(str, style, 480.0f - x, false);
		if (im.ptr) {
			PPGeDrawTextImage(im, x, y, style);
			return;
		}
	}

	if (style.hasShadow) {
		// This doesn't have the nicer shadow because it's so many verts.
		PPGePrepareText(text, x + 1, y + 2, style.align, style.scale, style.scale, PPGE_LINE_USE_ELLIPSIS);
		PPGeDrawCurrentText(style.shadowColor);
	}

	PPGePrepareText(text, x, y, style.align, style.scale, style.scale, PPGE_LINE_USE_ELLIPSIS);
	PPGeDrawCurrentText(style.color);
}

static std::string_view StripTrailingWhite(std::string_view s) {
	size_t lastChar = s.find_last_not_of(" \t\r\n");
	if (lastChar != s.npos) {
		return s.substr(0, lastChar + 1);
	}
	return s;
}

static std::string_view CropLinesToCount(std::string_view s, int numLines) {
	std::vector<std::string_view> lines;
	SplitString(s, '\n', lines);
	if ((int)lines.size() <= numLines) {
		return s;
	}

	size_t len = 0;
	for (int i = 0; i < numLines; ++i) {
		len += lines[i].size() + 1;
	}

	return s.substr(0, len);
}

void PPGeDrawTextWrapped(std::string_view text, float x, float y, float wrapWidth, float wrapHeight, const PPGeStyle &style) {
	std::string s = PPGeSanitizeText(text);
	if (wrapHeight != 0.0f) {
		s = StripTrailingWhite(s);
	}

	int zoom = (PSP_CoreParameter().pixelHeight + 479) / 480;
	zoom = std::min(zoom, PSP_CoreParameter().renderScaleFactor);
	float maxScaleDown = zoom == 1 ? 1.3f : 2.0f;

	if (HasTextDrawer()) {
		float actualWidth, actualHeight;
		Bounds b(0, 0, wrapWidth <= 0 ? 480.0f - x : wrapWidth, wrapHeight);
		int tdalign = 0;
		textDrawer->SetFontScale(style.scale, style.scale);
		textDrawer->MeasureStringRect(s, b, &actualWidth, &actualHeight, tdalign | FLAG_WRAP_TEXT);

		// Check if we need to scale the text down to fit better.
		PPGeStyle adjustedStyle = style;
		if (wrapHeight != 0.0f && actualHeight > wrapHeight) {
			// Cheap way to get the line height.
			float oneLine, twoLines;
			textDrawer->MeasureString("|", &actualWidth, &oneLine);
			textDrawer->MeasureStringRect("|\n|", Bounds(0, 0, 480, 272), &actualWidth, &twoLines);

			float lineHeight = twoLines - oneLine;
			if (actualHeight > wrapHeight * maxScaleDown) {
				float maxLines = floor(wrapHeight * maxScaleDown / lineHeight);
				actualHeight = (maxLines + 1) * lineHeight;
				// Add an ellipsis if it's just too long to be readable.
				// On a PSP, it does this without scaling it down.
				s = StripTrailingWhite(CropLinesToCount(s, (int)maxLines));
				s.append("\n...");
			}

			adjustedStyle.scale *= wrapHeight / actualHeight;
		}

		PPGeTextDrawerImage im = PPGeGetTextImage(s, adjustedStyle, wrapWidth <= 0 ? 480.0f - x : wrapWidth, true);
		if (im.ptr) {
			PPGeDrawTextImage(im, x, y, adjustedStyle);
			return;
		}
	}

	int sx = style.hasShadow ? 1 : 0;
	int sy = style.hasShadow ? 2 : 0;
	PPGePrepareText(s, x + sx, y + sy, style.align, style.scale, style.scale, PPGE_LINE_USE_ELLIPSIS | PPGE_LINE_WRAP_WORD, wrapWidth);

	float scale = style.scale;
	float lineHeightScale = style.scale;
	float actualHeight = char_lines_metrics.lineHeight * char_lines_metrics.numLines;
	if (wrapHeight != 0.0f && actualHeight > wrapHeight) {
		if (actualHeight > wrapHeight * maxScaleDown) {
			float maxLines = floor(wrapHeight * maxScaleDown / char_lines_metrics.lineHeight);
			actualHeight = (maxLines + 1) * char_lines_metrics.lineHeight;
			// Add an ellipsis if it's just too long to be readable.
			// On a PSP, it does this without scaling it down.
			s = StripTrailingWhite(CropLinesToCount(s, (int)maxLines));
			s.append("\n...");
		}

		// Measure the text again after scaling down.
		PPGeResetCurrentText();
		float reduced = style.scale * wrapHeight / actualHeight;
		// Try to keep the font as large as possible, so reduce the line height some.
		scale = reduced * 1.15f;
		lineHeightScale = reduced;
		PPGePrepareText(s, x + sx, y + sy, style.align, scale, lineHeightScale, PPGE_LINE_USE_ELLIPSIS | PPGE_LINE_WRAP_WORD, wrapWidth);
	}
	if (style.hasShadow) {
		// This doesn't have the nicer shadow because it's so many verts.
		PPGeDrawCurrentText(style.shadowColor);
		PPGePrepareText(s, x, y, style.align, scale, lineHeightScale, PPGE_LINE_USE_ELLIPSIS | PPGE_LINE_WRAP_WORD, wrapWidth);
	}
	PPGeDrawCurrentText(style.color);
}

// Draws a "4-patch" for button-like things that can be resized
void PPGeDraw4Patch(ImageID atlasImage, float x, float y, float w, float h, u32 color) {
	if (!dlPtr)
		return;
	const AtlasImage *img = g_ppge_atlas.getImage(atlasImage);
	if (!img)
		return;
	float borderx = img->w / 20;
	float bordery = img->h / 20;
	float u1 = img->u1, uhalf = (img->u1 + img->u2) / 2, u2 = img->u2;
	float v1 = img->v1, vhalf = (img->v1 + img->v2) / 2, v2 = img->v2;
	float xmid1 = x + borderx;
	float xmid2 = x + w - borderx;
	float ymid1 = y + bordery;
	float ymid2 = y + h - bordery;
	float x2 = x + w;
	float y2 = y + h;
	BeginVertexData();
	// Top row
	Vertex(x, y, u1, v1, atlasWidth, atlasHeight, color);
	Vertex(xmid1, ymid1, uhalf, vhalf, atlasWidth, atlasHeight, color);
	Vertex(xmid1, y, uhalf, v1, atlasWidth, atlasHeight, color);
	Vertex(xmid2, ymid1, uhalf, vhalf, atlasWidth, atlasHeight, color);
	Vertex(xmid2, y, uhalf, v1, atlasWidth, atlasHeight, color);
	Vertex(x2, ymid1, u2, vhalf, atlasWidth, atlasHeight, color);
	// Middle row
	Vertex(x, ymid1, u1, vhalf, atlasWidth, atlasHeight, color);
	Vertex(xmid1, ymid2, uhalf, vhalf, atlasWidth, atlasHeight, color);
	Vertex(xmid1, ymid1, uhalf, vhalf, atlasWidth, atlasHeight, color);
	Vertex(xmid2, ymid2, uhalf, vhalf, atlasWidth, atlasHeight, color);
	Vertex(xmid2, ymid1, uhalf, vhalf, atlasWidth, atlasHeight, color);
	Vertex(x2, ymid2, u2, v2, atlasWidth, atlasHeight, color);
	// Bottom row
	Vertex(x, ymid2, u1, vhalf, atlasWidth, atlasHeight, color);
	Vertex(xmid1, y2, uhalf, v2, atlasWidth, atlasHeight, color);
	Vertex(xmid1, ymid2, uhalf, vhalf, atlasWidth, atlasHeight, color);
	Vertex(xmid2, y2, uhalf, v2, atlasWidth, atlasHeight, color);
	Vertex(xmid2, ymid2, uhalf, vhalf, atlasWidth, atlasHeight, color);
	Vertex(x2, y2, u2, v2, atlasWidth, atlasHeight, color);
	EndVertexDataAndDraw(GE_PRIM_RECTANGLES);
}

void PPGeDrawRect(float x1, float y1, float x2, float y2, u32 color) {
	if (!dlPtr)
		return;

	WriteCmd(GE_CMD_TEXTUREMAPENABLE, 0);

	BeginVertexData();
	Vertex(x1, y1, 0, 0, 0, 0, color);
	Vertex(x2, y2, 0, 0, 0, 0, color);
	EndVertexDataAndDraw(GE_PRIM_RECTANGLES);

	WriteCmd(GE_CMD_TEXTUREMAPENABLE, 1);
}

// Just blits an image to the screen, multiplied with the color.
void PPGeDrawImage(ImageID atlasImage, float x, float y, const PPGeStyle &style) {
	if (!dlPtr)
		return;

	const AtlasImage *img = g_ppge_atlas.getImage(atlasImage);
	if (!img) {
		return;
	}
	float w = img->w;
	float h = img->h;
	BeginVertexData();
	if (style.hasShadow) {
		for (float dy = 0.0f; dy <= 2.0f; dy += 1.0f) {
			for (float dx = 0.0f; dx <= 1.0f; dx += 0.5f) {
				if (dx == 0.0f && dy == 0.0f)
					continue;
				Vertex(x + dx, y + dy, img->u1, img->v1, atlasWidth, atlasHeight, alphaMul(style.shadowColor, 0.35f));
				Vertex(x + dx + w, y + dy + h, img->u2, img->v2, atlasWidth, atlasHeight, alphaMul(style.shadowColor, 0.35f));
			}
		}
	}
	Vertex(x, y, img->u1, img->v1, atlasWidth, atlasHeight, style.color);
	Vertex(x + w, y + h, img->u2, img->v2, atlasWidth, atlasHeight, style.color);
	EndVertexDataAndDraw(GE_PRIM_RECTANGLES);
}

void PPGeDrawImage(ImageID atlasImage, float x, float y, float w, float h, const PPGeStyle &style) {
	if (!dlPtr)
		return;

	const AtlasImage *img = g_ppge_atlas.getImage(atlasImage);
	if (!img) {
		return;
	}
	BeginVertexData();
	if (style.hasShadow) {
		for (float dy = 0.0f; dy <= 2.0f; dy += 1.0f) {
			for (float dx = 0.0f; dx <= 1.0f; dx += 0.5f) {
				if (dx == 0.0f && dy == 0.0f)
					continue;
				Vertex(x + dx, y + dy, img->u1, img->v1, atlasWidth, atlasHeight, alphaMul(style.shadowColor, 0.35f));
				Vertex(x + dx + w, y + dy + h, img->u2, img->v2, atlasWidth, atlasHeight, alphaMul(style.shadowColor, 0.35f));
			}
		}
	}
	Vertex(x, y, img->u1, img->v1, atlasWidth, atlasHeight, style.color);
	Vertex(x + w, y + h, img->u2, img->v2, atlasWidth, atlasHeight, style.color);
	EndVertexDataAndDraw(GE_PRIM_RECTANGLES);
}

void PPGeDrawImage(float x, float y, float w, float h, float u1, float v1, float u2, float v2, int tw, int th, const PPGeImageStyle &style) {
	if (!dlPtr)
		return;
	BeginVertexData();
	Vertex(x, y, u1, v1, tw, th, style.color);
	Vertex(x + w, y + h, u2, v2, tw, th, style.color);
	EndVertexDataAndDraw(GE_PRIM_RECTANGLES);
}

void PPGeSetDefaultTexture()
{
	WriteCmd(GE_CMD_TEXTUREMAPENABLE, 1);
	int wp2 = GetPow2(atlasWidth);
	int hp2 = GetPow2(atlasHeight);
	WriteCmd(GE_CMD_CLUTADDR, palette.ptr & 0xFFFFF0);
	WriteCmd(GE_CMD_CLUTADDRUPPER, (palette.ptr & 0xFF000000) >> 8);
	WriteCmd(GE_CMD_CLUTFORMAT, 0x00FF02);
	WriteCmd(GE_CMD_LOADCLUT, 2);
	WriteCmd(GE_CMD_TEXSIZE0, wp2 | (hp2 << 8));
	WriteCmd(GE_CMD_TEXMAPMODE, 0 | (1 << 8));
	WriteCmd(GE_CMD_TEXMODE, 0);
	WriteCmd(GE_CMD_TEXFORMAT, GE_TFMT_CLUT4);  // 4-bit CLUT
	WriteCmd(GE_CMD_TEXFILTER, (1 << 8) | 1);   // mag = LINEAR min = LINEAR
	WriteCmd(GE_CMD_TEXWRAP, (1 << 8) | 1);  // clamp texture wrapping
	WriteCmd(GE_CMD_TEXFUNC, (0 << 16) | (1 << 8) | 0);  // RGBA texture reads, modulate, no color doubling
	WriteCmd(GE_CMD_TEXADDR0, atlasPtr & 0xFFFFF0);
	WriteCmd(GE_CMD_TEXBUFWIDTH0, atlasWidth | ((atlasPtr & 0xFF000000) >> 8));
	WriteCmd(GE_CMD_TEXFLUSH, 0);
}

void PPGeSetTexture(u32 dataAddr, int width, int height)
{
	WriteCmd(GE_CMD_TEXTUREMAPENABLE, 1);
	int wp2 = GetPow2(width);
	int hp2 = GetPow2(height);
	WriteCmd(GE_CMD_TEXSIZE0, wp2 | (hp2 << 8));
	WriteCmd(GE_CMD_TEXMAPMODE, 0 | (1 << 8));
	WriteCmd(GE_CMD_TEXMODE, 0);
	WriteCmd(GE_CMD_TEXFORMAT, GE_TFMT_8888);  // 4444
	WriteCmd(GE_CMD_TEXFILTER, (1 << 8) | 1);   // mag = LINEAR min = LINEAR
	WriteCmd(GE_CMD_TEXWRAP, (1 << 8) | 1);  // clamp texture wrapping
	WriteCmd(GE_CMD_TEXFUNC, (0 << 16) | (1 << 8) | 0);  // RGBA texture reads, modulate, no color doubling
	WriteCmd(GE_CMD_TEXADDR0, dataAddr & 0xFFFFF0);
	WriteCmd(GE_CMD_TEXBUFWIDTH0, width | ((dataAddr & 0xFF000000) >> 8));
	WriteCmd(GE_CMD_TEXFLUSH, 0);
}

void PPGeDisableTexture()
{
	WriteCmd(GE_CMD_TEXTUREMAPENABLE, 0);
}

std::vector<PPGeImage *> PPGeImage::loadedTextures_;

PPGeImage::PPGeImage(std::string_view pspFilename)
	: filename_(pspFilename) {
}

PPGeImage::PPGeImage(u32 pngPointer, size_t pngSize)
	: filename_(""), png_(pngPointer), size_(pngSize) {
}

PPGeImage::~PPGeImage() {
	Free();
}

bool PPGeImage::Load() {
	loadFailed_ = false;
	Free();

	// In case it fails to load.
	width_ = 0;
	height_ = 0;

	unsigned char *textureData;
	int success;
	if (filename_.empty()) {
		success = pngLoadPtr(Memory::GetPointerRange(png_, (u32)size_), size_, &width_, &height_, &textureData);
	} else {
		std::vector<u8> pngData;
		if (pspFileSystem.ReadEntireFile(filename_, pngData) < 0) {
			WARN_LOG(Log::sceGe, "PPGeImage cannot load file %s", filename_.c_str());
			loadFailed_ = true;
			return false;
		}

		success = pngLoadPtr((const unsigned char *)&pngData[0], pngData.size(), &width_, &height_, &textureData);
	}
	if (!success) {
		WARN_LOG(Log::sceGe, "Bad PPGeImage - not a valid png");
		loadFailed_ = true;
		return false;
	}

	u32 dataSize = width_ * height_ * 4;
	u32 texSize = dataSize + width_ * 4;
	texture_ = __PPGeDoAlloc(texSize, true, "Savedata Icon");
	if (texture_ == 0) {
		free(textureData);
		WARN_LOG(Log::sceGe, "Bad PPGeImage - unable to allocate space for texture");
		// Don't set loadFailed_ here, we'll try again if there's more memory later.
		return false;
	}

	Memory::Memcpy(texture_, textureData, dataSize, "PPGeTex");
	Memory::Memset(texture_ + dataSize, 0, texSize - dataSize, "PPGeTexClear");
	free(textureData);

	lastFrame_ = gpuStats.numFlips;
	loadedTextures_.push_back(this);
	return true;
}

bool PPGeImage::IsValid() {
	if (loadFailed_)
		return false;

	if (texture_ == 0) {
		Decimate();
		return Load();
	}
	return true;
}

void PPGeImage::Free() {
	if (texture_ != 0) {
		kernelMemory.Free(texture_);
		texture_ = 0;
		loadedTextures_.erase(std::remove(loadedTextures_.begin(), loadedTextures_.end(), this), loadedTextures_.end());
		loadFailed_ = false;
	}
}

void PPGeImage::DoState(PointerWrap &p) {
	auto s = p.Section("PPGeImage", 1, 2);
	if (!s)
		return;

	Do(p, filename_);
	Do(p, png_);
	Do(p, size_);
	Do(p, texture_);
	Do(p, width_);
	Do(p, height_);
	Do(p, lastFrame_);
	if (s >= 2) {
		Do(p, loadFailed_);
	} else {
		loadFailed_ = false;
	}
}

void PPGeImage::CompatLoad(u32 texture, int width, int height) {
	// Won't be reloadable, so don't add to loadedTextures_.
	texture_ = texture;
	width_ = width;
	height_ = height;
	loadFailed_ = false;
}

void PPGeImage::Decimate(int age) {
	int tooOldFrame = gpuStats.numFlips - age;
	for (size_t i = 0; i < loadedTextures_.size(); ++i) {
		if (loadedTextures_[i]->lastFrame_ < tooOldFrame) {
			loadedTextures_[i]->Free();
			// That altered loadedTextures_.
			--i;
		}
	}
}

void PPGeImage::SetTexture() {
	if (texture_ == 0 && !loadFailed_) {
		Decimate();
		Load();
	}

	if (texture_ != 0) {
		lastFrame_ = gpuStats.numFlips;
		PPGeSetTexture(texture_, width_, height_);
	} else {
		PPGeDisableTexture();
	}
}

void PPGeNotifyFrame() {
	if (textDrawer) {
		textDrawer->OncePerFrame();
	}

	PPGeDecimateTextImages();
	PPGeImage::Decimate();

	if (atlasRequiresReset) {
		__PPGeShutdown();
		__PPGeInit();
	}
}
