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

#include <algorithm>

#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/Math/math_util.h"
#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/StringUtils.h"

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Text/I18n.h"
#include "UI/DisplayLayoutScreen.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/PresentationCommon.h"

static const int leftColumnWidth = 200;
static const float orgRatio = 1.764706f;  // 480.0 / 272.0

enum Mode {
	MODE_INACTIVE = 0,
	MODE_MOVE = 1,
	MODE_RESIZE = 2,
};

static Bounds FRectToBounds(FRect rc) {
	Bounds b;
	b.x = rc.x * g_dpi_scale_x;
	b.y = rc.y * g_dpi_scale_y;
	b.w = rc.w * g_dpi_scale_x;
	b.h = rc.h * g_dpi_scale_y;
	return b;
}

class DisplayLayoutBackground : public UI::View {
public:
	DisplayLayoutBackground(UI::ChoiceStrip *mode, UI::LayoutParams *layoutParams) : UI::View(layoutParams), mode_(mode) {}

	bool Touch(const TouchInput &touch) {
		int mode = mode_ ? mode_->GetSelection() : 0;

		if ((touch.flags & TOUCH_MOVE) != 0 && dragging_) {
			float relativeTouchX = touch.x - startX_;
			float relativeTouchY = touch.y - startY_;

			switch (mode) {
			case MODE_MOVE:
				g_Config.fDisplayOffsetX = clamp_value(startDisplayOffsetX_ + relativeTouchX / bounds_.w, 0.0f, 1.0f);
				g_Config.fDisplayOffsetY = clamp_value(startDisplayOffsetY_ + relativeTouchY / bounds_.h, 0.0f, 1.0f);
				break;
			case MODE_RESIZE:
			{
				// Resize. Vertical = scaling; Up should be bigger so let's negate in that direction
				float diffYProp = -relativeTouchY * 0.007f;
				g_Config.fDisplayScale = clamp_value(startScale_ * powf(2.0f, diffYProp), 0.2f, 2.0f);
				break;
			}
			}
		}

		if ((touch.flags & TOUCH_DOWN) != 0 && !dragging_) {
			// Check that we're in the central 80% of the screen.
			// If outside, it may be a drag from displaying the back button on phones
			// where you have to drag from the side, etc.
			if (touch.x >= bounds_.w * 0.1f && touch.x <= bounds_.w * 0.9f &&
				touch.y >= bounds_.h * 0.1f && touch.y <= bounds_.h * 0.9f) {
				dragging_ = true;
				startX_ = touch.x;
				startY_ = touch.y;
				startDisplayOffsetX_ = g_Config.fDisplayOffsetX;
				startDisplayOffsetY_ = g_Config.fDisplayOffsetY;
				startScale_ = g_Config.fDisplayScale;
			}
		}

		if ((touch.flags & TOUCH_UP) != 0 && dragging_) {
			dragging_ = false;
		}

		return true;
	}

private:
	UI::ChoiceStrip *mode_;
	bool dragging_ = false;

	// Touch down state for drag to resize etc
	float startX_ = 0.0f;
	float startY_ = 0.0f;
	float startScale_ = -1.0f;
	float startDisplayOffsetX_ = -1.0f;
	float startDisplayOffsetY_ = -1.0f;
};

DisplayLayoutScreen::DisplayLayoutScreen(const Path &filename) : UIDialogScreenWithGameBackground(filename) {
	// Show background at full brightness
	darkenGameBackground_ = false;
}

void DisplayLayoutScreen::DrawBackground(UIContext &dc) {
	if (PSP_IsInited() && !g_Config.bSkipBufferEffects) {
		// We normally rely on the PSP screen.
		UIDialogScreenWithGameBackground::DrawBackground(dc);
	} else {
		// But if it's not present (we're not in game, or skip buffer effects is used),
		// we have to draw a substitute ourselves.

		// TODO: Clean this up a bit, this GetScreenFrame/CenterDisplay combo is too common.
		FRect screenFrame = GetScreenFrame(pixel_xres, pixel_yres);
		FRect rc;
		CenterDisplayOutputRect(&rc, 480.0f, 272.0f, screenFrame, g_Config.iInternalScreenRotation);

		dc.Flush();
		ImageID bg = ImageID("I_PSP_DISPLAY");
		dc.Draw()->DrawImageStretch(bg, FRectToBounds(rc), 0x7FFFFFFF);
	}
}

