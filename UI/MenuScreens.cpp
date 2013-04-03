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

#include <cmath>
#include <string>
#include <cstdio>

#ifdef _WIN32
namespace MainWindow {
	void BrowseAndBoot(std::string defaultPath);
}
#endif

#ifdef _MSC_VER
#define snprintf _snprintf
#endif

#include "base/display.h"
#include "base/logging.h"
#include "base/colorutil.h"
#include "base/timeutil.h"
#include "base/NativeApp.h"
#include "gfx_es2/glsl_program.h"
#include "gfx_es2/gl_state.h"
#include "input/input_state.h"
#include "math/curves.h"
#include "ui/ui.h"
#include "ui/ui_context.h"
#include "ui_atlas.h"
#include "util/random/rng.h"
#include "util/text/utf8.h"
#include "UIShader.h"

#include "Common/StringUtil.h"
#include "Core/System.h"
#include "Core/CoreParameter.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"
#include "Core/Config.h"
#include "Core/CoreParameter.h"
#include "Core/SaveState.h"

#include "MenuScreens.h"
#include "EmuScreen.h"
#include "GameInfoCache.h"
#include "android/jni/TestRunner.h"

#ifdef USING_QT_UI
#include <QFileDialog>
#include <QFile>
#include <QDir>
#endif

// Ugly communication with NativeApp
extern std::string game_title;

static const int symbols[4] = {
	I_CROSS,
	I_CIRCLE,
	I_SQUARE,
	I_TRIANGLE
};

static const uint32_t colors[4] = {
	/*
	0xFF6666FF, // blue
	0xFFFF6666, // red
	0xFFFF66FF, // pink
	0xFF66FF66, // green
	*/
	0xC0FFFFFF,
	0xC0FFFFFF,
	0xC0FFFFFF,
	0xC0FFFFFF,
};

static void DrawBackground(float alpha) {
	static float xbase[100] = {0};
	static float ybase[100] = {0};
	static int last_dp_xres = 0;
	static int last_dp_yres = 0;
	if (xbase[0] == 0.0f || last_dp_xres != dp_xres || last_dp_yres != dp_yres) {
		GMRng rng;
		for (int i = 0; i < 100; i++) {
			xbase[i] = rng.F() * dp_xres;
			ybase[i] = rng.F() * dp_yres;
		}
		last_dp_xres = dp_xres;
		last_dp_yres = dp_yres;
	}
	glstate.depthWrite.set(GL_TRUE);
	glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0.1f,0.2f,0.43f,1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	ui_draw2d.DrawImageStretch(I_BG, 0, 0, dp_xres, dp_yres);
	float t = time_now();
	for (int i = 0; i < 100; i++) {
		float x = xbase[i];
		float y = ybase[i] + 40*cos(i * 7.2 + t * 1.3);
		float angle = sin(i + t);
		int n = i & 3;
		ui_draw2d.DrawImageRotated(symbols[n], x, y, 1.0f, angle, colorAlpha(colors[n], alpha * 0.1f));
	}
}

// For private alphas, etc.
void DrawWatermark() {
	// ui_draw2d.DrawTextShadow(UBUNTU24, "PRIVATE BUILD", dp_xres / 2, 10, 0xFF0000FF, ALIGN_HCENTER);
}

void LogoScreen::update(InputState &input_state) {
	frames_++;
	if (frames_ > 180 || input_state.pointer_down[0]) {
		if (bootFilename_.size()) {
			screenManager()->switchScreen(new EmuScreen(bootFilename_));
		} else {
			screenManager()->switchScreen(new MenuScreen());
		}
	}
}

void LogoScreen::sendMessage(const char *message, const char *value) {
	if (!strcmp(message, "boot")) {
		screenManager()->switchScreen(new EmuScreen(value));
	}
}

