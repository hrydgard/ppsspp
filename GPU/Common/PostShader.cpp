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
#include "Common/File/DirListing.h"
#include "Common/File/VFS/VFS.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/thin3d.h"
#include "Common/StringUtils.h"

#include "Core/System.h"
#include "Core/Util/PathUtil.h"
#include "GPU/Common/PostShader.h"

static std::vector<ShaderInfo> shaderInfo;
// Okay, not really "post" shaders, but related.
static std::vector<TextureShaderInfo> textureShaderInfo;
static std::vector<TextureSamplingShaderInfo> textureSamplingShaderInfo;
static std::string textureSamplingShaderSource;

static Draw::GPUVendor VendorFromString(const std::string &vendor) {
	// TODO: This should probably be a function somewhere.
	if (vendor == "ARM") {
		return Draw::GPUVendor::VENDOR_ARM;
	} else if (vendor == "Qualcomm") {
		return Draw::GPUVendor::VENDOR_QUALCOMM;
	} else if (vendor == "IMGTEC") {
		return Draw::GPUVendor::VENDOR_IMGTEC;
	} else if (vendor == "NVIDIA") {
		return Draw::GPUVendor::VENDOR_NVIDIA;
	} else if (vendor == "AMD") {
		return Draw::GPUVendor::VENDOR_AMD;
	} else if (vendor == "Broadcom") {
		return Draw::GPUVendor::VENDOR_BROADCOM;
	} else if (vendor == "Apple") {
		return Draw::GPUVendor::VENDOR_APPLE;
	} else if (vendor == "Intel") {
		return Draw::GPUVendor::VENDOR_INTEL;
	} else if (vendor == "Mesa") {
		return Draw::GPUVendor::VENDOR_MESA;
	} else if (vendor == "Vivante") {
		return Draw::GPUVendor::VENDOR_VIVANTE;
	}
	return Draw::GPUVendor::VENDOR_UNKNOWN;
}

// Scans the directories for shader ini files and collects info about all the shaders found.

