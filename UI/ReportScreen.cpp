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
	void Update(const InputState &input_state) override;

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
	SetSpacing(-8.0f);

	I18NCategory *rp = GetI18NCategory("Reporting");
	group_ = new LinearLayout(ORIENT_HORIZONTAL);
	Add(new InfoItem(rp->T(captionKey), ""));
	Add(group_);

	group_->SetSpacing(0.0f);
	SetupChoices();
}

void RatingChoice::Update(const InputState &input_state) {
	LinearLayout::Update(input_state);

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

	EventParams e2;
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
	: UIScreenWithGameBackground(gamePath), overall_(-1), graphics_(-1), speed_(-1), gameplay_(-1),
	includeScreenshot_(true) {
	enableReporting_ = Reporting::IsEnabled();
	ratingEnabled_ = enableReporting_;
}

void ReportScreen::update(InputState &input) {
	if (screenshot_) {
		if (includeScreenshot_) {
			screenshot_->SetVisibility(V_VISIBLE);
		} else {
			screenshot_->SetVisibility(V_GONE);
		}
	}
	UIScreenWithGameBackground::update(input);
}

EventReturn ReportScreen::HandleChoice(EventParams &e) {
	if (overall_ == 4) {
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
	UpdateSubmit();
	return EVENT_DONE;
}

EventReturn ReportScreen::HandleReportingChange(EventParams &e) {
	if (overall_ == 4) {
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
	ViewGroup *leftColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, FILL_PARENT, 0.4f, contentMargins));
	LinearLayout *leftColumnItems = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(WRAP_CONTENT, FILL_PARENT));
	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);

	leftColumnItems->Add(new TextView(rp->T("FeedbackDesc", "How's the emulation?  Let us and the community know!"), new LinearLayoutParams(Margins(12, 5, 0, 5))));
	if (!Reporting::IsEnabled()) {
		reportingNotice_ = leftColumnItems->Add(new TextView(rp->T("FeedbackDisabled", "Compatibility server reports must be enabled."), new LinearLayoutParams(Margins(12, 5, 0, 5))));
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
		leftColumnItems->Add(new TextView(rp->T("FeedbackIncludeCRC", "Note: Battery will be used to send a disc CRC"), new LinearLayoutParams(Margins(12, 5, 0, 5))))->SetEnabledPtr(&enableReporting_);
	}
