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
#include <functional>

#include "Common/Render/DrawBuffer.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/UI.h"

#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/Math/curves.h"
#include "Common/File/VFS/VFS.h"

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Random/Rng.h"
#include "Common/TimeUtil.h"
#include "Common/File/FileUtil.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/HLE/sceUtility.h"
#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"
#include "GPU/Common/PostShader.h"

#include "UI/ControlMappingScreen.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/EmuScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/GameSettingsScreen.h"
#include "UI/MainScreen.h"
#include "UI/MiscScreens.h"
#include "UI/MemStickScreen.h"

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

static const ImageID symbols[4] = {
	ImageID("I_CROSS"),
	ImageID("I_CIRCLE"),
	ImageID("I_SQUARE"),
	ImageID("I_TRIANGLE"),
};

static const uint32_t colors[4] = {
	0xC0FFFFFF,
	0xC0FFFFFF,
	0xC0FFFFFF,
	0xC0FFFFFF,
};

static std::unique_ptr<ManagedTexture> bgTexture;

class Animation {
public:
	virtual ~Animation() {}
	virtual void Draw(UIContext &dc, double t, float alpha, float x, float y, float z) = 0;
};

static constexpr float XFAC = 0.3f;
static constexpr float YFAC = 0.3f;
static constexpr float ZFAC = 0.12f;
static constexpr float XSPEED = 0.05f;
static constexpr float YSPEED = 0.05f;
static constexpr float ZSPEED = 0.1f;

class MovingBackground : public Animation {
public:
	void Draw(UIContext &dc, double t, float alpha, float x, float y, float z) override {
		if (!bgTexture)
			return;

		dc.Flush();
		dc.GetDrawContext()->BindTexture(0, bgTexture->GetTexture());
		Bounds bounds = dc.GetBounds();

		x = std::min(std::max(x/bounds.w, 0.0f), 1.0f) * XFAC;
		y = std::min(std::max(y/bounds.h, 0.0f), 1.0f) * YFAC;
		z = 1.0f + std::max(XFAC, YFAC) + (z-1.0f) * ZFAC;

		lastX_ = abs(x-lastX_) > 0.001f ? x*XSPEED+lastX_*(1.0f-XSPEED) : x;
		lastY_ = abs(y-lastY_) > 0.001f ? y*YSPEED+lastY_*(1.0f-YSPEED) : y;
		lastZ_ = abs(z-lastZ_) > 0.001f ? z*ZSPEED+lastZ_*(1.0f-ZSPEED) : z;

		float u1 = lastX_/lastZ_;
		float v1 = lastY_/lastZ_;
		float u2 = (1.0f+lastX_)/lastZ_;
		float v2 = (1.0f+lastY_)/lastZ_;

		dc.Draw()->DrawTexRect(bounds, u1, v1, u2, v2, whiteAlpha(alpha));

		dc.Flush();
		dc.RebindTexture();
	}

private:
	float lastX_ = 0.0f;
	float lastY_ = 0.0f;
	float lastZ_ = 1.0f + std::max(XFAC, YFAC);
};

class WaveAnimation : public Animation {
public:
	void Draw(UIContext &dc, double t, float alpha, float x, float y, float z) override {
		const uint32_t color = colorAlpha(0xFFFFFFFF, alpha * 0.2f);
		const float speed = 1.0;

		Bounds bounds = dc.GetBounds();
		dc.Flush();
		dc.BeginNoTex();

		// Be sure to not overflow our vertex buffer
		const float step = ceil(24*bounds.w/pixel_in_dps_x) > MAX_VERTS ? 24*bounds.w/(MAX_VERTS-48) : pixel_in_dps_x;

		t *= speed;
		for (float x = 0; x < bounds.w; x += step) {
			float i = x * 1280/bounds.w;

			float wave0 = sin(i*0.005+t*0.8)*0.05 + sin(i*0.002+t*0.25)*0.02 + sin(i*0.001+t*0.3)*0.03 + 0.625;
			float wave1 = sin(i*0.0044+t*0.4)*0.07 + sin(i*0.003+t*0.1)*0.02 + sin(i*0.001+t*0.3)*0.01 + 0.625;
			dc.Draw()->RectVGradient(x, wave0*bounds.h, step, (1.0-wave0)*bounds.h, color, 0x00000000);
			dc.Draw()->RectVGradient(x, wave1*bounds.h, step, (1.0-wave1)*bounds.h, color, 0x00000000);

			// Add some "antialiasing"
			dc.Draw()->RectVGradient(x, wave0*bounds.h-3*pixel_in_dps_y, step, 3*pixel_in_dps_y, 0x00000000, color);
			dc.Draw()->RectVGradient(x, wave1*bounds.h-3*pixel_in_dps_y, step, 3*pixel_in_dps_y, 0x00000000, color);
		}

		dc.Flush();
		dc.Begin();
	}
};

class FloatingSymbolsAnimation : public Animation {
public:
	void Draw(UIContext &dc, double t, float alpha, float x, float y, float z) override {
		float xres = dc.GetBounds().w;
		float yres = dc.GetBounds().h;
		if (last_xres != xres || last_yres != yres) {
			Regenerate(xres, yres);
		}

		for (int i = 0; i < COUNT; i++) {
			float x = xbase[i] + dc.GetBounds().x;
			float y = ybase[i] + dc.GetBounds().y + 40 * cosf(i * 7.2f + t * 1.3f);
			float angle = (float)sin(i + t);
			int n = i & 3;
			ui_draw2d.DrawImageRotated(symbols[n], x, y, 1.0f, angle, colorAlpha(colors[n], alpha * 0.1f));
		}
	}

private:
	static constexpr int COUNT = 100;

