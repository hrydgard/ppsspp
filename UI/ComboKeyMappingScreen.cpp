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

#include "Common/System/Display.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

#include "Common/Data/Text/I18n.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/File/PathBrowser.h"
#include "Common/Math/curves.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"

#include "UI/ComboKeyMappingScreen.h"

class ButtonPreview : public UI::View {
public:
	ButtonPreview(ImageID bgImg, ImageID img, float rotationIcon, bool flipShape, float rotationShape, int x, int y)
		: View(new UI::AnchorLayoutParams(x, y, UI::NONE, UI::NONE, true)), bgImg_(bgImg), img_(img), rotI_(rotationIcon),
		flipS_(flipShape), rotS_(rotationShape), x_(x), y_(y) {}

	void Draw(UIContext &dc) override {
		float opacity = g_Config.iTouchButtonOpacity / 100.0f;

		uint32_t colorBg = colorAlpha(g_Config.iTouchButtonStyle != 0 ? 0xFFFFFF : 0xc0b080, opacity);
		uint32_t color = colorAlpha(0xFFFFFF, opacity);

		dc.Draw()->DrawImageRotated(bgImg_, x_, y_, 1.0f, rotS_*PI/180, colorBg, flipS_);
		dc.Draw()->DrawImageRotated(img_, x_, y_, 1.0f, rotI_*PI/180, color, false);
	}
private:
	int x_;
	int y_;
	float rotI_;
	float rotS_;
	bool flipS_;
	ImageID bgImg_;
	ImageID img_;
};