void LogoScreen::render() {
	float t = (float)frames_ / 60.0f;

	float alpha = t;
	if (t > 1.0f) alpha = 1.0f;
	float alphaText = alpha;
	if (t > 2.0f) alphaText = 3.0f - t;

	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(alpha);

	ui_draw2d.SetFontScale(1.5f, 1.5f);
	ui_draw2d.DrawText(UBUNTU48, "PPSSPP", dp_xres / 2, dp_yres / 2 - 30, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	ui_draw2d.SetFontScale(1.0f, 1.0f);
	ui_draw2d.DrawText(UBUNTU24, "Created by Henrik Rydgard", dp_xres / 2, dp_yres / 2 + 40, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	ui_draw2d.DrawText(UBUNTU24, "Free Software under GPL 2.0", dp_xres / 2, dp_yres / 2 + 70, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	ui_draw2d.DrawText(UBUNTU24, "www.ppsspp.org", dp_xres / 2, dp_yres / 2 + 130, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	if (bootFilename_.size()) {
		ui_draw2d.DrawText(UBUNTU24, bootFilename_.c_str(), dp_xres / 2, dp_yres / 2 + 180, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	}

	DrawWatermark();
	UIEnd();
}


// ==================
//		Menu Screen
// ==================

void MenuScreen::update(InputState &input_state) {
	globalUIState = UISTATE_MENU;
	frames_++;
}

void MenuScreen::sendMessage(const char *message, const char *value) {
	if (!strcmp(message, "boot")) {
		screenManager()->switchScreen(new EmuScreen(value));
	}
}

void MenuScreen::render() {
	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);

	double xoff = 150 - frames_ * frames_ * 0.4f;
	if (xoff < -20)
		xoff = -20;
	if (frames_ > 200)  // seems the above goes nuts after a while...
		xoff = -20;

	int w = LARGE_BUTTON_WIDTH + 40;

	ui_draw2d.DrawTextShadow(UBUNTU48, "PPSSPP", dp_xres + xoff - w/2, 75, 0xFFFFFFFF, ALIGN_HCENTER | ALIGN_BOTTOM);
	ui_draw2d.SetFontScale(0.7f, 0.7f);
	ui_draw2d.DrawTextShadow(UBUNTU24, PPSSPP_GIT_VERSION, dp_xres + xoff, 85, 0xFFFFFFFF, ALIGN_RIGHT | ALIGN_BOTTOM);
	ui_draw2d.SetFontScale(1.0f, 1.0f);
	VLinear vlinear(dp_xres + xoff, 100, 20);
	VLinear vlinear2(-xoff, 100, 20);

	if (UIButton(GEN_ID, vlinear, w, "Load...", ALIGN_RIGHT)) {
#if defined(USING_QT_UI)
		QString fileName = QFileDialog::getOpenFileName(NULL, "Load ROM", g_Config.currentDirectory.c_str(), "PSP ROMs (*.iso *.cso *.pbp *.elf)");
		if (QFile::exists(fileName)) {
			QDir newPath;
			g_Config.currentDirectory = newPath.filePath(fileName).toStdString();
			g_Config.Save();
			screenManager()->switchScreen(new EmuScreen(fileName.toStdString()));
		}
#elif _WIN32
		MainWindow::BrowseAndBoot("");
#else
		FileSelectScreenOptions options;
		options.allowChooseDirectory = true;
		options.filter = "iso:cso:pbp:elf:prx:";
		options.folderIcon = I_ICON_FOLDER;
		options.iconMapping["iso"] = I_ICON_UMD;
		options.iconMapping["cso"] = I_ICON_UMD;
		options.iconMapping["pbp"] = I_ICON_EXE;
		options.iconMapping["elf"] = I_ICON_EXE;
		screenManager()->switchScreen(new FileSelectScreen(options));
#endif
		UIReset();
	}

#ifndef _WIN32
	if (UIButton(GEN_ID, vlinear, w, "Settings", ALIGN_RIGHT)) {
		screenManager()->push(new SettingsScreen(), 0);
		UIReset();
	}
#endif

	if (UIButton(GEN_ID, vlinear, w, "Credits", ALIGN_RIGHT)) {
		screenManager()->switchScreen(new CreditsScreen());
		UIReset();
	}

#ifndef _WIN32
	if (UIButton(GEN_ID, vlinear, w, "Exit", ALIGN_RIGHT)) {
		// TODO: Save when setting changes, rather than when we quit
		NativeShutdown();
		// TODO: Need a more elegant way to quit
		exit(0);
	}
#endif

	if (UIButton(GEN_ID, vlinear, w, "www.ppsspp.org", ALIGN_RIGHT)) {
		LaunchBrowser("http://www.ppsspp.org/");
	}

	int recentW = 350;
	if (g_Config.recentIsos.size()) {
		ui_draw2d.DrawText(UBUNTU24, "Recent", -xoff, 80, 0xFFFFFFFF, ALIGN_BOTTOMLEFT);
	}
	for (size_t i = 0; i < g_Config.recentIsos.size(); i++) {
		std::string filename;
		std::string rec = g_Config.recentIsos[i];
		for (size_t j = 0; j < rec.size(); j++)
			if (rec[j] == '\\') rec[j] = '/';
		SplitPath(rec, nullptr, &filename, nullptr);

		UIContext *ctx = screenManager()->getUIContext();
		// This might create a texture so we must flush first.
		UIFlush();
		GameInfo *ginfo = g_gameInfoCache.GetInfo(g_Config.recentIsos[i], false);

		if (ginfo) {
			if (UITextureButton(ctx, GEN_ID_LOOP(i), vlinear2, 144, 80, ginfo->iconTexture, ALIGN_LEFT)) {
				screenManager()->switchScreen(new EmuScreen(g_Config.recentIsos[i]));
			}
		} else {
			if (UIButton(GEN_ID_LOOP(i), vlinear2, recentW, filename.c_str(), ALIGN_LEFT)) {
				screenManager()->switchScreen(new EmuScreen(g_Config.recentIsos[i]));
			}
		}
	}
	DrawWatermark();

	UIEnd();
}


void PauseScreen::update(InputState &input) {
	globalUIState = UISTATE_PAUSEMENU;
	if (input.pad_buttons_down & PAD_BUTTON_BACK) {
		screenManager()->finishDialog(this, DR_CANCEL);
	}
}

void PauseScreen::sendMessage(const char *msg, const char *value) {
	if (!strcmp(msg, "run")) {
		screenManager()->finishDialog(this, DR_CANCEL);
	}
}

void PauseScreen::render() {
	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);

	const char *title;
	// Try to ignore (tm) etc.
	if (UTF8StringNonASCIICount(game_title.c_str()) > 2) {
		title = "(can't display japanese title)";
	} else {
		title = game_title.c_str();
	}


	UIContext *ctx = screenManager()->getUIContext();
	// This might create a texture so we must flush first.
	UIFlush();
	GameInfo *ginfo = g_gameInfoCache.GetInfo(PSP_CoreParameter().fileToStart, true);

	if (ginfo && ginfo->pic1Texture) {
		ginfo->pic1Texture->Bind(0);
		ui_draw2d.DrawTexRect(0,0,dp_xres, dp_yres, 0,0,1,1,0xFFc0c0c0);
		ui_draw2d.Flush();
		ctx->RebindTexture();
	}

	if (ginfo && ginfo->pic0Texture) {
		ginfo->pic0Texture->Bind(0);
		// Pic0 is drawn in the bottom right corner, overlaying pic1.
		float sizeX = dp_xres / 480 * ginfo->pic0Texture->Width();
		float sizeY = dp_yres / 272 * ginfo->pic0Texture->Height();
		ui_draw2d.DrawTexRect(dp_xres - sizeX, dp_yres - sizeY, dp_xres, dp_yres, 0,0,1,1,0xFFc0c0c0);
		ui_draw2d.Flush();
		ctx->RebindTexture();
	}

	if (ginfo && ginfo->iconTexture) {
		ginfo->iconTexture->Bind(0);
		ui_draw2d.DrawTexRect(10,10,10+144, 10+80, 0,0,1,1,0xFFFFFFFF);
		ui_draw2d.Flush();
		ctx->RebindTexture();
	}

	ui_draw2d.DrawText(UBUNTU48, title, 10+144+10, 20, 0xFFFFFFFF, ALIGN_LEFT);

	int x = 30;
	int y = 50;
	int stride = 40;
	int columnw = 400;
	UICheckBox(GEN_ID, x, y += stride, "Show Debug Statistics", ALIGN_TOPLEFT, &g_Config.bShowDebugStats);
	UICheckBox(GEN_ID, x, y += stride, "Show FPS", ALIGN_TOPLEFT, &g_Config.bShowFPSCounter);

	// TODO: Maybe shouldn't show this if the screen ratios are very close...
	UICheckBox(GEN_ID, x, y += stride, "Stretch to display", ALIGN_TOPLEFT, &g_Config.bStretchToDisplay);

	UICheckBox(GEN_ID, x, y += stride, "Hardware Transform", ALIGN_TOPLEFT, &g_Config.bHardwareTransform);
	UICheckBox(GEN_ID, x, y += stride, "Linear Filtering", ALIGN_TOPLEFT, &g_Config.bLinearFiltering);
	if (UICheckBox(GEN_ID, x, y += stride, "Buffered Rendering", ALIGN_TOPLEFT, &g_Config.bBufferedRendering)) {
		if (gpu)
			gpu->Resized();
	}
	bool fs = g_Config.iFrameSkip == 1;
	UICheckBox(GEN_ID, x, y += stride, "Frameskip (beta)", ALIGN_TOPLEFT, &fs);
	g_Config.iFrameSkip = fs ? 1 : 0;

	// TODO: Add UI for more than one slot.
	HLinear hlinear1(x, y + 80, 20);
	if (UIButton(GEN_ID, hlinear1, LARGE_BUTTON_WIDTH, "Save State", ALIGN_LEFT)) {
		SaveState::SaveSlot(0, 0, 0);
		screenManager()->finishDialog(this, DR_CANCEL);
	}
	if (UIButton(GEN_ID, hlinear1, LARGE_BUTTON_WIDTH, "Load State", ALIGN_LEFT)) {
		SaveState::LoadSlot(0, 0, 0);
		screenManager()->finishDialog(this, DR_CANCEL);
	}

	VLinear vlinear(dp_xres - 10, 160, 20);
	if (UIButton(GEN_ID, vlinear, LARGE_BUTTON_WIDTH, "Continue", ALIGN_RIGHT)) {
		screenManager()->finishDialog(this, DR_CANCEL);
	}
#ifndef _WIN32
	if (UIButton(GEN_ID, vlinear, LARGE_BUTTON_WIDTH, "Settings", ALIGN_RIGHT)) {
		screenManager()->push(new SettingsScreen(), 0);
	}
#endif
	if (UIButton(GEN_ID, vlinear, LARGE_BUTTON_WIDTH, "Return to Menu", ALIGN_RIGHT)) {
		screenManager()->finishDialog(this, DR_OK);
	}

	/*
	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres - 10), LARGE_BUTTON_WIDTH*2, "Debug: Dump Next Frame", ALIGN_BOTTOMRIGHT)) {
		gpu->DumpNextFrame();
	}
	*/

	DrawWatermark();
	UIEnd();
}

void SettingsScreen::update(InputState &input) {
	globalUIState = UISTATE_MENU;
	if (input.pad_buttons_down & PAD_BUTTON_BACK) {
		g_Config.Save();
		screenManager()->finishDialog(this, DR_OK);
	}
}

void SettingsScreen::render() {
	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);

	ui_draw2d.DrawText(UBUNTU48, "Settings", dp_xres / 2, 20, 0xFFFFFFFF, ALIGN_HCENTER);

	// TODO: Need to add tabs soon...
	// VLinear vlinear(10, 80, 10);
	
	int x = 30;
	int y = 30;
	int stride = 40;
	int columnw = 400;
	UICheckBox(GEN_ID, x, y += stride, "Sound Emulation", ALIGN_TOPLEFT, &g_Config.bEnableSound);
	UICheckBox(GEN_ID, x + columnw, y, "MipMapping", ALIGN_TOPLEFT, &g_Config.bMipMap);
	if (UICheckBox(GEN_ID, x, y += stride, "Buffered Rendering", ALIGN_TOPLEFT, &g_Config.bBufferedRendering)) {
		if (gpu)
			gpu->Resized();
	}
	if (g_Config.bBufferedRendering) {
		bool doubleRes = g_Config.iWindowZoom == 2;
		if (UICheckBox(GEN_ID, x + columnw, y, "2x Render Resolution", ALIGN_TOPLEFT, &doubleRes)) {
			if (gpu)
				gpu->Resized();
		}
		g_Config.iWindowZoom = doubleRes ? 2 : 1;
	}
#ifndef __SYMBIAN32__
	UICheckBox(GEN_ID, x, y += stride, "Hardware Transform", ALIGN_TOPLEFT, &g_Config.bHardwareTransform);
	UICheckBox(GEN_ID, x + columnw, y, "Draw using Stream VBO", ALIGN_TOPLEFT, &g_Config.bUseVBO);
#endif
	UICheckBox(GEN_ID, x, y += stride, "Vertex Cache", ALIGN_TOPLEFT, &g_Config.bVertexCache);
	UICheckBox(GEN_ID, x + columnw, y, "Use Media Engine", ALIGN_TOPLEFT, &g_Config.bUseMediaEngine);

	UICheckBox(GEN_ID, x, y += stride, "JIT (Dynarec)", ALIGN_TOPLEFT, &g_Config.bJit);
	if (g_Config.bJit)
		UICheckBox(GEN_ID, x + columnw, y, "Fastmem (may be unstable)", ALIGN_TOPLEFT, &g_Config.bFastMemory);

	UICheckBox(GEN_ID, x, y += stride, "On-screen Touch Controls", ALIGN_TOPLEFT, &g_Config.bShowTouchControls);
	if (g_Config.bShowTouchControls) {
		UICheckBox(GEN_ID, x + columnw, y, "Large Controls", ALIGN_TOPLEFT, &g_Config.bLargeControls);
		UICheckBox(GEN_ID, x + columnw, y += stride, "Show Analog Stick", ALIGN_TOPLEFT, &g_Config.bShowAnalogStick);
	} else {
		y += stride;
	}
	UICheckBox(GEN_ID, x, y, "Tilt to Analog (horizontal)", ALIGN_TOPLEFT, &g_Config.bAccelerometerToAnalogHoriz);
	

	ui_draw2d.DrawText(UBUNTU24, "Some settings may require a restart to apply.", dp_xres/2, y += stride + 20, 0xFFFFFFFF, ALIGN_HCENTER);

	// UICheckBox(GEN_ID, x, y += stride, "Draw raw framebuffer (for some homebrew)", ALIGN_TOPLEFT, &g_Config.bDisplayFramebuffer);

	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres-10), LARGE_BUTTON_WIDTH, "Back", ALIGN_RIGHT | ALIGN_BOTTOM)) {
		screenManager()->finishDialog(this, DR_OK);
	}
	if (UIButton(GEN_ID, Pos(10, dp_yres-10), LARGE_BUTTON_WIDTH, "Developer Menu", ALIGN_BOTTOMLEFT)) {
		screenManager()->push(new DeveloperScreen());
	}

	UIEnd();
}

