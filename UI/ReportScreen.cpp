// Copyright (c) 2014- PPSSPP Project.

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

#include <string>
#include "base/display.h"
// TODO: For text align flags, probably shouldn';t be in gfx_es2/...
#include "gfx_es2/draw_buffer.h"
#include "i18n/i18n.h"
#include "thin3d/thin3d.h"
#include "ui/ui_context.h"
#include "UI/PauseScreen.h"
#include "UI/ReportScreen.h"

#include "Core/Core.h"
#include "Core/Reporting.h"
#include "Core/Screenshot.h"
#include "Core/System.h"
#include "Common/FileUtil.h"
#include "Common/Log.h"

using namespace UI;

class RatingChoice : public LinearLayout {
public:
	RatingChoice(const char *captionKey, int *value, LayoutParams *layoutParams = 0);

	RatingChoice *SetEnabledPtr(bool *enabled);

	Event OnChoice;

protected:
	void Update() override;

	virtual void SetupChoices();
	virtual int TotalChoices() {
		return 3;
	}
	void AddChoice(int i, const std::string &title);
	StickyChoice *GetChoice(int i) {
		return static_cast<StickyChoice *>(group_->GetViewByIndex(i));
	}

	LinearLayout *group_;

private:
	EventReturn OnChoiceClick(EventParams &e);

	int *value_;
};

RatingChoice::RatingChoice(const char *captionKey, int *value, LayoutParams *layoutParams)
		: LinearLayout(ORIENT_VERTICAL, layoutParams), value_(value) {
	SetSpacing(0.0f);

	I18NCategory *rp = GetI18NCategory("Reporting");
	group_ = new LinearLayout(ORIENT_HORIZONTAL);
	Add(new TextView(rp->T(captionKey), FLAG_WRAP_TEXT, false))->SetShadow(true);
	Add(group_);

	group_->SetSpacing(0.0f);
	SetupChoices();
}

void RatingChoice::Update() {
	LinearLayout::Update();

	for (int i = 0; i < TotalChoices(); i++) {
		StickyChoice *chosen = GetChoice(i);
		bool down = chosen->IsDown();
		if (down && *value_ != i) {
			chosen->Release();
		} else if (!down && *value_ == i) {
			chosen->Press();
		}
	}
}

RatingChoice *RatingChoice::SetEnabledPtr(bool *ptr) {
	for (int i = 0; i < TotalChoices(); i++) {
		GetChoice(i)->SetEnabledPtr(ptr);
	}

	return this;
}

void RatingChoice::SetupChoices() {
	I18NCategory *rp = GetI18NCategory("Reporting");
	AddChoice(0, rp->T("Bad"));
	AddChoice(1, rp->T("OK"));
	AddChoice(2, rp->T("Great"));
}

void RatingChoice::AddChoice(int i, const std::string &title) {
	auto c = group_->Add(new StickyChoice(title, ""));
	c->OnClick.Handle(this, &RatingChoice::OnChoiceClick);
}

EventReturn RatingChoice::OnChoiceClick(EventParams &e) {
	// Unstick the other choices that weren't clicked.
	int total = TotalChoices();
	for (int i = 0; i < total; i++) {
		StickyChoice *v = GetChoice(i);
		if (v != e.v) {
			v->Release();
		} else {
			*value_ = i;
		}
	}

	EventParams e2{};
	e2.v = e.v;
	e2.a = *value_;
	// Dispatch immediately (we're already on the UI thread as we're in an event handler).
	OnChoice.Dispatch(e2);
	return EVENT_DONE;
}

class CompatRatingChoice : public RatingChoice {
public:
	CompatRatingChoice(const char *captionKey, int *value, LayoutParams *layoutParams = 0);

protected:
	virtual void SetupChoices() override;
	virtual int TotalChoices() override {
		return 5;
	}
};

CompatRatingChoice::CompatRatingChoice(const char *captionKey, int *value, LayoutParams *layoutParams)
		: RatingChoice(captionKey, value, layoutParams) {
	SetupChoices();
}