#endif

	std::string path = GetSysDirectory(DIRECTORY_SCREENSHOT);
	if (!File::Exists(path)) {
		File::CreateDir(path);
	}
	screenshotFilename_ = path + ".reporting.jpg";
	int shotWidth = 0, shotHeight = 0;
	if (TakeGameScreenshot(screenshotFilename_.c_str(), SCREENSHOT_JPG, SCREENSHOT_DISPLAY, &shotWidth, &shotHeight, 4)) {
		float scale = 340.0f * (1.0f / g_dpi_scale) * (1.0f / shotHeight);
		leftColumnItems->Add(new CheckBox(&includeScreenshot_, rp->T("FeedbackIncludeScreen", "Include a screenshot")))->SetEnabledPtr(&enableReporting_);
		screenshot_ = leftColumnItems->Add(new AsyncImageFileView(screenshotFilename_, IS_DEFAULT, nullptr, new LinearLayoutParams(shotWidth * scale, shotHeight * scale, Margins(12, 0))));
	} else {
		includeScreenshot_ = false;
		screenshot_ = nullptr;
	}

	leftColumnItems->Add(new CompatRatingChoice("Overall", &overall_))->SetEnabledPtr(&enableReporting_)->OnChoice.Handle(this, &ReportScreen::HandleChoice);
	leftColumnItems->Add(new RatingChoice("Graphics", &graphics_))->SetEnabledPtr(&ratingEnabled_)->OnChoice.Handle(this, &ReportScreen::HandleChoice);
	leftColumnItems->Add(new RatingChoice("Speed", &speed_))->SetEnabledPtr(&ratingEnabled_)->OnChoice.Handle(this, &ReportScreen::HandleChoice);
	leftColumnItems->Add(new RatingChoice("Gameplay", &gameplay_))->SetEnabledPtr(&ratingEnabled_)->OnChoice.Handle(this, &ReportScreen::HandleChoice);

	rightColumnItems->SetSpacing(0.0f);
	rightColumnItems->Add(new Choice(rp->T("Open Browser")))->OnClick.Handle(this, &ReportScreen::HandleBrowser);
	submit_ = new Choice(rp->T("Submit Feedback"));
	rightColumnItems->Add(submit_)->OnClick.Handle(this, &ReportScreen::HandleSubmit);
	UpdateSubmit();

	rightColumnItems->Add(new Spacer(25.0));
	rightColumnItems->Add(new Choice(di->T("Back"), "", false, new AnchorLayoutParams(150, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	root_ = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
	root_->Add(leftColumn);
	root_->Add(rightColumn);

	leftColumn->Add(leftColumnItems);
	rightColumn->Add(rightColumnItems);
}

void ReportScreen::UpdateSubmit() {
	submit_->SetEnabled(enableReporting_ && overall_ >= 0 && graphics_ >= 0 && speed_ >= 0 && gameplay_ >= 0);
}

EventReturn ReportScreen::HandleSubmit(EventParams &e) {
	const char *compat;
	switch (overall_) {
	case 0: compat = "perfect"; break;
	case 1: compat = "playable"; break;
	case 2: compat = "ingame"; break;
	case 3: compat = "menu"; break;
	case 4: compat = "none"; break;
	default: compat = "unknown"; break;
	}

	if (Reporting::Enable(enableReporting_, "report.ppsspp.org")) {
		Reporting::UpdateConfig();
		g_Config.Save();
	}

	std::string filename = includeScreenshot_ ? screenshotFilename_ : "";
	Reporting::ReportCompatibility(compat, graphics_ + 1, speed_ + 1, gameplay_ + 1, filename);
	screenManager()->finishDialog(this, DR_OK);
	screenManager()->push(new ReportFinishScreen(gamePath_));
	return EVENT_DONE;
}

EventReturn ReportScreen::HandleBrowser(EventParams &e) {
	const std::string url = "http://" + Reporting::ServerHost() + "/";
	LaunchBrowser(url.c_str());
	return EVENT_DONE;
}

ReportFinishScreen::ReportFinishScreen(const std::string &gamePath)
	: UIScreenWithGameBackground(gamePath), resultNotice_(nullptr), setStatus_(false) {
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

	leftColumnItems->Add(new TextView(rp->T("FeedbackThanks", "Thanks for your feedback."), new LinearLayoutParams(Margins(12, 5, 0, 5))));
	resultNotice_ = leftColumnItems->Add(new TextView(rp->T("FeedbackDelayInfo", "Your data is being submitted in the background."), new LinearLayoutParams(Margins(12, 5, 0, 5))));

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

void ReportFinishScreen::update(InputState &input) {
	I18NCategory *rp = GetI18NCategory("Reporting");

	if (!setStatus_) {
		Reporting::Status status = Reporting::GetStatus();
		switch (status) {
		case Reporting::Status::WORKING:
			resultNotice_->SetText(rp->T("FeedbackSubmitDone", "Your data has been submitted."));
			break;

		case Reporting::Status::FAILING:
			resultNotice_->SetText(rp->T("FeedbackSubmitFail", "Could not submit data to server.  Try updating PPSSPP."));
			break;

		case Reporting::Status::BUSY:
		default:
			// Can't update yet.
			break;
		}
	}

	UIScreenWithGameBackground::update(input);
}

UI::EventReturn ReportFinishScreen::HandleViewFeedback(UI::EventParams &e) {
	const std::string url = "http://" + Reporting::ServerHost() + "/game/" + Reporting::CurrentGameID();
	LaunchBrowser(url.c_str());
	return EVENT_DONE;
}
