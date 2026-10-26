// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <snappy-c.h>
#include <zstd.h>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"

enum class SerializeCompressType {
	NONE = 0,
	SNAPPY = 1,
	ZSTD = 2,
};

void PointerWrap::RewindForWrite(u8 *writePtr) {
	_assert_(mode == MODE_MEASURE);
	// Switch to writing mode, save the size for later checking and start again.
	measuredSize_ = Offset();
	mode = MODE_WRITE;
	*ptr = writePtr;
	ptrStart_ = writePtr;
}

bool PointerWrap::CheckAfterWrite() {
	_assert_(error != ERROR_NONE || mode == MODE_WRITE);
	size_t offset = Offset();
	if (measuredSize_ != 0 && offset != measuredSize_) {
		WARN_LOG(Log::SaveState, "CheckAfterWrite: Size mismatch! %d but expected %d", (int)offset, (int)measuredSize_);
		return false;
	}
	if (!checkpoints_.empty() && curCheckpoint_ != checkpoints_.size()) {
		WARN_LOG(Log::SaveState, "Checkpoint count mismatch!");
		return false;
	}
	return true;
}

PointerWrapSection PointerWrap::Section(const char *title, int minVer, int ver) {
	char marker[16] = {0};
	int foundVersion = ver;

	curTitle_ = title;

	// This is strncpy because we rely on its weird non-null-terminating zero-filling truncation behaviour.
	// Can't replace it with the more sensible truncate_cpy because that would break savestates.
	strncpy(marker, title, sizeof(marker));

	// Compare the measure and write passes. Sanity check to catch bugs, doesn't do anything for output.
	size_t offset = Offset();
	if (mode == MODE_MEASURE) {
		checkpoints_.emplace_back(marker, offset);
	} else if (mode == MODE_WRITE) {
		if (!checkpoints_.empty()) {
			if (checkpoints_.size() <= curCheckpoint_) {
				WARN_LOG(Log::SaveState, "Write: Not enough checkpoints from measure pass (%d). cur section: %s", (int)checkpoints_.size(), title);
				SetError(ERROR_FAILURE);
				return PointerWrapSection(*this, -1, title);
			}
			if (!checkpoints_[curCheckpoint_].Matches(marker, offset)) {
				WARN_LOG(Log::SaveState, "Checkpoint mismatch during write! Section %s but expected %s, offset %d but expected %d", title, marker, (int)offset, (int)checkpoints_[curCheckpoint_].offset);
				if (curCheckpoint_ > 1) {
					WARN_LOG(Log::SaveState, "Previous checkpoint: %s (%d)", checkpoints_[curCheckpoint_ - 1].title, (int)checkpoints_[curCheckpoint_ - 1].offset);
				}
				SetError(ERROR_FAILURE);
				return PointerWrapSection(*this, -1, title);
			}
		} else {
			WARN_LOG(Log::SaveState, "Writing savestate without checkpoints. This is OK but should be fixed.");
		}
		curCheckpoint_++;
	}

	if (!ExpectVoid(marker, sizeof(marker))) {
		// Might be before we added name markers for safety.
		if (foundVersion == 1 && ExpectVoid(&foundVersion, sizeof(foundVersion))) {
			DoMarker(title);
		} else {
			// Wasn't found, but maybe we can still load the state.
			foundVersion = 0;
		}
	} else {
		Do(*this, foundVersion);
	}

	if (error == ERROR_FAILURE || foundVersion < minVer || foundVersion > ver) {
		if (!firstBadSectionTitle_) {
			firstBadSectionTitle_ = title;
		}
		if (mode != MODE_NOOP) {
			WARN_LOG(Log::SaveState, "Savestate failure: wrong version %d found for section '%s'", foundVersion, title);
			SetError(ERROR_FAILURE);
		}
		return PointerWrapSection(*this, -1, title);
	}
	return PointerWrapSection(*this, foundVersion, title);
}