	float xbase[COUNT]{};
	float ybase[COUNT]{};
	float last_xres = 0;
	float last_yres = 0;

	void Regenerate(int xres, int yres) {
		GMRng rng;
		for (int i = 0; i < COUNT; i++) {
			xbase[i] = rng.F() * xres;
			ybase[i] = rng.F() * yres;
		}

		last_xres = xres;
		last_yres = yres;
	}
};

class RecentGamesAnimation : public Animation {
public:
	void Draw(UIContext &dc, double t, float alpha, float x, float y, float z) override {
		if (lastIndex_ == nextIndex_) {
			CheckNext(dc, t);
		} else if (t > nextT_) {
			lastIndex_ = nextIndex_;
		}

		if (g_Config.HasRecentIsos()) {
			std::shared_ptr<GameInfo> lastInfo = GetInfo(dc, lastIndex_);
			std::shared_ptr<GameInfo> nextInfo = GetInfo(dc, nextIndex_);
			dc.Flush();

			float lastAmount = Clamp((float)(nextT_ - t) * 1.0f / TRANSITION, 0.0f, 1.0f);
			DrawTex(dc, lastInfo, lastAmount * alpha * 0.2f);

			float nextAmount = lastAmount <= 0.0f ? 1.0f : 1.0f - lastAmount;
			DrawTex(dc, nextInfo, nextAmount * alpha * 0.2f);

			dc.RebindTexture();
		}
	}

private:
	void CheckNext(UIContext &dc, double t) {
		if (!g_Config.HasRecentIsos()) {
			return;
		}

		for (int index = lastIndex_ + 1; index != lastIndex_; ++index) {
			if (index < 0 || index >= (int)g_Config.RecentIsos().size()) {
				if (lastIndex_ == -1)
					break;
				index = 0;
			}

			std::shared_ptr<GameInfo> ginfo = GetInfo(dc, index);
			if (ginfo && ginfo->pending) {
				// Wait for it to load.  It might be the next one.
				break;
			}
			if (ginfo && (ginfo->pic1.texture || ginfo->pic0.texture)) {
				nextIndex_ = index;
				nextT_ = t + INTERVAL;
				break;
			}

			// Otherwise, keep going.  This skips games with no BG.
		}
	}

	std::shared_ptr<GameInfo> GetInfo(UIContext &dc, int index) {
		if (index < 0) {
			return nullptr;
		}
		const auto recentIsos = g_Config.RecentIsos();
		if (index >= (int)recentIsos.size())
			return nullptr;
		return g_gameInfoCache->GetInfo(dc.GetDrawContext(), Path(recentIsos[index]), GAMEINFO_WANTBG);
	}

	void DrawTex(UIContext &dc, std::shared_ptr<GameInfo> &ginfo, float amount) {
		if (!ginfo || amount <= 0.0f)
			return;
		GameInfoTex *pic = ginfo->GetBGPic();
		if (!pic)
			return;

		dc.GetDrawContext()->BindTexture(0, pic->texture->GetTexture());
		uint32_t color = whiteAlpha(amount) & 0xFFc0c0c0;
		dc.Draw()->DrawTexRect(dc.GetBounds(), 0, 0, 1, 1, color);
		dc.Flush();
	}

	static constexpr double INTERVAL = 8.0;
	static constexpr float TRANSITION = 3.0f;

	int lastIndex_ = -1;
	int nextIndex_ = -1;
	double nextT_ = -INTERVAL;
};

// TODO: Add more styles. Remember to add to the enum in ConfigValues.h and the selector in GameSettings too.

static BackgroundAnimation g_CurBackgroundAnimation = BackgroundAnimation::OFF;
static std::unique_ptr<Animation> g_Animation;
static bool bgTextureInited = false;

void UIBackgroundInit(UIContext &dc) {
	const Path bgPng = GetSysDirectory(DIRECTORY_SYSTEM) / "background.png";
	const Path bgJpg = GetSysDirectory(DIRECTORY_SYSTEM) / "background.jpg";
	if (File::Exists(bgPng) || File::Exists(bgJpg)) {
		const Path &bgFile = File::Exists(bgPng) ? bgPng : bgJpg;
		bgTexture = CreateTextureFromFile(dc.GetDrawContext(), bgFile.c_str(), DETECT, true);
	}
}

void UIBackgroundShutdown() {
	bgTexture.reset(nullptr);
	g_Animation.reset(nullptr);
	g_CurBackgroundAnimation = BackgroundAnimation::OFF;
	bgTextureInited = false;
}

