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
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Common/Common.h"
#include "Common/Input/InputState.h"

namespace UI {
	class View;
}

enum DialogResult {
	DR_OK,
	DR_CANCEL,
	DR_YES,
	DR_NO,
	DR_BACK,
};

class ScreenManager;
class UIContext;

namespace Draw {
	class DrawContext;
}

class Screen {
public:
	Screen() : screenManager_(nullptr) { }
	virtual ~Screen() {
		screenManager_ = nullptr;
	}

	virtual void onFinish(DialogResult reason) {}
	virtual void update() {}
	virtual void preRender() {}
	virtual void render() {}
	virtual void postRender() {}
	virtual void resized() {}
	virtual void dialogFinished(const Screen *dialog, DialogResult result) {}
	virtual void sendMessage(const char *msg, const char *value) {}
	virtual void deviceLost() {}
	virtual void deviceRestored() {}

	virtual void UnsyncTouch(const TouchInput &touch) = 0;
	virtual bool UnsyncKey(const KeyInput &touch) = 0;
	virtual void UnsyncAxis(const AxisInput &touch) = 0;

	virtual void RecreateViews() {}

	ScreenManager *screenManager() { return screenManager_; }
	void setScreenManager(ScreenManager *sm) { screenManager_ = sm; }

	// This one is icky to use because you can't know what's in it until you know
	// what screen it is.
	virtual void *dialogData() { return 0; }

	virtual const char *tag() const = 0;

	virtual bool isTransparent() const { return false; }
	virtual bool isTopLevel() const { return false; }

	virtual TouchInput transformTouch(const TouchInput &touch) { return touch; }

private:
	ScreenManager *screenManager_;
	DISALLOW_COPY_AND_ASSIGN(Screen);
};

class Transition {
public:
	Transition() {}
};

enum {
	LAYER_TRANSPARENT = 2,
};

typedef void(*PostRenderCallback)(UIContext *ui, void *userdata);

class ScreenManager {
public:
	virtual ~ScreenManager();

	void switchScreen(Screen *screen);
	void update();

	void setUIContext(UIContext *context) { uiContext_ = context; }
	UIContext *getUIContext() { return uiContext_; }

	void setDrawContext(Draw::DrawContext *context) { thin3DContext_ = context; }
	Draw::DrawContext *getDrawContext() { return thin3DContext_; }

	void setPostRenderCallback(PostRenderCallback cb, void *userdata) {
		postRenderCb_ = cb;
		postRenderUserdata_ = userdata;
	}

	void render();
	void resized();
	void shutdown();

	void deviceLost();
	void deviceRestored();

	// Push a dialog box in front. Currently 1-level only.
	void push(Screen *screen, int layerFlags = 0);

	// Recreate all views
	void RecreateAllViews();

	// Pops the dialog away.
	void finishDialog(Screen *dialog, DialogResult result = DR_OK);
	Screen *dialogParent(const Screen *dialog) const;

	// Instant touch, separate from the update() mechanism.
	void touch(const TouchInput &touch);
	bool key(const KeyInput &key);
	void axis(const AxisInput &touch);

	// Generic facility for gross hacks :P
	void sendMessage(const char *msg, const char *value);

	Screen *topScreen() const;

	void getFocusPosition(float &x, float &y, float &z);

	// Will delete any existing overlay screen.
	void SetOverlayScreen(Screen *screen);

	std::recursive_mutex inputLock_;

private:
	void pop();
	void switchToNext();
	void processFinishDialog();

	UIContext *uiContext_ = nullptr;
	Draw::DrawContext *thin3DContext_ = nullptr;

	PostRenderCallback postRenderCb_ = nullptr;
	void *postRenderUserdata_ = nullptr;

	const Screen *dialogFinished_ = nullptr;
	DialogResult dialogResult_{};

	Screen *overlayScreen_ = nullptr;

	struct Layer {
		Screen *screen;
		int flags;  // From LAYER_ enum above
		UI::View *focusedView;  // TODO: save focus here. Going for quick solution now to reset focus.
	};

	// Dialog stack. These are shown "on top" of base screens and the Android back button works as expected.
	// Used for options, in-game menus and other things you expect to be able to back out from onto something.
	std::vector<Layer> stack_;
	std::vector<Layer> nextStack_;

	std::unordered_map<int64_t, int> lastAxis_;
};