void PointerWrap::SetError(Error error_) {
	if (error < error_) {
		error = error_;
	}
	if (error > ERROR_WARNING) {
		// For the rest of this run, do nothing, to avoid running off the end of memory or something,
		// and also not logspam like MEASURE will do in an error case.
		mode = PointerWrap::MODE_NOOP;
		// Also, remember the bad section.
		if (!firstBadSectionTitle_) {
			firstBadSectionTitle_ = curTitle_;
		}
	}
}

bool PointerWrap::ExpectVoid(void *data, int size) {
	if (size < 0) {
		SetError(ERROR_FAILURE);
		return false;
	}
	switch (mode) {
	case MODE_READ:
		if (!CheckRead(size))
			return false;
		if (memcmp(data, *ptr, size) != 0) return false;
		break;
	case MODE_WRITE: memcpy(*ptr, data, size); break;
	case MODE_MEASURE: break;  // MODE_MEASURE - don't need to do anything
	case MODE_VERIFY:
		if (!CheckRead(size))
			return false;
		for (int i = 0; i < size; i++)
			_dbg_assert_msg_(((u8*)data)[i] == (*ptr)[i], "Savestate verification failure: %d (0x%X) (at %p) != %d (0x%X) (at %p).\n", ((u8*)data)[i], ((u8*)data)[i], &((u8*)data)[i], (*ptr)[i], (*ptr)[i], &(*ptr)[i]);
		break;
	default: break;  // throw an error?
	}
	(*ptr) += size;
	return true;
}

void PointerWrap::DoVoid(void *data, int size) {
	if (size < 0) {
		SetError(ERROR_FAILURE);
		return;
	}
	switch (mode) {
	case MODE_READ:
		if (!CheckRead(size))
			return;
		memcpy(data, *ptr, size);
		break;
	case MODE_WRITE: memcpy(*ptr, data, size); break;
	case MODE_MEASURE: break;  // MODE_MEASURE - don't need to do anything
	case MODE_VERIFY:
		if (!CheckRead(size))
			return;
		for (int i = 0; i < size; i++)
			_dbg_assert_msg_(((u8*)data)[i] == (*ptr)[i], "Savestate verification failure: %d (0x%X) (at %p) != %d (0x%X) (at %p).\n", ((u8*)data)[i], ((u8*)data)[i], &((u8*)data)[i], (*ptr)[i], (*ptr)[i], &(*ptr)[i]);
		break;
	default: break;  // throw an error?
	}
	(*ptr) += size;
}

// Not exactly sane but might catch some corrupt files.
const int MAX_SANE_STRING_LENGTH = 1024 * 1024;

void Do(PointerWrap &p, std::string &x) {
	int stringLen = (int)x.length() + 1;
	Do(p, stringLen);

	// Must be at least 1 (the NUL terminator alone, for an empty string) so that
	// stringLen - 1 below is never negative.
	if (stringLen < 1 || stringLen > MAX_SANE_STRING_LENGTH) {
		WARN_LOG(Log::SaveState, "Savestate failure: bad stringLen %d", stringLen);
		p.SetError(PointerWrap::ERROR_FAILURE);
		return;
	}
	// Ensure the whole string (including NUL terminator) is within bounds before reading.
	if (p.mode == PointerWrap::MODE_READ || p.mode == PointerWrap::MODE_VERIFY) {
		if (!p.CheckRead(stringLen))
			return;
	}

	switch (p.mode) {
	case PointerWrap::MODE_READ:
		// Use a length-bounded assign, not the raw-pointer constructor - the latter
		// strlen()s for a NUL terminator, which CheckRead() above didn't guarantee
		// exists within stringLen bytes for corrupted savestate data.
		x.assign((const char *)*p.ptr, stringLen - 1);
		break;
	case PointerWrap::MODE_WRITE: memcpy(*p.ptr, x.c_str(), stringLen); break;
	case PointerWrap::MODE_MEASURE: break;
	case PointerWrap::MODE_NOOP: break;
	case PointerWrap::MODE_VERIFY: _dbg_assert_msg_(!strcmp(x.c_str(), (char*)*p.ptr), "Savestate verification failure: \"%s\" != \"%s\" (at %p).\n", x.c_str(), (char *)*p.ptr, p.ptr); break;
	}
	(*p.ptr) += stringLen;
}

