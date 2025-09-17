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
#include "Common/Log/LogManager.h"
#include "Common/Render/ManagedTexture.h"
#include "Common/Render/AtlasGen.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"

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
	uint32_t uHeaderStyleBg = 0x00000000;
	uint32_t uInfoStyleFg = 0xFFFFFFFF;
	uint32_t uInfoStyleBg = 0x00000000;
	uint32_t uPopupStyleFg = 0xFFFFFFFF;
	uint32_t uPopupStyleBg = 0xFF5E4D1F;
	uint32_t uPopupTitleStyleFg = 0xFFFFFFFF;
	uint32_t uPopupTitleStyleBg = 0x00000000;  // default to invisible
	uint32_t uTooltipStyleFg = 0xFFFFFFFF;
	uint32_t uTooltipStyleBg = 0xC0303030;
	uint32_t uCollapsibleHeaderStyleFg = 0xFFFFFFFF;
	uint32_t uCollapsibleHeaderStyleBg = 0x55000000;
	uint32_t uBackgroundColor = 0xFF754D24;
	uint32_t uScrollbarColor = 0x80FFFFFF;
	uint32_t uPopupSliderColor = 0xFFFFFFFF;
	uint32_t uPopupSliderFocusedColor = 0xFFEDC24C;

	bool operator == (const std::string &other) {
		return name == other;
	}
	bool operator == (const ThemeInfo &other) {
		return name == other.name;
	}
};

static UI::Theme ui_theme;
static std::vector<ThemeInfo> themeInfos;

