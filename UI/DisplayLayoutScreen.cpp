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
#include "Common/UI/UIScreen.h"
#include "Common/UI/TabHolder.h"
#include "Common/Math/math_util.h"
#include "Common/System/NativeApp.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/StringUtils.h"

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Text/I18n.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/Background.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "GPU/GPUCommon.h"
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
	b.x = rc.x * g_display.dpi_scale_x;
	b.y = rc.y * g_display.dpi_scale_y;
	b.w = rc.w * g_display.dpi_scale_x;
	b.h = rc.h * g_display.dpi_scale_y;
	return b;
}

class DisplayLayoutBackground : public UI::View {
public:
	DisplayLayoutBackground(UI::ChoiceStrip *mode, DeviceOrientation orientation, UI::LayoutParams *layoutParams) : UI::View(layoutParams), mode_(mode), orientation_(orientation) {}

	bool Touch(const TouchInput &touch) override {
		int mode = mode_ ? mode_->GetSelection() : 0;

		DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(orientation_);

		if ((touch.flags & TouchInputFlags::MOVE) != 0 && dragging_) {
			float relativeTouchX = touch.x - startX_;
			float relativeTouchY = touch.y - startY_;

			switch (mode) {
			case MODE_MOVE:
				config.fDisplayOffsetX = clamp_value(startDisplayOffsetX_ + relativeTouchX / bounds_.w, 0.0f, 1.0f);
				config.fDisplayOffsetY = clamp_value(startDisplayOffsetY_ + relativeTouchY / bounds_.h, 0.0f, 1.0f);
				break;
			case MODE_RESIZE:
			{
				// Resize. Vertical = scaling; Up should be bigger so let's negate in that direction
				float diffYProp = -relativeTouchY * 0.007f;
				config.fDisplayScale = clamp_value(startScale_ * powf(2.0f, diffYProp), 0.2f, 2.0f);
				break;
			}
			}
		}

		if ((touch.flags & TouchInputFlags::DOWN) != 0 && !dragging_) {
			// Check that we're in the central 80% of the screen.
			// If outside, it may be a drag from displaying the back button on phones
			// where you have to drag from the side, etc.
			if (touch.x >= bounds_.w * 0.1f && touch.x <= bounds_.w * 0.9f &&
				touch.y >= bounds_.h * 0.1f && touch.y <= bounds_.h * 0.9f) {
				dragging_ = true;
				startX_ = touch.x;
				startY_ = touch.y;
				startDisplayOffsetX_ = config.fDisplayOffsetX;
				startDisplayOffsetY_ = config.fDisplayOffsetY;
				startScale_ = config.fDisplayScale;
			}
		}

		if ((touch.flags & TouchInputFlags::UP) != 0 && dragging_) {
			dragging_ = false;
		}

		return true;
	}

private:
	UI::ChoiceStrip *mode_;
	bool dragging_ = false;
	const DeviceOrientation orientation_;
	// Touch down state for drag to resize etc
	float startX_ = 0.0f;
	float startY_ = 0.0f;
	float startScale_ = -1.0f;
	float startDisplayOffsetX_ = -1.0f;
	float startDisplayOffsetY_ = -1.0f;
};

DisplayLayoutScreen::DisplayLayoutScreen(const Path &filename) : UIBaseDialogScreen(filename) {}

void DisplayLayoutScreen::DrawBackground(UIContext &dc) {
	if (PSP_GetBootState() == BootState::Complete && !g_Config.bSkipBufferEffects) {
		// We normally rely on the PSP screen showing through.
	} else {
		// But if it's not present (we're not in game, or skip buffer effects is used),
		// we have to draw a substitute ourselves.
		UIContext &dc = *screenManager()->getUIContext();
		DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());

		// TODO: Clean this up a bit, this GetScreenFrame/CenterDisplay combo is too common.
		FRect screenFrame = GetScreenFrame(config.bIgnoreScreenInsets, g_display.pixel_xres, g_display.pixel_yres);
		FRect rc;
		CalculateDisplayOutputRect(config, &rc, 480.0f, 272.0f, screenFrame, config.iInternalScreenRotation);

		dc.Flush();
		ImageID bg = ImageID("I_PSP_DISPLAY");
		dc.Draw()->DrawImageStretch(bg, dc.GetBounds(), 0x7F000000);
		dc.Draw()->DrawImageStretch(bg, FRectToBounds(rc), 0x7FFFFFFF);
	}
}