void LoadPostShaderInfo(Draw::DrawContext *draw, const std::vector<Path> &directories) {
	std::vector<ShaderInfo> notVisible;

	Draw::GPUVendor gpuVendor = Draw::GPUVendor::VENDOR_UNKNOWN;
	if (draw) {
		gpuVendor = draw->GetDeviceCaps().vendor;
	}

	shaderInfo.clear();
	textureShaderInfo.clear();
	textureSamplingShaderInfo.clear();

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

	auto appendSamplingShader = [&](const TextureSamplingShaderInfo &info) {
		auto beginErase = std::remove(textureSamplingShaderInfo.begin(), textureSamplingShaderInfo.end(), info.name);
		if (beginErase != textureSamplingShaderInfo.end()) {
			textureSamplingShaderInfo.erase(beginErase, textureSamplingShaderInfo.end());
		}
		textureSamplingShaderInfo.push_back(info);
	};

	for (size_t d = 0; d < directories.size(); d++) {
		std::vector<File::FileInfo> fileInfo;
		if (directories[d].IsAbsolute()) {
			// The memory stick's folder. The VFS would list it the same way, so an empty one got listed twice.
			File::GetFilesInDir(directories[d], &fileInfo, "ini:");
		} else {
			g_VFS.GetFileListing(directories[d].c_str(), &fileInfo, "ini:");
			if (fileInfo.empty()) {
				File::GetFilesInDir(directories[d], &fileInfo, "ini:");
			}
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
				std::string shaderType = "render";
				section.Get("Type", &shaderType);

				std::vector<std::string> vendorBlacklist;
				section.Get("VendorBlacklist", &vendorBlacklist);
				bool skipped = false;
				for (auto &item : vendorBlacklist) {
					const Draw::GPUVendor blacklistedVendor = VendorFromString(item);
					if (blacklistedVendor == gpuVendor && blacklistedVendor != Draw::GPUVendor::VENDOR_UNKNOWN) {
						skipped = true;
						break;
					}
				}
				if (skipped) {
					continue;
				}

				if (section.HasKey("Fragment") && section.HasKey("Vertex") &&
					(strncasecmp(shaderType.c_str(), "render", shaderType.size()) == 0 ||
					 strncasecmp(shaderType.c_str(), "StereoToMono", shaderType.size()) == 0)) {
					// Valid shader!
					ShaderInfo info{};
					std::string temp;
					info.section = section.name();
					info.name = section.name();
					info.visible = true;
					section.Get("Name", &info.name);
					section.Get("Parent", &info.parent);
					section.Get("Visible", &info.visible);
					std::string fragment;
					std::string vertex;
					section.Get("Fragment", &fragment);
					section.Get("Vertex", &vertex);
					// Shader inis come from downloads, so keep them to their own folder.
					if (HasParentDirComponent(fragment) || HasParentDirComponent(vertex)) {
						WARN_LOG(Log::G3D, "Ignoring shader '%s' with a parent dir in its paths", info.section.c_str());
						continue;
					}
					info.fragmentShaderFile = path / fragment;
					info.vertexShaderFile = path / vertex;
					section.Get("OutputResolution", &info.outputResolution);
					section.Get("Upscaling", &info.isUpscalingFilter);
					section.Get("SSAA", &info.SSAAFilterLevel);
					if (info.SSAAFilterLevel < 0 || info.SSAAFilterLevel > 8) {
						// It multiplies the render resolution, and shader inis come from downloads -
						// the neighbouring texture-shader "Scale" is bounded for the same reason.
						WARN_LOG(Log::G3D, "Ignoring out-of-range SSAA level %d in shader '%s'", info.SSAAFilterLevel, info.section.c_str());
						info.SSAAFilterLevel = 0;
					}
					section.Get("60fps", &info.requires60fps);
					section.Get("UsePreviousFrame", &info.usePreviousFrame);

					if (info.parent == "Off")
						info.parent.clear();

					if (strncasecmp(shaderType.c_str(), "stereotomono", shaderType.size()) == 0) {
						info.isStereo = true;
						info.isUpscalingFilter = false;
						info.parent.clear();
					}

					for (size_t i = 0; i < ARRAY_SIZE(info.settings); ++i) {
						auto &setting = info.settings[i];
						setting.name.clear();
						setting.value = 0.0;
						setting.minValue = -1.0f;
						setting.maxValue = 1.0f;
						setting.step = 0.01f;
						section.Get(StringFromFormat("SettingName%d", i + 1).c_str(), &setting.name);
						section.Get(StringFromFormat("SettingDefaultValue%d", i + 1).c_str(), &setting.value);
						section.Get(StringFromFormat("SettingMinValue%d", i + 1).c_str(), &setting.minValue);
						section.Get(StringFromFormat("SettingMaxValue%d", i + 1).c_str(), &setting.maxValue);
						section.Get(StringFromFormat("SettingStep%d", i + 1).c_str(), &setting.step);
					}

					// Let's ignore shaders we can't support. TODO: Not a very good check
					if (gl_extensions.IsGLES && !gl_extensions.GLES3) {
						bool requiresIntegerSupport = false;
						section.Get("RequiresIntSupport", &requiresIntegerSupport);
						if (requiresIntegerSupport)
							continue;
					}

					if (info.visible) {
						appendShader(info);
					} else {
						notVisible.push_back(info);
					}
				} else if (section.HasKey("Compute") && strncasecmp(shaderType.c_str(), "texture", shaderType.size()) == 0) {
					// This is a texture shader.
					TextureShaderInfo info{};
					std::string temp;
					info.section = section.name();
					info.name = section.name();
					info.scaleFactor = 0;
					section.Get("Name", &info.name);
					section.Get("Scale", &info.scaleFactor);
					bool hidden = false;
					section.Get("Hidden", &info.hidden);
					std::string cbufferFilename;
					if (section.Get("ConstantBuffer", &cbufferFilename)) {
						if (HasParentDirComponent(cbufferFilename)) {
							WARN_LOG(Log::G3D, "Ignoring texture shader '%s' with a parent dir in its paths", info.section.c_str());
							continue;
						}
						Path cbufferPath = path / cbufferFilename;
						info.constantBuffer = cbufferPath;
					}
					if (section.Get("Compute", &temp)) {
						if (HasParentDirComponent(temp)) {
							WARN_LOG(Log::G3D, "Ignoring texture shader '%s' with a parent dir in its paths", info.section.c_str());
							continue;
						}
						info.computeShaderFile = path / temp;
						info.computeShaderFiles.push_back(info.computeShaderFile);
						bool badPath = false;
						for (int computeIndex = 2; computeIndex <= 4; ++computeIndex) {
							temp.clear();
							if (section.Get(StringFromFormat("Compute%d", computeIndex).c_str(), &temp)) {
								if (HasParentDirComponent(temp)) {
									badPath = true;
								}
								info.computeShaderFiles.push_back(path / temp);
							}
						}
						if (badPath) {
							WARN_LOG(Log::G3D, "Ignoring texture shader '%s' with a parent dir in its paths", info.section.c_str());
						} else if (info.scaleFactor >= 2 && info.scaleFactor < 8) {
							appendTextureShader(info);
						}
					} else {
						ERROR_LOG(Log::G3D, "Compute field missing for compute shader");
					}
				} else if (section.HasKey("Sampling") && equalsNoCase(shaderType, "sampling")) {
					TextureSamplingShaderInfo info{};
					std::string temp;
					info.iniFile = fileInfo[f].fullName;
					info.section = section.name();
					info.name = section.name();
					section.Get("Name", &info.name);
					section.Get("Sampling", &temp);
					if (HasParentDirComponent(temp)) {
						WARN_LOG(Log::G3D, "Ignoring sampling shader '%s' with a parent dir in its path", info.section.c_str());
						continue;
					}
					info.file = path / temp;
					appendSamplingShader(info);
				} else if (!section.name().empty()) {
					WARN_LOG(Log::G3D, "Unrecognized shader type '%s' or invalid shader in section '%s'", shaderType.c_str(), section.name().c_str());
				}
			}
		}
	}

	// Sort shaders alphabetically.
	std::sort(shaderInfo.begin(), shaderInfo.end());
	std::sort(textureShaderInfo.begin(), textureShaderInfo.end());
	std::sort(textureSamplingShaderInfo.begin(), textureSamplingShaderInfo.end());

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

	TextureSamplingShaderInfo samplingOff{};
	samplingOff.name = "Off";
	samplingOff.section = "Off";
	textureSamplingShaderInfo.insert(textureSamplingShaderInfo.begin(), samplingOff);

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

