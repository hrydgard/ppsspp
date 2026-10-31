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


#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

#include "Common/File/Path.h"
#include "Common/TimeUtil.h"
#include "Common/UI/View.h"
#include "Common/UI/Root.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "UI/MiscViews.h"

class GameInfo;

// Icon dimensions in the large list, and its padding around the icon.
static constexpr float LARGE_LIST_ICON_W = 144.0f;
static constexpr float LARGE_LIST_ICON_H = 80.0f;
static constexpr float LARGE_LIST_PAD = 6.0f;
static constexpr float LARGE_LIST_LEFT_PAD = 11.0f;
static constexpr float LARGE_LIST_INFO_SIZE = 64.0f;

class GameButton : public UI::Clickable {
public:
	GameButton(const Path &gamePath, GameBrowserViewMode viewMode, UI::LayoutParams *layoutParams = nullptr)
		: UI::Clickable(layoutParams), viewMode_(viewMode), gamePath_(gamePath) {}

	void Draw(UIContext &dc) override;
	std::string DescribeText() const override;
	std::string SearchAlias() const override {
		// Without the extension, or searching for "iso" would match every ISO. Only three or more
		// letters counts as one, so a folder like "Game v1.2" keeps its version number.
		std::string filename = gamePath_.GetFilename();
		const std::string ext = gamePath_.GetFileExtension();
		if (ext.size() >= 4 && ext.size() <= filename.size() && std::all_of(ext.begin() + 1, ext.end(), [](char c) { return c >= 'a' && c <= 'z'; })) {
			filename.resize(filename.size() - ext.size());
		}
		return filename;
	}
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		switch (viewMode_) {
		case GameBrowserViewMode::GRID:
			w = 144 * g_Config.fGameGridScale;
			h = 80 * g_Config.fGameGridScale;
			break;
		case GameBrowserViewMode::LARGE_LIST:
			w = 500;
			h = LARGE_LIST_ICON_H * g_Config.fGameGridScale + LARGE_LIST_PAD * 2;
			break;
		default:  // List
			w = 500;
			h = 50;
			break;
		}
	}
	const Path &GamePath() const { return gamePath_; }

	void SetHoldEnabled(bool hold) {
		holdEnabled_ = hold;
	}
	bool Touch(const TouchInput &input) override {
		hovering_ = bounds_.Contains(input.x, input.y);
		if (HasInfoButton() && TouchInfoButton(input)) {
			return true;
		}
		const bool retval = UI::Clickable::Touch(input);
		if (hovering_ && (input.flags & TouchInputFlags::DOWN)) {
			holdStart_ = time_now_d();
		}
		if (input.flags & TouchInputFlags::UP) {
			holdStart_ = 0;
		}
		return retval;
	}

	bool Key(const KeyInput &key) override {
		bool showInfo = false;

		if (HasFocus() && UI::IsInfoKey(key)) {
			// If it's the button that's mapped to triangle, then show the info.
			if (key.flags & KeyInputFlags::DOWN) {
				infoKeyPressed_ = true;
			}
			if ((key.flags & KeyInputFlags::UP) && infoKeyPressed_) {
				showInfo = true;
				infoKeyPressed_ = false;
			}
		} else if (hovering_ && key.deviceId == DEVICE_ID_MOUSE && key.keyCode == NKCODE_EXT_MOUSEBUTTON_2) {
			// If it's the right mouse button, and it's not otherwise mapped, show the info also.
			if (key.flags & KeyInputFlags::DOWN) {
				showInfoPressed_ = true;
			}
			if ((key.flags & KeyInputFlags::UP) && showInfoPressed_) {
				showInfo = true;
				showInfoPressed_ = false;
			}
		}

		if (showInfo) {
			TriggerOnHoldClick();
			return true;
		}

		if (infoSelected_ && HasFocus() && UI::IsAcceptKey(key)) {
			if (key.flags & KeyInputFlags::DOWN) {
				infoButtonDown_ = true;
			}
			if ((key.flags & KeyInputFlags::UP) && infoButtonDown_) {
				infoButtonDown_ = false;
				TriggerOnHoldClick();
			}
			return true;
		}

		// Holding the accept button works like a long press, so a gamepad can do it too.
		if (holdEnabled_ && HasFocus() && UI::IsAcceptKey(key) && !(key.flags & KeyInputFlags::IS_REPEAT)) {
			if (key.flags & KeyInputFlags::DOWN) {
				holdStart_ = time_now_d();
			}
			if (key.flags & KeyInputFlags::UP) {
				holdStart_ = 0.0;
			}
		}

		return Clickable::Key(key);
	}

	void Update() override {
		// Hold button for 1.5 seconds to launch the game options
		if (holdEnabled_ && holdStart_ != 0.0 && holdStart_ < time_now_d() - 1.5) {
			TriggerOnHoldClick();
		}
	}

	void FocusChanged(UI::FocusFlags focusFlags) override {
		UI::Clickable::FocusChanged(focusFlags);
		if (focusFlags & UI::FocusFlags::LOST_FOCUS) {
			// Don't let a held accept button carry over from a button we've left.
			holdStart_ = 0.0;
			// Moving to another game selects the game, not its info button.
			infoSelected_ = false;
			infoButtonDown_ = false;
		}
		TriggerOnHighlight(focusFlags);
	}

	// Left and right move between the game and its info button, so up and down still go from game to game.
	bool CanMoveFocus(UI::FocusMove dir) const override {
		if (!HasInfoButton()) {
			return true;
		}
		// We handle these moves ourselves, so MoveFocus won't play the sound for them.
		if (dir == UI::FocusMove::RIGHT && !infoSelected_) {
			infoSelected_ = true;
			UI::PlayUISound(UI::UISound::SELECT);
			return false;
		}
		if (dir == UI::FocusMove::LEFT && infoSelected_) {
			infoSelected_ = false;
			UI::PlayUISound(UI::UISound::SELECT);
			return false;
		}
		return true;
	}

	void DeviceLost() override {
		if (video_) {
			video_->DeviceLost();
		}
	}

	UI::Event OnHoldClick;
	UI::Event OnHighlight;