void DisplayLayoutScreen::onFinish(DialogResult reason) {
	g_Config.Save("DisplayLayoutScreen::onFinish");
}

void DisplayLayoutScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	RecreateViews();
}

UI::EventReturn DisplayLayoutScreen::OnPostProcShaderChange(UI::EventParams &e) {
	g_Config.vPostShaderNames.erase(std::remove(g_Config.vPostShaderNames.begin(), g_Config.vPostShaderNames.end(), "Off"), g_Config.vPostShaderNames.end());

	NativeMessageReceived("gpu_configChanged", "");
	NativeMessageReceived("gpu_renderResized", "");  // To deal with shaders that can change render resolution like upscaling.
	NativeMessageReceived("postshader_updated", "");

	if (gpu) {
		gpu->NotifyConfigChanged();
	}
	return UI::EVENT_DONE;
}

static std::string PostShaderTranslateName(const char *value) {
	auto ps = GetI18NCategory("PostShaders");
	const ShaderInfo *info = GetPostShaderInfo(value);
	if (info) {
		return ps->T(value, info ? info->name.c_str() : value);
	} else {
		return value;
	}
}

void DisplayLayoutScreen::sendMessage(const char *message, const char *value) {
	UIDialogScreenWithGameBackground::sendMessage(message, value);
	if (!strcmp(message, "postshader_updated")) {
		g_Config.bShaderChainRequires60FPS = PostShaderChainRequires60FPS(GetFullPostShadersChain(g_Config.vPostShaderNames));
		RecreateViews();
	}
}

