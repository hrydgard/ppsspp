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


void UIScreen::update(InputState &input) {
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

	I18NCategory *c = GetI18NCategory("Plugin");
	// Build the UI.

	using namespace UI;

	root_ = new LinearLayout(ORIENT_VERTICAL);


	Margins textMargins(20,17);

	tvDescription_ = root_->Add(new TextView(0, "Audio decoding support", ALIGN_HCENTER, 1.0f, new LinearLayoutParams(textMargins)));
	tvDescription_ = root_->Add(new TextView(0, 
		"Would you like to install Atrac3+ decoding support by Mai?\n"
		"Note that there may be legality issues around non-clean-room\n"
		"reverse engineered code in the US and some other countries.\n"
		"Choose \"More Information\" for more info.\n", ALIGN_LEFT, 1.0f, new LinearLayoutParams(1.0, textMargins)));

	ViewGroup *buttonBar = new LinearLayout(ORIENT_HORIZONTAL);
	root_->Add(buttonBar);

	buttonDownload_ = new Button(c->T("Download"), new LinearLayoutParams(1.0));
	buttonBar->Add(buttonDownload_)->OnClick.Add(std::bind(&PluginScreen::OnDownload, this, p::_1));
	buttonBar->Add(new Button(c->T("More Information"), new LinearLayoutParams(1.0)))->OnClick.Add(std::bind(&PluginScreen::OnInformation, this, p::_1));
}

void PluginScreen::update(InputState &input) {
	downloader_.Update();

	if (json_.get() && json_->Done()) {
		std::string json;
		json_->buffer().TakeAll(&json);

		JsonReader reader(json.data(), json.size());
		reader.parse();
		const json_value *root = reader.root();

		std::string destination = Atrac3plus_Decoder::GetInstalledFilename();

		json_.reset();
	}

	if (at3plusdecoder_.get() && at3plusdecoder_->Done()) {
		// Done! yay.

	}

	UIScreen::update(input);
}

UI::EventReturn PluginScreen::OnDownload(UI::EventParams &e) {
	buttonDownload_->SetEnabled(false);

#if 0
#if defined(_M_IX86) && defined(_WIN32)
	at3plusdecoder_ = downloader_.StartDownload(root->getString("Win32"), destination);
#elif defined(_M_X64) && defined(_WIN32)
	at3plusdecoder_ = downloader_.StartDownload(root->getString("Win64"), destination);
#elif defined(ARMEABI)
	at3plusdecoder_ = downloader_.StartDownload(root->getString("armeabi"), destination);
#elif defined(ARMEABI_V7A)
	at3plusdecoder_ = downloader_.StartDownload(root->getString("armeabi-v7a"), destination);
#else
	// No decoder available for this arch
	// #error Unable to identify architecture
#endif
#endif

	return UI::EVENT_DONE;
}


UI::EventReturn PluginScreen::OnInformation(UI::EventParams &e) {
	return UI::EVENT_DONE;
}