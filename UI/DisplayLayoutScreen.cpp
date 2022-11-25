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
#include "Common/StringUtils.h"

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Text/I18n.h"
#include "UI/DisplayLayoutScreen.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/PresentationCommon.h"
#include "GPU/Common/PostShader.h"

static const int leftColumnWidth = 200;
static const float orgRatio = 1.764706f;  // 480.0 / 272.0

enum Mode {
	MODE_MOVE,
	MODE_RESIZE,
};

static Bounds FRectToBounds(FRect rc) {
	Bounds b;
	b.x = rc.x * g_dpi_scale_x;
	b.y = rc.y * g_dpi_scale_y;
	b.w = rc.w * g_dpi_scale_x;
	b.h = rc.h * g_dpi_scale_y;
	return b;
}

DisplayLayoutScreen::DisplayLayoutScreen(const Path &filename) : UIDialogScreenWithGameBackground(filename) {
	// Ignore insets - just couldn't get the logic to work.
	ignoreInsets_ = true;

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

bool DisplayLayoutScreen::touch(const TouchInput &touch) {
	UIScreen::touch(touch);

	using namespace UI;

	int mode = mode_ ? mode_->GetSelection() : 0;
	if (g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::AUTO) {
		mode = -1;
	}

	const Bounds &screenBounds = screenManager()->getUIContext()->GetBounds();
	if ((touch.flags & TOUCH_MOVE) != 0 && dragging_) {
		float relativeTouchX = touch.x - startX_;
		float relativeTouchY = touch.y - startY_;

		switch (mode) {
		case MODE_MOVE:
		{
			g_Config.fSmallDisplayOffsetX = startDisplayOffsetX_ + relativeTouchX * 0.5f / screenBounds.w;
			g_Config.fSmallDisplayOffsetY = startDisplayOffsetY_ + relativeTouchY * 0.5f / screenBounds.h;
			break;
		}
		case MODE_RESIZE:
		{
			// Resize. Vertical = scaling; Up should be bigger so let's negate in that direction
			float diffYProp = -relativeTouchY * 0.007f;
			g_Config.fSmallDisplayZoomLevel = startScale_ * powf(2.0f, diffYProp);
			break;
		}
		}
	}

	if ((touch.flags & TOUCH_DOWN) != 0 && !dragging_) {
		dragging_ = true;
		startX_ = touch.x;
		startY_ = touch.y;
		startDisplayOffsetX_ = g_Config.fSmallDisplayOffsetX;
		startDisplayOffsetY_ = g_Config.fSmallDisplayOffsetY;
		startScale_ = g_Config.fSmallDisplayZoomLevel;
	}

	if ((touch.flags & TOUCH_UP) != 0 && dragging_) {
		dragging_ = false;
	}

	return true;
}

void DisplayLayoutScreen::onFinish(DialogResult reason) {
	g_Config.Save("DisplayLayoutScreen::onFinish");
}

UI::EventReturn DisplayLayoutScreen::OnCenter(UI::EventParams &e) {
	g_Config.fSmallDisplayOffsetX = 0.5f;
	g_Config.fSmallDisplayOffsetY = 0.5f;
	RecreateViews();
	return UI::EVENT_DONE;
};

UI::EventReturn DisplayLayoutScreen::OnZoomTypeChange(UI::EventParams &e) {
	switch (g_Config.iSmallDisplayZoomType) {
	case (int)SmallDisplayZoom::AUTO:
	case (int)SmallDisplayZoom::PARTIAL_STRETCH:
	case (int)SmallDisplayZoom::STRETCH:
		g_Config.fSmallDisplayOffsetX = 0.5f;
		g_Config.fSmallDisplayOffsetY = 0.5f;
		break;
	default:
		// Not SmallDisplayZoom::MANUAL
		break;
	}
	RecreateViews();
	return UI::EVENT_DONE;
};

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

	ScrollView *rightScrollView = new ScrollView(ORIENT_VERTICAL, new AnchorLayoutParams(300.0f, FILL_PARENT, NONE, 10.f, 10.f, 10.f, false));
	ViewGroup *rightColumn = new LinearLayout(ORIENT_VERTICAL);
	rightScrollView->Add(rightColumn);
	root_->Add(rightScrollView);

	// We manually implement insets here for the buttons. This file defied refactoring :(
	float leftInset = System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_LEFT);

	bool displayRotEnable = !g_Config.bSkipBufferEffects || g_Config.bSoftwareRendering;
	bRotated_ = false;
	if (displayRotEnable && (g_Config.iInternalScreenRotation == ROTATION_LOCKED_VERTICAL || g_Config.iInternalScreenRotation == ROTATION_LOCKED_VERTICAL180)) {
		bRotated_ = true;
	}

	mode_ = nullptr;
	if (g_Config.iSmallDisplayZoomType >= (int)SmallDisplayZoom::AUTO) { // Scaling
		if (g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::AUTO) {
			float autoBound = bounds.h / 270.0f;
			// Case of screen rotated ~ only works with buffered rendering
			if (bRotated_) {
				autoBound = bounds.h / 480.0f;
			} else { // Without rotation in common cases like 1080p we cut off 2 pixels of height, this reflects other cases
				float resCommonWidescreen = autoBound - floor(autoBound);
				if (resCommonWidescreen != 0.0f) {
					float ratio = bounds.w / bounds.h;
					if (ratio < orgRatio) {
						autoBound = bounds.w / 480.0f;
					}
					else {
						autoBound = bounds.h / 272.0f;
					}
				}
			}
			g_Config.fSmallDisplayZoomLevel = autoBound;
			g_Config.fSmallDisplayOffsetX = 0.5f;
			g_Config.fSmallDisplayOffsetY = 0.5f;
		} else { // Manual Scaling
			Choice *center = new Choice(di->T("Center"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10 + leftInset, NONE, NONE, 74));
			center->OnClick.Handle(this, &DisplayLayoutScreen::OnCenter);
			root_->Add(center);
			float minZoom = 1.0f;
			PopupSliderChoiceFloat *zoomlvl = new PopupSliderChoiceFloat(&g_Config.fSmallDisplayZoomLevel, minZoom, 10.0f, di->T("Zoom"), 1.0f, screenManager(), di->T("* PSP res"), new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10 + leftInset, NONE, NONE, 10 + 64 + 64));
			root_->Add(zoomlvl);
			mode_ = new ChoiceStrip(ORIENT_VERTICAL, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10 + leftInset, NONE, NONE, 158 + 64 + 10));
			mode_->AddChoice(di->T("Move"));
			mode_->AddChoice(di->T("Resize"));
			mode_->SetSelection(0, false);
		}
	}

	if (mode_) {
		root_->Add(mode_);
	}

	static const char *zoomLevels[] = { "Stretching", "Partial Stretch", "Auto Scaling", "Manual Scaling" };
	auto zoom = new PopupMultiChoice(&g_Config.iSmallDisplayZoomType, gr->T("Mode"), zoomLevels, 0, ARRAY_SIZE(zoomLevels), gr->GetName(), screenManager(), new AnchorLayoutParams(400, WRAP_CONTENT, bounds.w / 2.0f - 200.0f, NONE, NONE, 10));
	zoom->OnChoice.Handle(this, &DisplayLayoutScreen::OnZoomTypeChange);
	rightColumn->Add(zoom);

	static const char *displayRotation[] = { "Landscape", "Portrait", "Landscape Reversed", "Portrait Reversed" };
	auto rotation = new PopupMultiChoice(&g_Config.iInternalScreenRotation, gr->T("Rotation"), displayRotation, 1, ARRAY_SIZE(displayRotation), co->GetName(), screenManager());
	rotation->SetEnabledFunc([] {
		return !g_Config.bSkipBufferEffects || g_Config.bSoftwareRendering;
	});
	rightColumn->Add(rotation);

	static const char *bufFilters[] = { "Linear", "Nearest", };
	rightColumn->Add(new PopupMultiChoice(&g_Config.iBufFilter, gr->T("Screen Scaling Filter"), bufFilters, 1, ARRAY_SIZE(bufFilters), gr->GetName(), screenManager()));

	rightColumn->Add(new ItemHeader(gr->T("Postprocessing effect")));

	Draw::DrawContext *draw = screenManager()->getDrawContext();

	bool multiViewSupported = draw->GetDeviceCaps().multiViewSupported;

	auto enableStereo = [=]() -> bool {
		return g_Config.bStereoRendering && multiViewSupported;
	};

	std::set<std::string> alreadyAddedShader;
	for (int i = 0; i < (int)g_Config.vPostShaderNames.size() + 1 && i < ARRAY_SIZE(shaderNames_); ++i) {
		// Vector element pointer get invalidated on resize, cache name to have always a valid reference in the rendering thread
		shaderNames_[i] = i == g_Config.vPostShaderNames.size() ? "Off" : g_Config.vPostShaderNames[i];
		rightColumn->Add(new ItemHeader(StringFromFormat("%s #%d", gr->T("Postprocessing Shader"), i + 1)));
		postProcChoice_ = rightColumn->Add(new ChoiceWithValueDisplay(&shaderNames_[i], "", &PostShaderTranslateName));
		postProcChoice_->OnClick.Add([=](EventParams &e) {
			auto gr = GetI18NCategory("Graphics");
			auto procScreen = new PostProcScreen(gr->T("Postprocessing Shader"), i, false);
			procScreen->OnChoice.Handle(this, &DisplayLayoutScreen::OnPostProcShaderChange);
			if (e.v)
				procScreen->SetPopupOrigin(e.v);
			screenManager()->push(procScreen);
			return UI::EVENT_DONE;
		});
		postProcChoice_->SetEnabledFunc([=] {
			return !g_Config.bSkipBufferEffects && !enableStereo();
		});

		// No need for settings on the last one.
		if (i == g_Config.vPostShaderNames.size())
			continue;

		auto shaderChain = GetPostShaderChain(g_Config.vPostShaderNames[i]);
		for (auto shaderInfo : shaderChain) {
			// Disable duplicated shader slider
			bool duplicated = alreadyAddedShader.find(shaderInfo->section) != alreadyAddedShader.end();
			alreadyAddedShader.insert(shaderInfo->section);
			for (size_t i = 0; i < ARRAY_SIZE(shaderInfo->settings); ++i) {
				auto &setting = shaderInfo->settings[i];
				if (!setting.name.empty()) {
					auto &value = g_Config.mPostShaderSetting[StringFromFormat("%sSettingValue%d", shaderInfo->section.c_str(), i + 1)];
					if (duplicated) {
						auto sliderName = StringFromFormat("%s %s", ps->T(setting.name), ps->T("(duplicated setting, previous slider will be used)"));
						PopupSliderChoiceFloat *settingValue = rightColumn->Add(new PopupSliderChoiceFloat(&value, setting.minValue, setting.maxValue, sliderName, setting.step, screenManager()));
						settingValue->SetEnabled(false);
					} else {
						PopupSliderChoiceFloat *settingValue = rightColumn->Add(new PopupSliderChoiceFloat(&value, setting.minValue, setting.maxValue, ps->T(setting.name), setting.step, screenManager()));
						settingValue->SetEnabledFunc([=] {
							return !g_Config.bSkipBufferEffects && !enableStereo();
						});
					}
				}
			}
		}
	}

	Choice *back = new Choice(di->T("Back"), "", false);
	back->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	rightColumn->Add(back);
}