void DeveloperScreen::update(InputState &input) {
	if (input.pad_buttons_down & PAD_BUTTON_BACK) {
		g_Config.Save();
		screenManager()->finishDialog(this, DR_OK);
	}
}

void DeveloperScreen::render() {
	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);

	ui_draw2d.DrawText(UBUNTU48, "Developer Tools", dp_xres / 2, 20, 0xFFFFFFFF, ALIGN_HCENTER);

	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres-10), LARGE_BUTTON_WIDTH, "Back", ALIGN_RIGHT | ALIGN_BOTTOM)) {
		screenManager()->finishDialog(this, DR_OK);
	}

	if (UIButton(GEN_ID, Pos(dp_xres / 2, 100), LARGE_BUTTON_WIDTH, "Run CPU tests", ALIGN_CENTER | ALIGN_TOP)) {
		// TODO: Run tests
		RunTests();
		// screenManager()->push(new EmuScreen())
	}


	if (UIButton(GEN_ID, Pos(10, dp_yres-10), LARGE_BUTTON_WIDTH, "Dump frame to log", ALIGN_BOTTOMLEFT)) {
		gpu->DumpNextFrame();
	}

	UIEnd();
}


class FileListAdapter : public UIListAdapter {
public:
	FileListAdapter(const FileSelectScreenOptions &options, const std::vector<FileInfo> *items, UIContext *ctx) 
		: options_(options), items_(items), ctx_(ctx) {}
	virtual size_t getCount() const { return items_->size(); }
	virtual void drawItem(int item, int x, int y, int w, int h, bool active) const;

private:
	const FileSelectScreenOptions &options_;
	const std::vector<FileInfo> *items_;
	const UIContext *ctx_;
};

