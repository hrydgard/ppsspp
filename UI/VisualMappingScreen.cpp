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

#include <unordered_map>
#include <vector>

#include "Common/Math/math_util.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/ScreenManager.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Input/InputState.h"
#include "Common/TimeUtil.h"
#include "Core/KeyMap.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/Config.h"
#include "UI/ControlMappingScreen.h"
#include "UI/VisualMappingScreen.h"

using KeyMap::MultiInputMapping;

class Backplate : public UI::InertView {
public:
	explicit Backplate(float scale, UI::LayoutParams *layoutParams = nullptr) : InertView(layoutParams), scale_(scale) {}

	void Draw(UIContext &dc) override {
		for (float dy = 0.0f; dy <= 4.0f; dy += 1.0f) {
			for (float dx = 0.0f; dx <= 4.0f; dx += 1.0f) {
				if (dx == 2.0f && dy == 2.0f)
					continue;
				DrawPSP(dc, dx, dy, 0x06C1B6B6);
			}
		}
		DrawPSP(dc, 2.0f, 2.0f, 0xC01C1818);
	}

	void DrawPSP(UIContext &dc, float xoff, float yoff, uint32_t color) {
		using namespace UI;

		const AtlasImage *whiteImage = dc.Draw()->GetAtlas()->getImage(dc.GetTheme().whiteImage);
		float centerU = (whiteImage->u1 + whiteImage->u2) * 0.5f;
		float centerV = (whiteImage->v1 + whiteImage->v2) * 0.5f;

		auto V = [&](float x, float y) {
			dc.Draw()->V(bounds_.x + (x + xoff) * scale_, bounds_.y + (y + yoff) * scale_, color, centerU, centerV);
		};
		auto R = [&](float x1, float y1, float x2, float y2) {
			V(x1, y1); V(x2, y1); V(x2, y2);
			V(x1, y1); V(x2, y2); V(x1, y2);
		};

		// Curved left side.
		V(12.0f, 44.0f); V(30.0f, 16.0f); V(30.0f, 44.0f);
		V(0.0f, 80.0f); V(12.0f, 44.0f); V(12.0f, 80.0f);
		R(12.0f, 44.0f, 30.0f, 80.0f);
		R(0.0f, 80.0f, 30.0f, 114.0f);
		V(0.0f, 114.0f); V(12.0f, 114.0f); V(12.0f, 154.0f);
		R(12.0f, 114.0f, 30.0f, 154.0f);
		V(12.0f, 154.0f); V(30.0f, 154.0f); V(30.0f, 180.0f);
		// Left side.
		V(30.0f, 16.0f); V(64.0f, 13.0f); V(64.0f, 184.0f);
		V(30.0f, 16.0f); V(64.0f, 184.0f); V(30.0f, 180.0f);
		V(64.0f, 13.0f); V(76.0f, 0.0f); V(76.0f, 13.0f);
		V(64.0f, 184.0f); V(76.0f, 200.0f); V(76.0f, 184.0f);
		R(64.0f, 13.0f, 76.0f, 184.0f);
		// Center.
		R(76.0f, 0.0f, 400.0f, 13.0f);
		R(76.0f, 167.0f, 400.0f, 200.0f);
		R(76.0f, 13.0f, 99.0f, 167.0f);
		R(377.0f, 13.0f, 400.0f, 167.0f);
		// Right side.
		V(400.0f, 0.0f); V(412.0f, 13.0f); V(400.0f, 13.0f);
		V(400.0f, 184.0f); V(412.0f, 184.0f); V(400.0f, 200.0f);
		R(400.0f, 13.0f, 412.0f, 184.0f);
		V(412.0f, 13.0f); V(446.0f, 16.0f); V(446.0f, 180.0f);
		V(412.0f, 13.0f); V(446.0f, 180.0f); V(412.0f, 184.0f);
		// Curved right side.
		V(446.0f, 16.0f); V(462.0f, 44.0f); V(446.0f, 44.0f);
		V(462.0f, 44.0f); V(474.0f, 80.0f); V(462.0f, 80.0f);
		R(446.0f, 44.0f, 462.0f, 80.0f);
		R(446.0f, 80.0f, 474.0f, 114.0f);
		V(462.0f, 114.0f); V(474.0f, 114.0f); V(462.0f, 154.0f);
		R(446.0f, 114.0f, 462.0f, 154.0f);
		V(446.0f, 154.0f); V(462.0f, 154.0f); V(446.0f, 180.0f);
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		w = 478.0f * scale_;
		h = 204.0f * scale_;
	}

protected:
	float scale_ = 1.0f;
};

