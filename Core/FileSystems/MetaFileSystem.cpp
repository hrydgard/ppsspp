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

#include <set>
#include "Common/StringUtils.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/Reporting.h"

static bool ApplyPathStringToComponentsVector(std::vector<std::string> &vector, const std::string &pathString)
{
	size_t len = pathString.length();
	size_t start = 0;

	while (start < len)
	{
		size_t i = pathString.find('/', start);
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
						WARN_LOG(HLE, "RealPath: ignoring .. beyond root - root directory is its own parent: \"%s\"", pathString.c_str());
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
		WARN_LOG(HLE, "RealPath: inPath is empty");
		outPath = currentDirectory;
		return true;
	}

	size_t inColon = inPath.find(':');
	if (inColon + 1 == inLen)
	{
		WARN_LOG(HLE, "RealPath: inPath is all prefix and no path: \"%s\"", inPath.c_str());

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
			ERROR_LOG(HLE, "RealPath: inPath \"%s\" is relative, but current directory is empty", inPath.c_str());
			return false;
		}
		
		size_t curDirColon = currentDirectory.find(':');
		if (curDirColon == std::string::npos)
		{
			ERROR_LOG(HLE, "RealPath: inPath \"%s\" is relative, but current directory \"%s\" has no prefix", inPath.c_str(), currentDirectory.c_str());
			return false;
		}
		if (curDirColon + 1 == curDirLen)
		{
			ERROR_LOG(HLE, "RealPath: inPath \"%s\" is relative, but current directory \"%s\" is all prefix and no path. Using \"/\" as path for current directory.", inPath.c_str(), currentDirectory.c_str());
		}
		else
		{
			const std::string curDirAfter = currentDirectory.substr(curDirColon + 1);
			if (! ApplyPathStringToComponentsVector(cmpnts, curDirAfter) )
			{
				ERROR_LOG(HLE,"RealPath: currentDirectory is not a valid path: \"%s\"", currentDirectory.c_str());
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
	}

	// Special case: "disc0:" is different from "disc0:/", so keep track of the single slash.
	if (inAfterColon == "/")
	{
		outPath = prefix + inAfterColon;
		return true;
	}

	if (! ApplyPathStringToComponentsVector(cmpnts, inAfterColon) )
	{
		WARN_LOG(HLE, "RealPath: inPath is not a valid path: \"%s\"", inPath.c_str());
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
	for (size_t i = 0; i < fileSystems.size(); i++)
	{
		if (fileSystems[i].system->OwnsHandle(handle))
			return fileSystems[i].system; //got it!
	}
	//none found?
	return 0;
}

extern u32 ioErrorCode;
bool MetaFileSystem::MapFilePath(const std::string &_inpath, std::string &outpath, MountPoint **system)
{
	std::string realpath;

	// Special handling: host0:command.txt (as seen in Super Monkey Ball Adventures, for example)
	// appears to mean the current directory on the UMD. Let's just assume the current directory.
	std::string inpath = _inpath;
	if (strncasecmp(inpath.c_str(), "host0:", strlen("host0:")) == 0) {
		INFO_LOG(HLE, "Host0 path detected, stripping: %s", inpath.c_str());
		inpath = inpath.substr(strlen("host0:"));
	}

	const std::string *currentDirectory = &startingDirectory;

	int currentThread = __KernelGetCurThread();
	currentDir_t::iterator it = currentDir.find(currentThread);
	if (it == currentDir.end()) 
	{
		//Attempt to emulate SCE_KERNEL_ERROR_NOCWD / 8002032C: may break things requiring fixes elsewhere
		if (inpath.find(':') == std::string::npos /* means path is relative */) 
		{
			ioErrorCode = SCE_KERNEL_ERROR_NOCWD;
			WARN_LOG_REPORT(HLE, "Path is relative, but current directory not set for thread %i. returning 8002032C(SCE_KERNEL_ERROR_NOCWD) instead.", currentThread);
		}
	}
	else
	{
		currentDirectory = &(it->second);
	}

	if ( RealPath(*currentDirectory, inpath, realpath) )
	{
		for (size_t i = 0; i < fileSystems.size(); i++)
		{
			size_t prefLen = fileSystems[i].prefix.size();
			if (strncasecmp(fileSystems[i].prefix.c_str(), realpath.c_str(), prefLen) == 0)
			{
				outpath = realpath.substr(prefLen);
				*system = &(fileSystems[i]);

				VERBOSE_LOG(HLE, "MapFilePath: mapped \"%s\" to prefix: \"%s\", path: \"%s\"", inpath.c_str(), fileSystems[i].prefix.c_str(), outpath.c_str());

				return true;
			}
		}
	}

	DEBUG_LOG(HLE, "MapFilePath: failed mapping \"%s\", returning false", inpath.c_str());
	return false;
}

void MetaFileSystem::Mount(std::string prefix, IFileSystem *system)
{
	MountPoint x;
	x.prefix=prefix;
	x.system=system;
	fileSystems.push_back(x);
}

void MetaFileSystem::Shutdown()
{
	current = 6;

	// Ownership is a bit convoluted. Let's just delete everything once.

	std::set<IFileSystem *> toDelete;
	for (size_t i = 0; i < fileSystems.size(); i++) {
		toDelete.insert(fileSystems[i].system);
	}

	for (auto iter = toDelete.begin(); iter != toDelete.end(); ++iter)
	{
		delete *iter;
	}

	fileSystems.clear();
	currentDir.clear();
	startingDirectory = "";
}

u32 MetaFileSystem::OpenFile(std::string filename, FileAccess access)
{
	std::string of;
	IFileSystem *system;
	if (MapFilePath(filename, of, &system))
	{
		return system->OpenFile(of, access);
	}
	else
	{
		return 0;
	}
}

PSPFileInfo MetaFileSystem::GetFileInfo(std::string filename)
{
	std::string of;
	IFileSystem *system;
	if (MapFilePath(filename, of, &system))
	{
		return system->GetFileInfo(of);
	}
	else
	{
		PSPFileInfo bogus; // TODO
		return bogus; 
	}
}

bool MetaFileSystem::GetHostPath(const std::string &inpath, std::string &outpath)
{
	std::string of;
	IFileSystem *system;
	if (MapFilePath(inpath, of, &system)) {
		return system->GetHostPath(of, outpath);
	} else {
		return false;
	}
}

std::vector<PSPFileInfo> MetaFileSystem::GetDirListing(std::string path)
{
	std::string of;
	IFileSystem *system;
	if (MapFilePath(path, of, &system))
	{
		return system->GetDirListing(of);
	}
	else
	{
		std::vector<PSPFileInfo> empty;
		return empty;
	}
}

void MetaFileSystem::ThreadEnded(int threadID)
{
	currentDir.erase(threadID);
}

int MetaFileSystem::ChDir(const std::string &dir)
{
	// Retain the old path and fail if the arg is 1023 bytes or longer.
	if (dir.size() >= 1023)
		return SCE_KERNEL_ERROR_NAMETOOLONG;

	int curThread = __KernelGetCurThread();
	
	std::string of;
	MountPoint *mountPoint;
	if (MapFilePath(dir, of, &mountPoint))
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
				WARN_LOG(HLE, "ChDir failed to map path \"%s\", saving as current directory anyway", dir.c_str());
				currentDir[curThread] = dir;
				return 0;
			}
		}

		WARN_LOG_REPORT(HLE, "ChDir failed to map device for \"%s\", failing", dir.c_str());
		return SCE_KERNEL_ERROR_NODEV;
	}
}