void Do(PointerWrap &p, std::wstring &x) {
	int stringLen = sizeof(wchar_t) * ((int)x.length() + 1);
	Do(p, stringLen);

	// The length is in bytes, so it has to be a whole number of characters, and at least the NUL
	// terminator. Otherwise read() below computes a negative character count.
	if (stringLen < (int)sizeof(wchar_t) || (stringLen % sizeof(wchar_t)) != 0 || stringLen > MAX_SANE_STRING_LENGTH) {
		WARN_LOG(Log::SaveState, "Savestate failure: bad stringLen %d", stringLen);
		p.SetError(PointerWrap::ERROR_FAILURE);
		return;
	}
	// Ensure the whole string is within bounds before reading.
	if (p.mode == PointerWrap::MODE_READ || p.mode == PointerWrap::MODE_VERIFY) {
		if (!p.CheckRead(stringLen))
			return;
	}

	auto read = [&]() {
		std::wstring r;
		// In case unaligned, use memcpy.
		r.resize((stringLen / sizeof(wchar_t)) - 1);
		memcpy(&r[0], *p.ptr, stringLen - sizeof(wchar_t));
		return r;
	};

	switch (p.mode) {
	case PointerWrap::MODE_READ: x = read(); break;
	case PointerWrap::MODE_WRITE: memcpy(*p.ptr, x.c_str(), stringLen); break;
	case PointerWrap::MODE_MEASURE: break;
	case PointerWrap::MODE_NOOP: break;
	case PointerWrap::MODE_VERIFY: _dbg_assert_msg_(x == read(), "Savestate verification failure: \"%ls\" != \"%ls\" (at %p).\n", x.c_str(), read().c_str(), p.ptr); break;
	}
	(*p.ptr) += stringLen;
}

void Do(PointerWrap &p, std::u16string &x) {
	int stringLen = sizeof(char16_t) * ((int)x.length() + 1);
	Do(p, stringLen);

	// The length is in bytes, so it has to be a whole number of characters, and at least the NUL
	// terminator. Otherwise read() below computes a negative character count.
	if (stringLen < (int)sizeof(char16_t) || (stringLen % sizeof(char16_t)) != 0 || stringLen > MAX_SANE_STRING_LENGTH) {
		WARN_LOG(Log::SaveState, "Savestate failure: bad stringLen %d", stringLen);
		p.SetError(PointerWrap::ERROR_FAILURE);
		return;
	}
	// Ensure the whole string is within bounds before reading.
	if (p.mode == PointerWrap::MODE_READ || p.mode == PointerWrap::MODE_VERIFY) {
		if (!p.CheckRead(stringLen))
			return;
	}

	auto read = [&]() {
		std::u16string r;
		// In case unaligned, use memcpy.
		r.resize((stringLen / sizeof(char16_t)) - 1);
		memcpy(&r[0], *p.ptr, stringLen - sizeof(char16_t));
		return r;
	};

	switch (p.mode) {
	case PointerWrap::MODE_READ: x = read(); break;
	case PointerWrap::MODE_WRITE: memcpy(*p.ptr, x.c_str(), stringLen); break;
	case PointerWrap::MODE_MEASURE: break;
	case PointerWrap::MODE_NOOP: break;
	case PointerWrap::MODE_VERIFY: _dbg_assert_msg_(x == read(), "Savestate verification failure: (at %p).\n", p.ptr); break;
	}
	(*p.ptr) += stringLen;
}

struct standard_tm {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};

void Do(PointerWrap &p, tm &t) {
	// We savestate this separately because some platforms use extra data at the end.
	// However, old files may have the native tm in them.
	// Since the first value in the struct is 0-59, we save a funny value and check for it.
	// If our funny value ('tm' 0x1337) exists, it's a new version savestate.
	int funnyValue = 0x13376D74;
	if (p.ExpectVoid(&funnyValue, sizeof(funnyValue))) {
		standard_tm stm;
		if (p.mode == PointerWrap::MODE_READ) {
			// Null out the extra members, e.g. tm_gmtoff or tm_zone.
			memset(&t, 0, sizeof(t));
		} else {
			memcpy(&stm, &t, sizeof(stm));
		}

		p.DoVoid((void *)&stm, sizeof(stm));
		memcpy(&t, &stm, sizeof(stm));
	} else {
		p.DoVoid((void *)&t, sizeof(t));
	}
}