void DrawBackground(UIContext &dc, float alpha, float x, float y, float z) {
	if (!bgTextureInited) {
		UIBackgroundInit(dc);
		bgTextureInited = true;
	}
	if (g_CurBackgroundAnimation != (BackgroundAnimation)g_Config.iBackgroundAnimation) {
		g_CurBackgroundAnimation = (BackgroundAnimation)g_Config.iBackgroundAnimation;

		switch (g_CurBackgroundAnimation) {
		case BackgroundAnimation::FLOATING_SYMBOLS:
			g_Animation.reset(new FloatingSymbolsAnimation());
			break;
		case BackgroundAnimation::RECENT_GAMES:
			g_Animation.reset(new RecentGamesAnimation());
			break;
		case BackgroundAnimation::WAVE:
			g_Animation.reset(new WaveAnimation());
			break;
		case BackgroundAnimation::MOVING_BACKGROUND:
			g_Animation.reset(new MovingBackground());
			break;
		default:
			g_Animation.reset(nullptr);
		}
	}

	uint32_t bgColor = whiteAlpha(alpha);

	if (bgTexture != nullptr) {
		dc.Flush();
		dc.GetDrawContext()->BindTexture(0, bgTexture->GetTexture());
		dc.Draw()->DrawTexRect(dc.GetBounds(), 0, 0, 1, 1, bgColor);

		dc.Flush();
		dc.RebindTexture();
	} else {
		// I_BG original color: 0xFF754D24
		ImageID img = ImageID("I_BG");
		ui_draw2d.DrawImageStretch(img, dc.GetBounds(), bgColor & dc.theme->backgroundColor);
	}

#if PPSSPP_PLATFORM(IOS)
	// iOS uses an old screenshot when restoring the task, so to avoid an ugly
	// jitter we accumulate time instead.
	static int frameCount = 0.0;
	frameCount++;
	double t = (double)frameCount / System_GetPropertyFloat(SYSPROP_DISPLAY_REFRESH_RATE);
#else
	double t = time_now_d();
#endif

	if (g_Animation) {
		g_Animation->Draw(dc, t, alpha, x, y, z);
	}
}

uint32_t GetBackgroundColorWithAlpha(const UIContext &dc) {
	return colorAlpha(colorBlend(dc.GetTheme().backgroundColor, 0, 0.5f), 0.65f);  // 0.65 = 166 = A6
}

void DrawGameBackground(UIContext &dc, const Path &gamePath, float x, float y, float z, bool transparent, bool darkenBackground) {
	using namespace Draw;
	using namespace UI;

	if (transparent && PSP_IsInited() && !g_Config.bSkipBufferEffects) {
		gpu->CheckDisplayResized();
		gpu->CheckConfigChanged();
		gpu->CopyDisplayToOutput(true);

		DrawContext *draw = dc.GetDrawContext();
		Viewport viewport;
		viewport.TopLeftX = 0;
		viewport.TopLeftY = 0;
		viewport.Width = pixel_xres;
		viewport.Height = pixel_yres;
		viewport.MaxDepth = 1.0;
		viewport.MinDepth = 0.0;
		draw->SetViewports(1, &viewport);
		dc.BeginFrame();
		dc.RebindTexture();
		dc.Begin();

		if (darkenBackground) {
			uint32_t color = GetBackgroundColorWithAlpha(dc);
			dc.FillRect(UI::Drawable(color), dc.GetBounds());
			dc.Flush();
		}
		return;
	}

	std::shared_ptr<GameInfo> ginfo;
	if (!gamePath.empty())
		ginfo = g_gameInfoCache->GetInfo(dc.GetDrawContext(), gamePath, GAMEINFO_WANTBG);
	dc.Flush();

	GameInfoTex *pic = ginfo ? ginfo->GetBGPic() : nullptr;
	if (pic) {
		dc.GetDrawContext()->BindTexture(0, pic->texture->GetTexture());
		uint32_t color = whiteAlpha(ease((time_now_d() - pic->timeLoaded) * 3)) & 0xFFc0c0c0;
		dc.Draw()->DrawTexRect(dc.GetBounds(), 0,0,1,1, color);
		dc.Flush();
		dc.RebindTexture();
	} else {
		::DrawBackground(dc, 1.0f, x, y, z);
		dc.RebindTexture();
		dc.Flush();
	}
}

void HandleCommonMessages(const char *message, const char *value, ScreenManager *manager, const Screen *activeScreen) {
	bool isActiveScreen = manager->topScreen() == activeScreen;

	if (!strcmp(message, "clear jit") && PSP_IsInited()) {
		if (MIPSComp::jit) {
			std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
			if (MIPSComp::jit)
				MIPSComp::jit->ClearCache();
		}
		currentMIPS->UpdateCore((CPUCore)g_Config.iCpuCore);
	} else if (!strcmp(message, "control mapping") && isActiveScreen && std::string(activeScreen->tag()) != "ControlMapping") {
		UpdateUIState(UISTATE_MENU);
		manager->push(new ControlMappingScreen(Path()));
	} else if (!strcmp(message, "display layout editor") && isActiveScreen && std::string(activeScreen->tag()) != "DisplayLayout") {
		UpdateUIState(UISTATE_MENU);
		manager->push(new DisplayLayoutScreen(Path()));
	} else if (!strcmp(message, "settings") && isActiveScreen && std::string(activeScreen->tag()) != "GameSettings") {
		UpdateUIState(UISTATE_MENU);
		manager->push(new GameSettingsScreen(Path()));
	} else if (!strcmp(message, "language screen") && isActiveScreen) {
		auto sy = GetI18NCategory("System");
		auto langScreen = new NewLanguageScreen(sy->T("Language"));
		langScreen->OnChoice.Add([](UI::EventParams &) {
			NativeMessageReceived("recreateviews", "");
			if (host) {
				host->UpdateUI();
			}
			return UI::EVENT_DONE;
		});
		manager->push(langScreen);
	} else if (!strcmp(message, "window minimized")) {
		if (!strcmp(value, "true")) {
			gstate_c.skipDrawReason |= SKIPDRAW_WINDOW_MINIMIZED;
		} else {
			gstate_c.skipDrawReason &= ~SKIPDRAW_WINDOW_MINIMIZED;
		}
	}
}