bool MetaFileSystem::MkDir(const std::string &dirname)
{
	std::string of;
	IFileSystem *system;
	if (MapFilePath(dirname, of, &system))
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
	std::string of;
	IFileSystem *system;
	if (MapFilePath(dirname, of, &system))
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
	std::string of;
	std::string rf;
	IFileSystem *osystem;
	IFileSystem *rsystem = NULL;
	if (MapFilePath(from, of, &osystem))
	{
		// If it's a relative path, it seems to always use from's filesystem.
		if (to.find(':/') != to.npos)
		{
			if (!MapFilePath(to, rf, &rsystem))
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
	std::string of;
	IFileSystem *system;
	if (MapFilePath(filename, of, &system))
	{
		return system->RemoveFile(of);
	}
	else
	{
		return false;
	}
}

void MetaFileSystem::CloseFile(u32 handle)
{
	IFileSystem *sys = GetHandleOwner(handle);
	if (sys)
		sys->CloseFile(handle);
}

size_t MetaFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size)
{
	IFileSystem *sys = GetHandleOwner(handle);
	if (sys)
		return sys->ReadFile(handle,pointer,size);
	else
		return 0;
}

size_t MetaFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size)
{
	IFileSystem *sys = GetHandleOwner(handle);
	if (sys)
		return sys->WriteFile(handle,pointer,size);
	else
		return 0;
}

size_t MetaFileSystem::SeekFile(u32 handle, s32 position, FileMove type)
{
	IFileSystem *sys = GetHandleOwner(handle);
	if (sys)
		return sys->SeekFile(handle,position,type);
	else
		return 0;
}

void MetaFileSystem::DoState(PointerWrap &p)
{
	p.Do(current);

	// Save/load per-thread current directory map
	p.Do(currentDir);

	u32 n = (u32) fileSystems.size();
	p.Do(n);
	if (n != (u32) fileSystems.size())
	{
		p.SetError(p.ERROR_FAILURE);
		ERROR_LOG(FILESYS, "Savestate failure: number of filesystems doesn't match.");
		return;
	}

	for (u32 i = 0; i < n; ++i)
		fileSystems[i].system->DoState(p);

	p.DoMarker("MetaFileSystem");
}