class MockScreen : public UI::InertView {
public:
	explicit MockScreen(UI::LayoutParams *layoutParams = nullptr) : InertView(layoutParams) {
	}

	void Draw(UIContext &dc) override {
		ImageID bg = ImageID("I_PSP_DISPLAY");
		dc.Draw()->DrawImageStretch(bg, bounds_, 0x7FFFFFFF);
	}
};

class MockButton : public UI::Clickable {
public:
	MockButton(int button, ImageID img, ImageID bg, float angle, UI::LayoutParams *layoutParams = nullptr)
		: Clickable(layoutParams), button_(button), img_(img), bgImg_(bg), angle_(angle) {
	}

	void Draw(UIContext &dc) override {
		uint32_t c = 0xFFFFFFFF;
		if (HasFocus() || Selected())
			c = dc.GetTheme().itemFocusedStyle.background.color;

		float scales[2]{};
		if (bgImg_.isValid())
			dc.Draw()->DrawImageRotatedStretch(bgImg_, bounds_, scales, angle_, c, flipHBG_);
		if (img_.isValid()) {
			scales[0] *= scaleX_;
			scales[1] *= scaleY_;
			if (timeLastPressed_ >= 0.0) {
				double sincePress = time_now_d() - timeLastPressed_;
				if (sincePress < 1.0) {
					c = colorBlend(c, dc.GetTheme().itemDownStyle.background.color, (float)sincePress);
				}
			}
			dc.Draw()->DrawImageRotatedStretch(img_, bounds_.Offset(offsetX_, offsetY_), scales, angle_, c);
		}
	}

	MockButton *SetScale(float s) {
		scaleX_ = s;
		scaleY_ = s;
		return this;
	}

	MockButton *SetScale(float x, float y) {
		scaleX_ = x;
		scaleY_ = y;
		return this;
	}

	MockButton *SetFlipHBG(bool b) {
		flipHBG_ = b;
		return this;
	}

	MockButton *SetOffset(float x, float y) {
		offsetX_ = x;
		offsetY_ = y;
		return this;
	}

	MockButton *SetSelectedButton(int *s) {
		selectedButton_ = s;
		return this;
	}

	bool Selected() {
		return selectedButton_ && *selectedButton_ == button_;
	}

	int Button() const {
		return button_;
	}

	void NotifyPressed() {
		timeLastPressed_ = time_now_d();
	}

private:
	int button_;
	ImageID img_;
	ImageID bgImg_;
	float angle_;
	float scaleX_ = 1.0f;
	float scaleY_ = 1.0f;
	float offsetX_ = 0.0f;
	float offsetY_ = 0.0f;
	bool flipHBG_ = false;
	int *selectedButton_ = nullptr;
	double timeLastPressed_ = -1.0;
};

class MockPSP : public UI::AnchorLayout {
public:
	static constexpr float SCALE = 1.4f;

	explicit MockPSP(UI::LayoutParams *layoutParams = nullptr);
	void SelectButton(int btn);
	void FocusButton(int btn);
	void NotifyPressed(int btn);
	float GetPopupOffset();

	bool SubviewFocused(View *view) override;

	UI::Event ButtonClick;

private:
	UI::AnchorLayoutParams *LayoutAt(float l, float t, float r, float b);
	UI::AnchorLayoutParams *LayoutSize(float w, float h, float l, float t);
	MockButton *AddButton(int button, ImageID img, ImageID bg, float angle, UI::LayoutParams *lp);

	void OnSelectButton(UI::EventParams &e);

	std::unordered_map<int, MockButton *> buttons_;
	UI::TextView *labelView_ = nullptr;
	int selectedButton_ = 0;
};

