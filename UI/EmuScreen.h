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

#include <list>
#include <string>
#include <vector>

#include "Common/File/Path.h"
#include "Common/Input/KeyCodes.h"
#include "Common/UI/Screen.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/Tween.h"
#include "Core/KeyMap.h"
#include "Core/ControlMapper.h"

struct AxisInput;

class AsyncImageFileView;
class ChatMenu;

class EmuScreen : public UIScreen {
public:
	EmuScreen(const Path &filename);
	~EmuScreen();

	const char *tag() const override { return "Emu"; }

	void update() override;
	ScreenRenderFlags render(ScreenRenderMode mode) override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;
	void sendMessage(UIMessage message, const char *value) override;
	void resized() override;
	ScreenRenderRole renderRole(bool isTop) const override;

	// Note: Unlike your average boring UIScreen, here we override the Unsync* functions
	// to get minimal latency and full control. We forward to UIScreen when needed.
	bool UnsyncTouch(const TouchInput &touch) override;
	bool UnsyncKey(const KeyInput &key) override;
	void UnsyncAxis(const AxisInput *axes, size_t count) override;

	// We also need to do some special handling of queued UI events to handle closing the chat window.
	bool key(const KeyInput &key) override;

protected:
	void darken();
	void focusChanged(ScreenFocusChange focusChange) override;

private:
	void CreateViews() override;
	UI::EventReturn OnDevTools(UI::EventParams &params);
	UI::EventReturn OnDisableCardboard(UI::EventParams &params);
	UI::EventReturn OnChat(UI::EventParams &params);
	UI::EventReturn OnResume(UI::EventParams &params);

	void bootGame(const Path &filename);
	bool bootAllowStorage(const Path &filename);
	void bootComplete();
	bool hasVisibleUI();
	void renderUI();

	void onVKey(int virtualKeyCode, bool down);
	void onVKeyAnalog(int virtualKeyCode, float value);

	void autoLoad();
	bool checkPowerDown();

	UI::Event OnDevMenu;
	UI::Event OnChatMenu;
	bool bootPending_ = true;
	Path gamePath_;

	// Something invalid was loaded, don't try to emulate
	bool invalid_ = true;
	bool quit_ = false;
	bool stopRender_ = false;
	std::string errorMessage_;

	// If set, pauses at the end of the frame.
	bool pauseTrigger_ = false;

	// The last read chat message count, and how many new ones there are.
	int chatMessages_ = 0;
	int newChatMessages_ = 0;

	// In-memory save state used for freezeFrame, which is useful for debugging.
	std::vector<u8> freezeState_;

	std::string tag_;

	double saveStatePreviewShownTime_ = 0.0;
	AsyncImageFileView *saveStatePreview_ = nullptr;
	int saveStateSlot_;

	UI::CallbackColorTween *loadingViewColor_ = nullptr;
	UI::VisibilityTween *loadingViewVisible_ = nullptr;
	UI::Spinner *loadingSpinner_ = nullptr;
	UI::TextView *loadingTextView_ = nullptr;
	UI::Button *resumeButton_ = nullptr;
	UI::Button *resetButton_ = nullptr;
	UI::Button *backButton_ = nullptr;
	UI::View *chatButton_ = nullptr;
	ChatMenu *chatMenu_ = nullptr;

	UI::Button *cardboardDisableButton_ = nullptr;

	std::string extraAssertInfoStr_;

	std::atomic<bool> doFrameAdvance_{};

	ControlMapper controlMapper_;
};
