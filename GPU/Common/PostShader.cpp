// Copyright (c) 2013- PPSSPP Project.

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


// Postprocessing shader manager

#include <string>
#include <vector>
#include <algorithm>

#include "file/ini_file.h"
#include "file/file_util.h"
#include "file/vfs.h"

#include "Core/Config.h"
#include "GPU/Common/PostShader.h"

static std::vector<ShaderInfo> shaderInfo;

// Scans the directories for shader ini files and collects info about all the shaders found.
// Additionally, scan the VFS assets. (TODO)

void LoadPostShaderInfo(std::vector<std::string> directories) {
	shaderInfo.clear();
	ShaderInfo off;
	off.name = "Off";
	off.section = "Off";
	off.outputResolution = false;
	shaderInfo.push_back(off);

	for (size_t d = 0; d < directories.size(); d++) {
		std::vector<FileInfo> fileInfo;
		getFilesInDir(directories[d].c_str(), &fileInfo, "ini:");

		if (fileInfo.size() == 0) {
			// TODO: Really gotta fix the filter, now it's gonna open shaders as ini files..
			VFSGetFileListing(directories[d].c_str(), &fileInfo, "ini:");
		}

		for (size_t f = 0; f < fileInfo.size(); f++) {
			IniFile ini;
			bool success = false;
			std::string name = fileInfo[f].fullName;
			std::string path = directories[d];
			// Hack around Android VFS path bug. really need to redesign this.
			if (name.substr(0, 7) == "assets/")
				name = name.substr(7);
			if (path.substr(0, 7) == "assets/")
				path = path.substr(7);

			if (ini.LoadFromVFS(name) || ini.Load(fileInfo[f].fullName)) {
				success = true;
				// vsh load. meh.
			}
			if (!success)
				continue;

			// Alright, let's loop through the sections and see if any is a shader.
			for (size_t i = 0; i < ini.Sections().size(); i++) {
				IniFile::Section &section = ini.Sections()[i];
				if (section.Exists("Fragment") && section.Exists("Vertex")) {
					// Valid shader!
					ShaderInfo info;
					std::string temp;
					info.section = section.name();
					section.Get("Name", &info.name, section.name().c_str());
					section.Get("Fragment", &temp, "");
					info.fragmentShaderFile = path + "/" + temp;
					section.Get("Vertex", &temp, "");
					info.vertexShaderFile = path + "/" + temp;
					section.Get("OutputResolution", &info.outputResolution, false);

#ifdef USING_GLES2
					// Let's ignore shaders we can't support. TODO: Check for GLES 3.0
					bool requiresIntegerSupport;
					section.Get("RequiresIntSupport", &requiresIntegerSupport, false);
					if (requiresIntegerSupport)
						continue;
#endif
					auto beginErase = std::find(shaderInfo.begin(), shaderInfo.end(), info.name);
					if (beginErase != shaderInfo.end()) {
						shaderInfo.erase(beginErase, shaderInfo.end());
					}
					shaderInfo.push_back(info);
				}
			}
		}
	}
}

// Scans the directories for shader ini files and collects info about all the shaders found.
void LoadAllPostShaderInfo() {
	std::vector<std::string> directories;
	directories.push_back("shaders");
	directories.push_back(g_Config.memStickDirectory + "PSP/shaders");
	LoadPostShaderInfo(directories);
}

const ShaderInfo *GetPostShaderInfo(std::string name) {
	LoadAllPostShaderInfo();
	for (size_t i = 0; i < shaderInfo.size(); i++) {
		if (shaderInfo[i].section == name)
			return &shaderInfo[i];
	}
	return 0;
}

const std::vector<ShaderInfo> &GetAllPostShaderInfo() {
	LoadAllPostShaderInfo();
	return shaderInfo;
}
