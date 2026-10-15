#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Common/Data/Format/PNGLoad.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/Thread/ThreadManager.h"
#include "GPU/Common/TextureReplacer.h"

#include "UnitTest.h"

// The fake hashes in textures.ini are designed to be readable: the address
// part starts with A, the CLUT hash with C, and the contents hash with 1.
static const u64 KEY_A = 0xA0000001C0000001ULL;  // addr 0xA0000001, clut 0xC0000001
static const u32 HASH_A = 0x10000001;
static const u64 KEY_B = 0xA0000002C0000002ULL;  // two mip levels
static const u32 HASH_B = 0x10000002;
static const u64 KEY_C = 0xA0000003C0000003ULL;  // hashrange
static const u32 HASH_C = 0x10000003;
static const u64 KEY_D = 0xA0000004C0000004ULL;  // ignored (empty filename)
static const u32 HASH_D = 0x10000004;
static const u64 KEY_E = 0xA0000005C0000005ULL;  // file missing
static const u32 HASH_E = 0x10000005;
static const u64 KEY_F = 0xA0000006C0000006ULL;  // same file as A, own hashrange
static const u32 HASH_F = 0x10000006;
static const u64 KEY_G = 0xA0000007C0000007ULL;  // ignoreAddress pack, hashrange
static const u32 HASH_G = 0x10000007;

static bool CreateTestPNG(const Path &filename, int w, int h, u32 color) {
	std::vector<u8> buf((size_t)w * h * 4);
	for (int i = 0; i < w * h; i++) {
		buf[i * 4] = (color >> 24) & 0xFF;
		buf[i * 4 + 1] = (color >> 16) & 0xFF;
		buf[i * 4 + 2] = (color >> 8) & 0xFF;
		buf[i * 4 + 3] = color & 0xFF;
	}
	return pngSave(filename, buf.data(), w, h, 4);
}

static bool WriteIni(const Path &packDir, const char *iniContent) {
	FILE *f = File::OpenCFile(packDir / "textures.ini", "w");
	if (!f) {
		return false;
	}
	fwrite(iniContent, 1, strlen(iniContent), f);
	fclose(f);
	return true;
}

static bool CreateTestPack(const Path &packDir) {
	File::DeleteDirRecursively(packDir);
	if (!File::CreateDir(packDir)) {
		return false;
	}

	static const char *iniContent =
		"[options]\n"
		"hash = quick\n"
		"version = 1\n"
		"\n"
		"[hashes]\n"
		"A0000001C000000110000001 = tex_a.png\n"
		"A0000002C000000210000002 = tex_b0.png\n"
		"A0000002C000000210000002_1 = tex_b1.png\n"
		"A0000003C000000310000003 = tex_c.png\n"
		"A0000004C000000410000004 =\n"
		"A0000005C000000510000005 = missing.png\n"
		"A0000006C000000610000006 = tex_a.png\n"
		"\n"
		"[hashranges]\n"
		"A0000003,512,512 = 256,256\n"
		"A0000006,64,64 = 32,32\n"
		"\n"
		"[filtering]\n"
		"A0000001C000000110000001 = nearest\n";

	if (!WriteIni(packDir, iniContent)) {
		return false;
	}

	if (!CreateTestPNG(packDir / "tex_a.png", 64, 64, 0xFF0000FF)) return false;
	if (!CreateTestPNG(packDir / "tex_b0.png", 64, 64, 0x00FF00FF)) return false;
	if (!CreateTestPNG(packDir / "tex_b1.png", 32, 32, 0x00FF00FF)) return false;
	if (!CreateTestPNG(packDir / "tex_c.png", 256, 256, 0x0000FFFF)) return false;

	return true;
}

