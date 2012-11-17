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
#include "MetaFileSystem.h"

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

bool MetaFileSystem::MapFilePath(std::string inpath, std::string &outpath, IFileSystem **system)
{
	// host0 HACK
	// need to figure out what to do about xxx:./... paths - is there a current dir per drive?
	if (!inpath.compare(0, 8, "host0:./"))
		inpath = currentDirectory + inpath.substr(7);

	for (size_t i = 0; i < fileSystems.size(); i++)
	{
		int prefLen = fileSystems[i].prefix.size();
		if (fileSystems[i].prefix == inpath.substr(0,prefLen))
		{
			outpath = inpath.substr(prefLen);
			*system = fileSystems[i].system;
			return true;
		}
	}
	return false;
}

void MetaFileSystem::Mount(std::string prefix, IFileSystem *system)
{
	System x;
	x.prefix=prefix;
	x.system=system;
	fileSystems.push_back(x);
}

void MetaFileSystem::UnmountAll()
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
	currentDirectory = "";
}

u32 MetaFileSystem::OpenFile(std::string filename, FileAccess access)
{
	std::string of;
	if (filename.find(':') == std::string::npos)
	{
		filename = currentDirectory + "/" + filename;
		DEBUG_LOG(HLE,"OpenFile: Expanded path to %s", filename.c_str());
	}
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
	if (filename.find(':') == std::string::npos)
	{
		filename = currentDirectory + "/" + filename;
		DEBUG_LOG(HLE,"GetFileInfo: Expanded path to %s", filename.c_str());
	}
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

std::vector<PSPFileInfo> MetaFileSystem::GetDirListing(std::string path)
{
	std::string of;
	if (path.find(':') == std::string::npos)
	{
		path = currentDirectory + "/" + path;
		DEBUG_LOG(HLE,"GetFileInfo: Expanded path to %s", path.c_str());
	}
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

bool MetaFileSystem::DeleteFile(const std::string &filename)
{
	std::string of;
	IFileSystem *system;
	if (MapFilePath(filename, of, &system))
	{
		return system->DeleteFile(of);
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

