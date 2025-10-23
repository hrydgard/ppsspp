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
#include <cmath>
#include <functional>

#include "Common/Render/DrawBuffer.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/UI.h"

#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/Math/curves.h"
#include "Common/File/VFS/VFS.h"

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Random/Rng.h"
#include "Common/TimeUtil.h"
#include "Common/File/FileUtil.h"
#include "Common/Render/ManagedTexture.h"

#include "Core/Config.h"
#include "Core/System.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/HLE/sceUtility.h"
#include "Core/Util/RecentFiles.h"
#include "GPU/GPUState.h"
#include "GPU/GPUCommon.h"
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

static Draw::Texture *bgTexture;

class Animation {
public:
	virtual ~Animation() = default;
	virtual void Draw(UIContext &dc, double t, float alpha, float x, float y, float z) = 0;
};

class MovingBackground : public Animation {
public:
	void Draw(UIContext &dc, double t, float alpha, float x, float y, float z) override {
		if (!bgTexture)
			return;

		dc.Flush();
		dc.GetDrawContext()->BindTexture(0, bgTexture);
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
	static constexpr float XFAC = 0.3f;
	static constexpr float YFAC = 0.3f;
	static constexpr float ZFAC = 0.12f;
	static constexpr float XSPEED = 0.05f;
	static constexpr float YSPEED = 0.05f;
	static constexpr float ZSPEED = 0.1f;

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

		// 500 is enough for any resolution really. 24 * 500 = 12000 which fits handily in our UI vertex buffer (max 65536 per flush).
		const int steps = std::max(20, std::min((int)g_display.dp_xres, 500));
		float step = (float)g_display.dp_xres / (float)steps;
		t *= speed;

		for (int n = 0; n < steps; n++) {
			float x = (float)n * step;
			float nextX = (float)(n + 1) * step;
			float i = x * 1280 / bounds.w;

			float wave0 = sin(i*0.005+t*0.8)*0.05 + sin(i*0.002+t*0.25)*0.02 + sin(i*0.001+t*0.3)*0.03 + 0.625;
			float wave1 = sin(i*0.0044+t*0.4)*0.07 + sin(i*0.003+t*0.1)*0.02 + sin(i*0.001+t*0.3)*0.01 + 0.625;
			dc.Draw()->RectVGradient(x, wave0*bounds.h, nextX, bounds.h, color, 0x00000000);
			dc.Draw()->RectVGradient(x, wave1*bounds.h, nextX, bounds.h, color, 0x00000000);

			// Add some "antialiasing"
			dc.Draw()->RectVGradient(x, wave0*bounds.h-3.0f * g_display.pixel_in_dps_y, nextX, wave0 * bounds.h, 0x00000000, color);
			dc.Draw()->RectVGradient(x, wave1*bounds.h-3.0f * g_display.pixel_in_dps_y, nextX, wave1 * bounds.h, 0x00000000, color);
		}

		dc.Flush();
		dc.Begin();
	}
};

class FloatingSymbolsAnimation : public Animation {
public:
	FloatingSymbolsAnimation(bool is_colored) {
		this->is_colored = is_colored;
	}

	void Draw(UIContext &dc, double t, float alpha, float x, float y, float z) override {
		dc.Flush();
		dc.Begin();
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
			Color color = is_colored ? colorAlpha(COLORS[n], alpha * 0.25f) : colorAlpha(DEFAULT_COLOR, alpha * 0.1f);
			ui_draw2d.DrawImageRotated(SYMBOLS[n], x, y, 1.0f, angle, color);
		}
		dc.Flush();
	}

private:
	static constexpr int COUNT = 100;
	static constexpr Color DEFAULT_COLOR = 0xC0FFFFFF;
	static constexpr Color COLORS[4] = { 0xFFE3B56F, 0xFF615BFF, 0xFFAA88F3, 0xFFC2CC7A, }; // X O D A
	static const ImageID SYMBOLS[4];

	bool is_colored = false;
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

const ImageID FloatingSymbolsAnimation::SYMBOLS[4] = {
	ImageID("I_CROSS"),
	ImageID("I_CIRCLE"),
	ImageID("I_SQUARE"),
	ImageID("I_TRIANGLE"),
};

