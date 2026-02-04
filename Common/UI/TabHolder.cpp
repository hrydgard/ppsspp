#include "Common/Math/curves.h"
#include "Common/Data/Text/I18n.h"
#include "Common/UI/TabHolder.h"
#include "Common/UI/Root.h"
#include "Common/UI/ScrollView.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/UIScreen.h"

namespace UI {

TabHolder::TabHolder(Orientation orientation, float stripSize, TabHolderFlags flags, View *bannerView, std::function<void()> contextMenuCallback, LayoutParams *layoutParams)
	: LinearLayout(Opposite(orientation), layoutParams), tabOrientation_(orientation), flags_(flags) {
	SetSpacing(0.0f);
	if (orientation == ORIENT_HORIZONTAL) {
		// This orientation supports adding a back button.
		tabStrip_ = new ChoiceStrip(orientation, new LayoutParams(WRAP_CONTENT, WRAP_CONTENT));
		tabStrip_->SetTopTabs(true);

		if (flags & TabHolderFlags::BackButton) {
			ViewGroup *container = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
			Choice *c = new Choice(ImageID("I_NAVIGATE_BACK"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT));
			c->OnClick.Add([](EventParams &e) {
				e.bubbleResult = DR_BACK;
			});
			container->Add(c);
			tabScroll_ = new ScrollView(orientation, new LinearLayoutParams(1.0f));
			tabScroll_->Add(tabStrip_);
			container->Add(tabScroll_);
			if (contextMenuCallback) {
				Choice *menuChoice = new Choice(ImageID("I_THREE_DOTS"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT));
				menuChoice->OnClick.Add([contextMenuCallback](EventParams &e) {
					contextMenuCallback();
				});
				container->Add(menuChoice);
			}
			Add(container);
		} else {
			tabScroll_ = new ScrollView(orientation, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
			tabScroll_->Add(tabStrip_);
			Add(tabScroll_);
		}
	} else {
		tabContainer_ = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(stripSize, FILL_PARENT));
		tabContainer_->Add(new Spacer(8.0f));
		tabContainer_->SetSpacing(0.0f);
		tabStrip_ = new ChoiceStrip(orientation, new LayoutParams(FILL_PARENT, FILL_PARENT));
		tabStrip_->SetTopTabs(true);
		tabScroll_ = new ScrollView(orientation, new LinearLayoutParams(1.0f));
		tabScroll_->Add(tabStrip_);
		tabContainer_->Add(tabScroll_);
		Add(tabContainer_);
	}
	tabStrip_->OnChoice.Handle(this, &TabHolder::OnTabClick);

	Add(new Spacer(4.0f))->SetSeparator();

	ViewGroup *contentHolder_ = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
	if (bannerView) {
		contentHolder_->Add(bannerView);
		bannerView_ = bannerView;
	}
	contents_ = contentHolder_->Add(new AnchorLayout(new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f)));
	contents_->SetClip(true);
	Add(contentHolder_);
}

void TabHolder::AddBack(UIScreen *parent) {
	if (tabContainer_) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		tabContainer_->Add(new UI::Spacer(8.0f));
		tabContainer_->Add(new Choice(di->T("Back"), ImageID("I_NAVIGATE_BACK"), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 0.0f, Margins(0, 0, 10, 10))))->OnClick.Handle<UIScreen>(parent, &UIScreen::OnBack);
	}
}

void TabHolder::AddTabContents(std::string_view title, ImageID imageId, ViewGroup *tabContents) {
	tabs_.push_back(tabContents);
	if (tabOrientation_ == ORIENT_HORIZONTAL && (flags_ & TabHolderFlags::HorizontalOnlyIcons) && imageId.isValid()) {
		tabStrip_->AddChoice(imageId);
	} else if (tabOrientation_ == ORIENT_VERTICAL && (flags_ & TabHolderFlags::VerticalShowIcons) && imageId.isValid()) {
		tabStrip_->AddChoice(title, imageId);
	} else {
		tabStrip_->AddChoice(title);
	}
	contents_->Add(tabContents);
	if (tabs_.size() > 1)
		tabContents->SetVisibility(V_GONE);
	tabContents->ReplaceLayoutParams(new AnchorLayoutParams(FILL_PARENT, FILL_PARENT));

	// Will be filled in later.
	tabTweens_.push_back(nullptr);
	// This entry doesn't need one.
	createFuncs_.push_back(nullptr);
}