void UIScreenWithBackground::DrawBackground(UIContext &dc) {
	float x, y, z;
	screenManager()->getFocusPosition(x, y, z);
	::DrawBackground(dc, 1.0f, x, y, z);
	dc.Flush();
}

void UIScreenWithGameBackground::DrawBackground(UIContext &dc) {
	float x, y, z;
	screenManager()->getFocusPosition(x, y, z);
	if (!gamePath_.empty()) {
		DrawGameBackground(dc, gamePath_, x, y, z, (g_Config.bTransparentBackground || forceTransparent_), darkenGameBackground_);
	} else {
		::DrawBackground(dc, 1.0f, x, y, z);
		dc.Flush();
	}
}

void UIScreenWithGameBackground::sendMessage(const char *message, const char *value) {
	if (!strcmp(message, "settings") && screenManager()->topScreen() == this) {
		screenManager()->push(new GameSettingsScreen(gamePath_));
	} else {
		UIScreenWithBackground::sendMessage(message, value);
	}
}

void UIDialogScreenWithGameBackground::DrawBackground(UIContext &dc) {
	using namespace UI;
	using namespace Draw;
	float x, y, z;
	screenManager()->getFocusPosition(x, y, z);
	if (!gamePath_.empty()) {
		DrawGameBackground(dc, gamePath_, x, y, z, (g_Config.bTransparentBackground || forceTransparent_), darkenGameBackground_);
	} else {
		::DrawBackground(dc, 1.0f, x, y, z);
		dc.Flush();
	}
}

void UIDialogScreenWithGameBackground::sendMessage(const char *message, const char *value) {
	if (!strcmp(message, "settings") && screenManager()->topScreen() == this) {
		screenManager()->push(new GameSettingsScreen(gamePath_));
	} else {
		UIDialogScreenWithBackground::sendMessage(message, value);
	}
}

void UIScreenWithBackground::sendMessage(const char *message, const char *value) {
	HandleCommonMessages(message, value, screenManager(), this);
}

void UIDialogScreenWithBackground::DrawBackground(UIContext &dc) {
	float x, y, z;
	screenManager()->getFocusPosition(x, y, z);
	::DrawBackground(dc, 1.0f, x, y, z);
	dc.Flush();
}

void UIDialogScreenWithBackground::AddStandardBack(UI::ViewGroup *parent) {
	using namespace UI;
	auto di = GetI18NCategory("Dialog");
	parent->Add(new Choice(di->T("Back"), "", false, new AnchorLayoutParams(150, 64, 10, NONE, NONE, 10)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
}

void UIDialogScreenWithBackground::sendMessage(const char *message, const char *value) {
	HandleCommonMessages(message, value, screenManager(), this);
}

PromptScreen::PromptScreen(const Path &gamePath, std::string message, std::string yesButtonText, std::string noButtonText, std::function<void(bool)> callback)
	: UIDialogScreenWithGameBackground(gamePath), message_(message), callback_(callback) {
	auto di = GetI18NCategory("Dialog");
	yesButtonText_ = di->T(yesButtonText.c_str());
	noButtonText_ = di->T(noButtonText.c_str());
}

void PromptScreen::CreateViews() {
	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	Margins actionMenuMargins(0, 100, 15, 0);

	root_ = new AnchorLayout();

	root_->Add(new TextView(message_, ALIGN_LEFT | FLAG_WRAP_TEXT, false, new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 15, 15, 330, 10)))->SetClip(false);

	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(300, WRAP_CONTENT, NONE, 15, 15, NONE));
	root_->Add(rightColumnItems);

	Choice *yesButton = rightColumnItems->Add(new Choice(yesButtonText_));
	yesButton->OnClick.Handle(this, &PromptScreen::OnYes);
	root_->SetDefaultFocusView(yesButton);
	if (!noButtonText_.empty())
		rightColumnItems->Add(new Choice(noButtonText_))->OnClick.Handle(this, &PromptScreen::OnNo);
}

UI::EventReturn PromptScreen::OnYes(UI::EventParams &e) {
	TriggerFinish(DR_OK);
	return UI::EVENT_DONE;
}

UI::EventReturn PromptScreen::OnNo(UI::EventParams &e) {
	TriggerFinish(DR_CANCEL);
	return UI::EVENT_DONE;
}

void PromptScreen::TriggerFinish(DialogResult result) {
	callback_(result == DR_OK || result == DR_YES);
	UIDialogScreenWithBackground::TriggerFinish(result);
}

TextureShaderScreen::TextureShaderScreen(const std::string &title) : ListPopupScreen(title) {}

void TextureShaderScreen::CreateViews() {
	auto ps = GetI18NCategory("TextureShaders");
	ReloadAllPostShaderInfo(screenManager()->getDrawContext());
	shaders_ = GetAllTextureShaderInfo();
	std::vector<std::string> items;
	int selected = -1;
	for (int i = 0; i < (int)shaders_.size(); i++) {
		if (shaders_[i].section == g_Config.sTextureShaderName)
			selected = i;
		items.push_back(ps->T(shaders_[i].section.c_str(), shaders_[i].name.c_str()));
	}
	adaptor_ = UI::StringVectorListAdaptor(items, selected);

	ListPopupScreen::CreateViews();
}

