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

#include "ppsspp_config.h"

#include <algorithm>

#include "UI/Theme.h"

#include "Common/File/FileUtil.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Data/Format/IniFile.h"
#include "Common/File/DirListing.h"
#include "Common/LogManager.h"

#include "Core/Config.h"

#include "Common/UI/View.h"
#include "Common/UI/Context.h"
#include "Core/System.h"

struct ThemeInfo {
	std::string name;

	uint32_t uItemStyleFg = 0xFFFFFFFF;
	uint32_t uItemStyleBg = 0x55000000;
	uint32_t uItemFocusedStyleFg = 0xFFFFFFFF;
	uint32_t uItemFocusedStyleBg = 0xFFEDC24C;
	uint32_t uItemDownStyleFg = 0xFFFFFFFF;
	uint32_t uItemDownStyleBg = 0xFFBD9939;
	uint32_t uItemDisabledStyleFg = 0x80EEEEEE;
	uint32_t uItemDisabledStyleBg = 0x55000000;

	uint32_t uHeaderStyleFg = 0xFFFFFFFF;
	uint32_t uInfoStyleFg = 0xFFFFFFFF;
	uint32_t uInfoStyleBg = 0x00000000;
	uint32_t uPopupTitleStyleFg = 0xFFE3BE59;
	uint32_t uPopupStyleFg = 0xFFFFFFFF;
	uint32_t uPopupStyleBg = 0xFF303030;
	uint32_t uBackgroundColor = 0xFF754D24;

	std::string sUIAtlas = "ui_atlas";

	bool operator == (const std::string &other) {
		return name == other;
	}
	bool operator == (const ThemeInfo &other) {
		return name == other.name;
	}
};

static UI::Theme ui_theme;
static std::vector<ThemeInfo> themeInfos;

static Atlas ui_atlas;
static Atlas font_atlas;

static void LoadThemeInfo(const std::vector<Path> &directories) {
	themeInfos.clear();
	ThemeInfo def{};
	def.name = "Default";
	themeInfos.push_back(def);

	// This will update the theme if already present, as such default in assets/theme will get priority if exist
	auto appendTheme = [&](const ThemeInfo &info) {
		auto beginErase = std::remove(themeInfos.begin(), themeInfos.end(), info.name);
		if (beginErase != themeInfos.end()) {
			themeInfos.erase(beginErase, themeInfos.end());
		}
		themeInfos.push_back(info);
	};

	for (size_t d = 0; d < directories.size(); d++) {
		std::vector<File::FileInfo> fileInfo;
		VFSGetFileListing(directories[d].c_str(), &fileInfo, "ini:");

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

			if (ini.LoadFromVFS(name.ToString()) || ini.Load(fileInfo[f].fullName)) {
				success = true;
				// vsh load. meh.
			}

			if (!success)
				continue;

			// Alright, let's loop through the sections and see if any is a themes.
			for (size_t i = 0; i < ini.Sections().size(); i++) {
				Section &section = ini.Sections()[i];
				ThemeInfo info;
				section.Get("Name", &info.name, section.name().c_str());

				section.Get("ItemStyleFg", &info.uItemStyleFg, info.uItemStyleFg);
				section.Get("ItemStyleBg", &info.uItemStyleBg, info.uItemStyleBg);
				section.Get("ItemFocusedStyleFg", &info.uItemFocusedStyleFg, info.uItemFocusedStyleFg);
				section.Get("ItemFocusedStyleBg", &info.uItemFocusedStyleBg, info.uItemFocusedStyleBg);
				section.Get("ItemDownStyleFg", &info.uItemDownStyleFg, info.uItemDownStyleFg);
				section.Get("ItemDownStyleBg", &info.uItemDownStyleBg, info.uItemDownStyleBg);
				section.Get("ItemDisabledStyleFg", &info.uItemDisabledStyleFg, info.uItemDisabledStyleFg);
				section.Get("ItemDisabledStyleBg", &info.uItemDisabledStyleBg, info.uItemDisabledStyleBg);

				section.Get("HeaderStyleFg", &info.uHeaderStyleFg, info.uHeaderStyleFg);
				section.Get("InfoStyleFg", &info.uInfoStyleFg, info.uInfoStyleFg);
				section.Get("InfoStyleBg", &info.uInfoStyleBg, info.uInfoStyleBg);
				section.Get("PopupTitleStyleFg", &info.uPopupTitleStyleFg, info.uPopupTitleStyleFg);
				section.Get("PopupStyleFg", &info.uPopupStyleFg, info.uPopupStyleFg);
				section.Get("PopupStyleBg", &info.uPopupStyleBg, info.uPopupStyleBg);
				section.Get("BackgroundColor", &info.uBackgroundColor, info.uBackgroundColor);

				std::string tmpPath;
				section.Get("UIAtlas", &tmpPath, "");
				if (tmpPath != "") {
					tmpPath = (path / tmpPath).ToString();

					File::FileInfo tmpInfo;
					if (VFSGetFileInfo((tmpPath+".meta").c_str(), &tmpInfo) && VFSGetFileInfo((tmpPath+".zim").c_str(), &tmpInfo)) {
						info.sUIAtlas = tmpPath;
					}
				}

				appendTheme(info);
			}
		}
	}
}

