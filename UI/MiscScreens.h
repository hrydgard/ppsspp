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

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Common/UI/UIScreen.h"
#include "Common/UI/PopupScreens.h"
#include "Common/File/DirListing.h"
#include "Common/File/Path.h"

struct ShaderInfo;
struct TextureShaderInfo;

extern Path boot_filename;
void UIBackgroundInit(UIContext &dc);
void UIBackgroundShutdown();

inline void NoOpVoidBool(bool) {}

class BackgroundScreen : public UIScreen {
public:
	ScreenRenderFlags render(ScreenRenderMode mode) override;
	void sendMessage(UIMessage message, const char *value) override;

private:
	void CreateViews() override {}
	const char *tag() const override { return "bg"; }

	Path gamePath_;
};

// This doesn't have anything to do with the background anymore. It's just a PPSSPP UIScreen
// that knows how handle sendMessage properly. Same for all the below.
class UIScreenWithBackground : public UIScreen {
public:
	UIScreenWithBackground() : UIScreen() {}
protected:
	void sendMessage(UIMessage message, const char *value) override;
};

class UIScreenWithGameBackground : public UIScreenWithBackground {
public:
	UIScreenWithGameBackground(const Path &gamePath) : UIScreenWithBackground(), gamePath_(gamePath) {}
	void sendMessage(UIMessage message, const char *value) override;
protected:
	Path gamePath_;

	bool forceTransparent_ = false;
	bool darkenGameBackground_ = true;
};

class UIDialogScreenWithBackground : public UIDialogScreen {
public:
	UIDialogScreenWithBackground() : UIDialogScreen() {}
protected:
	void sendMessage(UIMessage message, const char *value) override;
	void AddStandardBack(UI::ViewGroup *parent);
};

class UIDialogScreenWithGameBackground : public UIDialogScreenWithBackground {
public:
	UIDialogScreenWithGameBackground(const Path &gamePath)
		: UIDialogScreenWithBackground(), gamePath_(gamePath) {}
	void sendMessage(UIMessage message, const char *value) override;
protected:
	Path gamePath_;
};

class PromptScreen : public UIDialogScreenWithGameBackground {
public:
	PromptScreen(const Path& gamePath, std::string_view message, std::string_view yesButtonText, std::string_view noButtonText,
		std::function<void(bool)> callback = &NoOpVoidBool);

	void CreateViews() override;

	void TriggerFinish(DialogResult result) override;

	const char *tag() const override { return "Prompt"; }

private:
	UI::EventReturn OnYes(UI::EventParams &e);
	UI::EventReturn OnNo(UI::EventParams &e);

	std::string message_;
	std::string yesButtonText_;
	std::string noButtonText_;
	std::function<void(bool)> callback_;
};

class NewLanguageScreen : public UI::ListPopupScreen {
public:
	NewLanguageScreen(std::string_view title);

	const char *tag() const override { return "NewLanguage"; }

private:
	void OnCompleted(DialogResult result) override;
	bool ShowButtons() const override { return true; }
	std::vector<File::FileInfo> langs_;
};

class TextureShaderScreen : public UI::ListPopupScreen {
public:
	TextureShaderScreen(std::string_view title);

	void CreateViews() override;

	const char *tag() const override { return "TextureShader"; }

private:
	void OnCompleted(DialogResult result) override;
	bool ShowButtons() const override { return true; }
	std::vector<TextureShaderInfo> shaders_;
};

enum class AfterLogoScreen {
	TO_GAME_SETTINGS,
	DEFAULT,
	MEMSTICK_SCREEN_INITIAL_SETUP,
};

class LogoScreen : public UIScreen {
public:
	LogoScreen(AfterLogoScreen afterLogoScreen = AfterLogoScreen::DEFAULT);

	bool key(const KeyInput &key) override;
	void touch(const TouchInput &touch) override;
	void update() override;
	void DrawForeground(UIContext &ui) override;
	void sendMessage(UIMessage message, const char *value) override;
	void CreateViews() override {}

	const char *tag() const override { return "Logo"; }

private:
	void Next();
	int frames_ = 0;
	double sinceStart_ = 0.0;
	bool switched_ = false;
	AfterLogoScreen afterLogoScreen_;
};

class CreditsScreen : public UIDialogScreenWithBackground {
public:
	CreditsScreen();
	void update() override;
	void DrawForeground(UIContext &ui) override;

	void CreateViews() override;

	const char *tag() const override { return "Credits"; }

private:
	UI::EventReturn OnSupport(UI::EventParams &e);
	UI::EventReturn OnPPSSPPOrg(UI::EventParams &e);
	UI::EventReturn OnPrivacy(UI::EventParams &e);
	UI::EventReturn OnForums(UI::EventParams &e);
	UI::EventReturn OnDiscord(UI::EventParams &e);
	UI::EventReturn OnShare(UI::EventParams &e);
	UI::EventReturn OnTwitter(UI::EventParams &e);

	double startTime_ = 0.0;
};

class SettingInfoMessage : public UI::LinearLayout {
public:
	SettingInfoMessage(int align, float cutOffY, UI::AnchorLayoutParams *lp);

	void Show(std::string_view text, const UI::View *refView = nullptr);

	void Draw(UIContext &dc) override;
	std::string GetText() const;

private:
	UI::TextView *text_ = nullptr;
	double timeShown_ = 0.0;
	float cutOffY_;
	bool showing_ = false;
};

uint32_t GetBackgroundColorWithAlpha(const UIContext &dc);