void TabHolder::AddTabDeferred(std::string_view title, ImageID imageId, std::function<ViewGroup *()> createCb) {
	tabs_.push_back(nullptr);  // marker
	if (tabOrientation_ == ORIENT_HORIZONTAL && (flags_ & TabHolderFlags::HorizontalOnlyIcons) && imageId.isValid()) {
		tabStrip_->AddChoice(imageId);
	} else if (tabOrientation_ == ORIENT_VERTICAL && (flags_ & TabHolderFlags::VerticalShowIcons) && imageId.isValid()) {
		tabStrip_->AddChoice(title, imageId);
	} else {
		tabStrip_->AddChoice(title);
	}
	tabTweens_.push_back(nullptr);
	createFuncs_.push_back(createCb);

	if (tabs_.size() == 1) {
		EnsureTab(0);
	}
}

void TabHolder::EnsureAllCreated() {
	for (int i = 0; i < createFuncs_.size(); i++) {
		if (createFuncs_[i]) {
			EnsureTab(i);
			tabs_[i]->SetVisibility(i == currentTab_ ? V_VISIBLE : V_GONE);
		}
	}
}

bool TabHolder::EnsureTab(int index) {
	_dbg_assert_(index >= 0 && index < createFuncs_.size());

	if (!tabs_[index]) {
		_dbg_assert_(index < createFuncs_.size());
		_dbg_assert_(createFuncs_[index]);
		std::function<UI::ViewGroup * ()> func;
		createFuncs_[index].swap(func);

		ViewGroup *tabContents = func();
		tabs_[index] = tabContents;
		contents_->Add(tabContents);

		tabContents->ReplaceLayoutParams(new AnchorLayoutParams(FILL_PARENT, FILL_PARENT));
		return true;
	} else {
		return false;
	}
}

bool TabHolder::SetCurrentTab(int tab, bool skipTween) {
	if (tab >= (int)tabs_.size()) {
		// Ignore
		return false;
	}

	bool created = false;

	if (tab != currentTab_) {
		_dbg_assert_(tabs_[currentTab_]);  // we should always have a tab to switch *from*.
		created = EnsureTab(tab);
	}

	auto setupTween = [&](View *view, AnchorTranslateTween *&tween) {
		_dbg_assert_(view != nullptr);
		if (tween)
			return;

		tween = new AnchorTranslateTween(0.15f, bezierEaseInOut);
		tween->Finish.Add([&](EventParams &e) {
			e.v->SetVisibility(tabs_[currentTab_] == e.v ? V_VISIBLE : V_GONE);
		});
		view->AddTween(tween)->Persist();
	};

	if (tab != currentTab_) {
		Orientation orient = Opposite(orientation_);
		// Direction from which the new tab will come.
		float dir = tab < currentTab_ ? -1.0f : 1.0f;

		// First, setup any missing tweens.
		setupTween(tabs_[currentTab_], tabTweens_[currentTab_]);
		setupTween(tabs_[tab], tabTweens_[tab]);

		// Currently displayed, so let's reset it.
		if (skipTween) {
			tabs_[currentTab_]->SetVisibility(V_GONE);
			tabTweens_[tab]->Reset(Point2D(0.0f, 0.0f));
			tabTweens_[tab]->Apply(tabs_[tab]);
		} else {
			tabTweens_[currentTab_]->Reset(Point2D(0.0f, 0.0f));

			if (orient == ORIENT_HORIZONTAL) {
				tabTweens_[tab]->Reset(Point2D(bounds_.w * dir, 0.0f));
				tabTweens_[currentTab_]->Divert(Point2D(bounds_.w * -dir, 0.0f));
			} else {
				tabTweens_[tab]->Reset(Point2D(0.0f, bounds_.h * dir));
				tabTweens_[currentTab_]->Divert(Point2D(0.0f, bounds_.h * -dir));
			}
			// Actually move it to the initial position now, just to avoid any flicker.
			tabTweens_[tab]->Apply(tabs_[tab]);
			tabTweens_[tab]->Divert(Point2D(0.0f, 0.0f));
		}
		tabs_[tab]->SetVisibility(V_VISIBLE);

		currentTab_ = tab;
	}
	tabStrip_->SetSelection(tab, false);

	return created;
}

void TabHolder::OnTabClick(EventParams &e) {
	// We have e.b set when it was an explicit click action.
	// In that case, we make the view gone and then visible - this scrolls scrollviews to the top.
	if (e.b != 0) {
		EnsureTab(e.a);
		SetCurrentTab((int)e.a);
	}
}