static bool TestLookups(TextureReplacer *replacer) {
	// Key A: single mip, found.
	ReplacedTexture *texA = replacer->FindReplacement(ReplacementCacheKey(KEY_A, HASH_A), 64, 64);
	EXPECT_TRUE(texA != nullptr);

	// Key B: two mip levels, found.
	ReplacedTexture *texB = replacer->FindReplacement(ReplacementCacheKey(KEY_B, HASH_B), 64, 64);
	EXPECT_TRUE(texB != nullptr);

	// Key C: found via hashrange.
	ReplacedTexture *texC = replacer->FindReplacement(ReplacementCacheKey(KEY_C, HASH_C), 512, 512);
	EXPECT_TRUE(texC != nullptr);

	// Key D: explicitly ignored (empty filename).
	EXPECT_TRUE(replacer->FindReplacement(ReplacementCacheKey(KEY_D, HASH_D), 16, 16) == nullptr);

	// Key E: file missing, still creates a texture object (loads as NOT_FOUND).
	ReplacedTexture *texE = replacer->FindReplacement(ReplacementCacheKey(KEY_E, HASH_E), 16, 16);
	EXPECT_TRUE(texE != nullptr);

	// Key F: the same file as A, but its own hashrange and no filtering, so it can't share A's texture.
	ReplacedTexture *texF = replacer->FindReplacement(ReplacementCacheKey(KEY_F, HASH_F), 64, 64);
	EXPECT_TRUE(texF != nullptr);
	EXPECT_TRUE(texF != texA);

	// Unknown key: not in the ini at all.
	ReplacementCacheKey unknownKey(0x2000000020000000ULL, 0x20000000);
	EXPECT_TRUE(replacer->FindReplacement(unknownKey, 16, 16) == nullptr);

	if (!texA || !texB || !texC || !texE || !texF) {
		return false;
	}

	// Load the actual textures using the thread manager.
	g_threadManager.Init(1, 1);

	// Key A: 64x64 single level, nearest filtering.
	EXPECT_TRUE(texA->Poll(1.0));
	EXPECT_EQ_INT(texA->NumLevels(), 1);
	int w = 0, h = 0;
	texA->GetSize(0, &w, &h);
	EXPECT_EQ_INT(w, 64);
	EXPECT_EQ_INT(h, 64);
	TextureFiltering filtering = TEX_FILTER_AUTO;
	EXPECT_TRUE(texA->ForceFiltering(&filtering));
	EXPECT_TRUE(filtering == TEX_FILTER_FORCE_NEAREST);

	// Key B: two mip levels, 64x64 and 32x32.
	EXPECT_TRUE(texB->Poll(1.0));
	EXPECT_EQ_INT(texB->NumLevels(), 2);
	texB->GetSize(0, &w, &h);
	EXPECT_EQ_INT(w, 64);
	EXPECT_EQ_INT(h, 64);
	texB->GetSize(1, &w, &h);
	EXPECT_EQ_INT(w, 32);
	EXPECT_EQ_INT(h, 32);

	// Key C: hashrange maps 512x512 -> 256x256, upscaled back to 512x512.
	EXPECT_TRUE(texC->Poll(1.0));
	EXPECT_EQ_INT(texC->NumLevels(), 1);
	texC->GetSize(0, &w, &h);
	EXPECT_EQ_INT(w, 512);
	EXPECT_EQ_INT(h, 512);

	// Key F: hashrange maps 64x64 -> 32x32, so the 64x64 image covers 128x128.
	EXPECT_TRUE(texF->Poll(1.0));
	texF->GetSize(0, &w, &h);
	EXPECT_EQ_INT(w, 128);
	EXPECT_EQ_INT(h, 128);
	EXPECT_FALSE(texF->ForceFiltering(&filtering));

	// Key E: missing file should end up NOT_FOUND.
	EXPECT_TRUE(texE->Poll(1.0));
	EXPECT_TRUE(texE->State() == ReplacementState::NOT_FOUND);

	g_threadManager.Teardown();
	return true;
}