static UI::Style MakeStyle(uint32_t fg, uint32_t bg) {
	UI::Style s;
	s.background = UI::Drawable(bg);
	s.fgColor = fg;
	return s;
}

static void LoadAtlasMetadata(Atlas &metadata, const char *filename, bool required) {
	size_t atlas_data_size = 0;
	const uint8_t *atlas_data = VFSReadFile(filename, &atlas_data_size);
	bool load_success = atlas_data != nullptr && metadata.Load(atlas_data, atlas_data_size);
	if (!load_success) {
		if (required)
			ERROR_LOG(G3D, "Failed to load %s - graphics will be broken", filename);
		else
			WARN_LOG(G3D, "Failed to load %s", filename);
		// Stumble along with broken visuals instead of dying...
	}
	delete[] atlas_data;
}

void UpdateTheme(UIContext *ctx) {
	// First run, get the default in at least
	if (themeInfos.empty()) {
		ReloadAllThemeInfo();
	}

	size_t i;
	for (i = 0; i < themeInfos.size(); ++i) {
		if (themeInfos[i].name == g_Config.sThemeName) {
			break;
		}
	}

	// Reset to Default if not found
	if (i >= themeInfos.size()) {
		g_Config.sThemeName = "Default";
		i = 0;
	}

#if defined(USING_WIN_UI) || PPSSPP_PLATFORM(UWP) || defined(USING_QT_UI)
	ui_theme.uiFont = UI::FontStyle(FontID("UBUNTU24"), g_Config.sFont.c_str(), 22);
	ui_theme.uiFontSmall = UI::FontStyle(FontID("UBUNTU24"), g_Config.sFont.c_str(), 15);
	ui_theme.uiFontSmaller = UI::FontStyle(FontID("UBUNTU24"), g_Config.sFont.c_str(), 12);
#else
	ui_theme.uiFont = UI::FontStyle(FontID("UBUNTU24"), "", 20);
	ui_theme.uiFontSmall = UI::FontStyle(FontID("UBUNTU24"), "", 14);
	ui_theme.uiFontSmaller = UI::FontStyle(FontID("UBUNTU24"), "", 11);
#endif

	ui_theme.checkOn = ImageID("I_CHECKEDBOX");
	ui_theme.checkOff = ImageID("I_SQUARE");
	ui_theme.whiteImage = ImageID("I_SOLIDWHITE");
	ui_theme.sliderKnob = ImageID("I_CIRCLE");
	ui_theme.dropShadow4Grid = ImageID("I_DROP_SHADOW");

	// Actual configurable themes setting start here
	ui_theme.itemStyle = MakeStyle(themeInfos[i].uItemStyleFg, themeInfos[i].uItemStyleBg);
	ui_theme.itemFocusedStyle = MakeStyle(themeInfos[i].uItemFocusedStyleFg, themeInfos[i].uItemFocusedStyleBg);
	ui_theme.itemDownStyle = MakeStyle(themeInfos[i].uItemDownStyleFg, themeInfos[i].uItemDownStyleBg);
	ui_theme.itemDisabledStyle = MakeStyle(themeInfos[i].uItemDisabledStyleFg, themeInfos[i].uItemDisabledStyleBg);

	ui_theme.headerStyle.fgColor = themeInfos[i].uHeaderStyleFg;
	ui_theme.infoStyle = MakeStyle(themeInfos[i].uInfoStyleFg, themeInfos[i].uInfoStyleBg);

	ui_theme.popupTitle.fgColor = themeInfos[i].uPopupTitleStyleFg;
	ui_theme.popupStyle = MakeStyle(themeInfos[i].uPopupStyleFg, themeInfos[i].uPopupStyleBg);
	ui_theme.backgroundColor = themeInfos[i].uBackgroundColor;

	// Load any missing atlas metadata (the images are loaded from UIContext).
	LoadAtlasMetadata(ui_atlas, (themeInfos[i].sUIAtlas + ".meta").c_str(), true);
#if !(PPSSPP_PLATFORM(WINDOWS) || PPSSPP_PLATFORM(ANDROID))
	LoadAtlasMetadata(font_atlas, "font_atlas.meta", ui_atlas.num_fonts == 0);
#else
	LoadAtlasMetadata(font_atlas, "asciifont_atlas.meta", ui_atlas.num_fonts == 0);
#endif

	ctx->setUIAtlas(themeInfos[i].sUIAtlas + ".zim");
}

UI::Theme *GetTheme() {
	return &ui_theme;
}

Atlas *GetFontAtlas() {
	return &font_atlas;
}

Atlas *GetUIAtlas() {
	return &ui_atlas;
}

void ReloadAllThemeInfo() {
	std::vector<Path> directories;
	directories.push_back(Path("themes"));  // For VFS
	directories.push_back(GetSysDirectory(DIRECTORY_CUSTOM_THEMES));
	LoadThemeInfo(directories);
}

std::vector<std::string> GetThemeInfoNames() {
	std::vector<std::string> names;
	for (auto& i : themeInfos)
		names.push_back(i.name);

	return names;
}
