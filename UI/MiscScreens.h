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
#include "UI/BaseScreens.h"
#include "UI/SimpleDialogScreen.h"

struct ShaderInfo;
struct TextureShaderInfo;

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

class PromptScreen : public UIBaseDialogScreen {
public:
	PromptScreen(const Path &gamePath, std::string_view message, std::string_view yesButtonText, std::string_view noButtonText,
		std::function<void(bool)> callback = &NoOpVoidBool);

	void CreateViews() override;

	void TriggerFinish(DialogResult result) override;

	const char *tag() const override { return "Prompt"; }

private:
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

class CreditsScreen : public UISimpleBaseDialogScreen {
public:
	CreditsScreen() : UISimpleBaseDialogScreen() {}
	void update() override;

protected:
	std::string_view GetTitle() const override;

	void CreateDialogViews(UI::ViewGroup *parent) override;
	bool CanScroll() const override { return false; }
	const char *tag() const override { return "Credits"; }
};
