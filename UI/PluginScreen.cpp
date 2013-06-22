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

#include "base/functional.h"
#include "UI/PluginScreen.h"
#include "ext/vjson/json.h"
#include "i18n/i18n.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui_context.h"
#include "UI/ui_atlas.h"
#include "Core/HW/atrac3plus.h"

#ifdef __APPLE__
#include "TargetConditionals.h"
#if TARGET_OS_MAC
#define MACOSX
#endif
#endif

void DrawBackground(float alpha);

void PluginScreen::DrawBackground(UIContext &dc)
{
	::DrawBackground(1.0f);
}

PluginScreen::PluginScreen() {
	// Let's start by downloading the json. We'll find out in Update when it's finished.
	json_ = downloader_.StartDownload("http://www.ppsspp.org/update/at3plusdecoder.json", "");
}

void PluginScreen::CreateViews() {
	I18NCategory *p = GetI18NCategory("Plugin");
	// Build the UI.

	using namespace UI;

	root_ = new LinearLayout(ORIENT_VERTICAL);

	Margins textMargins(20,17);
	Margins buttonMargins(10,10);

	root_->Add(new TextView(UBUNTU24, "Atrac3+ Audio Support", ALIGN_HCENTER, 1.5f, new LinearLayoutParams(textMargins)));

	ViewGroup *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0));
	LinearLayout *scrollContents = new LinearLayout(ORIENT_VERTICAL);
	root_->Add(scroll);
	scroll->Add(scrollContents);

	tvDescription_ = scrollContents->Add(new TextView(0, "Looking for download...", ALIGN_LEFT, 1.0f, new LinearLayoutParams(textMargins)));

	const char *legalityNotice =
		p->T("Origins are dubious", "* Mai's Atrac3+ decoder is currently required\n"
		"for background audio and voice in many games.\n"
		"Please note that the origins of this code are dubious.\n"
		"Choose More Information for more information.");

	scrollContents->Add(new TextView(0, legalityNotice, ALIGN_LEFT, 0.65f, new LinearLayoutParams(textMargins) ));

	progress_ = root_->Add(new ProgressBar());
	progress_->SetVisibility(V_GONE);

	ViewGroup *buttonBar = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(buttonMargins));
	root_->Add(buttonBar);

	buttonBack_ = new Button(p->T("Back"), new LinearLayoutParams(1.0));
	buttonBar->Add(buttonBack_)->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	buttonDownload_ = new Button(p->T("Download and install"), new LinearLayoutParams(1.0));
	buttonDownload_->SetEnabled(false);
	buttonBar->Add(buttonDownload_)->OnClick.Handle(this, &PluginScreen::OnDownload);
	buttonBar->Add(new Button(p->T("More Information"), new LinearLayoutParams(1.0)))->OnClick.Handle(this, &PluginScreen::OnInformation);
}

void PluginScreen::update(InputState &input) {
	UIScreen::update(input);

	I18NCategory *p = GetI18NCategory("Plugin");

	downloader_.Update();

	if (json_.get() && json_->Done()) {
		if (json_->ResultCode() != 200) {
			char codeStr[18];
			sprintf(codeStr, "%i", json_->ResultCode());
			tvDescription_->SetText(p->T("Failed to reach server", "Failed to reach server.\nPlease try again later and check that you have a\nworking internet connection."));
			buttonDownload_->SetEnabled(false);
		} else {
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
#elif defined(MACOSX)
			abi = "MacOSX64";
#endif
			const char *notSupportedText = p->T("SorryNoDownload", "Sorry, there is no automatic download of the decoder\navailable for this platform.");
			if (!abi.empty()) {
				at3plusdecoderUrl_ = root->getString(abi.c_str(), "");
				if (at3plusdecoderUrl_.empty()) {
					buttonDownload_->SetEnabled(false);
					tvDescription_->SetText(notSupportedText);
				} else {
					buttonDownload_->SetEnabled(true);
					const char *notInstalledText = p->T("To download and install", "To download and install Mai's Atrac3+ decoding\n support, click Download.");
					const char *reInstallText = p->T("Already installed", "Mai's Atrac3+ decoder already installed.\nWould you like to redownload and reinstall it?");
					tvDescription_->SetText(Atrac3plus_Decoder::IsInstalled() ? reInstallText : notInstalledText);
				}
			} else {
				tvDescription_->SetText(notSupportedText);
			}
		}

		json_.reset();
	}

	if (at3plusdecoder_.get() && at3plusdecoder_->Done()) {
		// Done! yay.
		progress_->SetProgress(1.0);

		if (at3plusdecoder_->ResultCode() == 200) {
			// Yay!
			tvDescription_->SetText(p->T("Installed Correctly", "Mai Atrac3plus plugin downloaded and installed.\n"
				                      "Please press Back."));
			buttonDownload_->SetVisibility(UI::V_GONE);
		} else {
			char codeStr[18];
			sprintf(codeStr, "%i", at3plusdecoder_->ResultCode());
			tvDescription_->SetText(p->T("Failed to download plugin", "Failed to download plugin.\nPlease try again later."));
			progress_->SetVisibility(UI::V_GONE);
			buttonDownload_->SetEnabled(true);
		}

		at3plusdecoder_.reset();
	}
}

UI::EventReturn PluginScreen::OnDownload(UI::EventParams &e) {
	buttonDownload_->SetEnabled(false);

	std::string destination = Atrac3plus_Decoder::GetInstalledFilename();
	at3plusdecoder_ = downloader_.StartDownload(at3plusdecoderUrl_, destination);
	progress_->SetVisibility(UI::V_VISIBLE);
	return UI::EVENT_DONE;
}

UI::EventReturn PluginScreen::OnInformation(UI::EventParams &e) {
	LaunchBrowser("http://www.ppsspp.org/at3plusdecoder.html");
	return UI::EVENT_DONE;
}

