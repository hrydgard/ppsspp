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

#include "Common/Log.h"
#include "Common/Data/Format/IniFile.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Common/File/VFS/VFS.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/thin3d.h"
#include "Common/StringUtils.h"

#include "Core/Config.h"
#include "Core/System.h"
#include "GPU/Common/PostShader.h"

static std::vector<ShaderInfo> shaderInfo;
// Okay, not really "post" shaders, but related.
static std::vector<TextureShaderInfo> textureShaderInfo;

// Scans the directories for shader ini files and collects info about all the shaders found.

void LoadPostShaderInfo(Draw::DrawContext *draw, const std::vector<Path> &directories) {
	std::vector<ShaderInfo> notVisible;

	Draw::GPUVendor gpuVendor = Draw::GPUVendor::VENDOR_UNKNOWN;
	if (draw) {
		gpuVendor = draw->GetDeviceCaps().vendor;
	}

	shaderInfo.clear();
	textureShaderInfo.clear();

	auto appendShader = [&](const ShaderInfo &info) {
		auto beginErase = std::remove(shaderInfo.begin(), shaderInfo.end(), info.name);
		if (beginErase != shaderInfo.end()) {
			shaderInfo.erase(beginErase, shaderInfo.end());
		}
		shaderInfo.push_back(info);
	};

	auto appendTextureShader = [&](const TextureShaderInfo &info) {
		auto beginErase = std::remove(textureShaderInfo.begin(), textureShaderInfo.end(), info.name);
		if (beginErase != textureShaderInfo.end()) {
			textureShaderInfo.erase(beginErase, textureShaderInfo.end());
		}
		textureShaderInfo.push_back(info);
	};

	for (size_t d = 0; d < directories.size(); d++) {
		std::vector<File::FileInfo> fileInfo;
		g_VFS.GetFileListing(directories[d].c_str(), &fileInfo, "ini:");

		if (fileInfo.empty()) {
			File::GetFilesInDir(directories[d], &fileInfo, "ini:");
		}

		for (size_t f = 0; f < fileInfo.size(); f++) {
			IniFile ini;
			bool success = false;
			if (fileInfo[f].isDirectory)
				continue;

			Path name = fileInfo[f].fullName;
			Path path = directories[d];
			// Hack around Android VFS path bug. really need to redesign this.
			if (name.ToString().substr(0, 7) == "assets/")
				name = Path(name.ToString().substr(7));
			if (path.ToString().substr(0, 7) == "assets/")
				path = Path(path.ToString().substr(7));

			if (ini.LoadFromVFS(g_VFS, name.ToString()) || ini.Load(fileInfo[f].fullName)) {
				success = true;
				// vsh load. meh.
			}

			if (!success)
				continue;

			// Alright, let's loop through the sections and see if any is a shader.
			for (size_t i = 0; i < ini.Sections().size(); i++) {
				Section &section = *(ini.Sections()[i].get());
				std::string shaderType;
				section.Get("Type", &shaderType, "render");

				std::vector<std::string> vendorBlacklist;
				section.Get("VendorBlacklist", vendorBlacklist);
				bool skipped = false;
				for (auto &item : vendorBlacklist) {
					Draw::GPUVendor blacklistedVendor = Draw::GPUVendor::VENDOR_UNKNOWN;
					// TODO: This should probably be a function somewhere.
					if (item == "ARM") {
						blacklistedVendor = Draw::GPUVendor::VENDOR_ARM;
					} else if (item == "Qualcomm") {
						blacklistedVendor = Draw::GPUVendor::VENDOR_QUALCOMM;
					} else if (item == "IMGTEC") {
						blacklistedVendor = Draw::GPUVendor::VENDOR_IMGTEC;
					} else if (item == "NVIDIA") {
						blacklistedVendor = Draw::GPUVendor::VENDOR_NVIDIA;
					} else if (item == "AMD") {
						blacklistedVendor = Draw::GPUVendor::VENDOR_AMD;
					} else if (item == "Broadcom") {
						blacklistedVendor = Draw::GPUVendor::VENDOR_BROADCOM;
					} else if (item == "Apple") {
						blacklistedVendor = Draw::GPUVendor::VENDOR_APPLE;
					} else if (item == "Intel") {
						blacklistedVendor = Draw::GPUVendor::VENDOR_INTEL;
					} else if (item == "Mesa") {
						blacklistedVendor = Draw::GPUVendor::VENDOR_MESA;
					}
					if (blacklistedVendor == gpuVendor && blacklistedVendor != Draw::GPUVendor::VENDOR_UNKNOWN) {
						skipped = true;
						break;
					}
				}

				if (skipped) {
					continue;
				}

				if (section.Exists("Fragment") && section.Exists("Vertex") &&
					(strncasecmp(shaderType.c_str(), "render", shaderType.size()) == 0 ||
					 strncasecmp(shaderType.c_str(), "StereoToMono", shaderType.size()) == 0)) {
					// Valid shader!
					ShaderInfo info{};
					std::string temp;
					info.section = section.name();

					section.Get("Name", &info.name, section.name().c_str());
					section.Get("Parent", &info.parent, "");
					section.Get("Visible", &info.visible, true);
					section.Get("Fragment", &temp, "");
					info.fragmentShaderFile = path / temp;
					section.Get("Vertex", &temp, "");
					info.vertexShaderFile = path / temp;
					section.Get("OutputResolution", &info.outputResolution, false);
					section.Get("Upscaling", &info.isUpscalingFilter, false);
					section.Get("SSAA", &info.SSAAFilterLevel, 0);
					section.Get("60fps", &info.requires60fps, false);
					section.Get("UsePreviousFrame", &info.usePreviousFrame, false);

					if (info.parent == "Off")
						info.parent.clear();

					if (strncasecmp(shaderType.c_str(), "stereotomono", shaderType.size()) == 0) {
						info.isStereo = true;
						info.isUpscalingFilter = false;
						info.parent.clear();
					}

					for (size_t i = 0; i < ARRAY_SIZE(info.settings); ++i) {
						auto &setting = info.settings[i];
						section.Get(StringFromFormat("SettingName%d", i + 1).c_str(), &setting.name, "");
						section.Get(StringFromFormat("SettingDefaultValue%d", i + 1).c_str(), &setting.value, 0.0f);
						section.Get(StringFromFormat("SettingMinValue%d", i + 1).c_str(), &setting.minValue, -1.0f);
						section.Get(StringFromFormat("SettingMaxValue%d", i + 1).c_str(), &setting.maxValue, 1.0f);
						section.Get(StringFromFormat("SettingStep%d", i + 1).c_str(), &setting.step, 0.01f);
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
				} else if (section.Exists("Compute") && strncasecmp(shaderType.c_str(), "texture", shaderType.size()) == 0) {
					// This is a texture shader.
					TextureShaderInfo info{};
					std::string temp;
					info.section = section.name();
					section.Get("Name", &info.name, section.name().c_str());
					section.Get("Compute", &temp, "");
					section.Get("Scale", &info.scaleFactor, 0);
					info.computeShaderFile = path / temp;
					if (info.scaleFactor >= 2 && info.scaleFactor < 8) {
						appendTextureShader(info);
					}
				} else if (!section.name().empty()) {
					WARN_LOG(Log::G3D, "Unrecognized shader type '%s' or invalid shader in section '%s'", shaderType.c_str(), section.name().c_str());
				}
			}
		}
	}

	// Sort shaders alphabetically.
	std::sort(shaderInfo.begin(), shaderInfo.end());
	std::sort(textureShaderInfo.begin(), textureShaderInfo.end());

	ShaderInfo off{};
	off.visible = true;
	off.name = "Off";
	off.section = "Off";
	for (size_t i = 0; i < ARRAY_SIZE(off.settings); ++i) {
		off.settings[i].name.clear();
		off.settings[i].value = 0.0f;
		off.settings[i].minValue = -1.0f;
		off.settings[i].maxValue = 1.0f;
		off.settings[i].step = 0.01f;
	}

	TextureShaderInfo textureOff{};
	textureOff.name = "Off";
	textureOff.section = "Off";
	textureShaderInfo.insert(textureShaderInfo.begin(), textureOff);

	// We always want the not visible ones at the end.  Makes menus easier.
	shaderInfo.reserve(notVisible.size() + 1);
	shaderInfo.insert(shaderInfo.begin(), off);
	for (const auto &info : notVisible) {
		appendShader(info);
	}
}

// Scans the directories for shader ini files and collects info about all the shaders found.
void ReloadAllPostShaderInfo(Draw::DrawContext *draw) {
	std::vector<Path> directories;
	directories.push_back(Path("shaders"));  // For VFS
	directories.push_back(GetSysDirectory(DIRECTORY_CUSTOM_SHADERS));
	LoadPostShaderInfo(draw, directories);
}

void RemoveUnknownPostShaders(std::vector<std::string> *names) {
	for (auto iter = names->begin(); iter != names->end(); ) {
		if (GetPostShaderInfo(*iter) == nullptr) {
			iter = names->erase(iter);
		} else {
			++iter;
		}
	}
}

const ShaderInfo *GetPostShaderInfo(std::string_view name) {
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

		if (!shaderInfo->parent.empty()) {
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

std::vector<const ShaderInfo *> GetFullPostShadersChain(const std::vector<std::string> &names) {
	std::vector<const ShaderInfo *> fullChain;
	for (const auto &shaderName : names) {
		const auto &shaderChain = GetPostShaderChain(shaderName);
		fullChain.insert(fullChain.end(), shaderChain.begin(), shaderChain.end());
	}
	return fullChain;
}

bool PostShaderChainRequires60FPS(const std::vector<const ShaderInfo *> &chain) {
	for (auto shaderInfo : chain) {
		if (shaderInfo->requires60fps)
			return true;
	}
	return false;
}

const std::vector<ShaderInfo> &GetAllPostShaderInfo() {
	return shaderInfo;
}

const TextureShaderInfo *GetTextureShaderInfo(std::string_view name) {
	for (auto &info : textureShaderInfo) {
		if (info.section == name) {
			return &info;
		}
	}
	return nullptr;
}
const std::vector<TextureShaderInfo> &GetAllTextureShaderInfo() {
	return textureShaderInfo;
}

void FixPostShaderOrder(std::vector<std::string> *names) {
	// There's one rule only that we enforce - only one shader can use UsePreviousFrame,
	// and it has to be the last one. So we simply remove any we find from the list,
	// and then append it to the end if there is one.
	std::string prevFrameShader;
	for (auto iter = names->begin(); iter != names->end(); ) {
		const ShaderInfo *info = GetPostShaderInfo(*iter);
		if (info) {
			if (info->usePreviousFrame) {
				prevFrameShader = *iter;
				iter = names->erase(iter++);
				continue;
			}
		}
		++iter;
	}

	if (!prevFrameShader.empty()) {
		names->push_back(prevFrameShader);
	}
}