void CompatRatingChoice::SetupChoices() {
	I18NCategory *rp = GetI18NCategory("Reporting");
	group_->Clear();
	AddChoice(0, rp->T("Perfect"));
	AddChoice(1, rp->T("Plays"));
	AddChoice(2, rp->T("In-game"));
	AddChoice(3, rp->T("Menu/Intro"));
	AddChoice(4, rp->T("Nothing"));
}

ReportScreen::ReportScreen(const std::string &gamePath)
	: UIDialogScreenWithGameBackground(gamePath) {
	enableReporting_ = Reporting::IsEnabled();
	ratingEnabled_ = enableReporting_;
}

void ReportScreen::update() {
	if (screenshot_) {
		if (includeScreenshot_) {
			screenshot_->SetVisibility(V_VISIBLE);
		} else {
			screenshot_->SetVisibility(V_GONE);
		}
	}
	UIDialogScreenWithGameBackground::update();
}

void ReportScreen::resized() {
	UIDialogScreenWithGameBackground::resized();
	RecreateViews();
}

EventReturn ReportScreen::HandleChoice(EventParams &e) {
	if (overall_ == ReportingOverallScore::NONE) {
		graphics_ = 0;
		speed_ = 0;
		gameplay_ = 0;
		ratingEnabled_ = false;
	} else if (!ratingEnabled_) {
		graphics_ = -1;
		speed_ = -1;
		gameplay_ = -1;
		ratingEnabled_ = true;
	}

	// Whether enabled before or not, move to Great when Perfect is selected.
	if (overall_ == ReportingOverallScore::PERFECT) {
		if (graphics_ == -1)
			graphics_ = 2;
		if (speed_ == -1)
			speed_ = 2;
		if (gameplay_ == -1)
			gameplay_ = 2;
	}

	UpdateSubmit();
	UpdateOverallDescription();
	return EVENT_DONE;
}

EventReturn ReportScreen::HandleReportingChange(EventParams &e) {
	if (overall_ == ReportingOverallScore::NONE) {
		ratingEnabled_ = false;
	} else {
		ratingEnabled_ = enableReporting_;
	}
	if (reportingNotice_) {
		reportingNotice_->SetTextColor(enableReporting_ ? 0xFFFFFFFF : 0xFF3030FF);
	}
	UpdateSubmit();
	return EVENT_DONE;
}

void ReportScreen::CreateViews() {
	I18NCategory *rp = GetI18NCategory("Reporting");
	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *sy = GetI18NCategory("System");

	Margins actionMenuMargins(0, 20, 15, 0);
	Margins contentMargins(0, 20, 5, 5);
	float leftColumnWidth = dp_xres - actionMenuMargins.horiz() - contentMargins.horiz() - 300.0f;
	ViewGroup *leftColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, FILL_PARENT, 0.4f, contentMargins));
	LinearLayout *leftColumnItems = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(WRAP_CONTENT, FILL_PARENT));
	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);

	leftColumnItems->Add(new TextView(rp->T("FeedbackDesc", "How's the emulation?  Let us and the community know!"), FLAG_WRAP_TEXT, false, new LinearLayoutParams(Margins(12, 5, 0, 5))))->SetShadow(true);
	if (!Reporting::IsEnabled()) {
		reportingNotice_ = leftColumnItems->Add(new TextView(rp->T("FeedbackDisabled", "Compatibility server reports must be enabled."), FLAG_WRAP_TEXT, false, new LinearLayoutParams(Margins(12, 5, 0, 5))));
		reportingNotice_->SetShadow(true);
		reportingNotice_->SetTextColor(0xFF3030FF);
		CheckBox *reporting = leftColumnItems->Add(new CheckBox(&enableReporting_, sy->T("Enable Compatibility Server Reports")));
		reporting->SetEnabled(Reporting::IsSupported());
		reporting->OnClick.Handle(this, &ReportScreen::HandleReportingChange);
	} else {
		reportingNotice_ = nullptr;
	}