void TabHolder::PersistData(PersistStatus status, std::string anonId, PersistMap &storage) {
	ViewGroup::PersistData(status, anonId, storage);

	std::string tag = Tag();
	if (tag.empty()) {
		tag = anonId;
	}

	PersistBuffer &buffer = storage["TabHolder::" + tag];
	switch (status) {
	case PERSIST_SAVE:
		buffer.resize(1);
		buffer[0] = currentTab_;
		break;

	case PERSIST_RESTORE:
		if (buffer.size() == 1) {
			if (SetCurrentTab(buffer[0], true)) {
				// Re-run PersistData. TODO: Only need to do it for the new tab.
				ViewGroup::PersistData(status, anonId, storage);
			}
		}
		break;
	}
}

void TabHolder::EnableTab(int tab, bool enabled) {
	tabStrip_->EnableChoice(tab, enabled);
}


ChoiceStrip::ChoiceStrip(Orientation orientation, LayoutParams *layoutParams)
	: LinearLayout(orientation, layoutParams) {
	SetSpacing(0.0f);
}

void ChoiceStrip::AddChoice(std::string_view title, ImageID imageId) {
	StickyChoice *c = new StickyChoice(title, imageId,
		orientation_ == ORIENT_HORIZONTAL ?
		nullptr :
		new LinearLayoutParams(FILL_PARENT, ITEM_HEIGHT));
	c->OnClick.Handle(this, &ChoiceStrip::OnChoiceClick);
	Add(c);
	choices_.push_back(c);
	if (selected_ == (int)choices_.size() - 1)
		c->Press();
}

void ChoiceStrip::AddChoice(ImageID buttonImage) {
	StickyChoice *c = new StickyChoice(buttonImage,
		orientation_ == ORIENT_HORIZONTAL ?
		nullptr :
		new LinearLayoutParams(FILL_PARENT, ITEM_HEIGHT));
	c->OnClick.Handle(this, &ChoiceStrip::OnChoiceClick);
	Add(c);
	choices_.push_back(c);
	if (selected_ == (int)choices_.size() - 1)
		c->Press();
}

void ChoiceStrip::OnChoiceClick(EventParams &e) {
	// Unstick the other choices that weren't clicked.
	for (int i = 0; i < (int)choices_.size(); i++) {
		if (choices_[i] != e.v) {
			choices_[i]->Release();
		} else {
			selected_ = i;
		}
	}

	EventParams e2{};
	e2.v = selected_ < choices_.size() ? choices_[selected_] : nullptr;
	e2.a = selected_;
	// Set to 1 to indicate an explicit click.
	e2.b = 1;
	// Dispatch immediately (we're already on the UI thread as we're in an event handler).
	OnChoice.Dispatch(e2);
}

void ChoiceStrip::SetSelection(int sel, bool triggerClick) {
	int prevSelected = selected_;
	if (selected_ < choices_.size()) {
		StickyChoice *prevChoice = choices_[selected_];
		prevChoice->Release();
	}
	selected_ = sel;
	if (selected_ < choices_.size()) {
		StickyChoice *newChoice = choices_[selected_];
		newChoice->Press();
		if (topTabs_ && prevSelected != selected_) {
			EventParams e{};
			e.v = choices_[selected_];
			e.a = selected_;
			// Set to 0 to indicate a selection change (not a click.)
			e.b = triggerClick ? 1 : 0;
			OnChoice.Trigger(e);
		}
	}
}

void ChoiceStrip::EnableChoice(int choice, bool enabled) {
	if (choice < (int)choices_.size()) {
		choices_[choice]->SetEnabled(enabled);
	}
}

bool ChoiceStrip::Key(const KeyInput &input) {
	if (topTabs_ && (input.flags & KeyInputFlags::DOWN)) {
		// These keyboard shortcuts ignore focus - the assumption is that there's only
		// one choice strip with topTabs_ enabled visible at a time.
		if (IsTabLeftKey(input)) {
			if (selected_ > 0) {
				SetSelection(selected_ - 1, true);
				UI::PlayUISound(UI::UISound::TOGGLE_OFF);  // Maybe make specific sounds for this at some point?
			}
			return true;
		} else if (IsTabRightKey(input)) {
			if (selected_ < (int)choices_.size() - 1) {
				SetSelection(selected_ + 1, true);
				UI::PlayUISound(UI::UISound::TOGGLE_ON);
			}
			return true;
		}
	}
	return ViewGroup::Key(input);
}

std::string ChoiceStrip::DescribeText() const {
	auto u = GetI18NCategory(I18NCat::UI_ELEMENTS);
	return DescribeListUnordered(u->T("Choices:"));
}

}  // namespace UI
