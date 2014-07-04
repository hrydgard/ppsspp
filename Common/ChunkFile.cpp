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

#include <cstring>

#include "ChunkFile.h"

PointerWrapSection PointerWrap::Section(const char *title, int ver) {
	return Section(title, ver, ver);
}

PointerWrapSection PointerWrap::Section(const char *title, int minVer, int ver) {
	char marker[16] = {0};
	int foundVersion = ver;

	strncpy(marker, title, sizeof(marker));
	if (!ExpectVoid(marker, sizeof(marker)))
	{
		// Might be before we added name markers for safety.
		if (foundVersion == 1 && ExpectVoid(&foundVersion, sizeof(foundVersion)))
			DoMarker(title);
		// Wasn't found, but maybe we can still load the state.
		else
			foundVersion = 0;
	}
	else
		Do(foundVersion);

	if (error == ERROR_FAILURE || foundVersion < minVer || foundVersion > ver) {
		WARN_LOG(COMMON, "Savestate failure: wrong version %d found for %s", foundVersion, title);
		SetError(ERROR_FAILURE);
		return PointerWrapSection(*this, -1, title);
	}
	return PointerWrapSection(*this, foundVersion, title);
}

void PointerWrap::SetError(Error error_) {
	if (error < error_) {
		error = error_;
	}
	if (error > ERROR_WARNING) {
		mode = PointerWrap::MODE_MEASURE;
	}
}

bool PointerWrap::ExpectVoid(void *data, int size) {
	switch (mode) {
	case MODE_READ:	if (memcmp(data, *ptr, size) != 0) return false; break;
	case MODE_WRITE: memcpy(*ptr, data, size); break;
	case MODE_MEASURE: break;  // MODE_MEASURE - don't need to do anything
	case MODE_VERIFY:
		for (int i = 0; i < size; i++)
			_dbg_assert_msg_(COMMON, ((u8*)data)[i] == (*ptr)[i], "Savestate verification failure: %d (0x%X) (at %p) != %d (0x%X) (at %p).\n", ((u8*)data)[i], ((u8*)data)[i], &((u8*)data)[i], (*ptr)[i], (*ptr)[i], &(*ptr)[i]);
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
			_dbg_assert_msg_(COMMON, ((u8*)data)[i] == (*ptr)[i], "Savestate verification failure: %d (0x%X) (at %p) != %d (0x%X) (at %p).\n", ((u8*)data)[i], ((u8*)data)[i], &((u8*)data)[i], (*ptr)[i], (*ptr)[i], &(*ptr)[i]);
		break;
	default: break;  // throw an error?
	}
	(*ptr) += size;
}

void PointerWrap::Do(std::string &x) {
	int stringLen = (int)x.length() + 1;
	Do(stringLen);

	switch (mode) {
	case MODE_READ:		x = (char*)*ptr; break;
	case MODE_WRITE:	memcpy(*ptr, x.c_str(), stringLen); break;
	case MODE_MEASURE: break;
	case MODE_VERIFY: _dbg_assert_msg_(COMMON, !strcmp(x.c_str(), (char*)*ptr), "Savestate verification failure: \"%s\" != \"%s\" (at %p).\n", x.c_str(), (char*)*ptr, ptr); break;
	}
	(*ptr) += stringLen;
}