// TODO: Don't really belong here.
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
			}

			if (!success)
				continue;

			// Alright, let's loop through the sections and see if any is a theme.
			for (size_t i = 0; i < ini.Sections().size(); i++) {
				Section &section = *(ini.Sections()[i].get());

				if (section.name().empty()) {
					continue;
				}

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
				section.Get("HeaderStyleBg", &info.uHeaderStyleBg, info.uHeaderStyleBg);
				section.Get("InfoStyleFg", &info.uInfoStyleFg, info.uInfoStyleFg);
				section.Get("InfoStyleBg", &info.uInfoStyleBg, info.uInfoStyleBg);
				section.Get("PopupStyleFg", &info.uPopupStyleFg, info.uItemStyleFg);  // Backwards compat
				section.Get("PopupStyleBg", &info.uPopupStyleBg, info.uPopupStyleBg);
				section.Get("TooltipStyleFg", &info.uTooltipStyleFg, info.uTooltipStyleFg);  // Backwards compat
				section.Get("TooltipStyleBg", &info.uTooltipStyleBg, info.uTooltipStyleBg);
				section.Get("PopupTitleStyleFg", &info.uPopupTitleStyleFg, info.uItemStyleFg);  // Backwards compat
				section.Get("PopupTitleStyleBg", &info.uPopupTitleStyleBg, info.uPopupTitleStyleBg);
				section.Get("CollapsibleHeaderStyleFg", &info.uCollapsibleHeaderStyleFg, info.uItemStyleFg);  // Backwards compat
				section.Get("CollapsibleHeaderStyleBg", &info.uCollapsibleHeaderStyleBg, info.uItemStyleBg);
				section.Get("BackgroundColor", &info.uBackgroundColor, info.uBackgroundColor);
				section.Get("ScrollbarColor", &info.uScrollbarColor, info.uScrollbarColor);
				section.Get("PopupSliderColor", &info.uPopupSliderColor, info.uPopupSliderColor);
				section.Get("PopupSliderFocusedColor", &info.uPopupSliderFocusedColor, info.uPopupSliderFocusedColor);

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

static void LoadAtlasMetadata(Atlas &metadata, const char *filename) {
	size_t atlas_data_size = 0;
	const uint8_t *atlas_data = g_VFS.ReadFile(filename, &atlas_data_size);
	bool load_success = atlas_data != nullptr && metadata.LoadMeta(atlas_data, atlas_data_size);
	if (!load_success) {
		ERROR_LOG(Log::G3D, "Failed to load %s - graphics may be broken", filename);
		// Stumble along with broken visuals instead of dying...
	}
	delete[] atlas_data;
}

void UpdateTheme() {
	// First run, get the default in at least
	if (themeInfos.empty()) {
		ReloadAllThemeInfo();
	}

	int defaultThemeIndex = -1;
	int selectedThemeIndex = -1;
	for (int i = 0; i < themeInfos.size(); ++i) {
		if (themeInfos[i].name == "Default") {
			defaultThemeIndex = i;
		}
		if (themeInfos[i].name == g_Config.sThemeName) {
			selectedThemeIndex = i;
		}
	}

	// Reset to Default if not found
	if (selectedThemeIndex < 0 || selectedThemeIndex >= themeInfos.size()) {
		g_Config.sThemeName = "Default";
		selectedThemeIndex = defaultThemeIndex;
		if (selectedThemeIndex < 0) {
			_dbg_assert_(false);
			// No themes? Bad.
			return;
		}
	}

#if defined(USING_WIN_UI) || PPSSPP_PLATFORM(UWP) || defined(USING_QT_UI)
	ui_theme.uiFont = UI::FontStyle(FontID("UBUNTU24"), g_Config.sFont.c_str(), 22);
	ui_theme.uiFontSmall = UI::FontStyle(FontID("UBUNTU24"), g_Config.sFont.c_str(), 17);
	ui_theme.uiFontBig = UI::FontStyle(FontID("UBUNTU24"), g_Config.sFont.c_str(), 28);
#else
	ui_theme.uiFont = UI::FontStyle(FontID("UBUNTU24"), "", 20);
	ui_theme.uiFontSmall = UI::FontStyle(FontID("UBUNTU24"), "", 15);
	ui_theme.uiFontBig = UI::FontStyle(FontID("UBUNTU24"), "", 26);
#endif

	ui_theme.checkOn = ImageID("I_CHECKEDBOX");
	ui_theme.checkOff = ImageID("I_SQUARE");
	ui_theme.whiteImage = ImageID("I_SOLIDWHITE");
	ui_theme.sliderKnob = ImageID("I_CIRCLE");
	ui_theme.dropShadow4Grid = ImageID("I_DROP_SHADOW");

	const ThemeInfo &themeInfo = themeInfos[selectedThemeIndex];

	// Actual configurable themes setting start here
	ui_theme.itemStyle = MakeStyle(themeInfo.uItemStyleFg, themeInfo.uItemStyleBg);
	ui_theme.itemFocusedStyle = MakeStyle(themeInfo.uItemFocusedStyleFg, themeInfo.uItemFocusedStyleBg);
	ui_theme.itemDownStyle = MakeStyle(themeInfo.uItemDownStyleFg, themeInfo.uItemDownStyleBg);
	ui_theme.itemDisabledStyle = MakeStyle(themeInfo.uItemDisabledStyleFg, themeInfo.uItemDisabledStyleBg);

	ui_theme.headerStyle = MakeStyle(themeInfo.uHeaderStyleFg, themeInfo.uHeaderStyleBg);
	ui_theme.collapsibleHeaderStyle = MakeStyle(themeInfo.uCollapsibleHeaderStyleFg, themeInfo.uCollapsibleHeaderStyleBg);
	ui_theme.infoStyle = MakeStyle(themeInfo.uInfoStyleFg, themeInfo.uInfoStyleBg);

	ui_theme.popupStyle = MakeStyle(themeInfo.uPopupStyleFg, themeInfo.uPopupStyleBg);
	ui_theme.popupTitleStyle = MakeStyle(themeInfo.uPopupTitleStyleFg, themeInfo.uPopupTitleStyleBg);

	ui_theme.tooltipStyle = MakeStyle(themeInfo.uTooltipStyleFg, themeInfo.uTooltipStyleBg);

	ui_theme.backgroundColor = themeInfo.uBackgroundColor;
	ui_theme.scrollbarColor = themeInfo.uScrollbarColor;

	ui_theme.popupSliderColor = themeInfo.uPopupSliderColor;
	ui_theme.popupSliderFocusedColor = themeInfo.uPopupSliderFocusedColor;
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
	for (const auto &info : themeInfos) {
		names.push_back(info.name);
	}
	return names;
}

Draw::Texture *GenerateUIAtlas(Draw::DrawContext *draw, Atlas *atlas) {
	// can't be const, yet...
	ImageDesc imageDescs[] = {
		{"I_SOLIDWHITE", "white.png"},
		{"I_CROSS", "cross.png"},
		{"I_CIRCLE", "circle.png"},
		{"I_SQUARE", "square.png"},
		{"I_TRIANGLE", "triangle.png"},
		{"I_SELECT", "select.png"},
		{"I_START", "start.png"},
		{"I_ARROW", "arrow.png"},
		{"I_DIR", "dir.png"},
		{"I_ROUND", "round.png"},
		{"I_RECT", "rect.png"},
		{"I_STICK", "stick.png"},
		{"I_STICK_BG", "stick_bg.png"},
		{"I_SHOULDER", "shoulder.png"},
		{"I_DIR_LINE", "dir_line.png"},
		{"I_ROUND_LINE", "round_line.png"},
		{"I_RECT_LINE", "rect_line.png"},
		{"I_SHOULDER_LINE", "shoulder_line.png"},
		{"I_STICK_LINE", "stick_line.png"},
		{"I_STICK_BG_LINE", "stick_bg_line.png"},
		{"I_CHECKEDBOX", "checkedbox.png"},
		{"I_BG", "background2.png"},
		{"I_L", "L.png"},
		{"I_R", "R.png"},
		{"I_DROP_SHADOW", "dropshadow.png"},
		{"I_LINES", "lines.png"},
		{"I_GRID", "grid.png"},
		{"I_LOGO", "logo.png"},
		{"I_ICON", "icon_regular_72.png"},
		{"I_ICONGOLD", "icon_gold_72.png"},
		{"I_FOLDER", "folder_line.png"},
		{"I_UP_DIRECTORY", "up_line.png"},
		{"I_GEAR", "gear.png"},
		{"I_1", "1.png"},
		{"I_2", "2.png"},
		{"I_3", "3.png"},
		{"I_4", "4.png"},
		{"I_5", "5.png"},
		{"I_6", "6.png"},
		{"I_PSP_DISPLAY", "psp_display.png"},
		{"I_FLAG_JP", "flag_jp.png"},
		{"I_FLAG_US", "flag_us.png"},
		{"I_FLAG_EU", "flag_eu.png"},
		{"I_FLAG_HK", "flag_hk.png"},
		{"I_FLAG_AS", "flag_as.png"},
		{"I_FLAG_KO", "flag_ko.png"},
		{"I_FULLSCREEN", "fullscreen.png"},
		{"I_RESTORE", "restore.png"},
		{"I_SDCARD", "sdcard.png"},
		{"I_HOME", "home.png"},
		{"I_A", "a.png"},
		{"I_B", "b.png"},
		{"I_C", "c.png"},
		{"I_D", "d.png"},
		{"I_E", "e.png"},
		{"I_F", "f.png"},
		{"I_SQUARE_SHAPE", "square_shape.png"},
		{"I_SQUARE_SHAPE_LINE", "square_shape_line.png"},
		{"I_FOLDER_OPEN", "folder_open_line.png"},
		{"I_WARNING", "warning.png"},
		{"I_TRASHCAN", "trashcan.png"},
		{"I_PLUS", "plus.png"},
		{"I_ROTATE_LEFT", "rotate_left.png"},
		{"I_ROTATE_RIGHT", "rotate_right.png"},
		{"I_ARROW_LEFT", "arrow_left.png"},
		{"I_ARROW_RIGHT", "arrow_right.png"},
		{"I_ARROW_UP", "arrow_up.png"},
		{"I_ARROW_DOWN", "arrow_down.png"},
		{"I_SLIDERS", "sliders.png"},
		{"I_THREE_DOTS", "three_dots.png"},
		{"I_INFO", "info.png"},
		{"I_RETROACHIEVEMENTS_LOGO", "retroachievements_logo.png"},
		{"I_CHECKMARK", "checkmark.png"},
		{"I_PLAY", "play.png"},
		{"I_STOP", "stop.png"},
		{"I_PAUSE", "pause.png"},
		{"I_FASTFORWARD", "fast_forward.png"},
		{"I_RECORD", "record.png"},
		{"I_SPEAKER", "speaker.png"},
		{"I_SPEAKER_MAX", "speaker_max.png"},
		{"I_SPEAKER_OFF", "speaker_off.png"},
		{"I_WINNER_CUP", "winner_cup.png"},
		{"I_EMPTY", "empty.png"},
	};


	Bucket bucket;

	// Script fully read, now read images and rasterize the fonts.
	Image images[ARRAY_SIZE(imageDescs)];

	Instant start = Instant::Now();

	for (int i = 0; i < ARRAY_SIZE(imageDescs); i++) {
		imageDescs[i].result_index = i;

		Image &img = images[i];

		bool success = true;
		if (equals(imageDescs[i].fileName, "white.png")) {
			img.resize(16, 16);
			img.fill(0xFFFFFFFF);
		} else {
			std::string name = "ui_images/";
			name.append(imageDescs[i].fileName);
			bool success = img.LoadPNG(name.c_str());
			if (!success) {
				ERROR_LOG(Log::G3D, "Failed to load %s\n", name.c_str());
			}
		}
	}
	INFO_LOG(Log::G3D, " - Loaded %zu images in %.2f ms\n", bucket.data.size(), start.ElapsedMs());

	Instant addStart = Instant::Now();
	for (int i = 0; i < ARRAY_SIZE(imageDescs); i++) {
		bucket.AddImage(std::move(images[i]), i);
	}

	INFO_LOG(Log::G3D, " - Added %zu images in %.2f ms\n", bucket.data.size(), addStart.ElapsedMs());

	int image_width = 512;
	Image dest;

	Instant bucketStart = Instant::Now();
	std::vector<Data> results = bucket.Resolve(image_width, dest);
	INFO_LOG(Log::G3D, " - Bucketed %zu images in %.2f ms\n", results.size(), bucketStart.ElapsedMs());

	// Fill out the atlas structure.
	std::vector<AtlasImage> genAtlasImages;
	genAtlasImages.reserve(ARRAY_SIZE(imageDescs));
	for (int i = 0; i < ARRAY_SIZE(imageDescs); i++) {
		genAtlasImages.push_back(imageDescs[i].ToAtlasImage(imageDescs[i].result_index, (float)dest.width(), (float)dest.height(), results));
	}

	atlas->Clear();
	atlas->images = new AtlasImage[genAtlasImages.size()];
	std::copy(genAtlasImages.begin(), genAtlasImages.end(), atlas->images);
	atlas->num_images = (int)genAtlasImages.size();

	// For debug, write out the atlas.
	// dest.SavePNG("../gen.png");

	// Then, create the texture too.
	Draw::TextureDesc desc{};
	desc.width = image_width;
	desc.height = dest.height();
	desc.depth = 1;
	desc.mipLevels = 1;
	desc.format = Draw::DataFormat::R8G8B8A8_UNORM;
	desc.type = Draw::TextureType::LINEAR2D;
	desc.initData.push_back((const u8 *)dest.data());
	desc.tag = "UIAtlas";

	INFO_LOG(Log::G3D, "UI atlas generated in %.2f ms, size %dx%d with %zu images\n", start.ElapsedMs(), desc.width, desc.height, genAtlasImages.size());
	return draw->CreateTexture(desc);
}

AtlasData AtlasProvider(Draw::DrawContext *draw, AtlasChoice atlas) {
	switch (atlas) {
	case AtlasChoice::General:
	{
		// Generate the atlas from scratch.
		Draw::Texture *tex = GenerateUIAtlas(draw, &ui_atlas);
		return { &ui_atlas, tex };
	}
	case AtlasChoice::Font:
	{
		Draw::Texture *fontTexture = nullptr;
#if PPSSPP_PLATFORM(WINDOWS) || PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
		// Load the smaller ascii font only, like on Android. For debug ui etc.
		// NOTE: We better be sure here that the correct metadata is loaded..
		LoadAtlasMetadata(font_atlas, "asciifont_atlas.meta");
		fontTexture = CreateTextureFromFile(draw, "asciifont_atlas.zim", ImageFileType::ZIM, false);
		if (!fontTexture) {
			WARN_LOG(Log::System, "Failed to load font_atlas.zim or asciifont_atlas.zim");
		}
#else
		// Load the full font texture.
		LoadAtlasMetadata(font_atlas, "font_atlas.meta");
		fontTexture = CreateTextureFromFile(draw, "font_atlas.zim", ImageFileType::ZIM, false);
#endif
		return {
			&font_atlas,
			fontTexture,
		};
	}
	default:
		return {};
	};
}
