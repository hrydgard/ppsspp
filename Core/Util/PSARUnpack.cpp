// Copyright (c) 2026- PPSSPP Project.

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

#include <cstring>
#include <map>
#include <string_view>

#include "zlib.h"

#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/ELF/PBPReader.h"
#include "Core/ELF/PrxDecrypter.h"
#include "Core/Loaders.h"
#include "Core/Util/PSARUnpack.h"

extern "C" {
#include "ext/libkirk/kirk_engine.h"
}

// A PSAR record is [header][entry], where the header is 0x150 bytes of PRX-style encryption
// metadata and the entry is 0x110 bytes describing one file. Pre-decrypted archives (rare, and
// only produced by other tools) have no header at all.
static const u32 PSAR_MAGIC = 0x52415350;  // "PSAR"
static const u32 PSAR_DECRYPTED_MARKER = 0x2C333333;
static const u32 PSAR_ENTRY_SIZE = 0x110;
static const u32 PSAR_HEADER_SIZE = 0x150;
// The KIRK CMD7 key the "demangle" step below uses. Same for every firmware.
static const int PSAR_DEMANGLE_KEYSEED = 0x55;
// Sanity bound on the sizes we take from the archive, so a corrupt one can't ask for a huge
// allocation. No file inside an updater comes close.
static const u32 PSAR_MAX_ENTRY_BYTES = 64 * 1024 * 1024;

const char *PSARCompressionToString(PSARCompression c) {
	switch (c) {
	case PSARCompression::None: return "none";
	case PSARCompression::Zlib: return "zlib";
	case PSARCompression::KL4E: return "KL4E";
	case PSARCompression::KL3E: return "KL3E";
	case PSARCompression::LZR: return "LZR";
	default: return "unknown";
	}
}

static u32 ReadU32(const u8 *p) {
	u32 value;
	memcpy(&value, p, sizeof(value));
	return value;
}

static u16 ReadU16(const u8 *p) {
	u16 value;
	memcpy(&value, p, sizeof(value));
	return value;
}

static PSARCompression DetectCompression(const u8 *data, size_t size) {
	if (size < 4) {
		return PSARCompression::Unknown;
	}
	// Standard zlib stream: CM=8, CINFO=7, then the check byte.
	if (data[0] == 0x78 && data[1] == 0x9C) {
		return PSARCompression::Zlib;
	}
	if (!memcmp(data, "KL4E", 4)) {
		return PSARCompression::KL4E;
	}
	if (!memcmp(data, "KL3E", 4)) {
		return PSARCompression::KL3E;
	}
	if (!memcmp(data, "2RLZ", 4)) {
		return PSARCompression::LZR;
	}
	return PSARCompression::Unknown;
}

// Turns a name out of the archive into a path relative to the output directory:
// "flash0:/font/ltn0.pgf" becomes "flash0/font/ltn0.pgf". Returns false for anything that could
// escape the output directory - these names come from a file we didn't write.
static bool RelativePathFromEntryName(std::string_view name, std::string *out) {
	std::string path(name);
	if (path.empty()) {
		return false;
	}
	for (size_t i = 0; i < path.size(); i++) {
		if (path[i] == ':' || path[i] == '\\') {
			path[i] = '/';
		}
	}
	// Collapse the "flash0:/" -> "flash0//" the above just produced, and any other doubles.
	std::string cleaned;
	cleaned.reserve(path.size());
	for (size_t i = 0; i < path.size(); i++) {
		if (path[i] == '/' && (cleaned.empty() || cleaned.back() == '/')) {
			continue;
		}
		cleaned.push_back(path[i]);
	}
	if (cleaned.empty()) {
		return false;
	}
	// No traversal, no absolute paths, no drive letters - all of which would land outside outputDir.
	if (cleaned.find("..") != std::string::npos) {
		return false;
	}
	*out = cleaned;
	return true;
}

