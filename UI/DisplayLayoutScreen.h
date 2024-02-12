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

#include <deque>

#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "GPU/Common/PostShader.h"

#include "MiscScreens.h"

class DisplayLayoutScreen : public UIDialogScreenWithGameBackground {
public:
	DisplayLayoutScreen(const Path &filename);
	void CreateViews() override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;
	void onFinish(DialogResult reason) override;

	void resized() override {
		RecreateViews();
	}

	bool wantBrightBackground() const override { return true; }

	const char *tag() const override { return "DisplayLayout"; }
	
protected:
	UI::EventReturn OnPostProcShaderChange(UI::EventParams &e);

	void sendMessage(UIMessage message, const char *value) override;
	void DrawBackground(UIContext &dc) override;

private:
	UI::ChoiceStrip *mode_ = nullptr;
	UI::Choice *postProcChoice_ = nullptr;
	std::string shaderNames_[256];
	std::deque<bool> settingsVisible_;  // vector<bool> is an insane bitpacked specialization!
};

class PostProcScreen : public UI::ListPopupScreen {
public:
	PostProcScreen(std::string_view title, int id, bool showStereoShaders)
		: ListPopupScreen(title), id_(id), showStereoShaders_(showStereoShaders) { }

	void CreateViews() override;

	const char *tag() const override { return "PostProc"; }

private:
	void OnCompleted(DialogResult result) override;
	bool ShowButtons() const override { return true; }
	std::vector<ShaderInfo> shaders_;
	int id_;
	bool showStereoShaders_;
	std::vector<int> indexTranslation_;
};