void DisplayLayoutScreen::onFinish(DialogResult reason) {
	g_Config.Save("DisplayLayoutScreen::onFinish");
}

void DisplayLayoutScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	RecreateViews();
}

static void NotifyPostChanges() {
	System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
	System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);  // To deal with shaders that can change render resolution like upscaling.
	System_PostUIMessage(UIMessage::POSTSHADER_UPDATED);

	if (gpu) {
		gpu->NotifyConfigChanged();
	}
}

void DisplayLayoutScreen::OnPostProcShaderChange(UI::EventParams &e) {
	// Remove the virtual "Off" entry. TODO: Get rid of it generally.
	g_Config.vPostShaderNames.erase(std::remove(g_Config.vPostShaderNames.begin(), g_Config.vPostShaderNames.end(), "Off"), g_Config.vPostShaderNames.end());
	FixPostShaderOrder(&g_Config.vPostShaderNames);
	NotifyPostChanges();
}

static std::string PostShaderTranslateName(std::string_view value) {
	if (value == "Off") {
		auto gr = GetI18NCategory(I18NCat::GRAPHICS);
		// Off is a legacy fake item (gonna migrate off it later).
		return std::string(gr->T("Add postprocessing shader"));
	}

	const ShaderInfo *info = GetPostShaderInfo(value);
	if (info) {
		auto ps = GetI18NCategory(I18NCat::POSTSHADERS);
		return std::string(ps->T(value, info->name));
	} else {
		return std::string(value);
	}
}

void DisplayLayoutScreen::sendMessage(UIMessage message, const char *value) {
	UIBaseDialogScreen::sendMessage(message, value);
	if (message == UIMessage::POSTSHADER_UPDATED) {
		g_Config.bShaderChainRequires60FPS = PostShaderChainRequires60FPS(GetFullPostShadersChain(g_Config.vPostShaderNames));
		RecreateViews();
	}
}

