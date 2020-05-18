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
#include "gfx_es2/gpu_features.h"

#include "Core/Config.h"
#include "GPU/Common/PostShader.h"

static std::vector<ShaderInfo> shaderInfo;

// Scans the directories for shader ini files and collects info about all the shaders found.
// Additionally, scan the VFS assets. (TODO)

void LoadPostShaderInfo(std::vector<std::string> directories) {
	std::vector<ShaderInfo> notVisible;

	shaderInfo.clear();
	ShaderInfo off{};
	off.visible = true;
	off.name = "Off";
	off.section = "Off";
	for (size_t i = 0; i < ARRAY_SIZE(off.settings); ++i) {
		off.settings[i].name = "";
		off.settings[i].value = 0.0f;
		off.settings[i].minValue = -1.0f;
		off.settings[i].maxValue = 1.0f;
		off.settings[i].step = 0.01f;
	}
	shaderInfo.push_back(off);

	auto appendShader = [&](const ShaderInfo &info) {
		auto beginErase = std::remove(shaderInfo.begin(), shaderInfo.end(), info.name);
		if (beginErase != shaderInfo.end()) {
			shaderInfo.erase(beginErase, shaderInfo.end());
		}
		shaderInfo.push_back(info);
	};

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
					section.Get("Parent", &info.parent, "");
					section.Get("Visible", &info.visible, true);
					section.Get("Fragment", &temp, "");
					info.fragmentShaderFile = path + "/" + temp;
					section.Get("Vertex", &temp, "");
					info.vertexShaderFile = path + "/" + temp;
					section.Get("OutputResolution", &info.outputResolution, false);
					section.Get("Upscaling", &info.isUpscalingFilter, false);
					section.Get("SSAA", &info.SSAAFilterLevel, 0);
					section.Get("60fps", &info.requires60fps, false);

					for (size_t i = 0; i < ARRAY_SIZE(info.settings); ++i) {
						auto &setting = info.settings[i];
						section.Get(StringFromFormat("SettingName%d", i + 1).c_str(), &setting.name, "");
						section.Get(StringFromFormat("SettingDefaultValue%d", i + 1).c_str(), &setting.value, 0.0f);
						section.Get(StringFromFormat("SettingMinValue%d", i + 1).c_str(), &setting.minValue, -1.0f);
						section.Get(StringFromFormat("SettingMaxValue%d", i + 1).c_str(), &setting.maxValue, 1.0f);
						section.Get(StringFromFormat("SettingStep%d", i + 1).c_str(), &setting.step, 0.01f);

						// Populate the default setting value.
						std::string section = StringFromFormat("%sSettingValue%d", info.section.c_str(), i + 1);
						if (!setting.name.empty() && g_Config.mPostShaderSetting.find(section) == g_Config.mPostShaderSetting.end()) {
							g_Config.mPostShaderSetting.insert(std::pair<std::string, float>(section, setting.value));
						}
					}

					// Let's ignore shaders we can't support. TODO: Not a very good check
					if (gl_extensions.IsGLES && !gl_extensions.GLES3) {
						bool requiresIntegerSupport;
						section.Get("RequiresIntSupport", &requiresIntegerSupport, false);
						if (requiresIntegerSupport)
							continue;
					}

					if (info.visible) {
						appendShader(info);
					} else {
						notVisible.push_back(info);
					}
				}
			}
		}
	}

	// We always want the not visible ones at the end.  Makes menus easier.
	for (const auto &info : notVisible) {
		appendShader(info);
	}
}

// Scans the directories for shader ini files and collects info about all the shaders found.
void ReloadAllPostShaderInfo() {
	std::vector<std::string> directories;
	directories.push_back("shaders");
	directories.push_back(g_Config.memStickDirectory + "PSP/shaders");
	LoadPostShaderInfo(directories);
}

const ShaderInfo *GetPostShaderInfo(const std::string &name) {
	for (size_t i = 0; i < shaderInfo.size(); i++) {
		if (shaderInfo[i].section == name)
			return &shaderInfo[i];
	}
	return nullptr;
}

std::vector<const ShaderInfo *> GetPostShaderChain(const std::string &name) {
	std::vector<const ShaderInfo *> backwards;
	const ShaderInfo *shaderInfo = GetPostShaderInfo(name);
	while (shaderInfo) {
		backwards.push_back(shaderInfo);

		if (!shaderInfo->parent.empty() && shaderInfo->parent != "Off") {
			shaderInfo = GetPostShaderInfo(shaderInfo->parent);
		} else {
			shaderInfo = nullptr;
		}
		auto dup = std::find(backwards.begin(), backwards.end(), shaderInfo);
		if (dup != backwards.end()) {
			// Don't loop forever.
			break;
		}
	}

	if (!backwards.empty())
		std::reverse(backwards.begin(), backwards.end());
	// Not backwards anymore.
	return backwards;
}

const std::vector<ShaderInfo> &GetAllPostShaderInfo() {
	return shaderInfo;
}