// ignoreAddress matches files without the address, but hash ranges still apply per address.
static bool TestIgnoreAddress(const Path &packDir) {
	static const char *iniContent =
		"[options]\n"
		"hash = xxh32\n"
		"ignoreAddress = true\n"
		"version = 1\n"
		"\n"
		"[hashes]\n"
		"00000000C000000710000007 = tex_c.png\n"
		"\n"
		"[hashranges]\n"
		"A0000007,512,512 = 256,256\n";
	if (!WriteIni(packDir, iniContent)) {
		return false;
	}

	TextureReplacer replacer(nullptr);
	std::string error;
	if (!replacer.LoadPackForTesting(packDir, &error)) {
		return false;
	}

	ReplacementCacheKey key(KEY_G, HASH_G);
	ReplacedTexture *texG = replacer.FindReplacement(key, 512, 512);
	EXPECT_TRUE(texG != nullptr);
	if (!texG) {
		return false;
	}
	EXPECT_TRUE(replacer.FindReplacement(key, 512, 512) == texG);

	g_threadManager.Init(1, 1);
	EXPECT_TRUE(texG->Poll(1.0));
	int w = 0, h = 0;
	texG->GetSize(0, &w, &h);
	EXPECT_EQ_INT(w, 512);
	EXPECT_EQ_INT(h, 512);
	g_threadManager.Teardown();
	return true;
}

static void AppendBE32(std::vector<u8> *out, u32 value) {
	for (int shift = 24; shift >= 0; shift -= 8) {
		out->push_back((value >> shift) & 0xFF);
	}
}

static u32 PNGCrc32(const u8 *data, size_t size) {
	u32 crc = 0xFFFFFFFF;
	for (size_t i = 0; i < size; i++) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; bit++) {
			crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
		}
	}
	return ~crc;
}

static void AppendPNGChunk(std::vector<u8> *out, const char *type, const std::vector<u8> &data) {
	AppendBE32(out, (u32)data.size());
	std::vector<u8> typeAndData(type, type + 4);
	typeAndData.insert(typeAndData.end(), data.begin(), data.end());
	out->insert(out->end(), typeAndData.begin(), typeAndData.end());
	AppendBE32(out, PNGCrc32(typeAndData.data(), typeAndData.size()));
}

// Writes an 8-bit palette PNG (a palette in reverse gray order, so the index is not the color)
// or an 8-bit grayscale PNG, one byte per pixel. The image data is stored uncompressed, which
// avoids needing libpng or zlib here.
static bool CreateIndexedPNG(const Path &filename, int w, int h, bool palette, const std::vector<u8> &indices) {
	std::vector<u8> raw;
	for (int y = 0; y < h; y++) {
		raw.push_back(0);  // No filter.
		raw.insert(raw.end(), indices.begin() + y * w, indices.begin() + (y + 1) * w);
	}

	// A zlib stream of stored blocks.
	std::vector<u8> zlib = { 0x78, 0x01 };
	for (size_t pos = 0; pos < raw.size() || pos == 0;) {
		const size_t len = std::min<size_t>(raw.size() - pos, 65535);
		const bool last = pos + len >= raw.size();
		zlib.push_back(last ? 1 : 0);
		zlib.push_back(len & 0xFF);
		zlib.push_back(len >> 8);
		zlib.push_back(~len & 0xFF);
		zlib.push_back((~len >> 8) & 0xFF);
		zlib.insert(zlib.end(), raw.begin() + pos, raw.begin() + pos + len);
		pos += len;
		if (last) {
			break;
		}
	}
	u32 a = 1, b = 0;
	for (u8 c : raw) {
		a = (a + c) % 65521;
		b = (b + a) % 65521;
	}
	AppendBE32(&zlib, (b << 16) | a);

	std::vector<u8> file = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	std::vector<u8> ihdr;
	AppendBE32(&ihdr, w);
	AppendBE32(&ihdr, h);
	ihdr.push_back(8);  // Bit depth.
	ihdr.push_back(palette ? 3 : 0);  // Color type: palette or grayscale.
	ihdr.push_back(0);
	ihdr.push_back(0);
	ihdr.push_back(0);
	AppendPNGChunk(&file, "IHDR", ihdr);
	if (palette) {
		std::vector<u8> plte;
		for (int i = 0; i < 256; i++) {
			plte.push_back(255 - i);
			plte.push_back(255 - i);
			plte.push_back(255 - i);
		}
		AppendPNGChunk(&file, "PLTE", plte);
	}
	AppendPNGChunk(&file, "IDAT", zlib);
	AppendPNGChunk(&file, "IEND", {});

	FILE *fp = File::OpenCFile(filename, "wb");
	if (!fp) {
		return false;
	}
	const bool ok = fwrite(file.data(), 1, file.size(), fp) == file.size();
	fclose(fp);
	return ok;
}