void FileListAdapter::drawItem(int item, int x, int y, int w, int h, bool selected) const
{
	int icon = -1;
	if ((*items_)[item].isDirectory) {
		icon = options_.folderIcon;
	} else {
		std::string extension = getFileExtension((*items_)[item].name);
		auto iter = options_.iconMapping.find(extension);
		if (iter != options_.iconMapping.end())
			icon = iter->second;
	}

	int iconSpace = this->itemHeight(item);
	ui_draw2d.DrawImage2GridH(selected ? I_BUTTON_SELECTED: I_BUTTON, x, y, x + w);
	ui_draw2d.DrawTextShadow(UBUNTU24, (*items_)[item].name.c_str(), x + UI_SPACE + iconSpace, y + 25, 0xFFFFFFFF, ALIGN_LEFT | ALIGN_VCENTER);

#if 0
	// This might create a texture so we must flush first.
	UIFlush();
	GameInfo *ginfo = 0;
	if (!(*items_)[item].isDirectory)
		ginfo = g_gameInfoCache.GetInfo((*items_)[item].fullName, false);
	if (ginfo && ginfo->iconTexture) {
		float scaled_w = h * (144.f / 80.f);
		UIFlush();
		ginfo->iconTexture->Bind(0);
		ui_draw2d.DrawTexRect(x + 10, y, x + 10 + scaled_w, y + h, 0, 0, 1, 1, 0xFFFFFFFF);
		ui_draw2d.Flush();
		ctx_->RebindTexture();
	} else {
		if (icon != -1)
			ui_draw2d.DrawImage(icon, x + UI_SPACE, y + 25, 1.0f, 0xFFFFFFFF, ALIGN_VCENTER | ALIGN_LEFT);
	}
#else
	if (icon != -1)
		ui_draw2d.DrawImage(icon, x + UI_SPACE, y + 25, 1.0f, 0xFFFFFFFF, ALIGN_VCENTER | ALIGN_LEFT);
#endif
}