MockPSP::MockPSP(UI::LayoutParams *layoutParams) : AnchorLayout(layoutParams) {
	Add(new Backplate(SCALE));
	Add(new MockScreen(LayoutAt(99.0f, 13.0f, 97.0f, 33.0f)));

	// Left side.
	AddButton(VIRTKEY_AXIS_Y_MAX, ImageID("I_STICK_LINE"), ImageID("I_STICK_BG_LINE"), 0.0f, LayoutSize(34.0f, 34.0f, 35.0f, 133.0f));
	AddButton(CTRL_LEFT, ImageID("I_ARROW"), ImageID("I_DIR_LINE"), M_PI * 0.0f, LayoutSize(28.0f, 20.0f, 14.0f, 75.0f))->SetOffset(-4.0f * SCALE, 0.0f);
	AddButton(CTRL_UP, ImageID("I_ARROW"), ImageID("I_DIR_LINE"), M_PI * 0.5f, LayoutSize(20.0f, 28.0f, 40.0f, 50.0f))->SetOffset(0.0f, -4.0f * SCALE);
	AddButton(CTRL_RIGHT, ImageID("I_ARROW"), ImageID("I_DIR_LINE"), M_PI * 1.0f, LayoutSize(28.0f, 20.0f, 58.0f, 75.0f))->SetOffset(4.0f * SCALE, 0.0f);
	AddButton(CTRL_DOWN, ImageID("I_ARROW"), ImageID("I_DIR_LINE"), M_PI * 1.5f, LayoutSize(20.0f, 28.0f, 40.0f, 92.0f))->SetOffset(0.0f, 4.0f * SCALE);

	// Top.
	AddButton(CTRL_LTRIGGER, ImageID("I_L"), ImageID("I_SHOULDER_LINE"), 0.0f, LayoutSize(50.0f, 16.0f, 29.0f, 0.0f));
	AddButton(CTRL_RTRIGGER, ImageID("I_R"), ImageID("I_SHOULDER_LINE"), 0.0f, LayoutSize(50.0f, 16.0f, 397.0f, 0.0f))->SetFlipHBG(true);

	// Bottom.
	AddButton(CTRL_HOME, ImageID("I_HOME"), ImageID("I_RECT_LINE"), 0.0f, LayoutSize(28.0f, 14.0f, 88.0f, 181.0f))->SetScale(1.0f, 0.65f);
	AddButton(CTRL_SELECT, ImageID("I_SELECT"), ImageID("I_RECT_LINE"), 0.0f, LayoutSize(28.0f, 14.0f, 330.0f, 181.0f));
	AddButton(CTRL_START, ImageID("I_START"), ImageID("I_RECT_LINE"), 0.0f, LayoutSize(28.0f, 14.0f, 361.0f, 181.0f));

	// Right side.
	AddButton(CTRL_TRIANGLE, ImageID("I_TRIANGLE"), ImageID("I_ROUND_LINE"), 0.0f, LayoutSize(23.0f, 23.0f, 419.0f, 46.0f))->SetScale(0.7f)->SetOffset(0.0f, -1.0f * SCALE);
	AddButton(CTRL_CIRCLE, ImageID("I_CIRCLE"), ImageID("I_ROUND_LINE"), 0.0f, LayoutSize(23.0f, 23.0f, 446.0f, 74.0f))->SetScale(0.7f);
	AddButton(CTRL_CROSS, ImageID("I_CROSS"), ImageID("I_ROUND_LINE"), 0.0f, LayoutSize(23.0f, 23.0f, 419.0f, 102.0f))->SetScale(0.7f);
	AddButton(CTRL_SQUARE, ImageID("I_SQUARE"), ImageID("I_ROUND_LINE"), 0.0f, LayoutSize(23.0f, 23.0f, 392.0f, 74.0f))->SetScale(0.7f);

	labelView_ = Add(new UI::TextView(""));
	labelView_->SetShadow(true);
	labelView_->SetVisibility(UI::V_GONE);
}

void MockPSP::SelectButton(int btn) {
	selectedButton_ = btn;
}

void MockPSP::FocusButton(int btn) {
	MockButton *view = buttons_[btn];
	if (view) {
		view->SetFocus(UI::FocusFlags::CAUSE_FORCED);
	} else {
		labelView_->SetVisibility(UI::V_GONE);
	}
}

void MockPSP::NotifyPressed(int btn) {
	MockButton *view = buttons_[btn];
	if (view)
		view->NotifyPressed();
}