static bool ReadFileBytes(const Path &filename, std::vector<u8> *data) {
	std::string str;
	if (!File::ReadBinaryFileToString(filename, &str)) {
		return false;
	}
	data->assign(str.begin(), str.end());
	return true;
}

static bool TestIndexedPNG(const Path &packDir) {
	std::vector<u8> indices(16 * 8);
	for (size_t i = 0; i < indices.size(); i++) {
		indices[i] = (u8)(i * 7 + 3);
	}

	for (int palette = 0; palette < 2; palette++) {
		Path file = packDir / "indexed_test.png";
		if (!CreateIndexedPNG(file, 16, 8, palette != 0, indices)) {
			return false;
		}
		std::vector<u8> png;
		if (!ReadFileBytes(file, &png)) {
			return false;
		}
		int w = 0, h = 0;
		unsigned char *out = nullptr;
		EXPECT_TRUE(pngLoadIndexed(png.data(), png.size(), &w, &h, &out) == 1);
		EXPECT_EQ_INT(w, 16);
		EXPECT_EQ_INT(h, 8);
		if (!out) {
			return false;
		}
		EXPECT_TRUE(memcmp(out, indices.data(), indices.size()) == 0);
		free(out);

		// Too small a limit is refused.
		EXPECT_TRUE(pngLoadIndexed(png.data(), png.size(), &w, &h, &out, 8, 8) == 0);
	}

	// Truecolor is not an index.
	Path rgba = packDir / "indexed_rgba.png";
	if (!CreateTestPNG(rgba, 4, 4, 0x11223344)) {
		return false;
	}
	std::vector<u8> png;
	if (!ReadFileBytes(rgba, &png)) {
		return false;
	}
	int w = 0, h = 0;
	unsigned char *out = nullptr;
	EXPECT_TRUE(pngLoadIndexed(png.data(), png.size(), &w, &h, &out) == 0);
	EXPECT_TRUE(out == nullptr);
	return true;
}

static void AppendLE32(std::vector<u8> *out, u32 value) {
	for (int shift = 0; shift < 32; shift += 8) {
		out->push_back((value >> shift) & 0xFF);
	}
}

static void AppendLE64(std::vector<u8> *out, u64 value) {
	AppendLE32(out, (u32)value);
	AppendLE32(out, (u32)(value >> 32));
}

// A zstd frame holding a single uncompressed block, which is valid zstd (for data under 256 bytes).
static std::vector<u8> ZstdRawFrame(const std::vector<u8> &data) {
	std::vector<u8> frame = { 0x28, 0xB5, 0x2F, 0xFD, 0x20, (u8)data.size() };
	const u32 blockHeader = ((u32)data.size() << 3) | 1;  // Last block, raw.
	frame.push_back(blockHeader & 0xFF);
	frame.push_back((blockHeader >> 8) & 0xFF);
	frame.push_back((blockHeader >> 16) & 0xFF);
	frame.insert(frame.end(), data.begin(), data.end());
	return frame;
}