FileSelectScreen::FileSelectScreen(const FileSelectScreenOptions &options) : options_(options) {
	currentDirectory_ = g_Config.currentDirectory;
#ifdef _WIN32
	// HACK
	// currentDirectory_ = "E:/PSP ISO/";
#endif
	updateListing();
}

void FileSelectScreen::updateListing() {
	listing_.clear();
	getFilesInDir(currentDirectory_.c_str(), &listing_, options_.filter);
	g_Config.currentDirectory = currentDirectory_;
	list_.contentChanged();
}

void FileSelectScreen::update(InputState &input_state) {
	if (input_state.pad_buttons_down & PAD_BUTTON_BACK) {
		g_Config.Save();
		screenManager()->switchScreen(new MenuScreen());
	}
}

void FileSelectScreen::render() {
	FileListAdapter adapter(options_, &listing_, screenManager()->getUIContext());

	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);

	if (list_.Do(GEN_ID, 10, BUTTON_HEIGHT + 20, dp_xres-20, dp_yres - BUTTON_HEIGHT - 30, &adapter)) {
		if (listing_[list_.selected].isDirectory) {
			currentDirectory_ = listing_[list_.selected].fullName;
			ILOG("%s", currentDirectory_.c_str());
			updateListing();
			list_.selected = -1;
		} else {
			std::string boot_filename = listing_[list_.selected].fullName;
			ILOG("Selected: %i : %s", list_.selected, boot_filename.c_str());
			list_.selected = -1;
			g_Config.Save();

			screenManager()->switchScreen(new EmuScreen(boot_filename));
		}
	}

	ui_draw2d.DrawImageStretch(I_BUTTON, 0, 0, dp_xres, 70);

	if (UIButton(GEN_ID, Pos(10,10), SMALL_BUTTON_WIDTH, "Up", ALIGN_TOPLEFT)) {
		currentDirectory_ = getDir(currentDirectory_);
		updateListing();
	}
	ui_draw2d.DrawTextShadow(UBUNTU24, currentDirectory_.c_str(), 20 + SMALL_BUTTON_WIDTH, 10 + 25, 0xFFFFFFFF, ALIGN_LEFT | ALIGN_VCENTER);
