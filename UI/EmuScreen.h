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
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Common/File/Path.h"
#include "Common/Input/KeyCodes.h"
#include "Common/UI/Screen.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/Tween.h"
#include "Core/KeyMap.h"
#include "Core/ControlMapper.h"
#include "Core/CoreParameter.h"
#include "UI/ImDebugger/ImCommand.h"

struct AxisInput;
struct DisplayLayoutConfig;

class AsyncImageFileView;
class ImposeHomeScreen;
class ChatMenu;
class ImDebugger;
struct ImCommand;
struct ImGuiContext;

class EmuScreen : public UIScreen, protected ControlListener {
public:
	// mountIso, if set, is mounted on umd1: when filename is an ELF/PBP (like the VSH).
	// umdAutoboot overrides the VSH's UMD auto-start setting.
	EmuScreen(const Path &filename, const Path &mountIso = Path(), std::optional<bool> umdAutoboot = std::nullopt);
	~EmuScreen();

	const char *tag() const override { return "Emu"; }

	void update() override;
	ScreenRenderFlags render(ScreenRenderMode mode) override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;
	void sendMessage(UIMessage message, const char *value) override;
	void resized() override;
	ScreenRenderRole renderRole(bool isTop) const override;

	InputMode PassInputToMapper() const override;

	// We also need to do some special handling of queued UI events to handle closing the chat window.
	bool key(const KeyInput &key) override;
	bool touch(const TouchInput &key) override;

	void deviceLost() override;
	void deviceRestored(Draw::DrawContext *draw) override;

protected:
	void darken();
	void drawGameOnOutputMessage();
	void focusChanged(ScreenFocusChange focusChange) override;
	ScreenRenderFlags PreRender(ScreenRenderMode mode) override;
	bool renderOutput(const OutputDisplay &output) override;

	// ControlListener implementations
	void OnVKey(VirtKey virtualKeyCode, bool down) override;
	void OnVKeyAnalog(VirtKey virtualKeyCode, float value) override;
	void UpdatePSPButtons(uint32_t buttonMask, uint32_t changedMask) override;
	void SetPSPAnalog(int rotation, int stick, float x, float y) override;
	ViewLayoutMode LayoutMode() const override;

	bool AllowFocusMovement() const override;

private:
	void CreateViews() override;
	ScreenRenderFlags RunEmulation(bool skipBufferEffects);
	void UpdateGameOutput();
	DeviceOrientation GameLayoutOrientation() const;
	const DisplayLayoutConfig &GameLayoutConfig() const;
	void OnDevTools(UI::EventParams &params);
	void OpenChat(bool focus);

	void HandleFlip();
	void ProcessGameBoot(const Path &filename);
	bool bootAllowStorage(const Path &filename);
	void bootComplete();
	bool hasVisibleUI();
	void renderUI();

	void AutoLoadSaveState();
	bool checkPowerDown();
	void leaveGame();

	void ProcessQueuedVKeys();
	void ProcessVKey(VirtKey vkey, bool down);

	bool ShouldRunEmulation(ScreenRenderMode mode) const;

	UI::Event OnDevMenu;
	bool bootPending_ = true;
	bool bootIsReset_ = false;
	Path gamePath_;
	Path mountIso_;
	std::optional<bool> umdAutoboot_;
	// The rest of a BootRequest from the emulated system (see PSP_RequestBoot).
	std::optional<VSHLaunch> launchedFromVSH_;
	std::vector<u8> vshmainArgs_;

	bool quit_ = false;
	std::string errorMessage_;

	// If set, pauses at the end of the frame.
	// We also poll the pause trigger from the ControlMapper. That needs refactoring.
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
	// Shown while a savestate is being saved or loaded, if that takes long enough to notice.
	UI::Spinner *saveStateSpinner_ = nullptr;
	double saveStateBusySince_ = -1.0;
	UI::Button *resumeButton_ = nullptr;
	UI::Button *resetButton_ = nullptr;
	UI::Button *backButton_ = nullptr;
	UI::Clickable *chatButton_ = nullptr;
	ChatMenu *chatMenu_ = nullptr;
	// Set when the chat opens. Stick movements don't reach the control mapper while it's open (key
	// releases do), so what's held is let go up front.
	bool releaseMapperInput_ = false;

	UI::Button *cardboardDisableButton_ = nullptr;

	std::string extraAssertInfoStr_;

	std::mutex queuedVirtKeysLock_;
	std::vector<std::pair<VirtKey, bool>> queuedVirtKeys_;

	bool frameStep_ = false;
#ifndef MOBILE_DEVICE
	bool startDumping_ = false;
#endif
	bool autoLoadFailed_ = false;  // to prevent repeat reloads
	bool readyToFinishBoot_ = false;
	bool skipBufferEffects_ = false;  // cached state, fetched once per frame.
	// The game goes to a secondary output (OutputRole::Game) instead of the backbuffer. Per frame too.
	bool gameOnOutput_ = false;
	DeviceOrientation gameOutputOrientation_ = DeviceOrientation::Landscape;

	uint32_t clearColor_ = 0;

	// The HOME button's quit screen (as impose draws it on a PSP).
	std::unique_ptr<ImposeHomeScreen> imposeHome_;
};

bool MustRunBehind();
bool ShouldRunBehind();
// The orientation of the output the game goes to (like a TV), if there's one. Its display layout is the one for that.
bool GetGameOutputOrientation(ScreenManager *screenManager, DeviceOrientation *orientation);
