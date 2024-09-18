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
#include <set>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Common/StringUtils.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/Reporting.h"
#include "Core/System.h"

static bool ApplyPathStringToComponentsVector(std::vector<std::string> &vector, const std::string &pathString)
{
	size_t len = pathString.length();
	size_t start = 0;

	while (start < len)
	{
		// TODO: This should only be done for ms0:/ etc.
		size_t i = pathString.find_first_of("/\\", start);
		if (i == std::string::npos)
			i = len;

		if (i > start)
		{
			std::string component = pathString.substr(start, i - start);
			if (component != ".")
			{
				if (component == "..")
				{
					if (vector.size() != 0)
					{
						vector.pop_back();
					}
					else
					{
						// The PSP silently ignores attempts to .. to parent of root directory
						WARN_LOG(Log::FileSystem, "RealPath: ignoring .. beyond root - root directory is its own parent: \"%s\"", pathString.c_str());
					}
				}
				else
				{
					vector.push_back(component);
				}
			}
		}

		start = i + 1;
	}

	return true;
}

/*
 * Changes relative paths to absolute, removes ".", "..", and trailing "/"
 * "drive:./blah" is absolute (ignore the dot) and "/blah" is relative (because it's missing "drive:")
 * babel (and possibly other games) use "/directoryThatDoesNotExist/../directoryThatExists/filename"
 */
static bool RealPath(const std::string &currentDirectory, const std::string &inPath, std::string &outPath)
{
	size_t inLen = inPath.length();
	if (inLen == 0)
	{
		outPath = currentDirectory;
		return true;
	}

	size_t inColon = inPath.find(':');
	if (inColon + 1 == inLen)
	{
		// There's nothing after the colon, e.g. umd0: - this is perfectly valid.
		outPath = inPath;
		return true;
	}

	bool relative = (inColon == std::string::npos);
	
	std::string prefix, inAfterColon;
	std::vector<std::string> cmpnts;  // path components
	size_t outPathCapacityGuess = inPath.length();

	if (relative)
	{
		size_t curDirLen = currentDirectory.length();
		if (curDirLen == 0)
		{
			ERROR_LOG(Log::FileSystem, "RealPath: inPath \"%s\" is relative, but current directory is empty", inPath.c_str());
			return false;
		}
		
		size_t curDirColon = currentDirectory.find(':');
		if (curDirColon == std::string::npos)
		{
			ERROR_LOG(Log::FileSystem, "RealPath: inPath \"%s\" is relative, but current directory \"%s\" has no prefix", inPath.c_str(), currentDirectory.c_str());
			return false;
		}
		if (curDirColon + 1 == curDirLen)
		{
			WARN_LOG(Log::FileSystem, "RealPath: inPath \"%s\" is relative, but current directory \"%s\" is all prefix and no path. Using \"/\" as path for current directory.", inPath.c_str(), currentDirectory.c_str());
		}
		else
		{
			const std::string curDirAfter = currentDirectory.substr(curDirColon + 1);
			if (! ApplyPathStringToComponentsVector(cmpnts, curDirAfter) )
			{
				ERROR_LOG(Log::FileSystem,"RealPath: currentDirectory is not a valid path: \"%s\"", currentDirectory.c_str());
				return false;
			}

			outPathCapacityGuess += curDirLen;
		}

		prefix = currentDirectory.substr(0, curDirColon + 1);
		inAfterColon = inPath;
	}
	else
	{
		prefix = inPath.substr(0, inColon + 1);
		inAfterColon = inPath.substr(inColon + 1);

		// Special case: "disc0:" is different from "disc0:/", so keep track of the single slash.
		if (inAfterColon == "/")
		{
			outPath = prefix + inAfterColon;
			return true;
		}
	}

	if (! ApplyPathStringToComponentsVector(cmpnts, inAfterColon) )
	{
		WARN_LOG(Log::FileSystem, "RealPath: inPath is not a valid path: \"%s\"", inPath.c_str());
		return false;
	}

	outPath.clear();
	outPath.reserve(outPathCapacityGuess);

	outPath.append(prefix);

	size_t numCmpnts = cmpnts.size();
	for (size_t i = 0; i < numCmpnts; i++)
	{
		outPath.append(1, '/');
		outPath.append(cmpnts[i]);
	}

	return true;
}

IFileSystem *MetaFileSystem::GetHandleOwner(u32 handle)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	for (size_t i = 0; i < fileSystems.size(); i++)
	{
		if (fileSystems[i].system->OwnsHandle(handle))
			return fileSystems[i].system.get();
	}

	// Not found
	return nullptr;
}

