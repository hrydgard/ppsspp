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

#pragma once

#include "base/functional.h"
#include "UI/PluginScreen.h"
#include "ext/vjson/json.h"
#include "i18n/i18n.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui_context.h"
#include "Core/HW/atrac3plus.h"

void UIScreen::update(InputState &input) {
	if (!root_) {
		CreateViews();
	}

	if (orientationChanged_) {
		delete root_;
		root_ = 0;
		CreateViews();
	}

	UpdateViewHierarchy(input, root_);
}

void UIScreen::render() {
	UI::LayoutViewHierarchy(*screenManager()->getUIContext(), root_);

	screenManager()->getUIContext()->Begin();
	root_->Draw(*screenManager()->getUIContext());
	screenManager()->getUIContext()->End();
	screenManager()->getUIContext()->Flush();
}

void UIScreen::touch(const TouchInput &touch) {
	root_->Touch(touch);
}

PluginScreen::PluginScreen() {
	// Let's start by downloading the json. We'll find out in Update when it's finished.
	json_ = downloader_.StartDownload("http://www.ppsspp.org/update/at3plusdecoder.json", "");
}

void PluginScreen::CreateViews() {
	I18NCategory *c = GetI18NCategory("Plugin");
	// Build the UI.

	using namespace UI;

	root_ = new LinearLayout(ORIENT_VERTICAL);

	Margins textMargins(20,17);

	root_->Add(new TextView(0, "Audio decoding support", ALIGN_HCENTER, 1.0f, new LinearLayoutParams(textMargins)));

	ViewGroup *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0));
	
	root_->Add(scroll);
	tvDescription_ = scroll->Add(new TextView(0, 
		"Would you like to install Atrac3+ decoding support by Mai?\n"
		"This is required for audio in many games.\n"
		"Note that there may be legality issues around non-clean-room\n"
		"reverse engineered code in the US and some other countries.\n"

		"Choose \"More Information\" for more info.\n", ALIGN_LEFT, 1.0f, new LinearLayoutParams(textMargins)));

	progress_ = root_->Add(new ProgressBar());
	progress_->SetVisibility(V_GONE);

	ViewGroup *buttonBar = new LinearLayout(ORIENT_HORIZONTAL);
	root_->Add(buttonBar);

	buttonBack_ = new Button(c->T("Back"));
	buttonBar->Add(buttonBack_);
	buttonDownload_ = new Button(c->T("Download"), new LinearLayoutParams(1.0));
	buttonBar->Add(buttonDownload_)->OnClick.Add(std::bind(&PluginScreen::OnDownload, this, placeholder::_1));
	buttonBar->Add(new Button(c->T("More Information"), new LinearLayoutParams(1.0)))->OnClick.Add(std::bind(&PluginScreen::OnInformation, this, placeholder::_1));
}

void PluginScreen::update(InputState &input) {
	UIScreen::update(input);

	downloader_.Update();

	if (json_.get() && json_->Done()) {
		std::string json;
		json_->buffer().TakeAll(&json);

		JsonReader reader(json.data(), json.size());
		const json_value *root = reader.root();

		std::string abi = "";
#if defined(_M_IX86) && defined(_WIN32)
		abi = "Win32";
#elif defined(_M_X64) && defined(_WIN32)
		abi = "Win64";
#elif defined(ARMEABI)
		abi = "armeabi";
#elif defined(ARMEABI_V7A)
		abi = "armeabi-v7a";
#endif
		if (!abi.empty()) {
			at3plusdecoderUrl_ = root->getString(abi.c_str(), "");
			if (at3plusdecoderUrl_.empty()) {
				buttonDownload_->SetEnabled(false);
			}
		}
		json_.reset();
	}

	if (at3plusdecoder_.get() && at3plusdecoder_->Done()) {
		// Done! yay.
		progress_->SetProgress(1.0);

		int result = at3plusdecoder_->ResultCode();

		if (result == 200) {
			// Yay!
			tvDescription_->SetText("Mai Atrac3plus plugin downloaded and installed.\n"
				                      "Please press Continue.");
			buttonDownload_->SetVisibility(UI::V_GONE);
		} else {
			char codeStr[8];
			sprintf(codeStr, "%i", result);
			tvDescription_->SetText(std::string("Failed to download (") + codeStr + ").\nPlease try again later.");
			buttonDownload_->SetEnabled(true);
		}
	}
}

UI::EventReturn PluginScreen::OnDownload(UI::EventParams &e) {
	buttonDownload_->SetEnabled(false);

	std::string destination = Atrac3plus_Decoder::GetInstalledFilename();

	at3plusdecoder_ = downloader_.StartDownload(at3plusdecoderUrl_, destination);
	// No decoder available for this arch
	// #error Unable to identify architecture

	return UI::EVENT_DONE;
}


UI::EventReturn PluginScreen::OnInformation(UI::EventParams &e) {
	return UI::EVENT_DONE;
}