// An entry name is either a real path ("flash0:/...") or a short token that only the archive's
// own name tables can resolve. We can use the former directly.
static bool EntryNameIsRealPath(std::string_view name) {
	return startsWithNoCase(name, "flash0:/") || startsWithNoCase(name, "flash1:/");
}

// The name tables inside the archive are DES-CBC encrypted, with a PRX blob underneath. Nothing
// here is PSP-specific - it's plain DES with the standard tables - so it lives at file scope
// rather than pretending to be part of the PSAR format.
static const u8 kIP[64] = {
	58,50,42,34,26,18,10,2, 60,52,44,36,28,20,12,4, 62,54,46,38,30,22,14,6, 64,56,48,40,32,24,16,8,
	57,49,41,33,25,17, 9,1, 59,51,43,35,27,19,11,3, 61,53,45,37,29,21,13,5, 63,55,47,39,31,23,15,7 };
static const u8 kFP[64] = {
	40,8,48,16,56,24,64,32, 39,7,47,15,55,23,63,31, 38,6,46,14,54,22,62,30, 37,5,45,13,53,21,61,29,
	36,4,44,12,52,20,60,28, 35,3,43,11,51,19,59,27, 34,2,42,10,50,18,58,26, 33,1,41, 9,49,17,57,25 };
static const u8 kE[48] = {
	32,1,2,3,4,5, 4,5,6,7,8,9, 8,9,10,11,12,13, 12,13,14,15,16,17,
	16,17,18,19,20,21, 20,21,22,23,24,25, 24,25,26,27,28,29, 28,29,30,31,32,1 };
static const u8 kP[32] = {
	16,7,20,21, 29,12,28,17, 1,15,23,26, 5,18,31,10, 2,8,24,14, 32,27,3,9, 19,13,30,6, 22,11,4,25 };
static const u8 kPC1[56] = {
	57,49,41,33,25,17, 9, 1,58,50,42,34,26,18, 10, 2,59,51,43,35,27, 19,11, 3,60,52,44,36,
	63,55,47,39,31,23,15, 7,62,54,46,38,30,22, 14, 6,61,53,45,37,29, 21,13, 5,28,20,12, 4 };
static const u8 kPC2[48] = {
	14,17,11,24, 1, 5, 3,28,15, 6,21,10, 23,19,12, 4,26, 8, 16, 7,27,20,13, 2,
	41,52,31,37,47,55, 30,40,51,45,33,48, 44,49,39,56,34,53, 46,42,50,36,29,32 };
static const u8 kShifts[16] = { 1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1 };
static const u8 kSBox[8][64] = {
	{ 14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7,  0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8,
	   4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0, 15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13 },
	{ 15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10,  3,13,4,7,15,2,8,14,12,0,1,10,6,9,11,5,
	   0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15, 13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9 },
	{ 10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8, 13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1,
	  13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7,  1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12 },
	{  7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15, 13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9,
	  10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4,   3,15,0,6,10,1,13,8,9,4,5,11,12,7,2,14 },
	{  2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9, 14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6,
	   4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14, 11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3 },
	{ 12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11,  10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8,
	   9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6,  4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13 },
	{  4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1, 13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6,
	   1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2,  6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12 },
	{ 13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7,   1,15,13,8,10,3,7,4,12,5,6,11,0,14,9,2,
	   7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8,  2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11 } };

// One bit per byte throughout - the tables are a few tens of KB in total, so this doesn't need
// to be a fast DES, and the permutations are much easier to read this way.
static void BytesToBits(const u8 *bytes, int byteCount, u8 *bits) {
	for (int i = 0; i < byteCount; i++) {
		for (int bit = 0; bit < 8; bit++) {
			bits[i * 8 + bit] = (bytes[i] >> (7 - bit)) & 1;
		}
	}
}

static void BitsToBytes(const u8 *bits, int byteCount, u8 *bytes) {
	for (int i = 0; i < byteCount; i++) {
		u8 value = 0;
		for (int bit = 0; bit < 8; bit++) {
			value = (u8)((value << 1) | bits[i * 8 + bit]);
		}
		bytes[i] = value;
	}
}