void DisplayLayoutScreen::CreateViews() {
	const Bounds &bounds = screenManager()->getUIContext()->GetBounds();

	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);
	auto co = GetI18NCategory(I18NCat::CONTROLS);
	auto ps = GetI18NCategory(I18NCat::POSTSHADERS);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	const bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	// Make it so that a touch can only affect one view. Makes manipulating the background through the buttons
	// impossible.
	root_->SetExclusiveTouch(true);

	// Add indicator of the current mode we're editing. Not sure why these strings are in Controls.
	root_->Add(new TextView(portrait ? co->T("Portrait") : co->T("Landscape"), new AnchorLayoutParams(portrait ? 10.0f : NONE, 10.0f, NONE, NONE)))->SetSmall(true);

	DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());
	bool internalPortrait = config.iInternalScreenRotation == ROTATION_LOCKED_VERTICAL || config.iInternalScreenRotation == ROTATION_LOCKED_VERTICAL180;

	LinearLayout *leftColumn;
	if (!portrait) {
		ScrollView *leftScrollView = new ScrollView(ORIENT_VERTICAL, new AnchorLayoutParams(420.0f, FILL_PARENT, 0.f, 0.f, NONE, 0.f));
		leftColumn = new LinearLayout(ORIENT_VERTICAL);
		leftColumn->padding.SetAll(8.0f);
		leftScrollView->Add(leftColumn);
		leftScrollView->SetClickableBackground(true);
		root_->Add(leftScrollView);
	}

	ScrollView *rightScrollView = new ScrollView(ORIENT_VERTICAL, new AnchorLayoutParams(300.0f, FILL_PARENT, NONE, 0.f, 0.f, 0.f));
	LinearLayout *rightColumn = new LinearLayout(ORIENT_VERTICAL);
	rightColumn->padding.SetAll(8.0f);
	rightColumn->SetSpacing(0.0f);
	rightScrollView->Add(rightColumn);
	rightScrollView->SetClickableBackground(true);
	root_->Add(rightScrollView);

	Choice *back = new Choice(di->T("Back"), ImageID("I_NAVIGATE_BACK"));
	back->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	rightColumn->Add(back);

	LinearLayout *bottomControls;
	if (portrait) {
		bottomControls = new LinearLayout(ORIENT_HORIZONTAL);
		rightColumn->Add(bottomControls);
		leftColumn = rightColumn;
	} else {
		bottomControls = new LinearLayout(ORIENT_HORIZONTAL, new AnchorLayoutParams(NONE, NONE, NONE, 10.0f));
		root_->Add(bottomControls);
	}

	// Set backgrounds for readability
	Drawable backgroundWithAlpha(GetBackgroundColorWithAlpha(*screenManager()->getUIContext()));
	leftColumn->SetBG(backgroundWithAlpha);
	rightColumn->SetBG(backgroundWithAlpha);

	if (!IsVREnabled()) {
		if (portrait == internalPortrait) {
			// Stretch doesn't make sense in portrait mode (looks crazy), so we only show it in landscape mode.
			// Vice versa if internal is portrait.
			auto stretch = new CheckBox(&config.bDisplayStretch, gr->T("Stretch"));
			stretch->SetDisabledPtr(&config.bDisplayIntegerScale);
			rightColumn->Add(stretch);
		}

		PopupSliderChoiceFloat *aspectRatio = new PopupSliderChoiceFloat(&config.fDisplayAspectRatio, 0.1f, 2.0f, 1.0f, gr->T("Aspect Ratio"), screenManager());
		rightColumn->Add(aspectRatio);
		aspectRatio->SetEnabledFunc([config]() {
			return !config.bDisplayStretch && !config.bDisplayIntegerScale;
		});
		aspectRatio->SetHasDropShadow(false);
		aspectRatio->SetLiveUpdate(true);

		rightColumn->Add(new CheckBox(&config.bDisplayIntegerScale, gr->T("Integer scale factor")));

		rightColumn->Add(new Spacer(12.0f));

		bool supportsInsets = false;
#if PPSSPP_PLATFORM(ANDROID)
		supportsInsets = System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 28;
#elif PPSSPP_PLATFORM(IOS)
		supportsInsets = true;
#endif
		// Hide insets option if no insets, or OS too old.
		if (supportsInsets && (
			System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_LEFT) != 0.0f ||
			System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_TOP) != 0.0f ||
			System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_RIGHT) != 0.0f ||
			System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_BOTTOM) != 0.0f)) {
			rightColumn->Add(new CheckBox(&config.bIgnoreScreenInsets, gr->T("Ignore camera notch when centering")));
		}

		static const char *displayRotation[] = { "Landscape", "Portrait", "Landscape Reversed", "Portrait Reversed" };
		auto rotation = new PopupMultiChoice(&config.iInternalScreenRotation, gr->T("Rotation"), displayRotation, 1, ARRAY_SIZE(displayRotation), I18NCat::CONTROLS, screenManager());
		rotation->OnChoice.Add([this](UI::EventParams &) {
			// Affects the presence of the Stretch checkbox.
			RecreateViews();
		});
		rotation->SetEnabledFunc([] {
			return !g_Config.bSkipBufferEffects || g_Config.bSoftwareRendering;
		});
		rotation->SetHideTitle(true);

		rightColumn->Add(new ItemHeader(gr->T("Display rotation")));
		rightColumn->Add(rotation);
		rightColumn->Add(new CheckBox(&config.bRotateControlsWithScreen, gr->T("Rotate controls")))->SetEnabledFunc([&config]() -> bool {
			return (!g_Config.bSkipBufferEffects || g_Config.bSoftwareRendering) && config.iInternalScreenRotation != 1;
		});

		rightColumn->Add(new Spacer(12.0f));

		Choice *center = new Choice(di->T("Reset"));
		center->OnClick.Add([&config, portrait](UI::EventParams &) {
			// Hm, not really ideal to have to use strings here.
			config.ResetToDefault(portrait ? "DisplayLayout.Portrait" : "DisplayLayout.Landscape");
		});
		rightColumn->Add(center);

		mode_ = new ChoiceStrip(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
		mode_->AddChoice(sy->T("Off"));
		mode_->AddChoice(ImageID("I_MOVE"));
		mode_->AddChoice(ImageID("I_RESIZE"));
		mode_->SetSelection(0, false);
		bottomControls->Add(mode_);
	}

	if (portrait) {
		leftColumn->Add(new Spacer(24.0f));
	}

	if (!IsVREnabled()) {
		static const char *bufFilters[] = { "Linear", "Nearest", };
		leftColumn->Add(new PopupMultiChoice(&config.iDisplayFilter, gr->T("Screen Scaling Filter"), bufFilters, 1, ARRAY_SIZE(bufFilters), I18NCat::GRAPHICS, screenManager()));
	}

	Draw::DrawContext *draw = screenManager()->getDrawContext();

	bool multiViewSupported = draw->GetDeviceCaps().multiViewSupported;

	auto enableStereo = [=]() -> bool {
		return g_Config.bStereoRendering && multiViewSupported;
	};

	leftColumn->Add(new ItemHeader(gr->T("Postprocessing shaders")));

	std::set<std::string> alreadyAddedShader;
	// If there's a single post shader and we're just entering the dialog,
	// auto-open the settings.
	if (settingsVisible_.empty() && g_Config.vPostShaderNames.size() == 1) {
		settingsVisible_.push_back(true);
	} else if (settingsVisible_.size() < g_Config.vPostShaderNames.size()) {
		settingsVisible_.resize(g_Config.vPostShaderNames.size());
	}

	static const ContextMenuItem postShaderContextMenu[] = {
		{ "Move Up", "I_ARROW_UP" },
		{ "Move Down", "I_ARROW_DOWN" },
		{ "Remove", "I_TRASHCAN" },
	};

	for (int i = 0; i < (int)g_Config.vPostShaderNames.size() + 1 && i < ARRAY_SIZE(shaderNames_); ++i) {
		// Vector element pointer get invalidated on resize, cache name to have always a valid reference in the rendering thread
		shaderNames_[i] = i == g_Config.vPostShaderNames.size() ? "Off" : g_Config.vPostShaderNames[i];

		LinearLayout *shaderRow = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(UI::FILL_PARENT, UI::WRAP_CONTENT));
		shaderRow->SetSpacing(4.0f);
		leftColumn->Add(shaderRow);

		if (shaderNames_[i] != "Off") {
			postProcChoice_ = shaderRow->Add(new ChoiceWithValueDisplay(&shaderNames_[i], "", &PostShaderTranslateName, new LinearLayoutParams(1.0f)));
		} else {
			postProcChoice_ = shaderRow->Add(new Choice(ImageID("I_PLUS")));
		}
		postProcChoice_->OnClick.Add([=](EventParams &e) {
			auto gr = GetI18NCategory(I18NCat::GRAPHICS);
			auto procScreen = new PostProcScreen(gr->T("Postprocessing shaders"), i, false);
			procScreen->SetHasDropShadow(false);
			procScreen->OnChoice.Handle(this, &DisplayLayoutScreen::OnPostProcShaderChange);
			if (e.v)
				procScreen->SetPopupOrigin(e.v);
			screenManager()->push(procScreen);
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
				CheckBox *checkBox = new CheckBox(&settingsVisible_[i], ImageID("I_SLIDERS"), new LinearLayoutParams(0.0f));
				auto settingsButton = shaderRow->Add(checkBox);
				settingsButton->OnClick.Add([=](EventParams &e) {
					RecreateViews();
				});
			}

			auto removeButton = shaderRow->Add(new Choice(ImageID("I_TRASHCAN"), new LinearLayoutParams(0.0f)));
			removeButton->OnClick.Add([=](EventParams &e) -> void {
				// Protect against possible race conditions.
				if (i < g_Config.vPostShaderNames.size()) {
					g_Config.vPostShaderNames.erase(g_Config.vPostShaderNames.begin() + i);
					FixPostShaderOrder(&g_Config.vPostShaderNames);
					NotifyPostChanges();
					RecreateViews();
				}
			});

			auto moreButton = shaderRow->Add(new Choice(ImageID("I_THREE_DOTS"), new LinearLayoutParams(0.0f)));
			moreButton->OnClick.Add([=](EventParams &e) -> void {
				if (i >= g_Config.vPostShaderNames.size()) {
					// Protect against possible race conditions.
					return;
				}

				PopupContextMenuScreen *contextMenu = new UI::PopupContextMenuScreen(postShaderContextMenu, ARRAY_SIZE(postShaderContextMenu), I18NCat::DIALOG, moreButton);
				screenManager()->push(contextMenu);
				const ShaderInfo *info = GetPostShaderInfo(g_Config.vPostShaderNames[i]);
				bool usesLastFrame = info ? info->usePreviousFrame : false;
				contextMenu->SetEnabled(0, i > 0 && !usesLastFrame);
				contextMenu->SetEnabled(1, i < g_Config.vPostShaderNames.size() - 1);
				contextMenu->OnChoice.Add([=](EventParams &e) -> void {
					switch (e.a) {
					case 0:  // Move up
						std::swap(g_Config.vPostShaderNames[i - 1], g_Config.vPostShaderNames[i]);
						break;
					case 1:  // Move down
						std::swap(g_Config.vPostShaderNames[i], g_Config.vPostShaderNames[i + 1]);
						break;
					case 2:  // Remove
						g_Config.vPostShaderNames.erase(g_Config.vPostShaderNames.begin() + i);
						break;
					default:
						return;
					}
					FixPostShaderOrder(&g_Config.vPostShaderNames);
					NotifyPostChanges();
					RecreateViews();
				});
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
					std::string key = StringFromFormat("%sSettingCurrentValue%d", shaderInfo->section.c_str(), i + 1);
					bool keyExisted = g_Config.mPostShaderSetting.find(key) != g_Config.mPostShaderSetting.end();
					auto &value = g_Config.mPostShaderSetting[key];
					if (!keyExisted)
						value = setting.value;

					if (duplicated) {
						auto sliderName = StringFromFormat("%s %s", ps->T_cstr(setting.name.c_str()), ps->T_cstr("(duplicated setting, previous slider will be used)"));
						PopupSliderChoiceFloat *settingValue = settingContainer->Add(new PopupSliderChoiceFloat(&value, setting.minValue, setting.maxValue, setting.value, sliderName, setting.step, screenManager()));
						settingValue->SetEnabled(false);
					} else {
						PopupSliderChoiceFloat *settingValue = settingContainer->Add(new PopupSliderChoiceFloat(&value, setting.minValue, setting.maxValue, setting.value, ps->T(setting.name), setting.step, screenManager()));
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

	root_->Add(new DisplayLayoutBackground(mode_, GetDeviceOrientation(), new AnchorLayoutParams(FILL_PARENT, FILL_PARENT, 0.0f, 0.0f, 0.0f, 0.0f)));
}

void PostProcScreen::CreateViews() {
	auto ps = GetI18NCategory(I18NCat::POSTSHADERS);
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
		items.push_back(std::string(ps->T(shaders_[i].section.c_str(), shaders_[i].name.c_str())));
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
		if (g_Config.sStereoToMonoShader != value) {
			g_Config.sStereoToMonoShader = value;
			System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
		}
	} else {
		if (id_ < (int)g_Config.vPostShaderNames.size()) {
			if (g_Config.vPostShaderNames[id_] != value) {
				g_Config.vPostShaderNames[id_] = value;
				System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
			}
		} else {
			g_Config.vPostShaderNames.push_back(value);
			System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
		}
	}
}
