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

#pragma once

#include <functional>
#include <memory>

#include "Common/File/Path.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/ViewGroup.h"
#include "UI/MiscScreens.h"
#include "UI/TextureUtil.h"

class GamePauseScreen : public UIDialogScreenWithGameBackground {
public:
	GamePauseScreen(const Path &filename) : UIDialogScreenWithGameBackground(filename), gamePath_(filename) {}
	virtual ~GamePauseScreen();

	virtual void dialogFinished(const Screen *dialog, DialogResult dr) override;

protected:
	virtual void CreateViews() override;
	virtual void update() override;
	void CallbackDeleteConfig(bool yes);

private:
	UI::EventReturn OnGameSettings(UI::EventParams &e);
	UI::EventReturn OnExitToMenu(UI::EventParams &e);
	UI::EventReturn OnReportFeedback(UI::EventParams &e);

	UI::EventReturn OnRewind(UI::EventParams &e);
	UI::EventReturn OnLoadUndo(UI::EventParams &e);
	UI::EventReturn OnLastSaveUndo(UI::EventParams &e);

	UI::EventReturn OnScreenshotClicked(UI::EventParams &e);
	UI::EventReturn OnCwCheat(UI::EventParams &e);

	UI::EventReturn OnCreateConfig(UI::EventParams &e);
	UI::EventReturn OnDeleteConfig(UI::EventParams &e);

	UI::EventReturn OnSwitchUMD(UI::EventParams &e);
	UI::EventReturn OnState(UI::EventParams &e);

	// hack
	bool finishNextFrame_ = false;
	Path gamePath_;
};

// AsyncImageFileView loads a texture from a file, and reloads it as necessary.
// TODO: Actually make async, doh.
class AsyncImageFileView : public UI::Clickable {
public:
	AsyncImageFileView(const Path &filename, UI::ImageSizeMode sizeMode, UI::LayoutParams *layoutParams = 0);
	~AsyncImageFileView();

	void GetContentDimensionsBySpec(const UIContext &dc, UI::MeasureSpec horiz, UI::MeasureSpec vert, float &w, float &h) const override;
	void Draw(UIContext &dc) override;
	std::string DescribeText() const override { return text_; }

	void DeviceLost() override;
	void DeviceRestored(Draw::DrawContext *draw) override;

	void SetFilename(const Path &filename);
	void SetColor(uint32_t color) { color_ = color; }
	void SetOverlayText(std::string text) { text_ = text; }
	void SetFixedSize(float fixW, float fixH) { fixedSizeW_ = fixW; fixedSizeH_ = fixH; }
	void SetCanBeFocused(bool can) { canFocus_ = can; }

	bool CanBeFocused() const override { return canFocus_; }

	const Path &GetFilename() const { return filename_; }

private:
	bool canFocus_;
	Path filename_;
	std::string text_;
	uint32_t color_;
	UI::ImageSizeMode sizeMode_;

	std::unique_ptr<ManagedTexture> texture_;
	bool textureFailed_;
	float fixedSizeW_;
	float fixedSizeH_;
};