void PointerWrap::DoMarker(const char *prevName, u32 arbitraryNumber) {
	u32 cookie = arbitraryNumber;
	Do(*this, cookie);
	if (mode == PointerWrap::MODE_READ && cookie != arbitraryNumber) {
		ERROR_LOG(Log::SaveState, "Error: After \"%s\", found %d (0x%X) instead of save marker %d (0x%X). Aborting savestate load...", prevName, cookie, cookie, arbitraryNumber, arbitraryNumber);
		SetError(ERROR_FAILURE);
	}
}

PointerWrapSection::~PointerWrapSection() {
	if (ver_ > 0) {
		p_.DoMarker(title_);
	}
}

CChunkFileReader::Error CChunkFileReader::LoadFileHeader(File::IOFile &pFile, SChunkHeader &header, std::string *title) {
	if (!pFile) {
		ERROR_LOG(Log::SaveState, "ChunkReader: Can't open file for reading");
		return ERROR_BAD_FILE;
	}

	const u64 fileSize = pFile.GetSize();
	u64 headerSize = sizeof(SChunkHeader);
	if (fileSize < headerSize) {
		ERROR_LOG(Log::SaveState, "ChunkReader: File too small");
		return ERROR_BAD_FILE;
	}

	if (!pFile.ReadArray(&header, 1)) {
		ERROR_LOG(Log::SaveState, "ChunkReader: Bad header size");
		return ERROR_BAD_FILE;
	}

	if (header.Revision < REVISION_MIN) {
		ERROR_LOG(Log::SaveState, "ChunkReader: Wrong file revision, got %d expected >= %d", header.Revision, REVISION_MIN);
		return ERROR_BAD_FILE;
	}

	if (header.Revision >= REVISION_TITLE) {
		char titleFixed[128];
		if (!pFile.ReadArray(titleFixed, sizeof(titleFixed))) {
			ERROR_LOG(Log::SaveState, "ChunkReader: Unable to read title");
			return ERROR_BAD_FILE;
		}

		if (title) {
			// Not necessarily terminated in the file.
			*title = std::string(titleFixed, strnlen(titleFixed, sizeof(titleFixed)));
		}

		headerSize += 128;
	} else if (title) {
		title->clear();
	}

	u32 sz = (u32)(fileSize - headerSize);
	if (header.ExpectedSize != sz) {
		ERROR_LOG(Log::SaveState, "ChunkReader: Bad file size, got %u expected %u", sz, header.ExpectedSize);
		return ERROR_BAD_FILE;
	}

	return ERROR_NONE;
}

// Not necessarily null-terminated.
static std::string HeaderGitVersion(const char (&gitVersion)[32]) {
	return std::string(gitVersion, strnlen(gitVersion, sizeof(gitVersion)));
}

CChunkFileReader::Error CChunkFileReader::GetFileTitle(const Path &filename, std::string *title, std::string *gitVersion) {
	if (!File::Exists(filename)) {
		ERROR_LOG(Log::SaveState, "ChunkReader: File doesn't exist");
		return ERROR_BAD_FILE;
	}

	File::IOFile pFile(filename, "rb");
	SChunkHeader header;
	Error err = LoadFileHeader(pFile, header, title);
	if (err == ERROR_NONE && gitVersion) {
		*gitVersion = HeaderGitVersion(header.GitVersion);
	}
	return err;
}