private:
	void TriggerOnHoldClick() {
		holdStart_ = 0.0;
		UI::EventParams e{};
		e.v = this;
		e.s = gamePath_.ToString();
		down_ = false;
		OnHoldClick.Trigger(e);
	}
	void TriggerOnHighlight(UI::FocusFlags focusFlags) {
		UI::EventParams e{};
		e.v = this;
		e.s = gamePath_.ToString();
		e.a = (u32)focusFlags;
		OnHighlight.Trigger(e);
	}

	void UpdateVideo(const std::shared_ptr<GameInfo> &ginfo, bool wantVideo);

	// The large list has room for an info button on each line, doing the same as a long press.
	bool HasInfoButton() const {
		return viewMode_ == GameBrowserViewMode::LARGE_LIST && holdEnabled_;
	}
	Bounds InfoButtonBounds() const {
		return Bounds(bounds_.x2() - LARGE_LIST_INFO_SIZE - LARGE_LIST_PAD, bounds_.centerY() - LARGE_LIST_INFO_SIZE * 0.5f, LARGE_LIST_INFO_SIZE, LARGE_LIST_INFO_SIZE);
	}
	// Returns true if the info button took the input.
	bool TouchInfoButton(const TouchInput &input) {
		if ((input.flags & TouchInputFlags::MOUSE) && (input.buttons & 1) == 0) {
			return false;
		}
		const bool inside = InfoButtonBounds().Contains(input.x, input.y);
		if (input.flags & TouchInputFlags::DOWN) {
			infoButtonDown_ = inside;
		}
		if (!infoButtonDown_) {
			return false;
		}
		if (input.flags & TouchInputFlags::UP) {
			infoButtonDown_ = false;
			if (inside && !(input.flags & TouchInputFlags::CANCEL)) {
				TriggerOnHoldClick();
			}
		}
		return true;
	}

	const GameBrowserViewMode viewMode_;
	const Path gamePath_;
	std::string title_;

	// The ICON1.PMF video, played in place of the icon in the large list, like the XMB does.
	std::unique_ptr<PMFVideoTexture> video_;
	Instant videoStart_;

	double holdStart_ = 0.0;
	bool holdEnabled_ = true;
	bool showInfoPressed_ = false;
	bool infoKeyPressed_ = false;
	bool hovering_ = false;
	bool infoButtonDown_ = false;
	// The info button has the keyboard focus, rather than the game. Changed from CanMoveFocus, hence mutable.
	mutable bool infoSelected_ = false;
};

// Also used for the folder buttons.
void DrawIconWithTextGrid(UIContext &dc, ImageID image, std::string_view text, const Bounds &bounds, const UI::Style &style);
void DrawIconWithTextList(UIContext &dc, ImageID image, std::string_view text, const Bounds &bounds, GameBrowserViewMode viewMode, const UI::Style &style, std::string_view rightText = {});