// The tables are 1-based, as they're always written.
static void Permute(const u8 *in, u8 *out, const u8 *table, int count) {
	for (int i = 0; i < count; i++) {
		out[i] = in[table[i] - 1];
	}
}

class DESContext {
public:
	explicit DESContext(const u8 key[8]) {
		u8 keyBits[64];
		BytesToBits(key, 8, keyBits);
		u8 cd[56];
		Permute(keyBits, cd, kPC1, 56);
		u8 *c = cd;
		u8 *d = cd + 28;
		for (int round = 0; round < 16; round++) {
			for (int shift = 0; shift < kShifts[round]; shift++) {
				const u8 cFirst = c[0];
				const u8 dFirst = d[0];
				memmove(c, c + 1, 27);
				memmove(d, d + 1, 27);
				c[27] = cFirst;
				d[27] = dFirst;
			}
			Permute(cd, subkeys_[round], kPC2, 48);
		}
	}

	void DecryptBlock(const u8 in[8], u8 out[8]) const {
		u8 bits[64];
		u8 permuted[64];
		BytesToBits(in, 8, bits);
		Permute(bits, permuted, kIP, 64);

		u8 left[32], right[32];
		memcpy(left, permuted, 32);
		memcpy(right, permuted + 32, 32);

		// Subkeys in reverse order is what makes this a decrypt.
		for (int round = 15; round >= 0; round--) {
			u8 expanded[48];
			Permute(right, expanded, kE, 48);
			for (int i = 0; i < 48; i++) {
				expanded[i] ^= subkeys_[round][i];
			}

			u8 substituted[32];
			for (int box = 0; box < 8; box++) {
				const u8 *six = expanded + box * 6;
				const int row = (six[0] << 1) | six[5];
				const int column = (six[1] << 3) | (six[2] << 2) | (six[3] << 1) | six[4];
				const u8 value = kSBox[box][row * 16 + column];
				for (int bit = 0; bit < 4; bit++) {
					substituted[box * 4 + bit] = (value >> (3 - bit)) & 1;
				}
			}

			u8 mixed[32];
			Permute(substituted, mixed, kP, 32);
			u8 next[32];
			for (int i = 0; i < 32; i++) {
				next[i] = left[i] ^ mixed[i];
			}
			memcpy(left, right, 32);
			memcpy(right, next, 32);
		}

		u8 combined[64];
		memcpy(combined, right, 32);
		memcpy(combined + 32, left, 32);
		Permute(combined, permuted, kFP, 64);
		BitsToBytes(permuted, 8, out);
	}

private:
	u8 subkeys_[16][48];
};

// One DES key and IV per firmware series.
struct PSARTableKey {
	u32 keyLow;
	u32 keyHigh;
	u8 iv[8];
};

static const PSARTableKey kTableKeys[] = {
	{ 0xB730E5C7, 0x95620B49, { 0x9E, 0xA4, 0x33, 0x81, 0x86, 0x0C, 0x52, 0x85 } },
	{ 0x45C9DC95, 0x5A7B3D9D, { 0xB2, 0xFE, 0xD9, 0x79, 0x8A, 0x02, 0xB1, 0x87 } },
	{ 0x6F20585A, 0x4CCE495B, { 0x81, 0x08, 0xC1, 0xF2, 0x35, 0x98, 0x69, 0xB0 } },
	{ 0x620BF15A, 0x73F45262, { 0x6D, 0x52, 0x1B, 0xA3, 0xC2, 0x36, 0xF9, 0x2B } },
	{ 0xFD9D4498, 0xA664C8F8, { 0xDB, 0x4E, 0x79, 0x41, 0xF5, 0x97, 0x30, 0xAD } },
	{ 0x3D6426E7, 0xD7BD7481, { 0xA6, 0x83, 0x0C, 0x2F, 0x63, 0x0B, 0x96, 0x29 } },
};

