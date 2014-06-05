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

#include "FileUtil.h"
#include "StringUtils.h"

#ifdef _WIN32
#include "CommonWindows.h"
#ifndef _XBOX
#include <shlobj.h>		// for SHGetFolderPath
#include <shellapi.h>
#include <commdlg.h>	// for GetSaveFileName
#endif
#include <io.h>
#include <direct.h>		// getcwd
#else
#include <sys/param.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CFString.h>
#include <CoreFoundation/CFURL.h>
#include <CoreFoundation/CFBundle.h>
#if !defined(IOS)
#include <mach-o/dyld.h>
#endif  // !defined(IOS)
#endif  // __APPLE__

#include "util/text/utf8.h"

#include <sys/stat.h>

#ifndef S_ISDIR
#define S_ISDIR(m)  (((m)&S_IFMT) == S_IFDIR)
#endif

#if !defined(__linux__) && !defined(_WIN32) && !defined(__QNX__)
#define stat64 stat
#define fstat64 fstat
#endif

#define DIR_SEP "/"
#ifdef _WIN32
#define DIR_SEP_CHRS "/\\"
#else
#define DIR_SEP_CHRS "/"
#endif

// Hack
#if defined(__SYMBIAN32__)
static inline int readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result) {
	struct dirent *readdir_entry;

	readdir_entry = readdir(dirp);
	if (readdir_entry == NULL) {
		*result = NULL;
		return errno;
	}

	*entry = *readdir_entry;
	*result = entry;
	return 0;
}
#endif