// Writes a KTX2 with one entry per level (level 0 first), each w x h one byte per texel.
static bool CreateIndexedKTX2(const Path &filename, int w, int h, u32 vkFormat, bool zstd, const std::vector<std::vector<u8>> &levels) {
	std::vector<std::vector<u8>> stored;
	for (const auto &level : levels) {
		stored.push_back(zstd ? ZstdRawFrame(level) : level);
	}

	std::vector<u8> file = { 0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n' };
	AppendLE32(&file, vkFormat);
	AppendLE32(&file, 1);  // typeSize
	AppendLE32(&file, w);
	AppendLE32(&file, h);
	AppendLE32(&file, 0);  // pixelDepth
	AppendLE32(&file, 0);  // layerCount
	AppendLE32(&file, 1);  // faceCount
	AppendLE32(&file, (u32)levels.size());
	AppendLE32(&file, zstd ? 2 : 0);
	// No data format descriptor, key/value data or supercompression global data.
	for (int i = 0; i < 4; i++) {
		AppendLE32(&file, 0);
	}
	AppendLE64(&file, 0);
	AppendLE64(&file, 0);

	u64 offset = file.size() + levels.size() * 24;
	for (size_t i = 0; i < levels.size(); i++) {
		AppendLE64(&file, offset);
		AppendLE64(&file, stored[i].size());
		AppendLE64(&file, levels[i].size());
		offset += stored[i].size();
	}
	for (const auto &level : stored) {
		file.insert(file.end(), level.begin(), level.end());
	}

	FILE *fp = File::OpenCFile(filename, "wb");
	if (!fp) {
		return false;
	}
	const bool ok = fwrite(file.data(), 1, file.size(), fp) == file.size();
	fclose(fp);
	return ok;
}

// Lossless KTX2 files (R8_UNORM, optionally zstd) as indexed replacements, with their mip levels.
static bool TestIndexedKTX2(const Path &packDir) {
	static const char *iniContent =
		"[options]\n"
		"hash = quick\n"
		"version = 1\n"
		"\n"
		"[hashes]\n"
		"A000000B0000000010000001 = ktx_raw.ktx2\n"
		"A000000C0000000010000002 = ktx_zstd.ktx2\n"
		"A000000D0000000010000003 = ktx_rgba.ktx2\n"
		"A000000E0000000010000004 = ktx_short.ktx2\n"
		"\n"
		"[indexed]\n"
		"A000000B0000000010000001 = true\n"
		"A000000C0000000010000002 = true\n"
		"A000000D0000000010000003 = true\n"
		"A000000E0000000010000004 = true\n";
	if (!WriteIni(packDir, iniContent)) {
		return false;
	}

	std::vector<std::vector<u8>> levels;
	for (int i = 0; i < 3; i++) {
		std::vector<u8> level((16 >> i) * (8 >> i));
		for (size_t j = 0; j < level.size(); j++) {
			level[j] = (u8)(j * 11 + i * 50 + 1);
		}
		levels.push_back(level);
	}
	if (!CreateIndexedKTX2(packDir / "ktx_raw.ktx2", 16, 8, 9, false, levels)) return false;
	if (!CreateIndexedKTX2(packDir / "ktx_zstd.ktx2", 16, 8, 9, true, levels)) return false;
	if (!CreateIndexedKTX2(packDir / "ktx_rgba.ktx2", 16, 8, 37, false, levels)) return false;
	if (!CreateIndexedKTX2(packDir / "ktx_short.ktx2", 16, 8, 9, false, { std::vector<u8>(10) })) return false;

	TextureReplacer replacer(nullptr);
	std::string error;
	if (!replacer.LoadPackForTesting(packDir, &error)) {
		return false;
	}

	ReplacedTexture *raw = replacer.FindReplacement(ReplacementCacheKey(0xA000000B00000001ULL, 0x10000001), 16, 8);
	ReplacedTexture *zstd = replacer.FindReplacement(ReplacementCacheKey(0xA000000C00000001ULL, 0x10000002), 16, 8);
	ReplacedTexture *rgba = replacer.FindReplacement(ReplacementCacheKey(0xA000000D00000001ULL, 0x10000003), 16, 8);
	ReplacedTexture *shortFile = replacer.FindReplacement(ReplacementCacheKey(0xA000000E00000001ULL, 0x10000004), 16, 8);
	if (!raw || !zstd || !rgba || !shortFile) {
		return false;
	}

	g_threadManager.Init(1, 1);
	for (ReplacedTexture *tex : { raw, zstd }) {
		EXPECT_TRUE(tex->Poll(1.0));
		EXPECT_TRUE(tex->State() == ReplacementState::ACTIVE);
		EXPECT_EQ_INT(tex->NumLevels(), 3);
		for (int i = 0; i < 3; i++) {
			int w = 0, h = 0;
			tex->GetIndexDataSize(i, &w, &h);
			EXPECT_EQ_INT(w, 16 >> i);
			EXPECT_EQ_INT(h, 8 >> i);
			std::vector<u8> copy(levels[i].size());
			EXPECT_TRUE(tex->CopyIndexLevelTo(i, copy.data(), copy.size()));
			EXPECT_TRUE(copy == levels[i]);
		}
	}

	// Not R8, and not enough data.
	EXPECT_TRUE(rgba->Poll(1.0));
	EXPECT_TRUE(rgba->State() == ReplacementState::NOT_FOUND);
	EXPECT_TRUE(shortFile->Poll(1.0));
	EXPECT_TRUE(shortFile->State() == ReplacementState::NOT_FOUND);
	g_threadManager.Teardown();
	return true;
}

// The [indexed] section marks an image as palette indices. It is looked up without the CLUT hash
// so that every palette of the texture shares the one image.
static bool TestIndexedReplacement(const Path &packDir) {
	static const char *iniContent =
		"[options]\n"
		"hash = quick\n"
		"version = 1\n"
		"\n"
		"[hashes]\n"
		"A00000080000000010000008 = indexed.png\n"
		"A00000090000000010000009 = indexed_rgba.png\n"
		"A000000A000000001000000A = indexed.png\n"
		"A000000B000000001000000B = indexed.png\n"
		"\n"
		"[indexed]\n"
		"A00000080000000010000008 = true\n"
		"A00000090000000010000009 = true\n"
		"A000000B000000001000000B = gpu\n";
	if (!WriteIni(packDir, iniContent)) {
		return false;
	}

	std::vector<u8> indices(16 * 8);
	for (size_t i = 0; i < indices.size(); i++) {
		indices[i] = (u8)(255 - i * 2);
	}
	if (!CreateIndexedPNG(packDir / "indexed.png", 16, 8, true, indices)) {
		return false;
	}
	if (!CreateTestPNG(packDir / "indexed_rgba.png", 16, 8, 0x11223344)) {
		return false;
	}

	TextureReplacer replacer(nullptr);
	std::string error;
	if (!replacer.LoadPackForTesting(packDir, &error)) {
		return false;
	}

	// Two palettes of the same texture.
	ReplacedTexture *pal1 = replacer.FindReplacement(ReplacementCacheKey(0xA0000008C0000001ULL, 0x10000008), 16, 8);
	ReplacedTexture *pal2 = replacer.FindReplacement(ReplacementCacheKey(0xA0000008C0000002ULL, 0x10000008), 16, 8);
	EXPECT_TRUE(pal1 != nullptr);
	EXPECT_TRUE(pal1 == pal2);

	// The same image without the [indexed] entry is a regular replacement, which can't share the texture.
	ReplacedTexture *plain = replacer.FindReplacement(ReplacementCacheKey(0xA000000AC0000001ULL, 0x1000000A), 16, 8);
	EXPECT_TRUE(plain != nullptr);
	EXPECT_TRUE(plain != pal1);

	ReplacedTexture *bad = replacer.FindReplacement(ReplacementCacheKey(0xA0000009C0000001ULL, 0x10000009), 16, 8);
	EXPECT_TRUE(bad != nullptr);
	if (!pal1 || !plain || !bad) {
		return false;
	}
	EXPECT_TRUE(pal1->IsIndexed());
	EXPECT_FALSE(pal1->IsIndexedOnGpu());
	EXPECT_FALSE(plain->IsIndexed());

	// The shader colors this one. A different mode makes it a different replacement even with the same file.
	ReplacedTexture *onGpu = replacer.FindReplacement(ReplacementCacheKey(0xA000000BC0000001ULL, 0x1000000B), 16, 8);
	EXPECT_TRUE(onGpu != nullptr);
	if (!onGpu) {
		return false;
	}
	EXPECT_TRUE(onGpu != pal1);
	EXPECT_TRUE(onGpu->IsIndexed());
	EXPECT_TRUE(onGpu->IsIndexedOnGpu());

	g_threadManager.Init(1, 1);
	EXPECT_TRUE(pal1->Poll(1.0));
	EXPECT_TRUE(pal1->State() == ReplacementState::ACTIVE);
	EXPECT_EQ_INT(pal1->NumLevels(), 1);
	int w = 0, h = 0;
	pal1->GetIndexDataSize(0, &w, &h);
	EXPECT_EQ_INT(w, 16);
	EXPECT_EQ_INT(h, 8);
	pal1->GetSize(0, &w, &h);
	EXPECT_EQ_INT(w, 16);
	EXPECT_EQ_INT(h, 8);
	EXPECT_TRUE(pal1->Format() == Draw::DataFormat::R8G8B8A8_UNORM);

	std::vector<u8> copy(indices.size());
	EXPECT_TRUE(pal1->CopyIndexLevelTo(0, copy.data(), copy.size()));
	EXPECT_TRUE(copy == indices);

	// For the GPU the indices go into an R8 image with its own pitch, and the padding is cleared.
	EXPECT_TRUE(onGpu->Poll(1.0));
	EXPECT_TRUE(onGpu->State() == ReplacementState::ACTIVE);
	const int pitch = 24;
	std::vector<u8> pitched(pitch * 8, 0xAA);
	EXPECT_TRUE(onGpu->CopyIndexLevelToPitched(0, pitched.data(), pitched.size(), pitch));
	for (int y = 0; y < 8; y++) {
		EXPECT_TRUE(memcmp(&pitched[pitch * y], &indices[16 * y], 16) == 0);
	}
	// The gap between the rows is not ours to touch.
	EXPECT_EQ_INT(pitched[16], 0xAA);
	// A pitch smaller than the row, or a buffer that is too small, is refused.
	EXPECT_FALSE(onGpu->CopyIndexLevelToPitched(0, pitched.data(), pitched.size(), 8));
	EXPECT_FALSE(onGpu->CopyIndexLevelToPitched(0, pitched.data(), pitch * 7, pitch));

	// A truecolor image can't serve as indices.
	EXPECT_TRUE(bad->Poll(1.0));
	EXPECT_TRUE(bad->State() == ReplacementState::NOT_FOUND);
	g_threadManager.Teardown();
	return true;
}

// Saves a palette PNG and checks that it is one, and that the indices and palette come back.
static bool TestSaveIndexedPNG(const Path &packDir) {
	const int w = 16, h = 8;
	std::vector<u8> indices(w * h);
	for (int i = 0; i < w * h; i++) {
		indices[i] = (u8)(i % 5);
	}
	u32 palette[5] = {0xFF0000FF, 0xFF00FF00, 0xFFFF0000, 0x80123456, 0x00000000};

	Path file = packDir / "saved_indexed.png";
	EXPECT_TRUE(pngSaveIndexed(file, indices.data(), w, h, palette, 5));
	EXPECT_FALSE(pngSaveIndexed(file, indices.data(), w, h, palette, 0));
	EXPECT_FALSE(pngSaveIndexed(file, indices.data(), w, h, palette, 257));

	std::string str;
	if (!File::ReadBinaryFileToString(file, &str) || str.size() < sizeof(PNGHeaderPeek)) {
		return false;
	}
	PNGHeaderPeek peek;
	memcpy(&peek, str.data(), sizeof(peek));
	EXPECT_TRUE(peek.IsValidPNGHeader());
	EXPECT_EQ_INT(peek.Width(), w);
	EXPECT_EQ_INT(peek.Height(), h);
	EXPECT_EQ_INT(peek.colorType, 3);
	EXPECT_EQ_INT(peek.bitDepth, 8);
	// The alpha of the last entry is 0 and the first three are opaque, so a tRNS chunk is needed.
	EXPECT_TRUE(str.find("PLTE") != std::string::npos);
	EXPECT_TRUE(str.find("tRNS") != std::string::npos);

	int lw = 0, lh = 0;
	unsigned char *loaded = nullptr;
	EXPECT_EQ_INT(pngLoadIndexed((const unsigned char *)str.data(), str.size(), &lw, &lh, &loaded), 1);
	if (!loaded) {
		return false;
	}
	EXPECT_EQ_INT(lw, w);
	EXPECT_EQ_INT(lh, h);
	EXPECT_TRUE(memcmp(loaded, indices.data(), indices.size()) == 0);
	free(loaded);

	// Fully opaque palettes have no use for tRNS.
	u32 opaque[2] = {0xFF000000, 0xFFFFFFFF};
	std::vector<u8> bits(w * h, 1);
	Path file2 = packDir / "saved_opaque.png";
	EXPECT_TRUE(pngSaveIndexed(file2, bits.data(), w, h, opaque, 2));
	if (!File::ReadBinaryFileToString(file2, &str)) {
		return false;
	}
	EXPECT_TRUE(str.find("tRNS") == std::string::npos);
	return true;
}

// saveIndexed = true saves the textures of the [indexed] section as palette PNGs, all saves every one.
static bool TestSaveIndexedOption(const Path &packDir) {
	static const char *header =
		"[options]\n"
		"hash = quick\n"
		"version = 1\n";
	static const char *body =
		"\n[hashes]\n"
		"A000000F0000000010000001 = listed_indexed.png\n"
		"\n[indexed]\n"
		"A000000F0000000010000001 = true\n";
	static const u32 addr = 0x09000000;
	// The CLUT hash in the key is ignored by the [indexed] lookup.
	ReplacedTextureDecodeInfo listed{0xA000000F12345678ULL, 0x10000001, addr, false, false, Draw::DataFormat::R8G8B8A8_UNORM};
	ReplacedTextureDecodeInfo other{0xA000001012345678ULL, 0x10000002, addr, false, false, Draw::DataFormat::R8G8B8A8_UNORM};
	ReplacedTextureDecodeInfo ppge = listed;
	ppge.addr = 0x05100000;

	static const struct {
		const char *line;
		bool listedSaved;
		bool otherSaved;
	} cases[] = {
		{"", false, false},
		{"saveIndexed = false\n", false, false},
		{"saveIndexed = true\n", true, false},
		{"saveIndexed = all\n", true, true},
		{"saveIndexed = bogus\n", false, false},
	};
	for (const auto &c : cases) {
		std::string ini = std::string(header) + c.line + body;
		if (!WriteIni(packDir, ini.c_str())) {
			return false;
		}
		TextureReplacer replacer(nullptr);
		std::string error;
		if (!replacer.LoadPackForTesting(packDir, &error, true)) {
			return false;
		}
		EXPECT_EQ_INT(replacer.WillSaveIndexed(listed), c.listedSaved);
		EXPECT_EQ_INT(replacer.WillSaveIndexed(other), c.otherSaved);
		EXPECT_FALSE(replacer.WillSaveIndexed(ppge));
		EXPECT_FALSE(replacer.HasSavedIndexed(listed));
	}

	// Without saving enabled, nothing is saved.
	std::string ini = std::string(header) + "saveIndexed = all\n" + body;
	if (!WriteIni(packDir, ini.c_str())) {
		return false;
	}
	TextureReplacer replacer(nullptr);
	std::string error;
	if (!replacer.LoadPackForTesting(packDir, &error)) {
		return false;
	}
	EXPECT_FALSE(replacer.WillSaveIndexed(other));
	return true;
}

bool TestTextureReplacer() {
	Path packDir = Path("unittest_texture_pack");
	if (!CreateTestPack(packDir)) {
		return false;
	}

	TextureReplacer replacer(nullptr);
	std::string error;
	if (!replacer.LoadPackForTesting(packDir, &error)) {
		ERROR_LOG(Log::G3D, "Failed to load test texture pack: %s", error.c_str());
		File::DeleteDirRecursively(packDir);
		return false;
	}

	if (!TestLookups(&replacer)) {
		File::DeleteDirRecursively(packDir);
		return false;
	}

	// Reloading the ini forgets what the old one said about each texture.
	EXPECT_TRUE(replacer.GetNumTrackedTextures() != 0);
	EXPECT_TRUE(replacer.LoadPackForTesting(packDir, &error));
	EXPECT_EQ_INT(replacer.GetNumTrackedTextures(), 0);

	if (!TestIgnoreAddress(packDir)) {
		File::DeleteDirRecursively(packDir);
		return false;
	}

	if (!TestIndexedPNG(packDir) || !TestIndexedReplacement(packDir) || !TestIndexedKTX2(packDir) ||
		!TestSaveIndexedPNG(packDir) || !TestSaveIndexedOption(packDir)) {
		File::DeleteDirRecursively(packDir);
		return false;
	}

	File::DeleteDirRecursively(packDir);
	return true;
}
