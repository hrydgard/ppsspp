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

static constexpr SerializeCompressType SAVE_TYPE = SerializeCompressType::ZSTD;

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
		firstBadSectionTitle_ = curTitle_;
	}
}

bool PointerWrap::ExpectVoid(void *data, int size) {
	switch (mode) {
	case MODE_READ:	if (memcmp(data, *ptr, size) != 0) return false; break;
	case MODE_WRITE: memcpy(*ptr, data, size); break;
	case MODE_MEASURE: break;  // MODE_MEASURE - don't need to do anything
	case MODE_VERIFY:
		for (int i = 0; i < size; i++)
			_dbg_assert_msg_(((u8*)data)[i] == (*ptr)[i], "Savestate verification failure: %d (0x%X) (at %p) != %d (0x%X) (at %p).\n", ((u8*)data)[i], ((u8*)data)[i], &((u8*)data)[i], (*ptr)[i], (*ptr)[i], &(*ptr)[i]);
		break;
	default: break;  // throw an error?
	}
	(*ptr) += size;
	return true;
}

void PointerWrap::DoVoid(void *data, int size) {
	switch (mode) {
	case MODE_READ:	memcpy(data, *ptr, size); break;
	case MODE_WRITE: memcpy(*ptr, data, size); break;
	case MODE_MEASURE: break;  // MODE_MEASURE - don't need to do anything
	case MODE_VERIFY:
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

	if (stringLen < 0 || stringLen > MAX_SANE_STRING_LENGTH) {
		WARN_LOG(Log::SaveState, "Savestate failure: bad stringLen %d", stringLen);
		p.SetError(PointerWrap::ERROR_FAILURE);
		return;
	}

	switch (p.mode) {
	case PointerWrap::MODE_READ: x = (char*)*p.ptr; break;
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

	if (stringLen < 0 || stringLen > MAX_SANE_STRING_LENGTH) {
		WARN_LOG(Log::SaveState, "Savestate failure: bad stringLen %d", stringLen);
		p.SetError(PointerWrap::ERROR_FAILURE);
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

	if (stringLen < 0 || stringLen > MAX_SANE_STRING_LENGTH) {
		WARN_LOG(Log::SaveState, "Savestate failure: bad stringLen %d", stringLen);
		p.SetError(PointerWrap::ERROR_FAILURE);
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
			*title = titleFixed;
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

CChunkFileReader::Error CChunkFileReader::GetFileTitle(const Path &filename, std::string *title) {
	if (!File::Exists(filename)) {
		ERROR_LOG(Log::SaveState, "ChunkReader: File doesn't exist");
		return ERROR_BAD_FILE;
	}

	File::IOFile pFile(filename, "rb");
	SChunkHeader header;
	return LoadFileHeader(pFile, header, title);
}

CChunkFileReader::Error CChunkFileReader::LoadFile(const Path &filename, std::string *gitVersion, u8 *&_buffer, size_t &sz, std::string *failureReason) {
	if (!File::Exists(filename)) {
		*failureReason = "LoadStateDoesntExist";
		ERROR_LOG(Log::SaveState, "ChunkReader: File doesn't exist");
		return ERROR_BAD_FILE;
	}

	File::IOFile pFile(filename, "rb");
	SChunkHeader header;
	Error err = LoadFileHeader(pFile, header, nullptr);
	if (err != ERROR_NONE) {
		return err;
	}

	// read the state
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
		} else if (SerializeCompressType(header.Compress) == SerializeCompressType::ZSTD) {
			size_t status = ZSTD_decompress((char *)uncomp_buffer, uncomp_size, (const char *)buffer, sz);
			success = !ZSTD_isError(status);
			if (success) {
				uncomp_size = status;
			}
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

	if (header.GitVersion[31]) {
		*gitVersion = std::string(header.GitVersion, 32);
	} else {
		*gitVersion = header.GitVersion;
	}

	return ERROR_NONE;
}

// Takes ownership of buffer.
CChunkFileReader::Error CChunkFileReader::SaveFile(const Path &filename, const std::string &title, const char *gitVersion, u8 *buffer, size_t sz) {
	INFO_LOG(Log::SaveState, "ChunkReader: Writing %s", filename.c_str());

	File::IOFile pFile(filename, "wb");
	if (!pFile) {
		ERROR_LOG(Log::SaveState, "ChunkReader: Error opening file for write");
		free(buffer);
		return ERROR_BAD_FILE;
	}

	// Make sure we can allocate a buffer to compress before compressing.
	size_t write_len;
	SerializeCompressType usedType = SAVE_TYPE;
	switch (usedType) {
	case SerializeCompressType::NONE:
		write_len = 0;
		break;
	case SerializeCompressType::SNAPPY:
		write_len = snappy_max_compressed_length(sz);
		break;
	case SerializeCompressType::ZSTD:
		write_len = ZSTD_compressBound(sz);
		break;
	}
	u8 *compressed_buffer = write_len == 0 ? nullptr : (u8 *)malloc(write_len);
	u8 *write_buffer = buffer;
	if (!compressed_buffer) {
		if (write_len != 0)
			ERROR_LOG(Log::SaveState, "ChunkReader: Unable to allocate compressed buffer");
		// We'll save uncompressed.  Better than not saving...
		write_len = sz;
		usedType = SerializeCompressType::NONE;
	} else {
		bool success = true;
		switch (usedType) {
		case SerializeCompressType::NONE:
			_assert_(false);
			break;
		case SerializeCompressType::SNAPPY:
			success = snappy_compress((const char *)buffer, sz, (char *)compressed_buffer, &write_len) == SNAPPY_OK;
			break;
		case SerializeCompressType::ZSTD:
			{
				auto ctx = ZSTD_createCCtx();
				if (!ctx) {
					success = false;
				} else {
					// TODO: If free disk space is low, we could max this out to 22?
					ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel, ZSTD_CLEVEL_DEFAULT);
					ZSTD_CCtx_setParameter(ctx, ZSTD_c_checksumFlag, 1);
					ZSTD_CCtx_setPledgedSrcSize(ctx, sz);
					write_len = ZSTD_compress2(ctx, compressed_buffer, write_len, buffer, sz);
					success = !ZSTD_isError(write_len);
				}
				ZSTD_freeCCtx(ctx);
			}
			break;
		}

		if (success) {
			free(buffer);
			write_buffer = compressed_buffer;
		} else {
			ERROR_LOG(Log::SaveState, "ChunkReader: Compression failed");
			free(compressed_buffer);

			// We can still save uncompressed.
			write_len = sz;
			usedType = SerializeCompressType::NONE;
		}
	}

	// Create header
	SChunkHeader header{};
	header.Compress = (int)usedType;
	header.Revision = REVISION_CURRENT;
	header.ExpectedSize = (u32)write_len;
	header.UncompressedSize = (u32)sz;
	truncate_cpy(header.GitVersion, gitVersion);

	// Setup the fixed-length title.
	char titleFixed[128]{};
	truncate_cpy(titleFixed, title.c_str());

	// Now let's start writing out the file...
	if (!pFile.WriteArray(&header, 1)) {
		ERROR_LOG(Log::SaveState, "ChunkReader: Failed writing header");
		free(write_buffer);
		return ERROR_BAD_FILE;
	}
	if (!pFile.WriteArray(titleFixed, sizeof(titleFixed))) {
		ERROR_LOG(Log::SaveState, "ChunkReader: Failed writing title");
		free(write_buffer);
		return ERROR_BAD_FILE;
	}

	if (!pFile.WriteBytes(write_buffer, write_len)) {
		ERROR_LOG(Log::SaveState, "ChunkReader: Failed writing compressed data");
		free(write_buffer);
		return ERROR_BAD_FILE;
	} else if (sz != write_len) {
		INFO_LOG(Log::SaveState, "Savestate: Compressed %i bytes into %i", (int)sz, (int)write_len);
	}
	free(write_buffer);

	INFO_LOG(Log::SaveState, "ChunkReader: Done writing %s", filename.c_str());
	return ERROR_NONE;
}