// This namespace has various generic functions related to files and paths.
// The code still needs a ton of cleanup.
// REMEMBER: strdup considered harmful!
namespace File
{

FILE *OpenCFile(const std::string &filename, const char *mode)
{
#if defined(_WIN32) && defined(UNICODE)
	return _wfopen(ConvertUTF8ToWString(filename).c_str(), ConvertUTF8ToWString(mode).c_str());
#else
	return fopen(filename.c_str(), mode);
#endif
}

bool OpenCPPFile(std::fstream & stream, const std::string &filename, std::ios::openmode mode)
{
#if defined(_WIN32) && defined(UNICODE)
	stream.open(ConvertUTF8ToWString(filename), mode);
#else
	stream.open(filename.c_str(), mode);
#endif
	return stream.is_open();
}


// Remove any ending forward slashes from directory paths
// Modifies argument.
static void StripTailDirSlashes(std::string &fname)
{
	if (fname.length() > 1)
	{
		size_t i = fname.length() - 1;
#ifdef _WIN32
		if (i == 2 && fname[1] == ':' && fname[2] == '\\')
			return;
#endif
		while (strchr(DIR_SEP_CHRS, fname[i]))
			fname[i--] = '\0';
	}
	return;
}

// _WIN32 only since std::strings are used everywhere else.
#if defined(_WIN32) && defined(UNICODE)
static void StripTailDirSlashes(std::wstring &fname)
{
	if (fname.length() > 1)
	{
		size_t i = fname.length() - 1;

		if (i == 2 && fname[1] == ':' && fname[2] == '\\')
			return;

		while (wcschr((const wchar_t*)_T(DIR_SEP_CHRS), fname[i]))
			fname[i--] = '\0';
	}
	return;
}
#endif

// Returns true if file filename exists
bool Exists(const std::string &filename)
{
	// Make sure Windows will no longer handle critical errors, which means no annoying "No disk" dialog
	// Save the old error mode 
#ifdef _WIN32
	int OldMode = SetErrorMode(SEM_FAILCRITICALERRORS);
#endif

	struct stat64 file_info;
#if defined(_WIN32) && defined(UNICODE)
	std::wstring copy = ConvertUTF8ToWString(filename);
	StripTailDirSlashes(copy);

	int result = _wstat64(copy.c_str(), &file_info);
#else
	std::string copy(filename);
	StripTailDirSlashes(copy);

	int result = stat64(copy.c_str(), &file_info);
#endif

	// Set the old error mode
#ifdef _WIN32
	SetErrorMode(OldMode);
#endif

	return (result == 0);
}

// Returns true if stat represents a directory
bool IsDirectory(const struct stat64 &file_info)
{
	return S_ISDIR(file_info.st_mode);
}

// Returns true if filename is a directory
bool IsDirectory(const std::string &filename)
{
	struct stat64 file_info;
#if defined(_WIN32) && defined(UNICODE)
	std::wstring copy = ConvertUTF8ToWString(filename);
	StripTailDirSlashes(copy);

	int result = _wstat64(copy.c_str(), &file_info);
#else
	std::string copy(filename);
	StripTailDirSlashes(copy);

	int result = stat64(copy.c_str(), &file_info);
#endif
	if (result < 0) {
		WARN_LOG(COMMON, "IsDirectory: stat failed on %s: %s", 
				 filename.c_str(), GetLastErrorMsg());
		return false;
	}

	return IsDirectory(file_info);
}

// Deletes a given filename, return true on success
// Doesn't supports deleting a directory
bool Delete(const std::string &filename)
{
	INFO_LOG(COMMON, "Delete: file %s", filename.c_str());

	// Return true because we care about the file no 
	// being there, not the actual delete.
	if (!Exists(filename))
	{
		WARN_LOG(COMMON, "Delete: %s does not exists", filename.c_str());
		return true;
	}

	// We can't delete a directory
	if (IsDirectory(filename))
	{
		WARN_LOG(COMMON, "Delete failed: %s is a directory", filename.c_str());
		return false;
	}

#ifdef _WIN32
	if (!DeleteFile(ConvertUTF8ToWString(filename).c_str()))
	{
		WARN_LOG(COMMON, "Delete: DeleteFile failed on %s: %s", 
				 filename.c_str(), GetLastErrorMsg());
		return false;
	}
#else
	if (unlink(filename.c_str()) == -1) {
		WARN_LOG(COMMON, "Delete: unlink failed on %s: %s", 
				 filename.c_str(), GetLastErrorMsg());
		return false;
	}
#endif

	return true;
}

// Returns true if successful, or path already exists.
bool CreateDir(const std::string &path)
{
	INFO_LOG(COMMON, "CreateDir: directory %s", path.c_str());
#ifdef _WIN32
	if (::CreateDirectory(ConvertUTF8ToWString(path).c_str(), NULL))
		return true;
	DWORD error = GetLastError();
	if (error == ERROR_ALREADY_EXISTS)
	{
		WARN_LOG(COMMON, "CreateDir: CreateDirectory failed on %s: already exists", path.c_str());
		return true;
	}
	ERROR_LOG(COMMON, "CreateDir: CreateDirectory failed on %s: %i", path.c_str(), error);
	return false;
#else
#ifdef BLACKBERRY
	if (mkdir(path.c_str(), 0775) == 0)
#else
	if (mkdir(path.c_str(), 0755) == 0)
#endif
		return true;

	int err = errno;

	if (err == EEXIST)
	{
		WARN_LOG(COMMON, "CreateDir: mkdir failed on %s: already exists", path.c_str());
		return true;
	}

	ERROR_LOG(COMMON, "CreateDir: mkdir failed on %s: %s", path.c_str(), strerror(err));
	return false;
#endif
}

// Creates the full path of fullPath returns true on success
bool CreateFullPath(const std::string &fullPath)
{
	int panicCounter = 100;
	DEBUG_LOG(COMMON, "CreateFullPath: path %s", fullPath.c_str());
		
	if (File::Exists(fullPath))
	{
		DEBUG_LOG(COMMON, "CreateFullPath: path exists %s", fullPath.c_str());
		return true;
	}

	size_t position = 0;

#ifdef _WIN32
	// Skip the drive letter, no need to create C:\.
	position = 3;
#endif

	while (true)
	{
		// Find next sub path
		position = fullPath.find_first_of(DIR_SEP_CHRS, position);

		// we're done, yay!
		if (position == fullPath.npos)
		{
			if (!File::Exists(fullPath))
				File::CreateDir(fullPath);
			return true;
		}
		std::string subPath = fullPath.substr(0, position);
		if (!File::Exists(subPath))
			File::CreateDir(subPath);

		// A safety check
		panicCounter--;
		if (panicCounter <= 0)
		{
			ERROR_LOG(COMMON, "CreateFullPath: directory structure too deep");
			return false;
		}
		position++;
	}
}


// Deletes a directory filename, returns true on success
bool DeleteDir(const std::string &filename)
{
	INFO_LOG(COMMON, "DeleteDir: directory %s", filename.c_str());

	// check if a directory
	if (!File::IsDirectory(filename))
	{
		ERROR_LOG(COMMON, "DeleteDir: Not a directory %s", filename.c_str());
		return false;
	}

#ifdef _WIN32
	if (::RemoveDirectory(ConvertUTF8ToWString(filename).c_str()))
		return true;
#else
	if (rmdir(filename.c_str()) == 0)
		return true;
#endif
	ERROR_LOG(COMMON, "DeleteDir: %s: %s", filename.c_str(), GetLastErrorMsg());

	return false;
}

// renames file srcFilename to destFilename, returns true on success 
bool Rename(const std::string &srcFilename, const std::string &destFilename)
{
	INFO_LOG(COMMON, "Rename: %s --> %s", 
			srcFilename.c_str(), destFilename.c_str());
	if (rename(srcFilename.c_str(), destFilename.c_str()) == 0)
		return true;
	ERROR_LOG(COMMON, "Rename: failed %s --> %s: %s", 
			  srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg());
	return false;
}

// copies file srcFilename to destFilename, returns true on success 
bool Copy(const std::string &srcFilename, const std::string &destFilename)
{
	INFO_LOG(COMMON, "Copy: %s --> %s", 
			srcFilename.c_str(), destFilename.c_str());
#ifdef _WIN32
	if (CopyFile(ConvertUTF8ToWString(srcFilename).c_str(), ConvertUTF8ToWString(destFilename).c_str(), FALSE))
		return true;

	ERROR_LOG(COMMON, "Copy: failed %s --> %s: %s", 
			srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg());
	return false;
#else

	// buffer size
#define BSIZE 1024

	char buffer[BSIZE];

	// Open input file
	FILE *input = fopen(srcFilename.c_str(), "rb");
	if (!input)
	{
		ERROR_LOG(COMMON, "Copy: input failed %s --> %s: %s", 
				srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg());
		return false;
	}

	// open output file
	FILE *output = fopen(destFilename.c_str(), "wb");
	if (!output)
	{
		fclose(input);
		ERROR_LOG(COMMON, "Copy: output failed %s --> %s: %s", 
				srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg());
		return false;
	}

	// copy loop
	while (!feof(input))
	{
		// read input
		int rnum = fread(buffer, sizeof(char), BSIZE, input);
		if (rnum != BSIZE)
		{
			if (ferror(input) != 0)
			{
				ERROR_LOG(COMMON, 
						"Copy: failed reading from source, %s --> %s: %s", 
						srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg());
				fclose(input);
				fclose(output);		
				return false;
			}
		}

		// write output
		int wnum = fwrite(buffer, sizeof(char), rnum, output);
		if (wnum != rnum)
		{
			ERROR_LOG(COMMON, 
					"Copy: failed writing to output, %s --> %s: %s", 
					srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg());
			fclose(input);
			fclose(output);				
			return false;
		}
	}
	// close flushs
	fclose(input);
	fclose(output);
	return true;
#endif
}

tm GetModifTime(const std::string &filename)
{
	tm return_time = {0};
	if (!Exists(filename))
	{
		WARN_LOG(COMMON, "GetCreateTime: failed %s: No such file", filename.c_str());
		return return_time;
	}

	if (IsDirectory(filename))
	{
		WARN_LOG(COMMON, "GetCreateTime: failed %s: is a directory", filename.c_str());
		return return_time;
	}
	struct stat64 buf;
	if (stat64(filename.c_str(), &buf) == 0)
	{
		DEBUG_LOG(COMMON, "GetCreateTime: %s: %lld",
				filename.c_str(), (long long)buf.st_mtime);
		localtime_r((time_t*)&buf.st_mtime,&return_time);
		return return_time;
	}

	ERROR_LOG(COMMON, "GetCreateTime: Stat failed %s: %s",
			filename.c_str(), GetLastErrorMsg());
	return return_time;
}

// Returns the size of filename (64bit)
u64 GetSize(const std::string &filename)
{
	struct stat64 file_info;
#if defined(_WIN32) && defined(UNICODE)
	int result = _wstat64(ConvertUTF8ToWString(filename).c_str(), &file_info);
#else
	int result = stat64(filename.c_str(), &file_info);
#endif
	if (result != 0)
	{
		WARN_LOG(COMMON, "GetSize: failed %s: No such file", filename.c_str());
		return 0;
	}

	if (IsDirectory(file_info))
	{
		WARN_LOG(COMMON, "GetSize: failed %s: is a directory", filename.c_str());
		return 0;
	}

	DEBUG_LOG(COMMON, "GetSize: %s: %lld", filename.c_str(), (long long)file_info.st_size);
	return file_info.st_size;
}

// Overloaded GetSize, accepts file descriptor
u64 GetSize(const int fd)
{
	struct stat64 buf;
	if (fstat64(fd, &buf) != 0) {
		ERROR_LOG(COMMON, "GetSize: stat failed %i: %s",
			fd, GetLastErrorMsg());
		return 0;
	}
	return buf.st_size;
}

// Overloaded GetSize, accepts FILE*
u64 GetSize(FILE *f)
{
	// can't use off_t here because it can be 32-bit
	u64 pos = ftello(f);
	if (fseeko(f, 0, SEEK_END) != 0) {
		ERROR_LOG(COMMON, "GetSize: seek failed %p: %s",
			  f, GetLastErrorMsg());
		return 0;
	}
	u64 size = ftello(f);
	if ((size != pos) && (fseeko(f, pos, SEEK_SET) != 0)) {
		ERROR_LOG(COMMON, "GetSize: seek failed %p: %s",
			  f, GetLastErrorMsg());
		return 0;
	}
	return size;
}

// creates an empty file filename, returns true on success 
bool CreateEmptyFile(const std::string &filename)
{
	INFO_LOG(COMMON, "CreateEmptyFile: %s", filename.c_str()); 

	FILE *pFile = OpenCFile(filename, "wb");
	if (!pFile) {
		ERROR_LOG(COMMON, "CreateEmptyFile: failed %s: %s",
				  filename.c_str(), GetLastErrorMsg());
		return false;
	}
	fclose(pFile);
	return true;
}

// Deletes the given directory and anything under it. Returns true on success.
bool DeleteDirRecursively(const std::string &directory)
{
	INFO_LOG(COMMON, "DeleteDirRecursively: %s", directory.c_str());
#ifdef _WIN32
	// Find the first file in the directory.
	WIN32_FIND_DATA ffd;
	HANDLE hFind = FindFirstFile(ConvertUTF8ToWString(directory + "\\*").c_str(), &ffd);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		FindClose(hFind);
		return false;
	}
		