// Which of those a firmware uses, from the version string in the archive's first block.
static int TableKeyIndexForVersion(std::string_view version) {
	if (startsWith(version, "3.8") || startsWith(version, "3.9")) {
		return 1;
	} else if (startsWith(version, "4.")) {
		return 2;
	} else if (startsWith(version, "5.")) {
		return 3;
	} else if (startsWith(version, "6.")) {
		return 4;
	}
	return 0;
}

// Entries "00001".."00012" are name tables rather than files.
static bool IsNameTableEntry(std::string_view name) {
	if (name.size() != 5 || name.compare(0, 3, "000") != 0) {
		return false;
	}
	if (name[3] < '0' || name[3] > '9' || name[4] < '0' || name[4] > '9') {
		return false;
	}
	const int index = (name[3] - '0') * 10 + (name[4] - '0');
	return index >= 1 && index <= 12;
}

// Decrypts a name table in place and returns the length of the text in it, or <= 0 on failure.
static int DecryptNameTable(std::vector<u8> &table, int keyIndex) {
	if (keyIndex < 0 || keyIndex >= (int)ARRAY_SIZE(kTableKeys) || table.size() < 8) {
		return -1;
	}
	const PSARTableKey &tableKey = kTableKeys[keyIndex];

	u8 key[8];
	const u64 combined = ((u64)tableKey.keyHigh << 32) | tableKey.keyLow;
	for (int i = 0; i < 8; i++) {
		key[i] = (u8)(combined >> (56 - i * 8));
	}
	const DESContext des(key);

	u8 previous[8];
	memcpy(previous, tableKey.iv, sizeof(previous));
	for (size_t offset = 0; offset + 8 <= table.size(); offset += 8) {
		u8 *at = table.data() + offset;
		u8 cipher[8], plain[8];
		memcpy(cipher, at, sizeof(cipher));
		des.DecryptBlock(cipher, plain);
		for (int i = 0; i < 8; i++) {
			at[i] = plain[i] ^ previous[i];
		}
		memcpy(previous, cipher, sizeof(previous));
	}

	// What's underneath is an ordinary PRX blob.
	return pspDecryptPRX(table.data(), table.data(), (u32)table.size());
}

// Table text is lines of "shortname,realpath". The tables are per PSP model, and a short name
// that appears in several of them means the same file, so the first one to claim it wins.
static void ParseNameTable(const char *text, size_t length, std::map<std::string, std::string> *names) {
	size_t start = 0;
	while (start < length) {
		size_t end = start;
		while (end < length && text[end] != '\r' && text[end] != '\n') {
			end++;
		}
		const std::string_view line(text + start, end - start);
		const size_t comma = line.find(',');
		if (comma != std::string_view::npos && comma > 0) {
			names->emplace(std::string(line.substr(0, comma)), std::string(line.substr(comma + 1)));
		}
		while (end < length && (text[end] == '\r' || text[end] == '\n')) {
			end++;
		}
		start = end;
	}
}

class PSARReader {
public:
	PSARReader(const u8 *psar, size_t size) : psar_(psar), size_(size) {}

	bool Init(std::string *error);

	// 1: got an entry. 0: no more entries. -1: failed.
	int NextEntry(std::string *error);

	const std::string &firmwareVersion() const { return firmwareVersion_; }
	const std::string &entryName() const { return entryName_; }
	bool entryIsDirectory() const { return entryIsDirectory_; }
	PSARCompression entryCompression() const { return entryCompression_; }
	// Empty for a directory, or for an entry we couldn't decompress.
	const std::vector<u8> &entryData() const { return entryData_; }

private:
	// Decrypts one record into 'out'. Returns the decrypted size, or <= 0 on failure.
	int DecodeBlock(u32 offset, u32 cbIn, std::vector<u8> &out);

	const u8 *psar_;
	size_t size_;

	bool decrypted_ = false;
	bool oldschool_ = false;
	u32 overhead_ = PSAR_HEADER_SIZE;
	u32 pos_ = 0;
	std::string firmwareVersion_;

	std::string entryName_;
	bool entryIsDirectory_ = false;
	PSARCompression entryCompression_ = PSARCompression::None;
	std::vector<u8> entryData_;

