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
#include "i18n/i18n.h"
#include "gfx_es2/draw_buffer.h"
#include "ui/ui_context.h"
#include "UI/ReportScreen.h"

#include "Core/Reporting.h"
#include "Common/Log.h"

using namespace UI;

class RatingChoice : public LinearLayout {
public:
	RatingChoice(const char *captionKey, int *value, LayoutParams *layoutParams = 0);

	Event OnChoice;

private:
	void AddChoice(int i, const std::string &title);
	EventReturn OnChoiceClick(EventParams &e);

	LinearLayout *group_;
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
	AddChoice(0, rp->T("Bad"));
	AddChoice(1, rp->T("OK"));
	AddChoice(2, rp->T("Great"));
}

void RatingChoice::AddChoice(int i, const std::string &title) {
	auto c = group_->Add(new StickyChoice(title, ""));
	c->OnClick.Handle(this, &RatingChoice::OnChoiceClick);
	if (*value_ == i)
		c->Press();
}

EventReturn RatingChoice::OnChoiceClick(EventParams &e) {
	// Unstick the other choices that weren't clicked.
	for (int i = 0; i < 3; i++) {
		auto v = group_->GetViewByIndex(i);
		if (v != e.v) {
			static_cast<StickyChoice *>(v)->Release();
		} else {
			*value_ = i;
		}
	}

	EventParams e2;
	e2.v = e.v;
	e2.a = *value_;
	// Dispatch immediately (we're already on the UI thread as we're in an event handler).
	return OnChoice.Dispatch(e2);
}

ReportScreen::ReportScreen(const std::string &gamePath)
	: UIScreenWithGameBackground(gamePath), graphics_(-1), speed_(-1), gameplay_(-1) {
}

EventReturn ReportScreen::HandleChoice(EventParams &e) {
	submit_->SetEnabled(graphics_ >= 0 && speed_ >= 0 && gameplay_ >= 0);
	return EVENT_DONE;
}

void ReportScreen::CreateViews() {
	I18NCategory *rp = GetI18NCategory("Reporting");
	I18NCategory *d = GetI18NCategory("Dialog");
	Margins actionMenuMargins(0, 100, 15, 0);
	ViewGroup *leftColumn = new AnchorLayout(new LinearLayoutParams(1.0f));
	ViewGroup *leftColumnItems = new LinearLayout(ORIENT_VERTICAL);
	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);

	leftColumnItems->Add(new InfoItem(rp->T("FeedbackDesc", "How's the emulation?  Let us and the community know!"), ""));

	// TODO: screenshot
	leftColumnItems->Add(new RatingChoice("Graphics", &graphics_))->OnChoice.Handle(this, &ReportScreen::HandleChoice);
	leftColumnItems->Add(new RatingChoice("Speed", &speed_))->OnChoice.Handle(this, &ReportScreen::HandleChoice);
	leftColumnItems->Add(new RatingChoice("Gameplay", &gameplay_))->OnChoice.Handle(this, &ReportScreen::HandleChoice);

	rightColumnItems->SetSpacing(0.0f);
	// TODO: Handle.
	rightColumnItems->Add(new Choice(rp->T("Open Browser")));
	submit_ = new Choice(rp->T("Submit Feedback"));
	// TODO: Handle.
	rightColumnItems->Add(submit_);
	submit_->SetEnabled(graphics_ >= 0 && speed_ >= 0 && gameplay_ >= 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);
	root_->Add(leftColumn);
	root_->Add(rightColumn);

	leftColumn->Add(leftColumnItems);
	leftColumn->Add(new Choice(d->T("Back"), "", false, new AnchorLayoutParams(150, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	rightColumn->Add(rightColumnItems);
}