bool MockPSP::SubviewFocused(View *view) {
	for (auto it : buttons_) {
		if (view == it.second) {
			labelView_->SetVisibility(UI::V_VISIBLE);
			labelView_->SetText(KeyMap::GetPspButtonName(it.first));

			const Bounds &pos = view->GetBounds().Offset(-GetBounds().x, -GetBounds().y);
			labelView_->ReplaceLayoutParams(new UI::AnchorLayoutParams(pos.centerX(), pos.y2() + 5, UI::NONE, UI::NONE));
		}
	}
	return AnchorLayout::SubviewFocused(view);
}

float MockPSP::GetPopupOffset() {
	MockButton *view = buttons_[selectedButton_];
	if (!view)
		return 0.0f;

	float ypos = view->GetBounds().centerY();
	if (ypos > bounds_.centerY()) {
		return -0.25f;
	}
	return 0.25f;
}

UI::AnchorLayoutParams *MockPSP::LayoutAt(float l, float t, float r, float b) {
	return new UI::AnchorLayoutParams(l * SCALE, t * SCALE, r * SCALE, b * SCALE);
}
UI::AnchorLayoutParams *MockPSP::LayoutSize(float w, float h, float l, float t) {
	return new UI::AnchorLayoutParams(w * SCALE, h * SCALE, l * SCALE, t * SCALE, UI::NONE, UI::NONE);
}

MockButton *MockPSP::AddButton(int button, ImageID img, ImageID bg, float angle, UI::LayoutParams *lp) {
	MockButton *view = Add(new MockButton(button, img, bg, angle, lp));
	view->OnClick.Handle(this, &MockPSP::OnSelectButton);
	view->SetSelectedButton(&selectedButton_);
	buttons_[button] = view;
	return view;
}

void MockPSP::OnSelectButton(UI::EventParams &e) {
	auto view = (MockButton *)e.v;
	e.a = view->Button();
	return ButtonClick.Dispatch(e);
}

static std::vector<int> bindAllOrder{
	CTRL_LTRIGGER,
	CTRL_RTRIGGER,
	CTRL_UP,
	CTRL_DOWN,
	CTRL_LEFT,
	CTRL_RIGHT,
	VIRTKEY_AXIS_Y_MAX,
	VIRTKEY_AXIS_Y_MIN,
	VIRTKEY_AXIS_X_MIN,
	VIRTKEY_AXIS_X_MAX,
	CTRL_HOME,
	CTRL_SELECT,
	CTRL_START,
	CTRL_CROSS,
	CTRL_CIRCLE,
	CTRL_TRIANGLE,
	CTRL_SQUARE,
};

void VisualMappingScreen::CreateViews() {
	using namespace UI;

	auto km = GetI18NCategory(I18NCat::KEYMAPPING);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	constexpr float leftColumnWidth = 200.0f;
	LinearLayout *leftColumn = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(leftColumnWidth, FILL_PARENT, Margins(10, 10, 0, 10))));

	ChoiceGroup *actions = leftColumn->Add(new ChoiceGroup());

	actions->Add(new Choice(km->T("Bind All")))->OnClick.Handle(this, &VisualMappingScreen::OnBindAll);
	actions->Add(new CheckBox(&replace_, km->T("Replace"), ""));

	Choice *backButton = leftColumn->Add(new Choice(di->T("Back"), ImageID("I_NAVIGATE_BACK")));
	backButton->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	backButton->SetRoundCorners();

	// TODO: Properly use layout instead of querying bounds.
	Bounds bounds = GetLayoutBounds(*screenManager()->getUIContext());
	// Account for left side.
	bounds.w -= leftColumnWidth + 10.0f;

	AnchorLayout *rightColumn = root_->Add(new AnchorLayout(new LinearLayoutParams(bounds.w, FILL_PARENT, 1.0f)));
	psp_ = rightColumn->Add(new MockPSP(new AnchorLayoutParams(bounds.centerX(), bounds.centerY(), NONE, NONE, Centering::Both)));
	psp_->ButtonClick.Handle(this, &VisualMappingScreen::OnMapButton);
}

bool VisualMappingScreen::key(const KeyInput &key) {
	if (key.flags & KeyInputFlags::DOWN) {
		std::vector<int> pspKeys;
		KeyMap::InputMappingToPspButton(InputMapping(key.deviceId, key.keyCode), &pspKeys);
		for (int pspKey : pspKeys) {
			switch (pspKey) {
			case VIRTKEY_AXIS_X_MIN:
			case VIRTKEY_AXIS_Y_MIN:
			case VIRTKEY_AXIS_X_MAX:
			case VIRTKEY_AXIS_Y_MAX:
				psp_->NotifyPressed(VIRTKEY_AXIS_Y_MAX);
				break;
			default:
				psp_->NotifyPressed(pspKey);
				break;
			}
		}
	}
	return UIBaseDialogScreen::key(key);
}