	std::vector<u8> block_;
	std::vector<u8> block2_;
};

int PSARReader::DecodeBlock(u32 offset, u32 cbIn, std::vector<u8> &out) {
	if (cbIn == 0 || cbIn > PSAR_MAX_ENTRY_BYTES || offset >= size_) {
		return -1;
	}
	const u8 *in = psar_ + offset;
	const size_t avail = size_ - offset;

	if (decrypted_) {
		if (cbIn > avail) {
			return -1;
		}
		out.assign(in, in + cbIn);
		return (int)cbIn;
	}

	// The decrypter reads a little past the block, so copy the extra 16 bytes it expects.
	if ((size_t)cbIn + 0x10 > avail) {
		return -1;
	}
	out.assign(in, in + cbIn + 0x10);

	if (!oldschool_) {
		// "Demangle": the 0x130 bytes at +0x20 are AES-128-CBC encrypted on top of everything
		// else, and hide the PRX tag at +0xD0 that says how to decrypt the rest.
		if (cbIn < 0x150) {
			return -1;
		}
		kirk7(out.data() + 0x20, in + 0x20, 0x130, PSAR_DEMANGLE_KEYSEED);
	}

	const int decrypted = pspDecryptPRX(out.data(), out.data(), cbIn);
	if (decrypted <= 0) {
		WARN_LOG(Log::Loader, "PSAR: block at %u (%u bytes) failed to decrypt: %d, tag %08x", offset, cbIn, decrypted, ReadU32(out.data() + 0xD0));
	}
	return decrypted;
}

bool PSARReader::Init(std::string *error) {
	if (size_ < 0x40 || ReadU32(psar_) != PSAR_MAGIC) {
		*error = "Not a PSAR archive";
		return false;
	}

	const u8 version = psar_[4];
	oldschool_ = version == 1;
	decrypted_ = ReadU32(psar_ + 0x20) == PSAR_DECRYPTED_MARKER;
	overhead_ = decrypted_ ? 0 : PSAR_HEADER_SIZE;

	INFO_LOG(Log::Loader, "PSAR version %d, %s, %d bytes", version, decrypted_ ? "already decrypted" : "encrypted", (int)size_);

	int decoded = DecodeBlock(0x10, overhead_ + PSAR_ENTRY_SIZE, block_);
	if (decoded != (int)PSAR_ENTRY_SIZE) {
		*error = StringFromFormat("Couldn't decrypt the PSAR's first block (got %d)", decoded);
		return false;
	}

	// The first block is a text record ending in the firmware version, e.g. "...,6.61".
	const char *text = (const char *)block_.data() + 0x10;
	const size_t textLen = strnlen(text, PSAR_ENTRY_SIZE - 0x10);
	const std::string_view firstLine(text, textLen);
	const size_t comma = firstLine.rfind(',');
	firmwareVersion_ = comma == std::string_view::npos ? std::string(firstLine) : std::string(firstLine.substr(comma + 1));

	pos_ = 0x10 + overhead_ + PSAR_ENTRY_SIZE;

	if (decrypted_) {
		decoded = DecodeBlock(pos_, ReadU32(block_.data() + 0x90), block2_);
		if (decoded <= 0) {
			*error = "Couldn't read the PSAR's second block";
			return false;
		}
		pos_ += overhead_ + decoded;
		return true;
	}

	if (!oldschool_) {
		// The second block's size isn't recorded anywhere, so try the ones real updaters use.
		// 100 covers most, 2.7x is bigger, and some store it in the first block.
		const u32 candidates[] = { 100, 144, ReadU16(block_.data() + 0x90) };
		decoded = -1;
		for (u32 candidate : candidates) {
			if (candidate == 0) {
				continue;
			}
			decoded = DecodeBlock(pos_, overhead_ + candidate, block2_);
			if (decoded > 0) {
				break;
			}
		}
		if (decoded <= 0) {
			*error = "Couldn't read the PSAR's second block";
			return false;
		}
		pos_ += overhead_ + ((decoded + 15) & ~15);
	}

	return true;
}