int MetaFileSystem::MapFilePath(const std::string &_inpath, std::string &outpath, MountPoint **system)
{
	int error = SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
	std::lock_guard<std::recursive_mutex> guard(lock);
	std::string realpath;

	std::string inpath = _inpath;

	// "ms0:/file.txt" is equivalent to "   ms0:/file.txt".  Yes, really.
	if (inpath.find(':') != inpath.npos) {
		size_t offset = 0;
		while (inpath[offset] == ' ') {
			offset++;
		}
		if (offset > 0) {
			inpath = inpath.substr(offset);
		}
	}

	// Special handling: host0:command.txt (as seen in Super Monkey Ball Adventures, for example)
	// appears to mean the current directory on the UMD. Let's just assume the current directory.
	if (strncasecmp(inpath.c_str(), "host0:", strlen("host0:")) == 0) {
		INFO_LOG(Log::FileSystem, "Host0 path detected, stripping: %s", inpath.c_str());
		// However, this causes trouble when running tests, since our test framework uses host0:.
		// Maybe it's really just supposed to map to umd0 or something?
		if (PSP_CoreParameter().headLess) {
			inpath = "umd0:" + inpath.substr(strlen("host0:"));
		} else {
			inpath = inpath.substr(strlen("host0:"));
		}
	}

	const std::string *currentDirectory = &startingDirectory;

	int currentThread = __KernelGetCurThread();
	currentDir_t::iterator it = currentDir.find(currentThread);
	if (it == currentDir.end()) 
	{
		//Attempt to emulate SCE_KERNEL_ERROR_NOCWD / 8002032C: may break things requiring fixes elsewhere
		if (inpath.find(':') == std::string::npos /* means path is relative */) 
		{
			error = SCE_KERNEL_ERROR_NOCWD;
			WARN_LOG(Log::FileSystem, "Path is relative, but current directory not set for thread %i. returning 8002032C(SCE_KERNEL_ERROR_NOCWD) instead.", currentThread);
		}
	}
	else
	{
		currentDirectory = &(it->second);
	}

	if (RealPath(*currentDirectory, inpath, realpath))
	{
		std::string prefix = realpath;
		size_t prefixPos = realpath.find(':');
		if (prefixPos != realpath.npos)
			prefix = NormalizePrefix(realpath.substr(0, prefixPos + 1));

		for (size_t i = 0; i < fileSystems.size(); i++)
		{
			size_t prefLen = fileSystems[i].prefix.size();
			if (strncasecmp(fileSystems[i].prefix.c_str(), prefix.c_str(), prefLen) == 0)
			{
				outpath = realpath.substr(prefixPos + 1);
				*system = &(fileSystems[i]);

				VERBOSE_LOG(Log::FileSystem, "MapFilePath: mapped \"%s\" to prefix: \"%s\", path: \"%s\"", inpath.c_str(), fileSystems[i].prefix.c_str(), outpath.c_str());

				return error == SCE_KERNEL_ERROR_NOCWD ? error : 0;
			}
		}

		error = SCE_KERNEL_ERROR_NODEV;
	}

	DEBUG_LOG(Log::FileSystem, "MapFilePath: failed mapping \"%s\", returning false", inpath.c_str());
	return error;
}

std::string MetaFileSystem::NormalizePrefix(std::string prefix) const {
	// Let's apply some mapping here since it won't break savestates.
	if (prefix == "memstick:")
		prefix = "ms0:";
	// Seems like umd00: etc. work just fine... avoid umd1/umd for tests.
	if (startsWith(prefix, "umd") && prefix != "umd1:" && prefix != "umd:")
		prefix = "umd0:";
	// Seems like umd00: etc. work just fine...
	if (startsWith(prefix, "host"))
		prefix = "host0:";

	// Should we simply make this case insensitive?
	if (prefix == "DISC0:")
		prefix = "disc0:";

	return prefix;
}

void MetaFileSystem::Mount(const std::string &prefix, std::shared_ptr<IFileSystem> system) {
	std::lock_guard<std::recursive_mutex> guard(lock);

	MountPoint x;
	x.prefix = prefix;
	x.system = system;
	for (auto &it : fileSystems) {
		if (it.prefix == prefix) {
			// Overwrite the old mount. Don't create a new one.
			it = x;
			return;
		}
	}

	// Prefix not yet mounted, do so.
	fileSystems.push_back(x);
}

// Assumes the lock is held
void MetaFileSystem::UnmountAll() {
	fileSystems.clear();
	currentDir.clear();
}

void MetaFileSystem::Unmount(const std::string &prefix) {
	std::lock_guard<std::recursive_mutex> guard(lock);
	for (auto iter = fileSystems.begin(); iter != fileSystems.end(); iter++) {
		if (iter->prefix == prefix) {
			fileSystems.erase(iter);
			return;
		}
	}
}

bool MetaFileSystem::Remount(const std::string &prefix, std::shared_ptr<IFileSystem> system) {
	std::lock_guard<std::recursive_mutex> guard(lock);
	for (auto &it : fileSystems) {
		if (it.prefix == prefix) {
			it.system = system;
			return true;
		}
	}
	return false;
}