// Streams the file through the decompressor, so the compressed data is never all in memory at once.
static bool DecompressZstdFromFile(File::IOFile &file, size_t compressedSize, u8 *out, size_t outSize) {
	ZSTD_DCtx *ctx = ZSTD_createDCtx();
	if (!ctx) {
		return false;
	}
	std::vector<u8> in(ZSTD_DStreamInSize());
	ZSTD_outBuffer output{ out, outSize, 0 };
	size_t left = compressedSize;
	size_t ret = 1;
	bool success = true;
	while (left > 0 && success) {
		const size_t chunk = std::min(left, in.size());
		if (!file.ReadBytes(in.data(), chunk)) {
			ERROR_LOG(Log::SaveState, "ChunkReader: Error reading file");
			success = false;
			break;
		}
		left -= chunk;
		ZSTD_inBuffer input{ in.data(), chunk, 0 };
		while (input.pos < input.size) {
			const size_t inPos = input.pos;
			const size_t outPos = output.pos;
			ret = ZSTD_decompressStream(ctx, &output, &input);
			if (ZSTD_isError(ret) || (input.pos == inPos && output.pos == outPos)) {
				// An error, or no progress: more data than the header's uncompressed size.
				success = false;
				break;
			}
		}
	}
	ZSTD_freeDCtx(ctx);
	// ret is 0 once a frame is completely decoded.
	return success && ret == 0 && output.pos == outSize;
}

// Grows the output as it's produced, rather than allocating ZSTD_compressBound() (about the size of
// the input) up front.
static bool CompressZstd(const u8 *data, size_t size, std::vector<u8> *out) {
	ZSTD_CCtx *ctx = ZSTD_createCCtx();
	if (!ctx) {
		return false;
	}
	// TODO: If free disk space is low, we could max this out to 22?
	ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel, ZSTD_CLEVEL_DEFAULT);
	ZSTD_CCtx_setParameter(ctx, ZSTD_c_checksumFlag, 1);
	ZSTD_CCtx_setPledgedSrcSize(ctx, size);
	// States typically compress 3-4x.
	out->reserve(size / 3);
	const size_t chunk = ZSTD_CStreamOutSize();
	ZSTD_inBuffer input{ data, size, 0 };
	size_t remaining;
	do {
		const size_t pos = out->size();
		out->resize(pos + chunk);
		ZSTD_outBuffer output{ out->data() + pos, chunk, 0 };
		remaining = ZSTD_compressStream2(ctx, &output, &input, ZSTD_e_end);
		out->resize(pos + output.pos);
	} while (!ZSTD_isError(remaining) && remaining != 0);
	ZSTD_freeCCtx(ctx);
	return !ZSTD_isError(remaining);
}

CChunkFileReader::Error CChunkFileReader::LoadFile(const Path &filename, std::string *gitVersion, u8 *&_buffer, size_t &sz) {
	if (!File::Exists(filename)) {
		ERROR_LOG(Log::SaveState, "ChunkReader: File doesn't exist");
		return ERROR_FILE_MISSING;
	}

	File::IOFile pFile(filename, "rb");
	SChunkHeader header;
	Error err = LoadFileHeader(pFile, header, nullptr);
	if (err != ERROR_NONE) {
		return err;
	}

	if (header.Compress) {
		// Sanity cap on the decompressed size to avoid a giant allocation from
		// an attacker-controlled header field. Real savestates are well under this.
		if (header.UncompressedSize > 0x40000000) {
			ERROR_LOG(Log::SaveState, "ChunkReader: UncompressedSize too large: %u", header.UncompressedSize);
			return ERROR_BAD_FILE;
		}
	}

	if (SerializeCompressType(header.Compress) == SerializeCompressType::ZSTD) {
		u8 *uncomp_buffer = new u8[header.UncompressedSize];
		if (!DecompressZstdFromFile(pFile, header.ExpectedSize, uncomp_buffer, header.UncompressedSize)) {
			ERROR_LOG(Log::SaveState, "ChunkReader: Failed to decompress file");
			delete [] uncomp_buffer;
			return ERROR_BAD_FILE;
		}
		_buffer = uncomp_buffer;
		sz = header.UncompressedSize;
		*gitVersion = HeaderGitVersion(header.GitVersion);
		return ERROR_NONE;
	}

	// Uncompressed, or snappy (older states).
	sz = header.ExpectedSize;
	u8 *buffer = new u8[sz];
	if (!pFile.ReadBytes(buffer, sz))
	{
		ERROR_LOG(Log::SaveState, "ChunkReader: Error reading file");
		delete [] buffer;
		return ERROR_BAD_FILE;
	}

	if (header.Compress) {
		u8 *uncomp_buffer = new u8[header.UncompressedSize];
		size_t uncomp_size = header.UncompressedSize;
		bool success = false;
		if (SerializeCompressType(header.Compress) == SerializeCompressType::SNAPPY) {
			auto status = snappy_uncompress((const char *)buffer, sz, (char *)uncomp_buffer, &uncomp_size);
			success = status == SNAPPY_OK;
		} else {
			ERROR_LOG(Log::SaveState, "ChunkReader: Unexpected compression type %d", header.Compress);
		}
		if (!success) {
			ERROR_LOG(Log::SaveState, "ChunkReader: Failed to decompress file");
			delete [] uncomp_buffer;
			delete [] buffer;
			return ERROR_BAD_FILE;
		}
		if ((u32)uncomp_size != header.UncompressedSize) {
			ERROR_LOG(Log::SaveState, "Size mismatch: file: %u  calc: %u", header.UncompressedSize, (u32)uncomp_size);
			delete [] uncomp_buffer;
			delete [] buffer;
			return ERROR_BAD_FILE;
		}
		_buffer = uncomp_buffer;
		sz = uncomp_size;
		delete [] buffer;
	} else {
		_buffer = buffer;
	}

	*gitVersion = HeaderGitVersion(header.GitVersion);

	return ERROR_NONE;
}