void VisualMappingScreen::axis(const AxisInput &axis) {
	std::vector<int> results;
	if (axis.value >= g_Config.fAnalogDeadzone * 0.7f)
		KeyMap::InputMappingToPspButton(InputMapping(axis.deviceId, axis.axisId, 1), &results);
	if (axis.value <= g_Config.fAnalogDeadzone * -0.7f)
		KeyMap::InputMappingToPspButton(InputMapping(axis.deviceId, axis.axisId, -1), &results);

	for (int result : results) {
		switch (result) {
		case VIRTKEY_AXIS_X_MIN:
		case VIRTKEY_AXIS_Y_MIN:
		case VIRTKEY_AXIS_X_MAX:
		case VIRTKEY_AXIS_Y_MAX:
			psp_->NotifyPressed(VIRTKEY_AXIS_Y_MAX);
			break;
		default:
			psp_->NotifyPressed(result);
			break;
		}
	}
	UIBaseDialogScreen::axis(axis);
}

void VisualMappingScreen::resized() {
	UIBaseDialogScreen::resized();
	RecreateViews();
}

void VisualMappingScreen::OnMapButton(UI::EventParams &e) {
	nextKey_ = e.a;
	MapNext(false);
}

void VisualMappingScreen::OnBindAll(UI::EventParams &e) {
	bindAll_ = 0;
	nextKey_ = bindAllOrder[bindAll_];
	MapNext(false);
}

void VisualMappingScreen::HandleKeyMapping(const KeyMap::MultiInputMapping &key) {
	KeyMap::SetInputMapping(nextKey_, key, replace_);
	KeyMap::UpdateNativeMenuKeys();

	if (bindAll_ < 0) {
		// For analog, we do each direction in a row.
		if (nextKey_ == VIRTKEY_AXIS_Y_MAX)
			nextKey_ = VIRTKEY_AXIS_Y_MIN;
		else if (nextKey_ == VIRTKEY_AXIS_Y_MIN)
			nextKey_ = VIRTKEY_AXIS_X_MIN;
		else if (nextKey_ == VIRTKEY_AXIS_X_MIN)
			nextKey_ = VIRTKEY_AXIS_X_MAX;
		else {
			if (nextKey_ == VIRTKEY_AXIS_X_MAX)
				psp_->FocusButton(VIRTKEY_AXIS_Y_MAX);
			else
				psp_->FocusButton(nextKey_);
			nextKey_ = 0;
		}
	} else if ((size_t)bindAll_ + 1 < bindAllOrder.size()) {
		bindAll_++;
		nextKey_ = bindAllOrder[bindAll_];
	} else {
		bindAll_ = -1;
		nextKey_ = 0;
	}
}

void VisualMappingScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	if (result == DR_YES && nextKey_ != 0) {
		MapNext(true);
	} else {
		// This means they canceled.
		if (nextKey_ != 0)
			psp_->FocusButton(nextKey_);
		nextKey_ = 0;
		bindAll_ = -1;
		psp_->SelectButton(0);
	}
}

void VisualMappingScreen::MapNext(bool successive) {
	if (nextKey_ == VIRTKEY_AXIS_Y_MIN || nextKey_ == VIRTKEY_AXIS_X_MIN || nextKey_ == VIRTKEY_AXIS_X_MAX) {
		psp_->SelectButton(VIRTKEY_AXIS_Y_MAX);
	} else {
		psp_->SelectButton(nextKey_);
	}

	auto dialog = new KeyMappingNewKeyDialog(nextKey_, true, [this](KeyMap::MultiInputMapping mapping) {
		HandleKeyMapping(mapping);
	}, I18NCat::KEYMAPPING);

	Bounds bounds = screenManager()->getUIContext()->GetLayoutBounds(LayoutMode(), false);
	dialog->SetPopupOffset(psp_->GetPopupOffset() * bounds.h);
	dialog->SetDelay(successive ? 0.5f : 0.1f);
	screenManager()->push(dialog);
}