class RecentGamesAnimation : public Animation {
public:
	void Draw(UIContext &dc, double t, float alpha, float x, float y, float z) override {
		if (lastIndex_ == nextIndex_) {
			CheckNext(dc, t);
		} else if (t > nextT_) {
			lastIndex_ = nextIndex_;
		}

		if (g_recentFiles.HasAny()) {
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
		if (!g_recentFiles.HasAny()) {
			return;
		}

		std::vector<std::string> recents = g_recentFiles.GetRecentFiles();

		for (int index = lastIndex_ + 1; index != lastIndex_; ++index) {
			if (index < 0 || index >= (int)recents.size()) {
				if (lastIndex_ == -1)
					break;
				index = 0;
			}

			std::shared_ptr<GameInfo> ginfo = GetInfo(dc, index);
			if (ginfo && !ginfo->Ready(GameInfoFlags::PIC1)) {
				// Wait for it to load.  It might be the next one.
				break;
			}
			if (ginfo && ginfo->pic1.texture) {
				nextIndex_ = index;
				nextT_ = t + INTERVAL;
				break;
			}

			// Otherwise, keep going.  This skips games with no BG.
		}
	}

	static std::shared_ptr<GameInfo> GetInfo(UIContext &dc, int index) {
		if (index < 0) {
			return nullptr;
		}
		const auto recentIsos = g_recentFiles.GetRecentFiles();
		if (index >= (int)recentIsos.size())
			return std::shared_ptr<GameInfo>();
		return g_gameInfoCache->GetInfo(dc.GetDrawContext(), Path(recentIsos[index]), GameInfoFlags::PIC1);
	}

	static void DrawTex(UIContext &dc, std::shared_ptr<GameInfo> &ginfo, float amount) {
		if (!ginfo || amount <= 0.0f)
			return;
		GameInfoTex *pic = ginfo->GetPIC1();
		if (!pic)
			return;

		dc.GetDrawContext()->BindTexture(0, pic->texture);
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

class BouncingIconAnimation : public Animation {
	public:
		void Draw(UIContext &dc, double t, float alpha, float x, float y, float z) override {
			dc.Flush();
			dc.Begin();

			// Handle change in resolution.
			float xres = dc.GetBounds().w;
			float yres = dc.GetBounds().h;
			if (last_xres != xres || last_yres != yres) {
				Recalculate(xres, yres);
			}

			// Draw the image.
			float xpos = xbase + dc.GetBounds().x;
			float ypos = ybase + dc.GetBounds().y;
			ImageID icon = !color_ix && System_GetPropertyBool(SYSPROP_APP_GOLD) ? ImageID("I_ICON_GOLD") : ImageID("I_ICON");
			ui_draw2d.DrawImage(icon, xpos, ypos, scale, COLORS[color_ix], ALIGN_CENTER);
			dc.Flush();

			// Switch direction if within border.
			bool should_recolor = true;
			if (xbase > xres - border || xbase < border) {
				xspeed *= -1.0f;
				RandomizeColor();
				should_recolor = false;
			}

			if (ybase > yres - border || ybase < border) {
				yspeed *= -1.0f;

				if (should_recolor) {
					RandomizeColor();
				}
			}

			// Place to border if out of bounds.
			if (xbase > xres - border) xbase = xres - border;
			else if (xbase < border) xbase = border;
			if (ybase > yres - border) ybase = yres - border;
			else if (ybase < border) ybase = border;

			// Update location.
			xbase += xspeed;
			ybase += yspeed;
		}

	private:
		static constexpr int COLOR_COUNT = 11;
		static constexpr Color COLORS[COLOR_COUNT] = { 0xFFFFFFFF, 0xFFFFFF00, 0xFFFF0000, 0xFF00FF00, 0xFF0000FF,
				0xFF00FFFF, 0xFFFF00FF, 0xFF4111D1, 0xFF3577F3, 0xFFAA77FF, 0xFF623B84 };

		float xbase = 0.0f;
		float ybase = 0.0f;	
		float last_xres = 0.0f;
		float last_yres = 0.0f;
		float xspeed = 1.0f;
		float yspeed = 1.0f;
		float scale = 1.0f;
		float border = 35.0f;
		int color_ix = 0;
		int last_color_ix = -1;
		GMRng rng;

		void Recalculate(int xres, int yres) {
			// First calculation.
			if (last_color_ix == -1) {
				xbase = xres / 2.0f;
				ybase = yres / 2.0f;
				last_color_ix = 0;
	
				// Determine initial direction.
				if ((int)(rng.F() * xres) % 2) xspeed *= -1.0f;
				if ((int)(rng.F() * yres) % 2) yspeed *= -1.0f;
			}

			// Scale certain attributes to resolution.
			scale = std::min(xres, yres) / 400.0f;
			float speed = scale < 2.5f ? scale * 0.58f : scale * 0.46f;
			xspeed = std::signbit(xspeed) ? speed * -1.0f : speed;
			yspeed = std::signbit(yspeed) ? speed * -1.0f : speed;
			border = 35.0f * scale;

			last_xres = xres;
			last_yres = yres;
		}

		void RandomizeColor() {
			do {
				color_ix = (int)(rng.F() * xbase) % COLOR_COUNT;
			} while (color_ix == last_color_ix);

			last_color_ix = color_ix;
		}
};

// TODO: Add more styles. Remember to add to the enum in ConfigValues.h and the selector in GameSettings too.

static BackgroundAnimation g_CurBackgroundAnimation = BackgroundAnimation::OFF;
static std::unique_ptr<Animation> g_Animation;
static bool bgTextureInited = false;  // Separate variable because init could fail.

void UIBackgroundInit(UIContext &dc) {
	const Path bgPng = GetSysDirectory(DIRECTORY_SYSTEM) / "background.png";
	const Path bgJpg = GetSysDirectory(DIRECTORY_SYSTEM) / "background.jpg";
	if (File::Exists(bgPng) || File::Exists(bgJpg)) {
		const Path &bgFile = File::Exists(bgPng) ? bgPng : bgJpg;
		bgTexture = CreateTextureFromFile(dc.GetDrawContext(), bgFile.c_str(), ImageFileType::DETECT, true);
	}
}

void UIBackgroundShutdown() {
	if (bgTexture) {
		bgTexture->Release();
		bgTexture = nullptr;
	}
	bgTextureInited = false;
	g_Animation.reset(nullptr);
	g_CurBackgroundAnimation = BackgroundAnimation::OFF;
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
			g_Animation.reset(new FloatingSymbolsAnimation(false));
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
		case BackgroundAnimation::BOUNCING_ICON:
			g_Animation.reset(new BouncingIconAnimation());
			break;
		case BackgroundAnimation::FLOATING_SYMBOLS_COLORED:
			g_Animation.reset(new FloatingSymbolsAnimation(true));
			break;
		default:
			g_Animation.reset(nullptr);
		}
	}

	uint32_t bgColor = whiteAlpha(alpha);

	if (bgTexture != nullptr) {
		dc.Flush();
		dc.Begin();
		dc.GetDrawContext()->BindTexture(0, bgTexture);
		dc.Draw()->DrawTexRect(dc.GetBounds(), 0, 0, 1, 1, bgColor);

		dc.Flush();
		dc.RebindTexture();
	} else {
		// I_BG original color: 0xFF754D24
		ImageID img = ImageID("I_BG");
		dc.Begin();
		dc.Draw()->DrawImageStretch(img, dc.GetBounds(), bgColor & dc.GetTheme().backgroundColor);
		dc.Flush();
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

void DrawGameBackground(UIContext &dc, const Path &gamePath, float x, float y, float z) {
	using namespace Draw;
	using namespace UI;
	dc.Flush();

	std::shared_ptr<GameInfo> ginfo;
	if (!gamePath.empty()) {
		ginfo = g_gameInfoCache->GetInfo(dc.GetDrawContext(), gamePath, GameInfoFlags::PIC1);
	}

	GameInfoTex *pic = (ginfo && ginfo->Ready(GameInfoFlags::PIC1)) ? ginfo->GetPIC1() : nullptr;
	if (pic) {
		dc.GetDrawContext()->BindTexture(0, pic->texture);
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

void HandleCommonMessages(UIMessage message, const char *value, ScreenManager *manager, const Screen *activeScreen) {
	bool isActiveScreen = manager->topScreen() == activeScreen;

	if (message == UIMessage::REQUEST_CLEAR_JIT && PSP_IsInited()) {
		// TODO: This seems to clearly be the wrong place to handle this.
		if (MIPSComp::jit) {
			std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
			if (MIPSComp::jit)
				MIPSComp::jit->ClearCache();
		}
		currentMIPS->UpdateCore((CPUCore)g_Config.iCpuCore);
	} else if (message == UIMessage::SHOW_CONTROL_MAPPING && isActiveScreen && std::string(activeScreen->tag()) != "ControlMapping") {
		UpdateUIState(UISTATE_MENU);
		manager->push(new ControlMappingScreen(Path()));
	} else if (message == UIMessage::SHOW_DISPLAY_LAYOUT_EDITOR && isActiveScreen && std::string(activeScreen->tag()) != "DisplayLayout") {
		UpdateUIState(UISTATE_MENU);
		manager->push(new DisplayLayoutScreen(Path()));
	} else if (message == UIMessage::SHOW_SETTINGS && isActiveScreen && std::string(activeScreen->tag()) != "GameSettings") {
		UpdateUIState(UISTATE_MENU);
		manager->push(new GameSettingsScreen(Path()));
	} else if (message == UIMessage::SHOW_LANGUAGE_SCREEN && isActiveScreen) {
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		auto langScreen = new NewLanguageScreen(sy->T("Language"));
		langScreen->OnChoice.Add([](UI::EventParams &) {
			System_PostUIMessage(UIMessage::RECREATE_VIEWS);
			System_Notify(SystemNotification::UI);
		});
		manager->push(langScreen);
	} else if (message == UIMessage::WINDOW_MINIMIZED) {
		if (!strcmp(value, "true")) {
			gstate_c.skipDrawReason |= SKIPDRAW_WINDOW_MINIMIZED;
		} else {
			gstate_c.skipDrawReason &= ~SKIPDRAW_WINDOW_MINIMIZED;
		}
	}
}

ScreenRenderFlags BackgroundScreen::render(ScreenRenderMode mode) {
	if (mode & ScreenRenderMode::FIRST) {
		SetupViewport();
	} else {
		_dbg_assert_(false);
	}

	UIContext *uiContext = screenManager()->getUIContext();

	uiContext->PushTransform({ translation_, scale_, alpha_ });

	uiContext->Begin();
	float x, y, z;
	screenManager()->getFocusPosition(x, y, z);

	if (!gamePath_.empty()) {
		::DrawGameBackground(*uiContext, gamePath_, x, y, z);
	} else {
		::DrawBackground(*uiContext, 1.0f, x, y, z);
	}

	uiContext->Flush();

	uiContext->PopTransform();

	return ScreenRenderFlags::NONE;
}

void BackgroundScreen::sendMessage(UIMessage message, const char *value) {
	switch (message) {
	case UIMessage::GAME_SELECTED:
		if (value && strlen(value)) {
			gamePath_ = Path(value);
		} else {
			gamePath_.clear();
		}
		break;
	default:
		break;
	}
}

void UIScreenWithGameBackground::sendMessage(UIMessage message, const char *value) {
	if (message == UIMessage::SHOW_SETTINGS && screenManager()->topScreen() == this) {
		screenManager()->push(new GameSettingsScreen(gamePath_));
	} else {
		UIScreenWithBackground::sendMessage(message, value);
	}
}

void UIDialogScreenWithGameBackground::sendMessage(UIMessage message, const char *value) {
	if (message == UIMessage::SHOW_SETTINGS && screenManager()->topScreen() == this) {
		screenManager()->push(new GameSettingsScreen(gamePath_));
	} else {
		UIDialogScreenWithBackground::sendMessage(message, value);
	}
}

void UIScreenWithBackground::sendMessage(UIMessage message, const char *value) {
	HandleCommonMessages(message, value, screenManager(), this);
}

void UIDialogScreenWithBackground::AddStandardBack(UI::ViewGroup *parent) {
	using namespace UI;
	auto di = GetI18NCategory(I18NCat::DIALOG);
	parent->Add(new Choice(di->T("Back"), "", false, new AnchorLayoutParams(190, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
}

void UIDialogScreenWithBackground::sendMessage(UIMessage message, const char *value) {
	HandleCommonMessages(message, value, screenManager(), this);
}

PromptScreen::PromptScreen(const Path &gamePath, std::string_view message, std::string_view yesButtonText, std::string_view noButtonText, std::function<void(bool)> callback)
	: UIDialogScreenWithGameBackground(gamePath), message_(message), callback_(callback) {
	yesButtonText_ = yesButtonText;
	noButtonText_ = noButtonText;
}

void PromptScreen::CreateViews() {
	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	const bool vertical = UsePortraitLayout();

	root_ = new AnchorLayout();
	ViewGroup *rightColumnItems;

	if (!vertical) {
		// Horizontal layout.
		root_->Add(new TextView(message_, ALIGN_LEFT | FLAG_WRAP_TEXT, false, new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 15, 105, 330, 10)))->SetClip(false);
		rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(300, WRAP_CONTENT, NONE, 105, 15, NONE));
		root_->Add(rightColumnItems);
	} else {
		// Vertical layout
		root_->Add(new TextView(message_, ALIGN_LEFT | FLAG_WRAP_TEXT, false, new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 15, 15, 55, NONE)))->SetClip(false);
		// Leave space for the version at the bottom.
		rightColumnItems = new LinearLayout(ORIENT_HORIZONTAL, new AnchorLayoutParams(FILL_PARENT, WRAP_CONTENT, 15, NONE, 15, 65));
		root_->Add(rightColumnItems);
	}

	Choice *yesButton = rightColumnItems->Add(new Choice(yesButtonText_, vertical ? new LinearLayoutParams(1.0f) : nullptr));
	yesButton->SetCentered(vertical);
	yesButton->OnClick.Add([this](UI::EventParams &e) {
		TriggerFinish(DR_OK);
	});
	if (!noButtonText_.empty()) {
		Choice *noButton = rightColumnItems->Add(new Choice(noButtonText_, vertical ? new LinearLayoutParams(1.0f) : nullptr));
		noButton->SetCentered(vertical);
		noButton->OnClick.Add([this](UI::EventParams &e) {
			TriggerFinish(DR_CANCEL);
		});
		root_->SetDefaultFocusView(noButton);
	} else {
		// This is an information screen, not a question.
		// Sneak in the version of PPSSPP in the bottom left corner, for debug-reporting user screenshots.
		std::string version = System_GetProperty(SYSPROP_BUILD_VERSION);
		root_->Add(new TextView(version, 0, true, new AnchorLayoutParams(10.0f, NONE, NONE, 10.0f)));
		root_->SetDefaultFocusView(yesButton);
	}
}

void PromptScreen::TriggerFinish(DialogResult result) {
	if (callback_) {
		callback_(result == DR_OK || result == DR_YES);
	}
	UIDialogScreenWithBackground::TriggerFinish(result);
}

TextureShaderScreen::TextureShaderScreen(std::string_view title) : ListPopupScreen(title) {}

void TextureShaderScreen::CreateViews() {
	auto ps = GetI18NCategory(I18NCat::TEXTURESHADERS);
	ReloadAllPostShaderInfo(screenManager()->getDrawContext());
	shaders_ = GetAllTextureShaderInfo();
	std::vector<std::string> items;
	int selected = -1;
	for (int i = 0; i < (int)shaders_.size(); i++) {
		if (shaders_[i].section == g_Config.sTextureShaderName)
			selected = i;
		items.emplace_back(ps->T(shaders_[i].section, shaders_[i].name));
	}
	adaptor_ = UI::StringVectorListAdaptor(items, selected);

	ListPopupScreen::CreateViews();
}

void TextureShaderScreen::OnCompleted(DialogResult result) {
	if (result != DR_OK)
		return;
	g_Config.sTextureShaderName = shaders_[listView_->GetSelected()].section;
}

NewLanguageScreen::NewLanguageScreen(std::string_view title) : ListPopupScreen(title) {
	// Disable annoying encoding warning
#ifdef _MSC_VER
#pragma warning(disable:4566)
#endif
	auto &langValuesMapping = g_Config.GetLangValuesMapping();

	std::vector<File::FileInfo> tempLangs;
	g_VFS.GetFileListing("lang", &tempLangs, "ini");
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

		const File::FileInfo &lang = tempLangs[i];
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
		iniLoadedSuccessfully = g_i18nrepo.LoadIni(g_Config.sLanguageIni);
	else
		iniLoadedSuccessfully = g_i18nrepo.LoadIni(g_Config.sLanguageIni, langOverridePath);

	if (iniLoadedSuccessfully) {
		RecreateViews();
	} else {
		// Failed to load the language ini. Shouldn't really happen, but let's just switch back to the old language.
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

void LogoScreen::sendMessage(UIMessage message, const char *value) {
	if (message == UIMessage::REQUEST_GAME_BOOT && screenManager()->topScreen() == this) {
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

void LogoScreen::touch(const TouchInput &touch) {
	if (touch.flags & TOUCH_DOWN) {
		Next();
	}
}

void LogoScreen::DrawForeground(UIContext &dc) {
	using namespace Draw;

	const Bounds &bounds = dc.GetBounds();

	dc.Begin();

	float t = (float)sinceStart_ / (logoScreenSeconds / 3.0f);

	float alpha = t;
	if (t > 1.0f)
		alpha = 1.0f;
	float alphaText = alpha;
	if (t > 2.0f)
		alphaText = 3.0f - t;
	uint32_t textColor = colorAlpha(dc.GetTheme().infoStyle.fgColor, alphaText);

	auto cr = GetI18NCategory(I18NCat::PSPCREDITS);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);
	char temp[256];
	// Manually formatting UTF-8 is fun.  \xXX doesn't work everywhere.
	snprintf(temp, sizeof(temp), "%s Henrik Rydg%c%crd", cr->T_cstr("created", "Created by"), 0xC3, 0xA5);
	if (System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		UI::DrawIconShine(dc, Bounds::FromCenter(bounds.centerX() - 125, bounds.centerY() - 30, 60.0f), 0.7f, true);
		dc.Draw()->DrawImage(ImageID("I_ICON_GOLD"), bounds.centerX() - 125, bounds.centerY() - 30, 1.2f, 0xFFFFFFFF, ALIGN_CENTER);
	} else {
		dc.Draw()->DrawImage(ImageID("I_ICON"), bounds.centerX() - 125, bounds.centerY() - 30, 1.2f, 0xFFFFFFFF, ALIGN_CENTER);
	}
	dc.Draw()->DrawImage(ImageID("I_LOGO"), bounds.centerX() + 45, bounds.centerY() - 30, 1.5f, 0xFFFFFFFF, ALIGN_CENTER);
	//dc.Draw()->DrawTextShadow(UBUNTU48, "PPSSPP", bounds.w / 2, bounds.h / 2 - 30, textColor, ALIGN_CENTER);
	dc.SetFontScale(1.0f, 1.0f);
	dc.SetFontStyle(dc.GetTheme().uiFont);
	dc.DrawText(temp, bounds.centerX(), bounds.centerY() + 40, textColor, ALIGN_CENTER);
	dc.DrawText(cr->T_cstr("license", "Free Software under GPL 2.0+"), bounds.centerX(), bounds.centerY() + 70, textColor, ALIGN_CENTER);

	int ppsspp_org_y = bounds.h / 2 + 130;
	dc.DrawText("www.ppsspp.org", bounds.centerX(), ppsspp_org_y, textColor, ALIGN_CENTER);

#if !PPSSPP_PLATFORM(UWP) || defined(_DEBUG)
	// Draw the graphics API, except on UWP where it's always D3D11
	std::string apiName(gr->T(screenManager()->getDrawContext()->GetInfoString(InfoField::APINAME)));
#ifdef _DEBUG
	apiName += ", debug build ";
	// Add some emoji for testing.
	apiName += CodepointToUTF8(0x1F41B) + CodepointToUTF8(0x1F41C) + CodepointToUTF8(0x1F914);
#endif
	dc.DrawText(apiName, bounds.centerX(), ppsspp_org_y + 50, textColor, ALIGN_CENTER);
#endif

	dc.Flush();
}

void CreditsScreen::CreateViews() {
	using namespace UI;
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto cr = GetI18NCategory(I18NCat::PSPCREDITS);
	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	Button *back = root_->Add(new Button(di->T("Back"), new AnchorLayoutParams(260, 64, NONE, NONE, 10, 10, false)));
	back->OnClick.Handle<UIScreen>(this, &UIScreen::OnOK);
	root_->SetDefaultFocusView(back);

	const bool gold = System_GetPropertyBool(SYSPROP_APP_GOLD);

	// Really need to redo this whole layout with some linear layouts...

	int rightYOffset = 0;
	if (!System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		ScreenManager *sm = screenManager();
		root_->Add(new Button(mm->T("Buy PPSSPP Gold"), new AnchorLayoutParams(260, 64, NONE, NONE, 10, 84, false)))->OnClick.Add([sm](UI::EventParams) {
			LaunchBuyGold(sm);
		});
		rightYOffset = 74;
	}
	root_->Add(new Button(cr->T("PPSSPP Forums"), new AnchorLayoutParams(260, 64, 10, NONE, NONE, 158, false)))->OnClick.Handle(this, &CreditsScreen::OnForums);
	root_->Add(new Button(cr->T("Discord"), new AnchorLayoutParams(260, 64, 10, NONE, NONE, 232, false)))->OnClick.Handle(this, &CreditsScreen::OnDiscord);
	root_->Add(new Button("www.ppsspp.org", new AnchorLayoutParams(260, 64, 10, NONE, NONE, 10, false)))->OnClick.Handle(this, &CreditsScreen::OnPPSSPPOrg);
	root_->Add(new Button(cr->T("Privacy Policy"), new AnchorLayoutParams(260, 64, 10, NONE, NONE, 84, false)))->OnClick.Handle(this, &CreditsScreen::OnPrivacy);
	root_->Add(new Button(cr->T("X @PPSSPP_emu"), new AnchorLayoutParams(260, 64, NONE, NONE, 10, rightYOffset + 84, false)))->OnClick.Handle(this, &CreditsScreen::OnX);

#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
	root_->Add(new Button(cr->T("Share PPSSPP"), new AnchorLayoutParams(260, 64, NONE, NONE, 10, rightYOffset + 158, false)))->OnClick.Handle(this, &CreditsScreen::OnShare);
#endif

	if (System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		root_->Add(new ShinyIcon(ImageID("I_ICON_GOLD"), new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 10, 10, NONE, NONE, false)))->SetScale(1.5f);
	} else {
		root_->Add(new ImageView(ImageID("I_ICON"), "", IS_DEFAULT, new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 10, 10, NONE, NONE, false)))->SetScale(1.5f);
	}
}

void CreditsScreen::OnX(UI::EventParams &e) {
	// Not sure we should change to x.com here, given various platform URL handlers etc. We can probably change it soon.
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://twitter.com/PPSSPP_emu");
}

void CreditsScreen::OnPPSSPPOrg(UI::EventParams &e) {
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org");
}

void CreditsScreen::OnPrivacy(UI::EventParams &e) {
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/privacy");
}

void CreditsScreen::OnForums(UI::EventParams &e) {
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://forums.ppsspp.org");
}

void CreditsScreen::OnDiscord(UI::EventParams &e) {
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://discord.gg/5NJB6dD");
}

void CreditsScreen::OnShare(UI::EventParams &e) {
	auto cr = GetI18NCategory(I18NCat::PSPCREDITS);
	System_ShareText(cr->T("CheckOutPPSSPP", "Check out PPSSPP, the awesome PSP emulator: https://www.ppsspp.org/"));
}

CreditsScreen::CreditsScreen() {
	startTime_ = time_now_d();
}

void CreditsScreen::update() {
	UIScreen::update();
	UpdateUIState(UISTATE_MENU);
}

void CreditsScreen::touch(const TouchInput &touch) {
	UIScreen::touch(touch);
	if (touch.id != 0)
		return;

	if (touch.flags & TOUCH_DOWN) {
		dragYStart_ = touch.y;
		dragYOffsetStart_ = dragOffset_;
	}
	if (touch.flags & TOUCH_UP) {
		dragYStart_ = -1.0f;
	}
	if (touch.flags & TOUCH_MOVE) {
		if (dragYStart_ >= 0.0f) {
			dragOffset_ = dragYOffsetStart_ + (touch.y - dragYStart_);
		}
	}
}


void CreditsScreen::DrawForeground(UIContext &dc) {
	auto cr = GetI18NCategory(I18NCat::PSPCREDITS);

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

	std::string_view credits[] = {
		System_GetPropertyBool(SYSPROP_APP_GOLD) ? "PPSSPP Gold" : "PPSSPP",
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
		"stenzek",  // For retroachievements integration
		"fp64",
		"",
		cr->T("specialthanks", "Special thanks to:"),
		specialthanksMaxim,
		specialthanksKeithGalocy,
		specialthanksOrphis,
		specialthanksangelxwind,
		specialthanksW_MS,
		specialthankssolarmystic,
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
#if PPSSPP_PLATFORM(ANDROID)
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
		"rcheevos",
		"SPIRV-Cross",
		"armips",
		"Basis Universal",
		"cityhash",
		"zstd",
		"glew",
		"libchdr",
		"minimp3",
		"xxhash",
		"naett-http",
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

	dc.Begin();
	const Bounds &bounds = dc.GetLayoutBounds();

	const int numItems = ARRAY_SIZE(credits);
	int itemHeight = 36;
	int totalHeight = numItems * itemHeight + bounds.h + 200;

	float t = (float)(time_now_d() - startTime_) * 60.0;

	float y = bounds.y2() - fmodf(t - dragOffset_, (float)totalHeight);
	for (int i = 0; i < numItems; i++) {
		float alpha = linearInOut(y+32, 64, bounds.y2() - 192, 64);
		uint32_t textColor = colorAlpha(dc.GetTheme().infoStyle.fgColor, alpha);

		if (alpha > 0.0f) {
			dc.SetFontScale(ease(alpha), ease(alpha));
			dc.DrawText(credits[i], bounds.centerX(), y, textColor, ALIGN_HCENTER);
			dc.SetFontScale(1.0f, 1.0f);
		}
		y += itemHeight;
	}

	dc.Flush();
}

SettingInfoMessage::SettingInfoMessage(int align, float cutOffY, UI::AnchorLayoutParams *lp)
	: UI::LinearLayout(UI::ORIENT_HORIZONTAL, lp), cutOffY_(cutOffY) {
	using namespace UI;
	SetSpacing(0.0f);
	Add(new UI::Spacer(10.0f));
	text_ = Add(new UI::TextView("", align, false, new LinearLayoutParams(1.0, Margins(0, 10))));
	Add(new UI::Spacer(10.0f));
}

void SettingInfoMessage::Show(std::string_view text, const UI::View *refView) {
	if (refView) {
		Bounds b = refView->GetBounds();
		const UI::AnchorLayoutParams *lp = GetLayoutParams()->As<UI::AnchorLayoutParams>();
		if (lp) {
			if (cutOffY_ != -1.0f && b.y >= cutOffY_) {
				ReplaceLayoutParams(new UI::AnchorLayoutParams(lp->width, lp->height, lp->left, 80.0f, lp->right, lp->bottom, lp->center));
			} else {
				ReplaceLayoutParams(new UI::AnchorLayoutParams(lp->width, lp->height, lp->left, g_display.dp_yres - 80.0f - 40.0f, lp->right, lp->bottom, lp->center));
			}
		}
	}
	if (text_) {
		text_->SetText(text);
	}
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

	UI::Style style = dc.GetTheme().tooltipStyle;

	if (alpha >= 0.001f) {
		uint32_t bgColor = alphaMul(style.background.color, alpha);
		dc.FillRect(UI::Drawable(bgColor), bounds_);
	}

	uint32_t textColor = alphaMul(style.fgColor, alpha);
	text_->SetTextColor(textColor);
	ViewGroup::Draw(dc);
	showing_ = sinceShow <= timeToShow; // Don't consider fade time
}

std::string SettingInfoMessage::GetText() const {
	return (showing_ && text_) ? text_->GetText() : "";
}

void ShinyIcon::Draw(UIContext &dc) {
	UI::DrawIconShine(dc, bounds_, 1.0f, animated_);
	UI::ImageView::Draw(dc);
}