int PSARReader::NextEntry(std::string *error) {
	entryName_.clear();
	entryData_.clear();
	entryIsDirectory_ = false;
	entryCompression_ = PSARCompression::None;

	if ((size_t)pos_ + overhead_ >= size_) {
		return 0;
	}

	int decoded = DecodeBlock(pos_, overhead_ + PSAR_ENTRY_SIZE, block_);
	if (decoded != (int)PSAR_ENTRY_SIZE) {
		*error = StringFromFormat("Couldn't decrypt the entry at %u (got %d)", pos_, decoded);
		return -1;
	}

	// A well-formed entry has a zero here. If it doesn't, we've lost our place in the archive.
	if (ReadU32(block_.data() + 0x100) != 0) {
		*error = StringFromFormat("Malformed entry at %u", pos_);
		return -1;
	}

	const char *name = (const char *)block_.data() + 4;
	entryName_.assign(name, strnlen(name, PSAR_ENTRY_SIZE - 4));

	pos_ += overhead_ + PSAR_ENTRY_SIZE;

	const u32 chunkSize = ReadU32(block_.data() + 0x104);
	const u32 expandedSize = ReadU32(block_.data() + 0x108);

	if (expandedSize == 0) {
		entryIsDirectory_ = true;
	} else if (expandedSize > PSAR_MAX_ENTRY_BYTES) {
		*error = StringFromFormat("Entry '%s' claims an unreasonable size (%u)", entryName_.c_str(), expandedSize);
		return -1;
	} else {
		decoded = DecodeBlock(pos_, chunkSize, block2_);
		if (decoded <= 0) {
			WARN_LOG(Log::Loader, "PSAR: couldn't decrypt the contents of '%s'", entryName_.c_str());
			entryCompression_ = PSARCompression::Unknown;
		} else {
			entryCompression_ = DetectCompression(block2_.data(), decoded);
			if (entryCompression_ == PSARCompression::Zlib) {
				entryData_.resize(expandedSize);
				uLongf destLen = expandedSize;
				const int zResult = uncompress(entryData_.data(), &destLen, block2_.data(), decoded);
				if (zResult != Z_OK || destLen != expandedSize) {
					WARN_LOG(Log::Loader, "PSAR: inflate failed for '%s' (%d)", entryName_.c_str(), zResult);
					entryData_.clear();
				}
			}
			// Anything else we leave empty - the caller reports it through the stats.
		}
	}

	pos_ += chunkSize;
	return 1;
}