IFileSystem *MetaFileSystem::GetSystemFromFilename(const std::string &filename) {
	size_t prefixPos = filename.find(':');
	if (prefixPos == filename.npos)
		return 0;
	return GetSystem(filename.substr(0, prefixPos + 1));
}

IFileSystem *MetaFileSystem::GetSystem(const std::string &prefix) {
	std::lock_guard<std::recursive_mutex> guard(lock);
	for (auto it = fileSystems.begin(); it != fileSystems.end(); ++it) {
		if (it->prefix == NormalizePrefix(prefix))
			return it->system.get();
	}
	return NULL;
}

void MetaFileSystem::Shutdown() {
	std::lock_guard<std::recursive_mutex> guard(lock);

	UnmountAll();
	Reset();
}

int MetaFileSystem::OpenFile(std::string filename, FileAccess access, const char *devicename)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	std::string of;
	MountPoint *mount;
	int error = MapFilePath(filename, of, &mount);
	if (error == 0)
		return mount->system->OpenFile(of, access, mount->prefix.c_str());
	else
		return error;
}

PSPFileInfo MetaFileSystem::GetFileInfo(std::string filename)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	std::string of;
	IFileSystem *system;
	int error = MapFilePath(filename, of, &system);
	if (error == 0)
	{
		return system->GetFileInfo(of);
	}
	else
	{
		PSPFileInfo bogus;
		return bogus; 
	}
}

std::vector<PSPFileInfo> MetaFileSystem::GetDirListing(const std::string &path, bool *exists) {
	std::lock_guard<std::recursive_mutex> guard(lock);
	std::string of;
	IFileSystem *system;
	int error = MapFilePath(path, of, &system);
	if (error == 0) {
		return system->GetDirListing(of, exists);
	} else {
		std::vector<PSPFileInfo> empty;
		if (exists)
			*exists = false;
		return empty;
	}
}

void MetaFileSystem::ThreadEnded(int threadID)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	currentDir.erase(threadID);
}

int MetaFileSystem::ChDir(const std::string &dir)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	// Retain the old path and fail if the arg is 1023 bytes or longer.
	if (dir.size() >= 1023)
		return SCE_KERNEL_ERROR_NAMETOOLONG;

	int curThread = __KernelGetCurThread();
	
	std::string of;
	MountPoint *mountPoint;
	int error = MapFilePath(dir, of, &mountPoint);
	if (error == 0)
	{
		currentDir[curThread] = mountPoint->prefix + of;
		return 0;
	}
	else
	{
		for (size_t i = 0; i < fileSystems.size(); i++)
		{
			const std::string &prefix = fileSystems[i].prefix;
			if (strncasecmp(prefix.c_str(), dir.c_str(), prefix.size()) == 0)
			{
				// The PSP is completely happy with invalid current dirs as long as they have a valid device.
				WARN_LOG(Log::FileSystem, "ChDir failed to map path \"%s\", saving as current directory anyway", dir.c_str());
				currentDir[curThread] = dir;
				return 0;
			}
		}

		WARN_LOG_REPORT(Log::FileSystem, "ChDir failed to map device for \"%s\", failing", dir.c_str());
		return SCE_KERNEL_ERROR_NODEV;
	}
}

bool MetaFileSystem::MkDir(const std::string &dirname)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	std::string of;
	IFileSystem *system;
	int error = MapFilePath(dirname, of, &system);
	if (error == 0)
	{
		return system->MkDir(of);
	}
	else
	{
		return false;
	}
}

bool MetaFileSystem::RmDir(const std::string &dirname)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	std::string of;
	IFileSystem *system;
	int error = MapFilePath(dirname, of, &system);
	if (error == 0)
	{
		return system->RmDir(of);
	}
	else
	{
		return false;
	}
}

int MetaFileSystem::RenameFile(const std::string &from, const std::string &to)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	std::string of;
	std::string rf;
	IFileSystem *osystem;
	IFileSystem *rsystem = NULL;
	int error = MapFilePath(from, of, &osystem);
	if (error == 0)
	{
		// If it's a relative path, it seems to always use from's filesystem.
		if (to.find(":/") != to.npos)
		{
			error = MapFilePath(to, rf, &rsystem);
			if (error < 0)
				return -1;
		}
		else
		{
			rf = to;
			rsystem = osystem;
		}

		if (osystem != rsystem)
			return SCE_KERNEL_ERROR_XDEV;

		return osystem->RenameFile(of, rf);
	}
	else
	{
		return -1;
	}
}

bool MetaFileSystem::RemoveFile(const std::string &filename)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	std::string of;
	IFileSystem *system;
	int error = MapFilePath(filename, of, &system);
	if (error == 0) {
		return system->RemoveFile(of);
	} else {
		return false;
	}
}