void DisplayLayoutScreen::CreateViews() {
	const Bounds &bounds = screenManager()->getUIContext()->GetBounds();

	using namespace UI;

	auto di = GetI18NCategory("Dialog");
	auto gr = GetI18NCategory("Graphics");
	auto co = GetI18NCategory("Controls");
	auto ps = GetI18NCategory("PostShaders");

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	// Make it so that a touch can only affect one view. Makes manipulating the background through the buttons
	// impossible.
	root_->SetExclusiveTouch(true);

	ScrollView *leftScrollView = new ScrollView(ORIENT_VERTICAL, new AnchorLayoutParams(400.0f, FILL_PARENT, 10.f, 10.f, NONE, 10.f, false));
	ViewGroup *leftColumn = new LinearLayout(ORIENT_VERTICAL);
	leftScrollView->Add(leftColumn);
	leftScrollView->SetClickableBackground(true);
	root_->Add(leftScrollView);

	ScrollView *rightScrollView = new ScrollView(ORIENT_VERTICAL, new AnchorLayoutParams(300.0f, FILL_PARENT, NONE, 10.f, 10.f, 10.f, false));
	ViewGroup *rightColumn = new LinearLayout(ORIENT_VERTICAL);
	rightScrollView->Add(rightColumn);
	rightScrollView->SetClickableBackground(true);
	root_->Add(rightScrollView);

	LinearLayout *bottomControls = new LinearLayout(ORIENT_HORIZONTAL, new AnchorLayoutParams(NONE, NONE, NONE, 10.0f, false));
	root_->Add(bottomControls);

	if (!IsVREnabled()) {
		auto stretch = new CheckBox(&g_Config.bDisplayStretch, gr->T("Stretch"));
		rightColumn->Add(stretch);

		PopupSliderChoiceFloat *aspectRatio = new PopupSliderChoiceFloat(&g_Config.fDisplayAspectRatio, 0.5f, 2.0f, gr->T("Aspect Ratio"), screenManager());
		rightColumn->Add(aspectRatio);
		aspectRatio->SetDisabledPtr(&g_Config.bDisplayStretch);
		aspectRatio->SetHasDropShadow(false);
		aspectRatio->SetLiveUpdate(true);

		mode_ = new ChoiceStrip(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
		mode_->AddChoice(di->T("Inactive"));
		mode_->AddChoice(di->T("Move"));
		mode_->AddChoice(di->T("Resize"));
		mode_->SetSelection(0, false);
		bottomControls->Add(mode_);

		static const char *displayRotation[] = { "Landscape", "Portrait", "Landscape Reversed", "Portrait Reversed" };
		auto rotation = new PopupMultiChoice(&g_Config.iInternalScreenRotation, gr->T("Rotation"), displayRotation, 1, ARRAY_SIZE(displayRotation), co->GetName(), screenManager());
		rotation->SetEnabledFunc([] {
			return !g_Config.bSkipBufferEffects || g_Config.bSoftwareRendering;
		});
		rightColumn->Add(rotation);

		Choice *center = new Choice(di->T("Reset"));
		center->OnClick.Add([&](UI::EventParams &) {
			g_Config.fDisplayAspectRatio = 1.0f;
			g_Config.fDisplayScale = 1.0f;
			g_Config.fDisplayOffsetX = 0.5f;
			g_Config.fDisplayOffsetY = 0.5f;
			return UI::EVENT_DONE;
		});
		rightColumn->Add(center);

		rightColumn->Add(new Spacer(12.0f));
	}

	Choice *back = new Choice(di->T("Back"), "", false);
	back->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	rightColumn->Add(back);

	static const char *bufFilters[] = { "Linear", "Nearest", };
	leftColumn->Add(new PopupMultiChoice(&g_Config.iBufFilter, gr->T("Screen Scaling Filter"), bufFilters, 1, ARRAY_SIZE(bufFilters), gr->GetName(), screenManager()));

	Draw::DrawContext *draw = screenManager()->getDrawContext();

	bool multiViewSupported = draw->GetDeviceCaps().multiViewSupported;

	auto enableStereo = [=]() -> bool {
		return g_Config.bStereoRendering && multiViewSupported;
	};

	leftColumn->Add(new ItemHeader(gr->T("Postprocessing effect")));

	std::set<std::string> alreadyAddedShader;
	settingsVisible_.resize(g_Config.vPostShaderNames.size());

	for (int i = 0; i < (int)g_Config.vPostShaderNames.size() + 1 && i < ARRAY_SIZE(shaderNames_); ++i) {
		// Vector element pointer get invalidated on resize, cache name to have always a valid reference in the rendering thread
		shaderNames_[i] = i == g_Config.vPostShaderNames.size() ? "Off" : g_Config.vPostShaderNames[i];

		LinearLayout *shaderRow = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(UI::FILL_PARENT, UI::WRAP_CONTENT));
		shaderRow->SetSpacing(4.0f);
		leftColumn->Add(shaderRow);

		postProcChoice_ = shaderRow->Add(new ChoiceWithValueDisplay(&shaderNames_[i], "", &PostShaderTranslateName, new LinearLayoutParams(1.0f)));
		postProcChoice_->OnClick.Add([=](EventParams &e) {
			auto gr = GetI18NCategory("Graphics");
			auto procScreen = new PostProcScreen(gr->T("Postprocessing Shader"), i, false);
			procScreen->SetHasDropShadow(false);
			procScreen->OnChoice.Handle(this, &DisplayLayoutScreen::OnPostProcShaderChange);
			if (e.v)
				procScreen->SetPopupOrigin(e.v);
			screenManager()->push(procScreen);
			return UI::EVENT_DONE;
		});
		postProcChoice_->SetEnabledFunc([=] {
			return !g_Config.bSkipBufferEffects && !enableStereo();
		});

		if (i < g_Config.vPostShaderNames.size()) {
			bool hasSettings = false;
			std::vector<const ShaderInfo *> shaderChain = GetPostShaderChain(g_Config.vPostShaderNames[i]);
			for (auto shaderInfo : shaderChain) {
				for (size_t i = 0; i < ARRAY_SIZE(shaderInfo->settings); ++i) {
					auto &setting = shaderInfo->settings[i];
					if (!setting.name.empty()) {
						hasSettings = true;
						break;
					}
				}
			}
			if (hasSettings) {
				auto settingsButton = shaderRow->Add(new Choice(ImageID("I_SLIDERS"), new LinearLayoutParams(0.0f)));
				settingsButton->OnClick.Add([=](EventParams &e) {
					settingsVisible_[i] = !settingsVisible_[i];
					RecreateViews();
					return UI::EVENT_DONE;
				});
			}
		}

		if (i > 0 && i < g_Config.vPostShaderNames.size()) {
			auto upButton = shaderRow->Add(new Choice(ImageID("I_ARROW_UP"), new LinearLayoutParams(0.0f)));
			upButton->OnClick.Add([=](EventParams &e) {
				std::swap(g_Config.vPostShaderNames[i - 1], g_Config.vPostShaderNames[i]);
				RecreateViews();
				return UI::EVENT_DONE;
			});
		}
		if (i < g_Config.vPostShaderNames.size() - 1) {
			auto downButton = shaderRow->Add(new Choice(ImageID("I_ARROW_DOWN"), new LinearLayoutParams(0.0f)));
			downButton->OnClick.Add([=](EventParams &e) {
				std::swap(g_Config.vPostShaderNames[i], g_Config.vPostShaderNames[i + 1]);
				RecreateViews();
				return UI::EVENT_DONE;
			});
		}
		if (i < g_Config.vPostShaderNames.size()) {
			auto deleteButton = shaderRow->Add(new Choice(ImageID("I_TRASHCAN"), new LinearLayoutParams(0.0f)));
			deleteButton->OnClick.Add([=](EventParams &e) {
				g_Config.vPostShaderNames.erase(g_Config.vPostShaderNames.begin() + i);
				RecreateViews();
				return UI::EVENT_DONE;
			});
		}

		// No need for settings on the last one.
		if (i == g_Config.vPostShaderNames.size())
			continue;

		if (!settingsVisible_[i])
			continue;

		std::vector<const ShaderInfo *> shaderChain = GetPostShaderChain(g_Config.vPostShaderNames[i]);
		for (auto shaderInfo : shaderChain) {
			// Disable duplicated shader slider
			bool duplicated = alreadyAddedShader.find(shaderInfo->section) != alreadyAddedShader.end();
			alreadyAddedShader.insert(shaderInfo->section);

			LinearLayout *settingContainer = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(UI::FILL_PARENT, UI::WRAP_CONTENT, UI::Margins(24.0f, 0.0f, 0.0f, 0.0f)));
			leftColumn->Add(settingContainer);
			for (size_t i = 0; i < ARRAY_SIZE(shaderInfo->settings); ++i) {
				auto &setting = shaderInfo->settings[i];
				if (!setting.name.empty()) {
					// This map lookup will create the setting in the mPostShaderSetting map if it doesn't exist, with a default value of 0.0.
					auto &value = g_Config.mPostShaderSetting[StringFromFormat("%sSettingValue%d", shaderInfo->section.c_str(), i + 1)];
					if (duplicated) {
						auto sliderName = StringFromFormat("%s %s", ps->T(setting.name), ps->T("(duplicated setting, previous slider will be used)"));
						PopupSliderChoiceFloat *settingValue = settingContainer->Add(new PopupSliderChoiceFloat(&value, setting.minValue, setting.maxValue, sliderName, setting.step, screenManager()));
						settingValue->SetEnabled(false);
					} else {
						PopupSliderChoiceFloat *settingValue = settingContainer->Add(new PopupSliderChoiceFloat(&value, setting.minValue, setting.maxValue, ps->T(setting.name), setting.step, screenManager()));
						settingValue->SetLiveUpdate(true);
						settingValue->SetHasDropShadow(false);
						settingValue->SetEnabledFunc([=] {
							return !g_Config.bSkipBufferEffects && !enableStereo();
						});
					}
				}
			}
		}
	}

	root_->Add(new DisplayLayoutBackground(mode_, new AnchorLayoutParams(FILL_PARENT, FILL_PARENT, 0.0f, 0.0f, 0.0f, 0.0f)));
}

