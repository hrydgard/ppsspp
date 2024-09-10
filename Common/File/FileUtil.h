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

#include <fstream>
#include <cstdio>
#include <string>
#include <time.h>
#include <cstdint>

#include "Common/File/Path.h"

// Some functions here support Android content URIs. These are marked as such.

#ifdef _MSC_VER
inline struct tm* localtime_r(const time_t *clock, struct tm *result) {
	if (localtime_s(result, clock) == 0)
		return result;
	return NULL;
}
#endif

namespace File {

// Mostly to handle UTF-8 filenames better on Windows.
FILE *OpenCFile(const Path &filename, const char *mode);

// Reminiscent of PSP's FileAccess enum, due to its use in emulating it.
enum OpenFlag {
	OPEN_NONE = 0,
	OPEN_READ = 1,
	OPEN_WRITE = 2,
	OPEN_APPEND = 4,
	OPEN_CREATE = 8,
	OPEN_TRUNCATE = 16,
	// EXCL?
};

// TODO: This currently only handles Android Content URIs, but might port the rest
// of DirectoryFileSystem::Open here eventually for symmetry.
int OpenFD(const Path &filename, OpenFlag flags);

// Resolves symlinks and similar.
std::string ResolvePath(const std::string &path);

// Returns true if file filename exists
bool Exists(const Path &path);

// Returns true if file filename exists in directory path.
bool ExistsInDir(const Path &path, const std::string &filename);

// Returns true if filename exists, and is a directory
// Supports Android content URIs.
bool IsDirectory(const Path &filename);

// Returns struct with modification date of file
bool GetModifTime(const Path &filename, tm &return_time);

// Returns the size of filename (64bit)
uint64_t GetFileSize(const Path &filename);

// Overloaded GetSize, accepts FILE*
uint64_t GetFileSize(FILE *f);

// Computes the recursive size of a directory. Warning: Might be slow!
uint64_t ComputeRecursiveDirectorySize(const Path &path);

// Returns true if successful, or path already exists.
bool CreateDir(const Path &filename);

void ChangeMTime(const Path &path, time_t mtime);

// Creates the full path of fullPath returns true on success
bool CreateFullPath(const Path &fullPath);

// Deletes a given file by name, return true on success
// Doesn't support deleting a directory (although it will work on some platforms - ideally shouldn't)
bool Delete(const Path &filename);

// Deletes a directory by name, returns true on success
// Directory must be empty.
bool DeleteDir(const Path &filename);

// Deletes the given directory and anything under it. Returns true on success.
bool DeleteDirRecursively(const Path &directory);

// Renames/moves file srcFilename to destFilename, returns true on success 
// Will usually only work with in the same partition or other unit of storage,
// so you might have to fall back to copy/delete.
bool Rename(const Path &srcFilename, const Path &destFilename);

// copies file srcFilename to destFilename, returns true on success 
bool Copy(const Path &srcFilename, const Path &destFilename);

// Tries to rename srcFilename to destFilename, if that fails,
// it tries to copy and delete the src if succeeded. If that fails too,
// returns false, otherwise returns true.
bool Move(const Path &srcFilename, const Path &destFilename);

// Move file, but only if it can be done quickly (rename or similar).
bool MoveIfFast(const Path &srcFilename, const Path &destFilename);

// creates an empty file filename, returns true on success 
bool CreateEmptyFile(const Path &filename);

// Opens ini file (cheats, texture replacements etc.)
// TODO: Belongs in System or something.
bool OpenFileInEditor(const Path &fileName);

// Uses some heuristics to determine if this is a folder that we would want to
// write to.
bool IsProbablyInDownloadsFolder(const Path &folder);

// TODO: Belongs in System or something.
const Path &GetExeDirectory();

const Path GetCurDirectory();

// simple wrapper for cstdlib file functions to
// hopefully will make error checking easier
// and make forgetting an fclose() harder
class IOFile {
public:
	IOFile() {}
	IOFile(const Path &filename, const char openmode[]);
	~IOFile();

	// Prevent copies.
	IOFile(const IOFile &) = delete;
	void operator=(const IOFile &) = delete;

	bool Open(const Path &filename, const char openmode[]);
	bool Close();

	template <typename T>
	bool ReadArray(T* data, size_t length)
	{
		if (!IsOpen() || length != std::fread(data, sizeof(T), length, m_file))
			m_good = false;

		return m_good;
	}

	template <typename T>
	bool WriteArray(const T* data, size_t length)
	{
		if (!IsOpen() || length != std::fwrite(data, sizeof(T), length, m_file))
			m_good = false;

		return m_good;
	}

	bool ReadBytes(void* data, size_t length)
	{
		return ReadArray(reinterpret_cast<char*>(data), length);
	}

	bool WriteBytes(const void* data, size_t length)
	{
		return WriteArray(reinterpret_cast<const char*>(data), length);
	}

	bool IsOpen() const { return nullptr != m_file; }

	// m_good is set to false when a read, write or other function fails
	bool IsGood() const { return m_good; }
	operator bool() const { return IsGood() && IsOpen(); }

	std::FILE* ReleaseHandle();

	std::FILE* GetHandle() { return m_file; }

	void SetHandle(std::FILE* file);

	bool Seek(int64_t off, int origin);
	uint64_t Tell();
	uint64_t GetSize();
	bool Resize(uint64_t size);
	bool Flush();

	// clear error state
	void Clear() {
		m_good = true;
#undef clearerr
		std::clearerr(m_file);
	}

private:
	std::FILE *m_file = nullptr;
	bool m_good = true;
};

// TODO: Refactor, this was moved from the old file_util.cpp.

// Whole-file reading/writing
bool WriteStringToFile(bool textFile, const std::string &str, const Path &filename);
bool WriteDataToFile(bool textFile, const void* data, size_t size, const Path &filename);

bool ReadFileToStringOptions(bool textFile, bool allowShort, const Path &path, std::string *str);

// Wrappers that clarify the intentions.
inline bool ReadBinaryFileToString(const Path &path, std::string *str) {
	return ReadFileToStringOptions(false, false, path, str);
}
inline bool ReadSysTextFileToString(const Path &path, std::string *str) {
	return ReadFileToStringOptions(true, true, path, str);
}
inline bool ReadTextFileToString(const Path &path, std::string *str) {
	return ReadFileToStringOptions(true, false, path, str);
}

// Return value must be delete[]-d.
uint8_t *ReadLocalFile(const Path &path, size_t *size);

}  // namespace
