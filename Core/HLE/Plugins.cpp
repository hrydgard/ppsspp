// Copyright (c) 2020- PPSSPP Project.

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

#include "Common/Data/Format/IniFile.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HLE/Plugins.h"
#include "Core/HLE/sceKernelModule.h"

namespace HLEPlugins {

static bool anyEnabled = false;
static std::vector<std::string> prxPlugins;

enum class PluginType {
	INVALID = 0,
	PRX,
};

struct PluginInfo {
	PluginType type;
	std::string filename;
	int version;
	uint32_t memory;
};

static PluginInfo ReadPluginIni(const std::string &subdir, IniFile &ini) {
	PluginInfo info;

	auto options = ini.GetOrCreateSection("options");
	std::string value;

	if (options->Get("type", &value, "")) {
		if (value == "prx") {
			info.type = PluginType::PRX;
		}
	}

	if (options->Get("filename", &value, "")) {
		info.filename = "ms0:/PSP/PLUGINS/" + subdir + "/" + value;
	} else {
		info.type = PluginType::INVALID;
	}

	options->Get("version", &info.version, 0);
	options->Get("memory", &info.memory, 0);
	if (info.memory > 93) {
		ERROR_LOG(SYSTEM, "Plugin memory too high, using 93 MB");
		info.memory = 93;
	}

	if (info.version == 0) {
		ERROR_LOG(SYSTEM, "Plugin without version ignored: %s", subdir.c_str());
		info.type = PluginType::INVALID;
		info.memory = 0;
	} else if (info.type == PluginType::INVALID && !info.filename.empty()) {
		ERROR_LOG(SYSTEM, "Plugin without valid type: %s", subdir.c_str());
	}

	return info;
}

static std::vector<PluginInfo> FindPlugins(const std::string &gameID, const std::string &lang) {
	std::vector<File::FileInfo> pluginDirs;
	GetFilesInDir(GetSysDirectory(DIRECTORY_PLUGINS), &pluginDirs);

	std::vector<PluginInfo> found;
	for (const auto &subdir : pluginDirs) {
		const Path &subdirFullName = subdir.fullName;
		if (!subdir.isDirectory || !File::Exists(subdirFullName / "plugin.ini"))
			continue;

		IniFile ini;
		if (!ini.Load(subdirFullName / "plugin.ini")) {
			ERROR_LOG(SYSTEM, "Failed to load plugin ini: %s/plugin.ini", subdirFullName.c_str());
			continue;
		}

		std::set<std::string> matches;

		std::string gameIni;
		if (ini.GetOrCreateSection("games")->Get("ALL", &gameIni, "")) {
			if (!strcasecmp(gameIni.c_str(), "true")) {
				matches.insert("plugin.ini");
			} else if (!gameIni.empty()) {
				matches.insert(gameIni);
			}
		}

		if (ini.GetOrCreateSection("games")->Get(gameID.c_str(), &gameIni, "")) {
			if (!strcasecmp(gameIni.c_str(), "true")) {
				matches.insert("plugin.ini");
			} else if (!gameIni.empty()) {
				matches.insert(gameIni);
			}
		}

		std::set<std::string> langMatches;
		for (const std::string &subini : matches) {
			if (!ini.Load(subdirFullName / subini)) {
				ERROR_LOG(SYSTEM, "Failed to load plugin ini: %s/%s", subdirFullName.c_str(), subini.c_str());
				continue;
			}

			found.push_back(ReadPluginIni(subdir.name, ini));

			if (ini.GetOrCreateSection("lang")->Get(lang.c_str(), &gameIni, "")) {
				if (!gameIni.empty() && matches.find(gameIni) == matches.end()) {
					langMatches.insert(gameIni);
				}
			}
		}

		for (const std::string &subini : langMatches) {
			if (!ini.Load(subdirFullName / subini)) {
				ERROR_LOG(SYSTEM, "Failed to load plugin ini: %s/%s", subdirFullName.c_str(), subini.c_str());
				continue;
			}

			found.push_back(ReadPluginIni(subdir.name, ini));
		}
	}

	return found;
}

void Init() {
	if (!g_Config.bLoadPlugins) {
		return;
	}

	std::vector<PluginInfo> plugins = FindPlugins(g_paramSFO.GetDiscID(), g_Config.sLanguageIni);
	for (auto &plugin : plugins) {
		if (plugin.memory << 20 > Memory::g_MemorySize) {
			Memory::g_MemorySize = plugin.memory << 20;
			anyEnabled = true;
		}

		if (plugin.type == PluginType::PRX) {
			prxPlugins.push_back(plugin.filename);
			anyEnabled = true;
		}
	}
}

bool Load() {
	bool started = false;
	for (const std::string &filename : prxPlugins) {
		std::string error_string = "";
		SceUID module = KernelLoadModule(filename, &error_string);
		if (!error_string.empty()) {
			ERROR_LOG(SYSTEM, "Unable to load plugin %s: %s", filename.c_str(), error_string.c_str());
			continue;
		}
		if (module < 0) {
			ERROR_LOG(SYSTEM, "Unable to load plugin %s: %08x", filename.c_str(), module);
			continue;
		}

		int ret = KernelStartModule(module, 0, 0, 0, nullptr, nullptr);
		if (ret < 0) {
			ERROR_LOG(SYSTEM, "Unable to start plugin %s: %08x", filename.c_str(), ret);
		}

		INFO_LOG(SYSTEM, "Loaded plugin: %s", filename.c_str());
		started = true;
	}

	return started;
}

void Unload() {
	// Nothing to do here, for now.
}

void Shutdown() {
	prxPlugins.clear();
	anyEnabled = false;
}

void DoState(PointerWrap &p) {
	auto s = p.Section("Plugins", 0, 1);
	if (!s)
		return;

	// Remember if any were enabled.
	Do(p, anyEnabled);
}

bool HasEnabled() {
	return anyEnabled;
}

};