// Takes ownership of buffer.
CChunkFileReader::Error CChunkFileReader::SaveFile(const Path &filename, const std::string &title, const char *gitVersion, u8 *buffer, size_t sz) {
	INFO_LOG(Log::SaveState, "ChunkReader: Writing %s", filename.c_str());

	// Compress before opening the file, which truncates it.
	std::vector<u8> compressed;
	SerializeCompressType usedType = SerializeCompressType::ZSTD;
	const u8 *writeData = buffer;
	size_t writeLen = sz;
	if (CompressZstd(buffer, sz, &compressed)) {
		// The uncompressed state isn't needed any more, so don't hold both while writing.
		free(buffer);
		buffer = nullptr;
		writeData = compressed.data();
		writeLen = compressed.size();
	} else {
		ERROR_LOG(Log::SaveState, "ChunkReader: Compression failed");
		// We can still save uncompressed.
		std::vector<u8>().swap(compressed);
		usedType = SerializeCompressType::NONE;
	}

	File::IOFile pFile(filename, "wb");
	if (!pFile) {
		ERROR_LOG(Log::SaveState, "ChunkReader: Error opening file for write");
		free(buffer);
		return ERROR_BAD_FILE;
	}

	// Create header
	SChunkHeader header{};
	header.Compress = (int)usedType;
	header.Revision = REVISION_CURRENT;
	header.ExpectedSize = (u32)writeLen;
	header.UncompressedSize = (u32)sz;
	truncate_cpy(header.GitVersion, gitVersion);

	// Setup the fixed-length title.
	char titleFixed[128]{};
	truncate_cpy(titleFixed, title.c_str());

	// Now let's start writing out the file...
	bool success = true;
	if (!pFile.WriteArray(&header, 1)) {
		ERROR_LOG(Log::SaveState, "ChunkReader: Failed writing header");
		success = false;
	} else if (!pFile.WriteArray(titleFixed, sizeof(titleFixed))) {
		ERROR_LOG(Log::SaveState, "ChunkReader: Failed writing title");
		success = false;
	} else if (!pFile.WriteBytes(writeData, writeLen)) {
		ERROR_LOG(Log::SaveState, "ChunkReader: Failed writing compressed data");
		success = false;
	} else if (sz != writeLen) {
		INFO_LOG(Log::SaveState, "Savestate: Compressed %i bytes into %i", (int)sz, (int)writeLen);
	}
	free(buffer);
	// Buffered writes can still fail here (a full disk.)
	if (!pFile.Close() && success) {
		ERROR_LOG(Log::SaveState, "ChunkReader: Failed closing file");
		success = false;
	}
	if (!success) {
		return ERROR_BAD_FILE;
	}

	INFO_LOG(Log::SaveState, "ChunkReader: Done writing %s", filename.c_str());
	return ERROR_NONE;
}