int MetaFileSystem::Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	IFileSystem *sys = GetHandleOwner(handle);
	if (sys)
		return sys->Ioctl(handle, cmd, indataPtr, inlen, outdataPtr, outlen, usec);
	return SCE_KERNEL_ERROR_ERROR;
}

PSPDevType MetaFileSystem::DevType(u32 handle)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	IFileSystem *sys = GetHandleOwner(handle);
	if (sys)
		return sys->DevType(handle);
	return PSPDevType::INVALID;
}

void MetaFileSystem::CloseFile(u32 handle)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	IFileSystem *sys = GetHandleOwner(handle);
	if (sys)
		sys->CloseFile(handle);
}

size_t MetaFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	IFileSystem *sys = GetHandleOwner(handle);
	if (sys)
		return sys->ReadFile(handle, pointer, size);
	else
		return 0;
}

size_t MetaFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	IFileSystem *sys = GetHandleOwner(handle);
	if (sys)
		return sys->WriteFile(handle, pointer, size);
	else
		return 0;
}

size_t MetaFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size, int &usec)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	IFileSystem *sys = GetHandleOwner(handle);
	if (sys)
		return sys->ReadFile(handle, pointer, size, usec);
	else
		return 0;
}

size_t MetaFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	IFileSystem *sys = GetHandleOwner(handle);
	if (sys)
		return sys->WriteFile(handle, pointer, size, usec);
	else
		return 0;
}

size_t MetaFileSystem::SeekFile(u32 handle, s32 position, FileMove type)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	IFileSystem *sys = GetHandleOwner(handle);
	if (sys)
		return sys->SeekFile(handle, position, type);
	else
		return 0;
}

int MetaFileSystem::ReadEntireFile(const std::string &filename, std::vector<u8> &data, bool quiet) {
	FileAccess access = FILEACCESS_READ;
	if (quiet) {
		access = (FileAccess)(access | FILEACCESS_PPSSPP_QUIET);
	}
	int handle = OpenFile(filename, access);
	if (handle < 0)
		return handle;

	SeekFile(handle, 0, FILEMOVE_END);
	size_t dataSize = GetSeekPos(handle);
	SeekFile(handle, 0, FILEMOVE_BEGIN);
	data.resize(dataSize);

	size_t result = ReadFile(handle, data.data(), dataSize);
	CloseFile(handle);

	if (result != dataSize)
		return SCE_KERNEL_ERROR_ERROR;

	return 0;
}

u64 MetaFileSystem::FreeSpace(const std::string &path)
{
	std::lock_guard<std::recursive_mutex> guard(lock);
	std::string of;
	IFileSystem *system;
	int error = MapFilePath(path, of, &system);
	if (error == 0)
		return system->FreeSpace(of);
	else
		return 0;
}

void MetaFileSystem::DoState(PointerWrap &p)
{
	std::lock_guard<std::recursive_mutex> guard(lock);

	auto s = p.Section("MetaFileSystem", 1);
	if (!s)
		return;

	Do(p, current);

	// Save/load per-thread current directory map
	Do(p, currentDir);

	u32 n = (u32) fileSystems.size();
	Do(p, n);
	bool skipPfat0 = false;
	if (n != (u32) fileSystems.size())
	{
		if (n == (u32) fileSystems.size() - 1) {
			skipPfat0 = true;
		} else {
			p.SetError(p.ERROR_FAILURE);
			ERROR_LOG(Log::FileSystem, "Savestate failure: number of filesystems doesn't match.");
			return;
		}
	}

	for (u32 i = 0; i < n; ++i) {
		if (!skipPfat0 || fileSystems[i].prefix != "pfat0:") {
			fileSystems[i].system->DoState(p);
		}
	}
}

int64_t MetaFileSystem::RecursiveSize(const std::string &dirPath) {
	u64 result = 0;
	auto allFiles = GetDirListing(dirPath);
	for (auto file : allFiles) {
		if (file.name == "." || file.name == "..")
			continue;
		if (file.type == FILETYPE_DIRECTORY) {
			result += RecursiveSize(dirPath + file.name);
		} else {
			result += file.size;
		}
	}
	return result;
}

int64_t MetaFileSystem::ComputeRecursiveDirectorySize(const std::string &filename) {
	std::lock_guard<std::recursive_mutex> guard(lock);
	std::string of;
	IFileSystem *system;
	int error = MapFilePath(filename, of, &system);
	if (error == 0) {
		int64_t size;
		if (system->ComputeRecursiveDirSizeIfFast(of, &size)) {
			// Some file systems can optimize this.
			return size;
		} else {
			// Those that can't, we just run a generic implementation.
			return RecursiveSize(filename);
		}
	} else {
		return false;
	}
}