void PostProcScreen::CreateViews() {
	auto ps = GetI18NCategory("PostShaders");
	ReloadAllPostShaderInfo(screenManager()->getDrawContext());
	shaders_ = GetAllPostShaderInfo();
	std::vector<std::string> items;
	int selected = -1;
	const std::string selectedName = id_ >= (int)g_Config.vPostShaderNames.size() ? "Off" : g_Config.vPostShaderNames[id_];

	for (int i = 0; i < (int)shaders_.size(); i++) {
		if (!shaders_[i].visible)
			continue;
		if (shaders_[i].isStereo != showStereoShaders_)
			continue;
		if (shaders_[i].section == selectedName)
			selected = (int)indexTranslation_.size();
		items.push_back(ps->T(shaders_[i].section.c_str(), shaders_[i].name.c_str()));
		indexTranslation_.push_back(i);
	}
	adaptor_ = UI::StringVectorListAdaptor(items, selected);
	ListPopupScreen::CreateViews();
}

void PostProcScreen::OnCompleted(DialogResult result) {
	if (result != DR_OK)
		return;
	const std::string &value = shaders_[indexTranslation_[listView_->GetSelected()]].section;
	// I feel this logic belongs more in the caller, but eh...
	if (showStereoShaders_) {
		g_Config.sStereoToMonoShader = value;
	} else {
		if (id_ < (int)g_Config.vPostShaderNames.size())
			g_Config.vPostShaderNames[id_] = value;
		else
			g_Config.vPostShaderNames.push_back(value);
	}
}