void TextureShaderScreen::OnCompleted(DialogResult result) {
	if (result != DR_OK)
		return;
	g_Config.sTextureShaderName = shaders_[listView_->GetSelected()].section;
}

NewLanguageScreen::NewLanguageScreen(const std::string &title) : ListPopupScreen(title) {
	// Disable annoying encoding warning
#ifdef _MSC_VER
#pragma warning(disable:4566)
#endif
	auto &langValuesMapping = g_Config.GetLangValuesMapping();

	std::vector<File::FileInfo> tempLangs;
	VFSGetFileListing("lang", &tempLangs, "ini");
	std::vector<std::string> listing;
	int selected = -1;
	int counter = 0;
	for (size_t i = 0; i < tempLangs.size(); i++) {
		// Skip README
		if (tempLangs[i].name.find("README") != std::string::npos) {
			continue;
		}

		// We only support Arabic on platforms where we have support for the native text rendering
		// APIs, as proper Arabic support is way too difficult to implement ourselves.
#if !(defined(USING_QT_UI) || PPSSPP_PLATFORM(WINDOWS) || PPSSPP_PLATFORM(ANDROID))
		if (tempLangs[i].name.find("ar_AE") != std::string::npos) {
			continue;
		}

		if (tempLangs[i].name.find("fa_IR") != std::string::npos) {
			continue;
		}
#endif

		File::FileInfo lang = tempLangs[i];
		langs_.push_back(lang);

		std::string code;
		size_t dot = lang.name.find('.');
		if (dot != std::string::npos)
			code = lang.name.substr(0, dot);

		std::string buttonTitle = lang.name;

		if (!code.empty()) {
			auto iter = langValuesMapping.find(code);
			if (iter == langValuesMapping.end()) {
				// No title found, show locale code
				buttonTitle = code;
			} else {
				buttonTitle = iter->second.first;
			}
		}
		if (g_Config.sLanguageIni == code)
			selected = counter;
		listing.push_back(buttonTitle);
		counter++;
	}

	adaptor_ = UI::StringVectorListAdaptor(listing, selected);
}

void NewLanguageScreen::OnCompleted(DialogResult result) {
	if (result != DR_OK)
		return;
	std::string oldLang = g_Config.sLanguageIni;
	std::string iniFile = langs_[listView_->GetSelected()].name;

	size_t dot = iniFile.find('.');
	std::string code;
	if (dot != std::string::npos)
		code = iniFile.substr(0, dot);

	if (code.empty())
		return;

	g_Config.sLanguageIni = code;

	bool iniLoadedSuccessfully = false;
	// Allow the lang directory to be overridden for testing purposes (e.g. Android, where it's hard to 
	// test new languages without recompiling the entire app, which is a hassle).
	const Path langOverridePath = GetSysDirectory(DIRECTORY_SYSTEM) / "lang";

	// If we run into the unlikely case that "lang" is actually a file, just use the built-in translations.
	if (!File::Exists(langOverridePath) || !File::IsDirectory(langOverridePath))
		iniLoadedSuccessfully = i18nrepo.LoadIni(g_Config.sLanguageIni);
	else
		iniLoadedSuccessfully = i18nrepo.LoadIni(g_Config.sLanguageIni, langOverridePath);

	if (iniLoadedSuccessfully) {
		// Dunno what else to do here.
		auto &langValuesMapping = g_Config.GetLangValuesMapping();

		auto iter = langValuesMapping.find(code);
		if (iter == langValuesMapping.end()) {
			// Fallback to English
			g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
		} else {
			g_Config.iLanguage = iter->second.second;
		}
		RecreateViews();
	} else {
		g_Config.sLanguageIni = oldLang;
	}
}

void LogoScreen::Next() {
	if (!switched_) {
		switched_ = true;
		Path gamePath = boot_filename;

		switch (afterLogoScreen_) {
		case AfterLogoScreen::TO_GAME_SETTINGS:
			if (!gamePath.empty()) {
				screenManager()->switchScreen(new EmuScreen(gamePath));
			} else {
				screenManager()->switchScreen(new MainScreen());
			}
			screenManager()->push(new GameSettingsScreen(gamePath));
			break;
		case AfterLogoScreen::MEMSTICK_SCREEN_INITIAL_SETUP:
			screenManager()->switchScreen(new MemStickScreen(true));
			break;
		case AfterLogoScreen::DEFAULT:
		default:
			if (boot_filename.size()) {
				screenManager()->switchScreen(new EmuScreen(gamePath));
			} else {
				screenManager()->switchScreen(new MainScreen());
			}
			break;
		}
	}
}

const float logoScreenSeconds = 2.5f;

LogoScreen::LogoScreen(AfterLogoScreen afterLogoScreen)
	: afterLogoScreen_(afterLogoScreen) {
}

void LogoScreen::update() {
	UIScreen::update();
	double rate = std::max(30.0, (double)System_GetPropertyFloat(SYSPROP_DISPLAY_REFRESH_RATE));

	if ((double)frames_ / rate > logoScreenSeconds) {
		Next();
	}
	frames_++;
	sinceStart_ = (double)frames_ / rate;
}