#ifndef ANDROID
	if (UIButton(GEN_ID, Pos(dp_xres - 10, 10), SMALL_BUTTON_WIDTH, "Back", ALIGN_RIGHT)) {
		g_Config.Save();
		screenManager()->switchScreen(new MenuScreen());
	}
#endif
	UIEnd();
}

void CreditsScreen::update(InputState &input_state) {
	globalUIState = UISTATE_MENU;
	if (input_state.pad_buttons_down & PAD_BUTTON_BACK) {
		screenManager()->switchScreen(new MenuScreen());
	}
	frames_++;
}

static const char * credits[] = {
	"PPSSPP",
	"",
	"",
	"A fast and portable PSP emulator",
	"",
	"Created by Henrik Rydgard",
	"",
	"Contributors:",
	"unknownbrackets",
	"orphis",
	"xsacha",
	"artart78",
	"tmaul",
	"ced2911",
	"soywiz",
	"kovensky",
	"raven02",
	"xele",
	"",
	"Written in C++ for speed and portability",
	"",
	"",
	"Free tools used:",
#ifdef ANDROID
	"Android SDK + NDK",
#elif defined(BLACKBERRY)
	"Blackberry NDK",
#endif
#if defined(USING_QT_UI)
	"Qt",
#else
	"SDL",
#endif
	"CMake",
	"freetype2",
	"zlib",
	"PSP SDK",
	"",
	"",
	"Check out the website:",
	"www.ppsspp.org",
	"compatibility lists, forums, and development info",
	"",
	"",
	"Also check out Dolphin, the best Wii/GC emu around:",
	"http://www.dolphin-emu.org",
	"",
	"",
	"PPSSPP is intended for educational purposes only.",
	"",
	"Please make sure that you own the rights to any games",
	"you play by owning the UMD or buying the digital",
	"download from the PSN store on your real PSP.",
	"",
	"",
	"PSP is a trademark by Sony, Inc.",
};

