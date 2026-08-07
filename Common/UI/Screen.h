// Super basic screen manager. Let's you, well, switch between screens. Can also be used
// to pop one screen in front for a bit while keeping another one running, it's basically
// a native "activity stack". Well actually that part is still a TODO.
//
// Semantics
//
// switchScreen: When you call this, on a newed screen, the ScreenManager takes ownership.
// On the next update, it switches to the new screen and deletes the previous screen.
//
// TODO: A way to do smooth transitions between screens. Will probably involve screenshotting
// the previous screen and then animating it on top of the current screen with transparency
// and/or other similar effects.

#pragma once

#include <cstdint>
#include <string>

#include "Common/Common.h"
#include "Common/Input/InputState.h"
#include "Common/System/System.h"

namespace UI {
	class View;
}

namespace Draw {
class DrawContext;
}

enum DialogResult {
	DR_NONE,
	DR_OK,
	DR_CANCEL,
	DR_YES,
	DR_NO,
	DR_BACK,
};
const char *DialogResultToString(DialogResult result);

class ScreenManager;
class UIContext;

enum class ScreenFocusChange {
	FOCUS_LOST_TOP,  // Another screen was pushed on top
	FOCUS_BECAME_TOP,  // Became the top screen again
};

enum class ScreenRenderMode {
	DEFAULT = 0,
	FIRST = 1,
	BEHIND = 4,
	TOP = 8,
};
ENUM_CLASS_BITOPS(ScreenRenderMode);

enum class ScreenRenderRole {
	NONE = 0,
	CAN_BE_BACKGROUND = 1,
	MUST_BE_FIRST = 2,
};
ENUM_CLASS_BITOPS(ScreenRenderRole);

enum class ViewLayoutMode {
	ApplyInsets = 0,   // For gameplay, option #1
	IgnoreInsets,  // For gameplay, option #2
	IgnoreBottomInset,
};

enum class QueuedEventType : u8 {
	KEY,
	AXIS,
	TOUCH,
};

struct QueuedEvent {
	QueuedEventType type;
	union {
		TouchInput touch;
		KeyInput key;
		AxisInput axis;
	};
};

enum class ScreenRenderFlags {
	NONE = 0,
	HANDLED_THROTTLING = 1,
};
ENUM_CLASS_BITOPS(ScreenRenderFlags);

enum class InputMode {
	None = 0,
	Keyboard = 1,
	Mouse = 2,
	Other = 4,
	ImDebuggerToggle = 8,  // ugly hack. debugger and pause goes through.
};
ENUM_CLASS_BITOPS(InputMode);

class Screen {
public:
	Screen() = default;
	virtual ~Screen();

	virtual void onFinish(DialogResult reason) {}
	virtual void update() {}
	virtual ScreenRenderFlags PreRender(ScreenRenderMode mode) { return ScreenRenderFlags::NONE; }
	virtual ScreenRenderFlags render(ScreenRenderMode mode) = 0;
	virtual void resized() {}
	virtual void dialogFinished(const Screen *dialog, DialogResult result) {}
	virtual void sendMessage(UIMessage message, const char *value) {}
	virtual void deviceLost() {}
	virtual void deviceRestored(Draw::DrawContext *draw) {}
	virtual ScreenRenderRole renderRole(bool isTop) const { return ScreenRenderRole::NONE; }
	virtual bool wantBrightBackground() const { return false; }  // special hack for DisplayLayoutScreen.

	virtual void focusChanged(ScreenFocusChange focusChange);

	virtual bool touch(const TouchInput &touch) = 0;
	virtual bool key(const KeyInput &key) = 0;
	virtual void axis(const AxisInput &axis) = 0;

	virtual void RecreateViews() {}

	// NOTE: This is polled every frame, not called on every input event.
	virtual InputMode PassInputToMapper() const { return InputMode::None; }

	ScreenManager *screenManager() { return screenManager_; }
	const ScreenManager *screenManager() const { return screenManager_; }
	void setScreenManager(ScreenManager *sm) { screenManager_ = sm; }

	virtual const char *tag() const = 0;

	virtual bool isTransparent() const { return false; }
	virtual bool isTopLevel() const { return false; }

	virtual TouchInput transformTouch(const TouchInput &touch) { return touch; }
	virtual bool WantsTextInput() const { return false; }

protected:
	int GetRequesterToken();
	void WipeRequesterToken() {
		token_ = -1;
	}

private:
	ScreenManager *screenManager_ = nullptr;
	int token_ = -1;

	DISALLOW_COPY_AND_ASSIGN(Screen);
};