	// windows loop
	do
	{
		const std::string virtualName = ConvertWStringToUTF8(ffd.cFileName);
#else
	struct dirent dirent, *result = NULL;
	DIR *dirp = opendir(directory.c_str());
	if (!dirp)
		return false;

	// non windows loop
	while (!readdir_r(dirp, &dirent, &result) && result)
	{
		const std::string virtualName = result->d_name;
#endif

		// check for "." and ".."
		if (((virtualName[0] == '.') && (virtualName[1] == '\0')) ||
			((virtualName[0] == '.') && (virtualName[1] == '.') && 
			 (virtualName[2] == '\0')))
			continue;

		std::string newPath = directory + DIR_SEP + virtualName;
		if (IsDirectory(newPath))
		{
			if (!DeleteDirRecursively(newPath))
			{
				#ifndef _WIN32
				closedir(dirp);
				#endif
				
				return false;
			}
		}
		else
		{
			if (!File::Delete(newPath))
			{
				#ifndef _WIN32
				closedir(dirp);
				#endif
				
				return false;
			}
		}

#ifdef _WIN32
	} while (FindNextFile(hFind, &ffd) != 0);
	FindClose(hFind);
#else
	}
	closedir(dirp);
#endif
	File::DeleteDir(directory);
		
	return true;
}