const TextureSamplingShaderInfo *GetTextureSamplingShaderInfo(std::string_view name) {
	for (auto &info : textureSamplingShaderInfo) {
		if (info.section == name) {
			return &info;
		}
	}
	return nullptr;
}

const std::vector<TextureSamplingShaderInfo> &GetAllTextureSamplingShaderInfo() {
	return textureSamplingShaderInfo;
}

// Sampling shaders go into every textured fragment shader, whose buffer is fixed (32 KB), so comments and
// indentation are dropped. Line breaks stay, for preprocessor lines and error line numbers.
static std::string CompactShaderSource(std::string_view src) {
	std::string out;
	out.reserve(src.size());
	bool lineStart = true;
	for (size_t i = 0; i < src.size(); i++) {
		const char c = src[i];
		if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
			while (i + 1 < src.size() && src[i + 1] != '\n') {
				i++;
			}
		} else if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
			i += 2;
			while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) {
				if (src[i] == '\n') {
					out.push_back('\n');
				}
				i++;
			}
			i++;
		} else if (c == '\r') {
			// Dropped.
		} else if ((c == ' ' || c == '\t') && lineStart) {
			// Indentation, dropped.
		} else {
			out.push_back(c);
			lineStart = c == '\n';
		}
	}
	return out;
}

bool LoadTextureSamplingShader(Draw::DrawContext *draw, std::string_view name, bool *changed) {
	std::string source;
	if (!name.empty() && name != "Off") {
		if (!GetTextureSamplingShaderInfo(name)) {
			ReloadAllPostShaderInfo(draw);
		}
		const TextureSamplingShaderInfo *info = GetTextureSamplingShaderInfo(name);
		if (info && !info->file.empty()) {
			size_t size = 0;
			uint8_t *data = g_VFS.ReadFile(info->file.c_str(), &size);
			if (data) {
				source = CompactShaderSource(std::string_view((const char *)data, size));
				delete[] data;
				// Leaves room in the fragment shader buffer for the rest of the largest shaders.
				if (source.size() > 12 * 1024) {
					ERROR_LOG(Log::G3D, "Sampling shader '%s' is too large (%d bytes without comments, max 12 KB)", info->file.c_str(), (int)source.size());
					source.clear();
				}
			} else {
				ERROR_LOG(Log::G3D, "Failed to read sampling shader '%s'", info->file.c_str());
			}
		}
	}
	*changed = source != textureSamplingShaderSource;
	textureSamplingShaderSource = std::move(source);
	return !textureSamplingShaderSource.empty();
}

const std::string &GetTextureSamplingShaderSource() {
	return textureSamplingShaderSource;
}

void SetTextureSamplingShaderSource(std::string_view source) {
	textureSamplingShaderSource = source;
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