#ifdef MOBILE_DEVICE
	if (!Core_GetPowerSaving()) {
		auto crcWarning = new TextView(rp->T("FeedbackIncludeCRC", "Note: Battery will be used to send a disc CRC"), FLAG_WRAP_TEXT, false, new LinearLayoutParams(Margins(12, 5, 0, 5)));
		crcWarning->SetShadow(true);
		crcWarning->SetEnabledPtr(&enableReporting_);
		leftColumnItems->Add(crcWarning);
	}
#endif

	std::string path = GetSysDirectory(DIRECTORY_SCREENSHOT);
	if (!File::Exists(path)) {
		File::CreateDir(path);
	}
	screenshotFilename_ = path + ".reporting.jpg";
	int shotWidth = 0, shotHeight = 0;
	if (TakeGameScreenshot(screenshotFilename_.c_str(), ScreenshotFormat::JPG, SCREENSHOT_DISPLAY, &shotWidth, &shotHeight, 4)) {
		float scale = 340.0f * (1.0f / g_dpi_scale_y) * (1.0f / shotHeight);
		leftColumnItems->Add(new CheckBox(&includeScreenshot_, rp->T("FeedbackIncludeScreen", "Include a screenshot")))->SetEnabledPtr(&enableReporting_);
		screenshot_ = leftColumnItems->Add(new AsyncImageFileView(screenshotFilename_, IS_KEEP_ASPECT, nullptr, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(12, 0))));
	} else {
		includeScreenshot_ = false;
		screenshot_ = nullptr;
	}

	leftColumnItems->Add(new CompatRatingChoice("Overall", (int *)&overall_))->SetEnabledPtr(&enableReporting_)->OnChoice.Handle(this, &ReportScreen::HandleChoice);
	overallDescription_ = leftColumnItems->Add(new TextView("", FLAG_WRAP_TEXT, false, new LinearLayoutParams(Margins(10, 0))));
	overallDescription_->SetShadow(true);

	UI::Orientation ratingsOrient = leftColumnWidth >= 750.0f ? ORIENT_HORIZONTAL : ORIENT_VERTICAL;
	UI::LinearLayout *ratingsHolder = new LinearLayout(ratingsOrient, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
	leftColumnItems->Add(ratingsHolder);
	ratingsHolder->Add(new RatingChoice("Graphics", &graphics_))->SetEnabledPtr(&ratingEnabled_)->OnChoice.Handle(this, &ReportScreen::HandleChoice);
	ratingsHolder->Add(new RatingChoice("Speed", &speed_))->SetEnabledPtr(&ratingEnabled_)->OnChoice.Handle(this, &ReportScreen::HandleChoice);
	ratingsHolder->Add(new RatingChoice("Gameplay", &gameplay_))->SetEnabledPtr(&ratingEnabled_)->OnChoice.Handle(this, &ReportScreen::HandleChoice);

	rightColumnItems->SetSpacing(0.0f);
	rightColumnItems->Add(new Choice(rp->T("Open Browser")))->OnClick.Handle(this, &ReportScreen::HandleBrowser);
	submit_ = new Choice(rp->T("Submit Feedback"));
	rightColumnItems->Add(submit_)->OnClick.Handle(this, &ReportScreen::HandleSubmit);
	UpdateSubmit();
	UpdateOverallDescription();

	rightColumnItems->Add(new Spacer(25.0));
	rightColumnItems->Add(new Choice(di->T("Back"), "", false, new AnchorLayoutParams(150, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	root_ = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
	root_->Add(leftColumn);
	root_->Add(rightColumn);

	leftColumn->Add(leftColumnItems);
	rightColumn->Add(rightColumnItems);
}

void ReportScreen::UpdateSubmit() {
	submit_->SetEnabled(enableReporting_ && overall_ != ReportingOverallScore::INVALID && graphics_ >= 0 && speed_ >= 0 && gameplay_ >= 0);
}

void ReportScreen::UpdateOverallDescription() {
	I18NCategory *rp = GetI18NCategory("Reporting");
	const char *desc;
	uint32_t c = 0xFFFFFFFF;
	switch (overall_) {
	case ReportingOverallScore::PERFECT: desc = rp->T("Perfect Description", "Flawless emulation for the entire game - great!"); break;
	case ReportingOverallScore::PLAYABLE: desc = rp->T("Plays Description", "Fully playable but might be with glitches"); break;
	case ReportingOverallScore::INGAME: desc = rp->T("In-game Description", "Gets into gameplay, but too buggy to complete"); break;
	case ReportingOverallScore::MENU: desc = rp->T("Menu/Intro Description", "Can't get into the game itself"); break;
	case ReportingOverallScore::NONE: desc = rp->T("Nothing Description", "Completely broken"); c = 0xFF0000FF; break;
	default: desc = rp->T("Unselected Overall Description", "How well does this game emulate?"); break;
	}

	overallDescription_->SetText(desc);
	overallDescription_->SetTextColor(c);
}

EventReturn ReportScreen::HandleSubmit(EventParams &e) {
	const char *compat;
	switch (overall_) {
	case ReportingOverallScore::PERFECT: compat = "perfect"; break;
	case ReportingOverallScore::PLAYABLE: compat = "playable"; break;
	case ReportingOverallScore::INGAME: compat = "ingame"; break;
	case ReportingOverallScore::MENU: compat = "menu"; break;
	case ReportingOverallScore::NONE: compat = "none"; break;
	default: compat = "unknown"; break;
	}

	if (Reporting::Enable(enableReporting_, "report.ppsspp.org")) {
		Reporting::UpdateConfig();
		g_Config.Save("ReportScreen::HandleSubmit");
	}

	std::string filename = includeScreenshot_ ? screenshotFilename_ : "";
	Reporting::ReportCompatibility(compat, graphics_ + 1, speed_ + 1, gameplay_ + 1, filename);
	TriggerFinish(DR_OK);
	screenManager()->push(new ReportFinishScreen(gamePath_, overall_));
	return EVENT_DONE;
}

EventReturn ReportScreen::HandleBrowser(EventParams &e) {
	const std::string url = "http://" + Reporting::ServerHost() + "/";
	LaunchBrowser(url.c_str());
	return EVENT_DONE;
}

ReportFinishScreen::ReportFinishScreen(const std::string &gamePath, ReportingOverallScore score)
	: UIDialogScreenWithGameBackground(gamePath), score_(score) {
}

void ReportFinishScreen::CreateViews() {
	I18NCategory *rp = GetI18NCategory("Reporting");
	I18NCategory *di = GetI18NCategory("Dialog");

	Margins actionMenuMargins(0, 20, 15, 0);
	Margins contentMargins(0, 20, 5, 5);
	ViewGroup *leftColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, FILL_PARENT, 0.4f, contentMargins));
	LinearLayout *leftColumnItems = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(WRAP_CONTENT, FILL_PARENT));
	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);

	leftColumnItems->Add(new TextView(rp->T("FeedbackThanks", "Thanks for your feedback."), FLAG_WRAP_TEXT, false, new LinearLayoutParams(Margins(12, 5, 0, 5))))->SetShadow(true);
	if (score_ == ReportingOverallScore::PERFECT || score_ == ReportingOverallScore::PLAYABLE) {
		resultNotice_ = leftColumnItems->Add(new TextView(rp->T("FeedbackDelayInfo", "Your data is being submitted in the background."), FLAG_WRAP_TEXT, false, new LinearLayoutParams(Margins(12, 5, 0, 5))));
	} else {
		resultNotice_ = leftColumnItems->Add(new TextView(rp->T("SuggestionsWaiting", "Submitting and checking other user feedback.."), FLAG_WRAP_TEXT, false, new LinearLayoutParams(Margins(12, 5, 0, 5))));
	}
	resultNotice_->SetShadow(true);
	resultItems_ = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(12, 5, 0, 5)));
	leftColumnItems->Add(resultItems_);

	rightColumnItems->SetSpacing(0.0f);
	rightColumnItems->Add(new Choice(rp->T("View Feedback")))->OnClick.Handle(this, &ReportFinishScreen::HandleViewFeedback);

	rightColumnItems->Add(new Spacer(25.0));
	rightColumnItems->Add(new Choice(di->T("Back"), "", false, new AnchorLayoutParams(150, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	root_ = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
	root_->Add(leftColumn);
	root_->Add(rightColumn);

	leftColumn->Add(leftColumnItems);
	rightColumn->Add(rightColumnItems);
}

void ReportFinishScreen::update() {
	I18NCategory *rp = GetI18NCategory("Reporting");

	if (!setStatus_) {
		Reporting::ReportStatus status = Reporting::GetStatus();
		switch (status) {
		case Reporting::ReportStatus::WORKING:
			ShowSuggestions();
			setStatus_ = true;
			break;

		case Reporting::ReportStatus::FAILING:
			resultNotice_->SetText(rp->T("FeedbackSubmitFail", "Could not submit data to server.  Try updating PPSSPP."));
			setStatus_ = true;
			break;

		case Reporting::ReportStatus::BUSY:
		default:
			// Can't update yet.
			break;
		}
	}

	UIDialogScreenWithGameBackground::update();
}

void ReportFinishScreen::ShowSuggestions() {
	I18NCategory *rp = GetI18NCategory("Reporting");

	auto suggestions = Reporting::CompatibilitySuggestions();
	if (score_ == ReportingOverallScore::PERFECT || score_ == ReportingOverallScore::PLAYABLE) {
		resultNotice_->SetText(rp->T("FeedbackSubmitDone", "Your data has been submitted."));
	} else if (suggestions.empty()) {
		resultNotice_->SetText(rp->T("SuggestionsNone", "This game isn't working for other users too."));
	} else {
		resultNotice_->SetText(rp->T("SuggestionsFound", "Other users have reported better results.  Tap View Feedback for more detail."));

		resultItems_->Clear();
		bool shownConfig = false;
		bool valid = false;
		for (auto item : suggestions) {
			const char *suggestion = nullptr;
			if (item == "Upgrade") {
				suggestion = rp->T("SuggestionUpgrade", "Upgrade to a newer PPSSPP build");
			} if (item == "Downgrade") {
				suggestion = rp->T("SuggestionDowngrade", "Downgrade to an older PPSSPP version (please report this bug)");
			} else if (item == "VerifyDisc") {
				suggestion = rp->T("SuggestionVerifyDisc", "Check your ISO is a good copy of your disc");
			} else if (item == "Config:CPUSpeed:0") {
				suggestion = rp->T("SuggestionCPUSpeed0", "Disable locked CPU speed setting");
			} else {
				bool isConfig = startsWith(item, "Config:");
				if (isConfig && !shownConfig) {
					suggestion = rp->T("SuggestionConfig", "See reports on website for good settings");
					shownConfig = true;
				}
				// Ignore unknown configs, hopefully we recognized "Upgrade" at least.
			}

			if (suggestion) {
				valid = true;
				resultItems_->Add(new TextView(std::string(" - ") + suggestion, FLAG_WRAP_TEXT, false))->SetShadow(true);
			}
		}

		if (!valid) {
			// No actual valid versions.  Let's just say upgrade and hope the server's not broken.
			resultItems_->Add(new TextView(std::string(" - ") + rp->T("SuggestionUpgrade", "Upgrade to a newer PPSSPP build"), FLAG_WRAP_TEXT, false))->SetShadow(true);
		}
	}
}

UI::EventReturn ReportFinishScreen::HandleViewFeedback(UI::EventParams &e) {
	const std::string url = "http://" + Reporting::ServerHost() + "/game/" + Reporting::CurrentGameID();
	LaunchBrowser(url.c_str());
	return EVENT_DONE;
}
