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

#pragma once

#include "ppsspp_config.h"

// Extremely simple serialization framework.
// Currently mis-named, a native ChunkFile is something different (a RIFF file)

// (mis)-features:
// + Super fast
// + Very simple
// + Same code is used for serialization and deserializaition (in most cases)
// + Sections can be versioned for backwards/forwards compatibility
// - Serialization code for anything complex has to be manually written.

#include <string>
#include <vector>
#include <cstdlib>

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/File/Path.h"

namespace File {
class IOFile;
};

template <class T>
struct LinkedListItem : public T
{
	LinkedListItem<T> *next;
};

class PointerWrap;

class PointerWrapSection
{
public:
	PointerWrapSection(PointerWrap &p, int ver, const char *title) : p_(p), ver_(ver), title_(title) {
	}
	~PointerWrapSection();
	
	bool operator == (const int &v) const { return ver_ == v; }
	bool operator != (const int &v) const { return ver_ != v; }
	bool operator <= (const int &v) const { return ver_ <= v; }
	bool operator >= (const int &v) const { return ver_ >= v; }
	bool operator <  (const int &v) const { return ver_ < v; }
	bool operator >  (const int &v) const { return ver_ > v; }

	operator bool() const  {
		return ver_ > 0;
	}

private:
	PointerWrap &p_;
	int ver_;
	const char *title_;
};

// Wrapper class
class PointerWrap
{
public:
	enum Mode {
		MODE_READ = 1, // load
		MODE_WRITE, // save
		MODE_MEASURE, // calculate size
		MODE_VERIFY, // compare
	};

	enum Error {
		ERROR_NONE = 0,
		ERROR_WARNING = 1,
		ERROR_FAILURE = 2,
	};

	u8 **ptr;
	Mode mode;
	Error error = ERROR_NONE;

	PointerWrap(u8 **ptr_, Mode mode_) : ptr(ptr_), mode(mode_) {}
	PointerWrap(unsigned char **ptr_, int mode_) : ptr((u8**)ptr_), mode((Mode)mode_) {}

	PointerWrapSection Section(const char *title, int ver);

	// The returned object can be compared against the version that was loaded.
	// This can be used to support versions as old as minVer.
	// Version = 0 means the section was not found.
	PointerWrapSection Section(const char *title, int minVer, int ver);

	void SetMode(Mode mode_) {mode = mode_;}
	Mode GetMode() const {return mode;}
	u8 **GetPPtr() {return ptr;}
	void SetError(Error error_);

	const char *GetBadSectionTitle() const {
		return firstBadSectionTitle_;
	}

	// Same as DoVoid, except doesn't advance pointer if it doesn't match on read.
	bool ExpectVoid(void *data, int size);
	void DoVoid(void *data, int size);

	void DoMarker(const char *prevName, u32 arbitraryNumber = 0x42);

private:
	const char *firstBadSectionTitle_ = nullptr;
};

class CChunkFileReader
{
public:
	enum Error {
		ERROR_NONE,
		ERROR_BAD_FILE,
		ERROR_BROKEN_STATE,
		ERROR_BAD_ALLOC,
	};

	// May fail badly if ptr doesn't point to valid data.
	template<class T>
	static Error LoadPtr(u8 *ptr, T &_class, std::string *errorString)
	{
		PointerWrap p(&ptr, PointerWrap::MODE_READ);
		_class.DoState(p);

		if (p.error != p.ERROR_FAILURE) {
			return ERROR_NONE;
		} else {
			std::string badSectionTitle = p.GetBadSectionTitle() ? p.GetBadSectionTitle() : "(unknown bad section)";
			*errorString = std::string("Failure at ") + badSectionTitle;
			return ERROR_BROKEN_STATE;
		}
	}

	template<class T>
	static size_t MeasurePtr(T &_class)
	{
		u8 *ptr = 0;
		PointerWrap p(&ptr, PointerWrap::MODE_MEASURE);
		_class.DoState(p);
		return (size_t)ptr;
	}

	// Expects ptr to have at least MeasurePtr bytes at ptr.
	template<class T>
	static Error SavePtr(u8 *ptr, T &_class, size_t expected_size)
	{
		const u8 *expected_end = ptr + expected_size;
		PointerWrap p(&ptr, PointerWrap::MODE_WRITE);
		_class.DoState(p);

		if (p.error != p.ERROR_FAILURE && (expected_end == ptr || expected_size == 0)) {
			return ERROR_NONE;
		} else {
			return ERROR_BROKEN_STATE;
		}
	}

	// Load file template
	template<class T>
	static Error Load(const Path &filename, std::string *gitVersion, T& _class, std::string *failureReason)
	{
		*failureReason = "LoadStateWrongVersion";

		u8 *ptr = nullptr;
		size_t sz;
		Error error = LoadFile(filename, gitVersion, ptr, sz, failureReason);
		if (error == ERROR_NONE) {
			failureReason->clear();
			error = LoadPtr(ptr, _class, failureReason);
			delete [] ptr;
			INFO_LOG(SAVESTATE, "ChunkReader: Done loading '%s'", filename.c_str());
		} else {
			WARN_LOG(SAVESTATE, "ChunkReader: Error found during load of '%s'", filename.c_str());
		}
		return error;
	}

	// Save file template
	template<class T>
	static Error Save(const Path &filename, const std::string &title, const char *gitVersion, T& _class)
	{
		// Get data
		size_t const sz = MeasurePtr(_class);
		u8 *buffer = (u8 *)malloc(sz);
		if (!buffer)
			return ERROR_BAD_ALLOC;
		Error error = SavePtr(buffer, _class, sz);

		// SaveFile takes ownership of buffer
		if (error == ERROR_NONE)
			error = SaveFile(filename, title, gitVersion, buffer, sz);
		return error;
	}
	
	template <class T>
	static Error Verify(T& _class)
	{
		u8 *ptr = 0;

		// Step 1: Measure the space required.
		PointerWrap p(&ptr, PointerWrap::MODE_MEASURE);
		_class.DoState(p);
		size_t const sz = (size_t)ptr;
		std::vector<u8> buffer(sz);

		// Step 2: Dump the state.
		ptr = &buffer[0];
		p.SetMode(PointerWrap::MODE_WRITE);
		_class.DoState(p);

		// Step 3: Verify the state.
		ptr = &buffer[0];
		p.SetMode(PointerWrap::MODE_VERIFY);
		_class.DoState(p);

		return ERROR_NONE;
	}

	static Error GetFileTitle(const Path &filename, std::string *title);

private:
	struct SChunkHeader
	{
		int Revision;
		int Compress;
		u32 ExpectedSize;
		u32 UncompressedSize;
		char GitVersion[32];
	};

	enum {
		REVISION_MIN = 4,
		REVISION_TITLE = 5,
		REVISION_CURRENT = REVISION_TITLE,
	};

	static Error LoadFile(const Path &filename, std::string *gitVersion, u8 *&buffer, size_t &sz, std::string *failureReason);
	static Error SaveFile(const Path &filename, const std::string &title, const char *gitVersion, u8 *buffer, size_t sz);
	static Error LoadFileHeader(File::IOFile &pFile, SChunkHeader &header, std::string *title);
};