void ComboKeyScreen::CreateViews() {
	using namespace UI;
	using namespace CustomKey;
	auto co = GetI18NCategory("Controls");
	auto mc = GetI18NCategory("MappableControls");
	root_ = new LinearLayout(ORIENT_VERTICAL);
	root_->Add(new ItemHeader(co->T("Custom Key Setting")));
	LinearLayout *root__ = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(1.0));
	root_->Add(root__);
	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(120, FILL_PARENT));
	auto di = GetI18NCategory("Dialog");

	ConfigCustomButton* cfg = nullptr;
	bool* show = nullptr;
	memset(array, 0, sizeof(array));
	switch (id_) {
	case 0: 
		cfg = &g_Config.CustomKey0;
		show = &g_Config.touchCombo0.show;
		for (int i = 0; i < ARRAY_SIZE(comboKeyList); i++)
			array[i] = (0x01 == ((g_Config.CustomKey0.key >> i) & 0x01));
		break;
	case 1:
		cfg = &g_Config.CustomKey1;
		show = &g_Config.touchCombo1.show;
		for (int i = 0; i < ARRAY_SIZE(comboKeyList); i++)
			array[i] = (0x01 == ((g_Config.CustomKey1.key >> i) & 0x01));
		break;
	case 2:
		cfg = &g_Config.CustomKey2;
		show = &g_Config.touchCombo2.show;
		for (int i = 0; i < ARRAY_SIZE(comboKeyList); i++)
			array[i] = (0x01 == ((g_Config.CustomKey2.key >> i) & 0x01));
		break;
	case 3:
		cfg = &g_Config.CustomKey3;
		show = &g_Config.touchCombo3.show;
		for (int i = 0; i < ARRAY_SIZE(comboKeyList); i++)
			array[i] = (0x01 == ((g_Config.CustomKey3.key >> i) & 0x01));
		break;
	case 4:
		cfg = &g_Config.CustomKey4;
		show = &g_Config.touchCombo4.show;
		for (int i = 0; i < ARRAY_SIZE(comboKeyList); i++)
			array[i] = (0x01 == ((g_Config.CustomKey4.key >> i) & 0x01));
		break;
	case 5: 
		cfg = &g_Config.CustomKey5;
		show = &g_Config.touchCombo5.show;
		for (int i = 0; i < ARRAY_SIZE(comboKeyList); i++)
			array[i] = (0x01 == ((g_Config.CustomKey5.key >> i) & 0x01));
		break;
	case 6:
		cfg = &g_Config.CustomKey6;
		show = &g_Config.touchCombo6.show;
		for (int i = 0; i < ARRAY_SIZE(comboKeyList); i++)
			array[i] = (0x01 == ((g_Config.CustomKey6.key >> i) & 0x01));
		break;
	case 7:
		cfg = &g_Config.CustomKey7;
		show = &g_Config.touchCombo7.show;
		for (int i = 0; i < ARRAY_SIZE(comboKeyList); i++)
			array[i] = (0x01 == ((g_Config.CustomKey7.key >> i) & 0x01));
		break;
	case 8:
		cfg = &g_Config.CustomKey8;
		show = &g_Config.touchCombo8.show;
		for (int i = 0; i < ARRAY_SIZE(comboKeyList); i++)
			array[i] = (0x01 == ((g_Config.CustomKey8.key >> i) & 0x01));
		break;
	case 9:
		cfg = &g_Config.CustomKey9;
		show = &g_Config.touchCombo9.show;
		for (int i = 0; i < ARRAY_SIZE(comboKeyList); i++)
			array[i] = (0x01 == ((g_Config.CustomKey9.key >> i) & 0x01));
		break;
	default:
		// This shouldn't happen, let's just not crash.
		cfg = &g_Config.CustomKey0;
		show = &g_Config.touchCombo0.show;
		break;
	}

	leftColumn->Add(new ButtonPreview(g_Config.iTouchButtonStyle == 0 ? comboKeyShapes[cfg->shape].i : comboKeyShapes[cfg->shape].l, 
			comboKeyImages[cfg->image].i, comboKeyImages[cfg->image].r, comboKeyShapes[cfg->shape].f, comboKeyShapes[cfg->shape].r, 62, 82));

	root__->Add(leftColumn);
	rightScroll_ = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 1.0f));
	leftColumn->Add(new Spacer(new LinearLayoutParams(1.0f)));
	leftColumn->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	root__->Add(rightScroll_);

	LinearLayout *vertLayout = new LinearLayout(ORIENT_VERTICAL);
	rightScroll_->Add(vertLayout);
	
	vertLayout->Add(new ItemHeader(co->T("Button Style")));
	vertLayout->Add(new CheckBox(show, co->T("Visible")));

	// All icon and name are defined in GamepadEmu.h
	static const char *imageNames[ARRAY_SIZE(comboKeyImages)];
	for (int i = 0; i < ARRAY_SIZE(imageNames); ++i) {
		imageNames[i] = comboKeyImages[i].n;
	}
	PopupMultiChoice *icon = vertLayout->Add(new PopupMultiChoice(&(cfg->image), co->T("Icon"), imageNames, 0, ARRAY_SIZE(imageNames), mc->GetName(), screenManager()));
	icon->OnChoice.Handle(this, &ComboKeyScreen::onCombo);
	
	// All shape and name are defined in GamepadEmu.h
	static const char *shapeNames[ARRAY_SIZE(comboKeyShapes)];
	for (int i = 0; i < ARRAY_SIZE(shapeNames); ++i) {
		shapeNames[i] = comboKeyShapes[i].n;
	}
	vertLayout->Add(new PopupMultiChoice(&(cfg->shape), co->T("Shape"), shapeNames, 0, ARRAY_SIZE(shapeNames), mc->GetName(), screenManager()))->OnChoice.Handle(this, &ComboKeyScreen::onCombo);

	vertLayout->Add(new ItemHeader(co->T("Button Binding")));
	vertLayout->Add(new CheckBox(&(cfg->toggle), co->T("Toggle mode")));

	const int cellSize = 400;
	UI::GridLayoutSettings gridsettings(cellSize, 64, 5);
	gridsettings.fillCells = true;
	GridLayout *grid = vertLayout->Add(new GridLayout(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

	// Button name, image and action are defined in GamepadEmu.h
	for (int i = 0; i < ARRAY_SIZE(comboKeyList); ++i) {
		LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		row->SetSpacing(0);

		CheckBox *checkbox = new CheckBox(&array[i], "", "", new LinearLayoutParams(50, WRAP_CONTENT));
		row->Add(checkbox);

		Choice *choice;
		if (comboKeyList[i].i.isValid()) {
			choice = new Choice(comboKeyList[i].i, new LinearLayoutParams(1.0f));
		} else {
			choice = new Choice(mc->T(comboKeyList[i].n), new LinearLayoutParams(1.0f));
		}

		ChoiceEventHandler *choiceEventHandler = new ChoiceEventHandler(checkbox);
		choice->OnClick.Handle(choiceEventHandler, &ChoiceEventHandler::onChoiceClick);

		choice->SetCentered(true);

		row->Add(choice);
		grid->Add(row);
	}
}

static uint64_t arrayToInt(bool ary[ARRAY_SIZE(CustomKey::comboKeyList)]) {
	uint64_t value = 0;
	for (int i = ARRAY_SIZE(CustomKey::comboKeyList)-1; i >= 0; i--) {
		value |= ary[i] ? 1 : 0;
		if (i > 0) {
			value = value << 1;
		}
	}
	return value;
}

void ComboKeyScreen::saveArray() {
	switch (id_) {
	case 0:
		g_Config.CustomKey0.key = arrayToInt(array);
		break;
	case 1:
		g_Config.CustomKey1.key = arrayToInt(array);
		break;
	case 2:
		g_Config.CustomKey2.key = arrayToInt(array);
		break;
	case 3:
		g_Config.CustomKey3.key = arrayToInt(array);
		break;
	case 4:
		g_Config.CustomKey4.key = arrayToInt(array);
		break;
	case 5:
		g_Config.CustomKey5.key = arrayToInt(array);
		break;
	case 6:
		g_Config.CustomKey6.key = arrayToInt(array);
		break;
	case 7:
		g_Config.CustomKey7.key = arrayToInt(array);
		break;
	case 8:
		g_Config.CustomKey8.key = arrayToInt(array);
		break;
	case 9:
		g_Config.CustomKey9.key = arrayToInt(array);
		break;
	}
}

void ComboKeyScreen::onFinish(DialogResult result) {
	saveArray();
	g_Config.Save("ComboKeyScreen::onFinish");
}

UI::EventReturn ComboKeyScreen::ChoiceEventHandler::onChoiceClick(UI::EventParams &e){
	checkbox_->Toggle();
	return UI::EVENT_DONE;
};

UI::EventReturn ComboKeyScreen::onCombo(UI::EventParams &e) {
	saveArray();
	CreateViews();
	return UI::EVENT_DONE;
}