void LogoScreen::sendMessage(const char *message, const char *value) {
	if (!strcmp(message, "boot") && screenManager()->topScreen() == this) {
		screenManager()->switchScreen(new EmuScreen(Path(value)));
	}
}

bool LogoScreen::key(const KeyInput &key) {
	if (key.deviceId != DEVICE_ID_MOUSE && (key.flags & KEY_DOWN)) {
		Next();
		return true;
	}
	return false;
}

bool LogoScreen::touch(const TouchInput &touch) {
	if (touch.flags & TOUCH_DOWN) {
		Next();
		return true;
	}
	return false;
}

void LogoScreen::render() {
	using namespace Draw;

	UIScreen::render();
	UIContext &dc = *screenManager()->getUIContext();

	const Bounds &bounds = dc.GetBounds();

	dc.Begin();

	float t = (float)sinceStart_ / (logoScreenSeconds / 3.0f);

	float alpha = t;
	if (t > 1.0f)
		alpha = 1.0f;
	float alphaText = alpha;
	if (t > 2.0f)
		alphaText = 3.0f - t;
	uint32_t textColor = colorAlpha(dc.theme->infoStyle.fgColor, alphaText);

	float x, y, z;
	screenManager()->getFocusPosition(x, y, z);
	::DrawBackground(dc, alpha, x, y, z);

	auto cr = GetI18NCategory("PSPCredits");
	auto gr = GetI18NCategory("Graphics");
	char temp[256];
	// Manually formatting UTF-8 is fun.  \xXX doesn't work everywhere.
	snprintf(temp, sizeof(temp), "%s Henrik Rydg%c%crd", cr->T("created", "Created by"), 0xC3, 0xA5);
	if (System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		dc.Draw()->DrawImage(ImageID("I_ICONGOLD"), bounds.centerX() - 120, bounds.centerY() - 30, 1.2f, 0xFFFFFFFF, ALIGN_CENTER);
	} else {
		dc.Draw()->DrawImage(ImageID("I_ICON"), bounds.centerX() - 120, bounds.centerY() - 30, 1.2f, 0xFFFFFFFF, ALIGN_CENTER);
	}
	dc.Draw()->DrawImage(ImageID("I_LOGO"), bounds.centerX() + 40, bounds.centerY() - 30, 1.5f, 0xFFFFFFFF, ALIGN_CENTER);
	//dc.Draw()->DrawTextShadow(UBUNTU48, "PPSSPP", bounds.w / 2, bounds.h / 2 - 30, textColor, ALIGN_CENTER);
	dc.SetFontScale(1.0f, 1.0f);
	dc.SetFontStyle(dc.theme->uiFont);
	dc.DrawText(temp, bounds.centerX(), bounds.centerY() + 40, textColor, ALIGN_CENTER);
	dc.DrawText(cr->T("license", "Free Software under GPL 2.0+"), bounds.centerX(), bounds.centerY() + 70, textColor, ALIGN_CENTER);

	int ppsspp_org_y = bounds.h / 2 + 130;
	dc.DrawText("www.ppsspp.org", bounds.centerX(), ppsspp_org_y, textColor, ALIGN_CENTER);

#if !PPSSPP_PLATFORM(UWP)
	// Draw the graphics API, except on UWP where it's always D3D11
	std::string apiName = screenManager()->getDrawContext()->GetInfoString(InfoField::APINAME);
#ifdef _DEBUG
	apiName += ", debug build";
#endif
	dc.DrawText(gr->T(apiName), bounds.centerX(), ppsspp_org_y + 50, textColor, ALIGN_CENTER);
#endif

	dc.Flush();
}

void CreditsScreen::CreateViews() {
	using namespace UI;
	auto di = GetI18NCategory("Dialog");
	auto cr = GetI18NCategory("PSPCredits");

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	Button *back = root_->Add(new Button(di->T("Back"), new AnchorLayoutParams(260, 64, NONE, NONE, 10, 10, false)));
	back->OnClick.Handle<UIScreen>(this, &UIScreen::OnOK);
	root_->SetDefaultFocusView(back);

	// Really need to redo this whole layout with some linear layouts...

	int rightYOffset = 0;
	if (!System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		root_->Add(new Button(cr->T("Buy Gold"), new AnchorLayoutParams(260, 64, NONE, NONE, 10, 84, false)))->OnClick.Handle(this, &CreditsScreen::OnSupport);
		rightYOffset = 74;
	}
	root_->Add(new Button(cr->T("PPSSPP Forums"), new AnchorLayoutParams(260, 64, 10, NONE, NONE, 158, false)))->OnClick.Handle(this, &CreditsScreen::OnForums);
	root_->Add(new Button(cr->T("Discord"), new AnchorLayoutParams(260, 64, 10, NONE, NONE, 232, false)))->OnClick.Handle(this, &CreditsScreen::OnDiscord);
	root_->Add(new Button("www.ppsspp.org", new AnchorLayoutParams(260, 64, 10, NONE, NONE, 10, false)))->OnClick.Handle(this, &CreditsScreen::OnPPSSPPOrg);
	root_->Add(new Button(cr->T("Privacy Policy"), new AnchorLayoutParams(260, 64, 10, NONE, NONE, 84, false)))->OnClick.Handle(this, &CreditsScreen::OnPrivacy);
	root_->Add(new Button(cr->T("Twitter @PPSSPP_emu"), new AnchorLayoutParams(260, 64, NONE, NONE, 10, rightYOffset + 84, false)))->OnClick.Handle(this, &CreditsScreen::OnTwitter);
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
	root_->Add(new Button(cr->T("Share PPSSPP"), new AnchorLayoutParams(260, 64, NONE, NONE, 10, rightYOffset + 158, false)))->OnClick.Handle(this, &CreditsScreen::OnShare);
#endif
	if (System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		root_->Add(new ImageView(ImageID("I_ICONGOLD"), "", IS_DEFAULT, new AnchorLayoutParams(100, 64, 10, 10, NONE, NONE, false)));
	} else {
		root_->Add(new ImageView(ImageID("I_ICON"), "", IS_DEFAULT, new AnchorLayoutParams(100, 64, 10, 10, NONE, NONE, false)));
	}
}