void PointerWrap::Do(std::wstring &x) {
	int stringLen = sizeof(wchar_t)*((int)x.length() + 1);
	Do(stringLen);

	switch (mode) {
	case MODE_READ:		x = (wchar_t*)*ptr; break;
	case MODE_WRITE:	memcpy(*ptr, x.c_str(), stringLen); break;
	case MODE_MEASURE: break;
	case MODE_VERIFY: _dbg_assert_msg_(COMMON, x == (wchar_t*)*ptr, "Savestate verification failure: \"%ls\" != \"%ls\" (at %p).\n", x.c_str(), (wchar_t*)*ptr, ptr); break;
	}
	(*ptr) += stringLen;
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

void PointerWrap::Do(tm &t) {
	// We savestate this separately because some platforms use extra data at the end.
	// However, old files may have the native tm in them.
	// Since the first value in the struct is 0-59, we save a funny value and check for it.
	// If our funny value ('tm' 0x1337) exists, it's a new version savestate.
	int funnyValue = 0x13376D74;
	if (ExpectVoid(&funnyValue, sizeof(funnyValue))) {
		standard_tm stm;
		if (mode == MODE_READ) {
			// Null out the extra members, e.g. tm_gmtoff or tm_zone.
			memset(&t, 0, sizeof(t));
		} else {
			memcpy(&stm, &t, sizeof(stm));
		}

		DoVoid((void *)&stm, sizeof(stm));
		memcpy(&t, &stm, sizeof(stm));
	} else {
		DoVoid((void *)&t, sizeof(t));
	}
}

void PointerWrap::DoMarker(const char *prevName, u32 arbitraryNumber) {
	u32 cookie = arbitraryNumber;
	Do(cookie);
	if (mode == PointerWrap::MODE_READ && cookie != arbitraryNumber) {
		PanicAlertT("Error: After \"%s\", found %d (0x%X) instead of save marker %d (0x%X). Aborting savestate load...", prevName, cookie, cookie, arbitraryNumber, arbitraryNumber);
		SetError(ERROR_FAILURE);
	}
}

PointerWrapSection::~PointerWrapSection() {
	if (ver_ > 0) {
		p_.DoMarker(title_);
	}
}

CChunkFileReader::Error CChunkFileReader::LoadFile(const std::string& _rFilename, int _Revision, const char *_VersionString, u8 *&_buffer, size_t &sz, std::string *_failureReason) {
	if (!File::Exists(_rFilename)) {
		*_failureReason = "LoadStateDoesntExist";
		ERROR_LOG(COMMON, "ChunkReader: File doesn't exist");
		return ERROR_BAD_FILE;
	}

	// Check file size
	const u64 fileSize = File::GetSize(_rFilename);
	static const u64 headerSize = sizeof(SChunkHeader);
	if (fileSize < headerSize)
	{
		ERROR_LOG(COMMON, "ChunkReader: File too small");
		return ERROR_BAD_FILE;
	}

	File::IOFile pFile(_rFilename, "rb");
	if (!pFile)
	{
		ERROR_LOG(COMMON, "ChunkReader: Can't open file for reading");
		return ERROR_BAD_FILE;
	}

	// read the header
	SChunkHeader header;
	if (!pFile.ReadArray(&header, 1))
	{
		ERROR_LOG(COMMON, "ChunkReader: Bad header size");
		return ERROR_BAD_FILE;
	}

	// Check revision
	if (header.Revision != _Revision)
	{
		ERROR_LOG(COMMON, "ChunkReader: Wrong file revision, got %d expected %d", header.Revision, _Revision);
		return ERROR_BAD_FILE;
	}

	// get size
	sz = (int)(fileSize - headerSize);
	if (header.ExpectedSize != sz)
	{
		ERROR_LOG(COMMON, "ChunkReader: Bad file size, got %u expected %u", (u32)sz, header.ExpectedSize);
		return ERROR_BAD_FILE;
	}

	// read the state
	u8 *buffer = new u8[sz];
	if (!pFile.ReadBytes(buffer, sz))
	{
		ERROR_LOG(COMMON, "ChunkReader: Error reading file");
		return ERROR_BAD_FILE;
	}

	_buffer = buffer;
	if (header.Compress) {
		u8 *uncomp_buffer = new u8[header.UncompressedSize];
		size_t uncomp_size = header.UncompressedSize;
		snappy_uncompress((const char *)buffer, sz, (char *)uncomp_buffer, &uncomp_size);
		if ((u32)uncomp_size != header.UncompressedSize) {
			ERROR_LOG(COMMON, "Size mismatch: file: %u  calc: %u", header.UncompressedSize, (u32)uncomp_size);
			return ERROR_BAD_FILE;
		}
		_buffer = uncomp_buffer;
		sz = uncomp_size;
		delete [] buffer;
	}

	return ERROR_NONE;
}

CChunkFileReader::Error CChunkFileReader::SaveFile(const std::string& _rFilename, int _Revision, const char *_VersionString, u8 *buffer, size_t sz) {
	INFO_LOG(COMMON, "ChunkReader: Writing %s" , _rFilename.c_str());

	File::IOFile pFile(_rFilename, "wb");
	if (!pFile)
	{
		ERROR_LOG(COMMON, "ChunkReader: Error opening file for write");
		return ERROR_BAD_FILE;
	}

	bool compress = true;

	// Create header
	SChunkHeader header;
	header.Compress = compress ? 1 : 0;
	header.Revision = _Revision;
	header.ExpectedSize = (u32)sz;
	header.UncompressedSize = (u32)sz;
	strncpy(header.GitVersion, _VersionString, 32);
	header.GitVersion[31] = '\0';

	// Write to file
	if (compress) {
		size_t comp_len = snappy_max_compressed_length(sz);
		u8 *compressed_buffer = new u8[comp_len];
		snappy_compress((const char *)buffer, sz, (char *)compressed_buffer, &comp_len);
		delete [] buffer;
		header.ExpectedSize = (u32)comp_len;
		if (!pFile.WriteArray(&header, 1))
		{
			ERROR_LOG(COMMON, "ChunkReader: Failed writing header");
			return ERROR_BAD_FILE;
		}
		if (!pFile.WriteBytes(&compressed_buffer[0], comp_len)) {
			ERROR_LOG(COMMON, "ChunkReader: Failed writing compressed data");
			return ERROR_BAD_FILE;
		}	else {
			INFO_LOG(COMMON, "Savestate: Compressed %i bytes into %i", (int)sz, (int)comp_len);
		}
		delete [] compressed_buffer;
	} else {
		if (!pFile.WriteArray(&header, 1))
		{
			ERROR_LOG(COMMON, "ChunkReader: Failed writing header");
			return ERROR_BAD_FILE;
		}
		if (!pFile.WriteBytes(&buffer[0], sz))
		{
			ERROR_LOG(COMMON, "ChunkReader: Failed writing data");
			return ERROR_BAD_FILE;
		}
		delete [] buffer;
	}

	INFO_LOG(COMMON, "ChunkReader: Done writing %s",  _rFilename.c_str());
	return ERROR_NONE;
}
