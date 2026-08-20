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
	INFO_LOG(Log::Loader, "Unpacking firmware %s", stats->firmwareVersion.c_str());

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

		const bool haveRealPath = EntryNameIsRealPath(reader.entryName());
		if (!haveRealPath) {
			// A short name that only the archive's own name tables can resolve - those are stored
			// inside it under their own encryption, which we don't decrypt yet. Still worth writing
			// out under the short name, but a filter can't match it.
			stats->unnamed++;
			if (!options.prefixFilter.empty()) {
				stats->skippedByFilter++;
				continue;
			}
		} else if (!options.prefixFilter.empty() && !startsWithNoCase(reader.entryName(), options.prefixFilter)) {
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
		if (!RelativePathFromEntryName(reader.entryName(), &relative)) {
			ERROR_LOG(Log::Loader, "PSAR: refusing to write '%s'", reader.entryName().c_str());
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