// Create directory and copy contents (does not overwrite existing files)
void CopyDir(const std::string &source_path, const std::string &dest_path)
{
#ifndef _WIN32
	if (source_path == dest_path) return;
	if (!File::Exists(source_path)) return;
	if (!File::Exists(dest_path)) File::CreateFullPath(dest_path);

	struct dirent_large { struct dirent entry; char padding[FILENAME_MAX+1]; };
	struct dirent_large diren;
	struct dirent *result = NULL;
	DIR *dirp = opendir(source_path.c_str());
	if (!dirp) return;

	while (!readdir_r(dirp, (dirent*) &diren, &result) && result)
	{
		const std::string virtualName(result->d_name);
		// check for "." and ".."
		if (((virtualName[0] == '.') && (virtualName[1] == '\0')) ||
			((virtualName[0] == '.') && (virtualName[1] == '.') &&
			(virtualName[2] == '\0')))
			continue;

		std::string source, dest;
		source = source_path + virtualName;
		dest = dest_path + virtualName;
		if (IsDirectory(source))
		{
			source += '/';
			dest += '/';
			if (!File::Exists(dest)) File::CreateFullPath(dest);
			CopyDir(source, dest);
		}
		else if (!File::Exists(dest)) File::Copy(source, dest);
	}
	closedir(dirp);
#endif
}