void CreditsScreen::render() {
	// TODO: This is kinda ugly, done on every frame...
	char temp[256];
	sprintf(temp, "PPSSPP %s", PPSSPP_GIT_VERSION);
	credits[0] = (const char *)temp;

	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);

	const int numItems = ARRAY_SIZE(credits);
	int itemHeight = 36;
	int totalHeight = numItems * itemHeight + dp_yres + 200;
	int y = dp_yres - (frames_ % totalHeight);
	for (int i = 0; i < numItems; i++) {
		float alpha = linearInOut(y+32, 64, dp_yres - 192, 64);
		if (alpha > 0.0f) {
			UIText(dp_xres/2, y, credits[i], whiteAlpha(alpha), ease(alpha), ALIGN_HCENTER);
		}
		y += itemHeight;
	}

	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres - 10), 200, "Back", ALIGN_BOTTOMRIGHT)) {
		screenManager()->switchScreen(new MenuScreen());
	}

	UIEnd();
}

void ErrorScreen::update(InputState &input_state) {
	if (input_state.pad_buttons_down & PAD_BUTTON_BACK) {
		screenManager()->finishDialog(this, DR_OK);
	}
}

void ErrorScreen::render()
{
	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);

	ui_draw2d.DrawText(UBUNTU48, errorTitle_.c_str(), dp_xres / 2, 30, 0xFFFFFFFF, ALIGN_HCENTER);
	ui_draw2d.DrawText(UBUNTU24, errorMessage_.c_str(), 40, 120, 0xFFFFFFFF, ALIGN_LEFT);

	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres - 10), 200, "Back", ALIGN_BOTTOMRIGHT)) {
		screenManager()->finishDialog(this, DR_OK);
	}

	UIEnd();
}