bool UnpackPSAR(const u8 *psar, size_t psarSize, const Path &outputDir, const PSARUnpackOptions &options, PSARUnpackStats *stats, std::string *error) {
	PSARUnpackStats localStats;
	if (!stats) {
		stats = &localStats;
	}
	std::string localError;
	if (!error) {
		error = &localError;
	}

	PSARReader reader(psar, psarSize);
	if (!reader.Init(error)) {
		return false;
	}
	stats->firmwareVersion = reader.firmwareVersion();
	const int tableKeyIndex = TableKeyIndexForVersion(stats->firmwareVersion);
	INFO_LOG(Log::Loader, "Unpacking firmware %s (name table key %d)", stats->firmwareVersion.c_str(), tableKeyIndex);

	// Filled in as we go - the tables come before the files they name.
	std::map<std::string, std::string> names;

	while (true) {
		const int result = reader.NextEntry(error);
		if (result < 0) {
			// Losing our place means the rest of the archive is unreadable, so stop rather than
			// spraying garbage - but keep whatever we already extracted.
			ERROR_LOG(Log::Loader, "PSAR: %s", error->c_str());
			stats->failed++;
			return false;
		}
		if (result == 0) {
			break;
		}

		stats->entries++;
		stats->compressionCounts[(int)reader.entryCompression()]++;

		if (options.verbose) {
			INFO_LOG(Log::Loader, "PSAR entry '%s' (%s, %d bytes)", reader.entryName().c_str(),
				PSARCompressionToString(reader.entryCompression()), (int)reader.entryData().size());
		}

		if (reader.entryIsDirectory()) {
			stats->directories++;
			continue;
		}

		// The name tables have to be read before anything they name shows up, which the archive's
		// own ordering takes care of.
		if (IsNameTableEntry(reader.entryName())) {
			stats->nameTables++;
			std::vector<u8> table = reader.entryData();
			const int textLength = DecryptNameTable(table, tableKeyIndex);
			if (textLength <= 0 || (size_t)textLength > table.size()) {
				ERROR_LOG(Log::Loader, "PSAR: couldn't decrypt name table '%s' (%d)", reader.entryName().c_str(), textLength);
				stats->failed++;
			} else {
				const size_t before = names.size();
				ParseNameTable((const char *)table.data(), textLength, &names);
				INFO_LOG(Log::Loader, "PSAR: name table '%s' added %d names", reader.entryName().c_str(), (int)(names.size() - before));
			}
			continue;
		}

		std::string realName;
		if (EntryNameIsRealPath(reader.entryName())) {
			realName = reader.entryName();
		} else {
			const auto found = names.find(reader.entryName());
			if (found != names.end()) {
				realName = found->second;
			}
		}

		if (realName.empty()) {
			// No table claimed this one. Still worth writing out under its short name, but a
			// filter has nothing to match it against.
			stats->unnamed++;
			realName = reader.entryName();
			if (!options.prefixFilter.empty()) {
				stats->skippedByFilter++;
				continue;
			}
		} else if (!options.prefixFilter.empty() && !startsWithNoCase(realName, options.prefixFilter)) {
			stats->skippedByFilter++;
			continue;
		}

		if (reader.entryData().empty()) {
			ERROR_LOG(Log::Loader, "PSAR: no usable contents for '%s' (%s)", reader.entryName().c_str(),
				PSARCompressionToString(reader.entryCompression()));
			stats->failed++;
			continue;
		}

		if (options.listOnly) {
			stats->written++;
			continue;
		}

		std::string relative;
		if (!RelativePathFromEntryName(realName, &relative)) {
			ERROR_LOG(Log::Loader, "PSAR: refusing to write '%s'", realName.c_str());
			stats->failed++;
			continue;
		}

		const Path destination = outputDir / relative;
		if (!File::CreateFullPath(destination.NavigateUp())) {
			ERROR_LOG(Log::Loader, "PSAR: couldn't create a directory for '%s'", relative.c_str());
			stats->failed++;
			continue;
		}
		if (!File::WriteDataToFile(false, reader.entryData().data(), reader.entryData().size(), destination)) {
			ERROR_LOG(Log::Loader, "PSAR: couldn't write '%s'", destination.c_str());
			stats->failed++;
			continue;
		}
		stats->written++;
	}

	return true;
}

bool UnpackUpdaterPBP(const Path &pbpFilename, const Path &outputDir, const PSARUnpackOptions &options, PSARUnpackStats *stats, std::string *error) {
	std::string localError;
	if (!error) {
		error = &localError;
	}

	FileLoader *loader = ConstructFileLoader(pbpFilename);
	if (!loader) {
		*error = "Couldn't open " + pbpFilename.ToString();
		return false;
	}

	PBPReader pbp(loader);
	if (!pbp.IsValid()) {
		*error = pbpFilename.ToString() + " is not a PBP";
		delete loader;
		return false;
	}

	std::vector<u8> psar;
	if (!pbp.GetSubFile(PBP_UNKNOWN_PSAR, &psar) || psar.size() < 0x40) {
		*error = "No DATA.PSAR in " + pbpFilename.ToString();
		delete loader;
		return false;
	}
	delete loader;

	INFO_LOG(Log::Loader, "Found a %d byte DATA.PSAR in %s", (int)psar.size(), pbpFilename.c_str());
	return UnpackPSAR(psar.data(), psar.size(), outputDir, options, stats, error);
}