// Returns the current directory
std::string GetCurrentDir()
{
	char *dir;
#ifndef _XBOX
	// Get the current working directory (getcwd uses malloc) 
	if (!(dir = __getcwd(NULL, 0))) {

		ERROR_LOG(COMMON, "GetCurrentDirectory failed: %s",
				  GetLastErrorMsg());
		return NULL;
	}
	std::string strDir = dir;
	free(dir);
	return strDir;
#else
	return "game:\\";
#endif
}

// Sets the current directory to the given directory
bool SetCurrentDir(const std::string &directory)
{
#ifndef _XBOX
	return __chdir(directory.c_str()) == 0;
#else
	return false;
#endif
}

const std::string &GetExeDirectory()
{
	static std::string ExePath;

	if (ExePath.empty())
#ifndef _XBOX
	{
#ifdef _WIN32
		TCHAR program_path[4096] = {0};
		GetModuleFileName(NULL, program_path, ARRAY_SIZE(program_path) - 1);
		program_path[ARRAY_SIZE(program_path) - 1] = '\0';
		TCHAR *last_slash = _tcsrchr(program_path, '\\');
		if (last_slash != NULL)
			*(last_slash + 1) = '\0';
#ifdef UNICODE
		ExePath = ConvertWStringToUTF8(program_path);
#else
		ExePath = program_path;
#endif

#elif (defined(__APPLE__) && !defined(IOS)) || defined(__linux__)
		char program_path[4096];
		uint32_t program_path_size = sizeof(program_path) - 1;

#if defined(__linux__)
		if (readlink("/proc/self/exe", program_path, 4095) > 0)
#elif defined(__APPLE__) && !defined(IOS)
		if (_NSGetExecutablePath(program_path, &program_path_size) == 0)
#else
#error Unmatched ifdef.
#endif
		{
			program_path[sizeof(program_path) - 1] = '\0';
			char *last_slash = strrchr(program_path, '/');
			if (last_slash != NULL)
				*(last_slash + 1) = '\0';
			ExePath = program_path;
		}
#endif
	}

	return ExePath;
#else
	static std::wstring ExePath = L"game:\\";
	return ExePath;
#endif
}


IOFile::IOFile()
	: m_file(NULL), m_good(true)
{}

IOFile::IOFile(std::FILE* file)
	: m_file(file), m_good(true)
{}

IOFile::IOFile(const std::string& filename, const char openmode[])
	: m_file(NULL), m_good(true)
{
	Open(filename, openmode);
}

IOFile::~IOFile()
{
	Close();
}

bool IOFile::Open(const std::string& filename, const char openmode[])
{
	Close();
#if defined(_WIN32) && defined(UNICODE)
	_wfopen_s(&m_file, ConvertUTF8ToWString(filename).c_str(), ConvertUTF8ToWString(openmode).c_str());
#else
	m_file = fopen(filename.c_str(), openmode);
#endif

	m_good = IsOpen();
	return m_good;
}

bool IOFile::Close()
{
	if (!IsOpen() || 0 != std::fclose(m_file))
		m_good = false;

	m_file = NULL;
	return m_good;
}

std::FILE* IOFile::ReleaseHandle()
{
	std::FILE* const ret = m_file;
	m_file = NULL;
	return ret;
}

void IOFile::SetHandle(std::FILE* file)
{
	Close();
	Clear();
	m_file = file;
}

u64 IOFile::GetSize()
{
	if (IsOpen())
		return File::GetSize(m_file);
	else
		return 0;
}

bool IOFile::Seek(s64 off, int origin)
{
	if (!IsOpen() || 0 != fseeko(m_file, off, origin))
		m_good = false;

	return m_good;
}

u64 IOFile::Tell()
{	
	if (IsOpen())
		return ftello(m_file);
	else
		return -1;
}

bool IOFile::Flush()
{
	if (!IsOpen() || 0 != std::fflush(m_file))
		m_good = false;

	return m_good;
}

bool IOFile::Resize(u64 size)
{
#ifndef _XBOX
	if (!IsOpen() || 0 !=
#ifdef _WIN32
		// ector: _chsize sucks, not 64-bit safe
		// F|RES: changed to _chsize_s. i think it is 64-bit safe
		_chsize_s(_fileno(m_file), size)
#else
		// TODO: handle 64bit and growing
		ftruncate(fileno(m_file), size)
#endif
	)
		m_good = false;

	return m_good;
#else
	// TODO: Implement.
	return false;
#endif
}

} // namespace