UI::EventReturn CreditsScreen::OnSupport(UI::EventParams &e) {
#ifdef __ANDROID__
	LaunchBrowser("market://details?id=org.ppsspp.ppssppgold");
#else
	LaunchBrowser("https://central.ppsspp.org/buygold");
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn CreditsScreen::OnTwitter(UI::EventParams &e) {
#ifdef __ANDROID__
	System_SendMessage("showTwitter", "PPSSPP_emu");
#else
	LaunchBrowser("https://twitter.com/#!/PPSSPP_emu");
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn CreditsScreen::OnPPSSPPOrg(UI::EventParams &e) {
	LaunchBrowser("https://www.ppsspp.org");
	return UI::EVENT_DONE;
}

UI::EventReturn CreditsScreen::OnPrivacy(UI::EventParams &e) {
	LaunchBrowser("https://www.ppsspp.org/privacy.html");
	return UI::EVENT_DONE;
}

UI::EventReturn CreditsScreen::OnForums(UI::EventParams &e) {
	LaunchBrowser("https://forums.ppsspp.org");
	return UI::EVENT_DONE;
}

UI::EventReturn CreditsScreen::OnDiscord(UI::EventParams &e) {
	LaunchBrowser("https://discord.gg/5NJB6dD");
	return UI::EVENT_DONE;
}

UI::EventReturn CreditsScreen::OnShare(UI::EventParams &e) {
	auto cr = GetI18NCategory("PSPCredits");
	System_SendMessage("sharetext", cr->T("CheckOutPPSSPP", "Check out PPSSPP, the awesome PSP emulator: https://www.ppsspp.org/"));
	return UI::EVENT_DONE;
}

CreditsScreen::CreditsScreen() {
	startTime_ = time_now_d();
}

void CreditsScreen::update() {
	UIScreen::update();
	UpdateUIState(UISTATE_MENU);
}

void CreditsScreen::render() {
	UIScreen::render();

	auto cr = GetI18NCategory("PSPCredits");

	std::string specialthanksMaxim = "Maxim ";
	specialthanksMaxim += cr->T("specialthanksMaxim", "for his amazing Atrac3+ decoder work");

	std::string specialthanksKeithGalocy = "Keith Galocy ";
	specialthanksKeithGalocy += cr->T("specialthanksKeithGalocy", "at NVIDIA (hardware, advice)");

	std::string specialthanksOrphis = "Orphis (";
	specialthanksOrphis += cr->T("build server");
	specialthanksOrphis += ')';

	std::string specialthanksangelxwind = "angelxwind (";
	specialthanksangelxwind += cr->T("iOS builds");
	specialthanksangelxwind += ')';

	std::string specialthanksW_MS = "W.MS (";
	specialthanksW_MS += cr->T("iOS builds");
	specialthanksW_MS += ')';

	std::string specialthankssolarmystic = "solarmystic (";
	specialthankssolarmystic += cr->T("testing");
	specialthankssolarmystic += ')';

	const char * credits[] = {
		"PPSSPP",
		"",
		cr->T("title", "A fast and portable PSP emulator"),
		"",
		"",
		cr->T("created", "Created by"),
		"Henrik Rydg\xc3\xa5rd",
		"",
		"",
		cr->T("contributors", "Contributors:"),
		"unknownbrackets",
		"oioitff",
		"xsacha",
		"raven02",
		"tpunix",
		"orphis",
		"sum2012",
		"mikusp",
		"aquanull",
		"The Dax",
		"bollu",
		"tmaul",
		"artart78",
		"ced2911",
		"soywiz",
		"kovensky",
		"xele",
		"chaserhjk",
		"evilcorn",
		"daniel dressler",
		"makotech222",
		"CPkmn",
		"mgaver",
		"jeid3",
		"cinaera/BeaR",
		"jtraynham",
		"Kingcom",
		"arnastia",
		"lioncash",
		"JulianoAmaralChaves",
		"vnctdj",
		"kaienfr",
		"shenweip",
		"Danyal Zia",
		"Igor Calabria",
		"Coldbird",
		"Kyhel",
		"xebra",
		"LunaMoo",
		"zminhquanz",
		"ANR2ME",
		"adenovan",
		"iota97",
		"Lubos",
		"",
		cr->T("specialthanks", "Special thanks to:"),
		specialthanksMaxim.c_str(),
		specialthanksKeithGalocy.c_str(),
		specialthanksOrphis.c_str(),
		specialthanksangelxwind.c_str(),
		specialthanksW_MS.c_str(),
		specialthankssolarmystic.c_str(),
		cr->T("all the forum mods"),
		"",
		cr->T("this translation by", ""),   // Empty string as this is the original :)
		cr->T("translators1", ""),
		cr->T("translators2", ""),
		cr->T("translators3", ""),
		cr->T("translators4", ""),
		cr->T("translators5", ""),
		cr->T("translators6", ""),
		"",
		cr->T("written", "Written in C++ for speed and portability"),
		"",
		"",
		cr->T("tools", "Free tools used:"),
#ifdef __ANDROID__
		"Android SDK + NDK",
#endif
#if defined(USING_QT_UI)
		"Qt",
#endif
#if defined(SDL)
		"SDL",
#endif
		"CMake",
		"freetype2",
		"zlib",
		"PSP SDK",
		"",
		"",
		cr->T("website", "Check out the website:"),
		"www.ppsspp.org",
		cr->T("list", "compatibility lists, forums, and development info"),
		"",
		"",
		cr->T("check", "Also check out Dolphin, the best Wii/GC emu around:"),
		"https://www.dolphin-emu.org",
		"",
		"",
		cr->T("info1", "PPSSPP is only intended to play games you own."),
		cr->T("info2", "Please make sure that you own the rights to any games"),
		cr->T("info3", "you play by owning the UMD or by buying the digital"),
		cr->T("info4", "download from the PSN store on your real PSP."),
		"",
		"",
		cr->T("info5", "PSP is a trademark by Sony, Inc."),
	};


	// TODO: This is kinda ugly, done on every frame...
	char temp[256];
	if (System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		snprintf(temp, sizeof(temp), "PPSSPP Gold %s", PPSSPP_GIT_VERSION);
	} else {
		snprintf(temp, sizeof(temp), "PPSSPP %s", PPSSPP_GIT_VERSION);
	}
	credits[0] = (const char *)temp;

	UIContext &dc = *screenManager()->getUIContext();
	dc.Begin();
	const Bounds &bounds = dc.GetLayoutBounds();

	const int numItems = ARRAY_SIZE(credits);
	int itemHeight = 36;
	int totalHeight = numItems * itemHeight + bounds.h + 200;

	float t = (float)(time_now_d() - startTime_) * 60.0;

	float y = bounds.y2() - fmodf(t, (float)totalHeight);
	for (int i = 0; i < numItems; i++) {
		float alpha = linearInOut(y+32, 64, bounds.y2() - 192, 64);
		uint32_t textColor = colorAlpha(dc.theme->infoStyle.fgColor, alpha);

		if (alpha > 0.0f) {
			dc.SetFontScale(ease(alpha), ease(alpha));
			dc.DrawText(credits[i], bounds.centerX(), y, textColor, ALIGN_HCENTER);
			dc.SetFontScale(1.0f, 1.0f);
		}
		y += itemHeight;
	}

	dc.Flush();
}

SettingInfoMessage::SettingInfoMessage(int align, UI::AnchorLayoutParams *lp)
	: UI::LinearLayout(UI::ORIENT_HORIZONTAL, lp) {
	using namespace UI;
	SetSpacing(0.0f);
	Add(new UI::Spacer(10.0f));
	text_ = Add(new UI::TextView("", align, false, new LinearLayoutParams(1.0, Margins(0, 10))));
	Add(new UI::Spacer(10.0f));
}

void SettingInfoMessage::Show(const std::string &text, const UI::View *refView) {
	if (refView) {
		Bounds b = refView->GetBounds();
		const UI::AnchorLayoutParams *lp = GetLayoutParams()->As<UI::AnchorLayoutParams>();
		if (b.y >= cutOffY_) {
			ReplaceLayoutParams(new UI::AnchorLayoutParams(lp->width, lp->height, lp->left, 80.0f, lp->right, lp->bottom, lp->center));
		} else {
			ReplaceLayoutParams(new UI::AnchorLayoutParams(lp->width, lp->height, lp->left, dp_yres - 80.0f - 40.0f, lp->right, lp->bottom, lp->center));
		}
	}
	text_->SetText(text);
	timeShown_ = time_now_d();
}

void SettingInfoMessage::Draw(UIContext &dc) {
	static const double FADE_TIME = 1.0;
	static const float MAX_ALPHA = 0.9f;

	// Let's show longer messages for more time (guesstimate at reading speed.)
	// Note: this will give multibyte characters more time, but they often have shorter words anyway.
	double timeToShow = std::max(1.5, text_->GetText().size() * 0.05);

	double sinceShow = time_now_d() - timeShown_;
	float alpha = MAX_ALPHA;
	if (timeShown_ == 0.0 || sinceShow > timeToShow + FADE_TIME) {
		alpha = 0.0f;
	} else if (sinceShow > timeToShow) {
		alpha = MAX_ALPHA - MAX_ALPHA * (float)((sinceShow - timeToShow) / FADE_TIME);
	}

	if (alpha >= 0.1f) {
		UI::Style style = dc.theme->popupTitle;
		style.background.color = colorAlpha(style.background.color, alpha - 0.1f);
		dc.FillRect(style.background, bounds_);
	}

	text_->SetTextColor(whiteAlpha(alpha));
	ViewGroup::Draw(dc);
	showing_ = sinceShow <= timeToShow; // Don't consider fade time
}

std::string SettingInfoMessage::GetText() const {
	return showing_ && text_ ? text_->GetText() : "";
}
