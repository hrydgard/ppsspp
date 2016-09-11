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

#include <vector>

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/NativeApp.h"

namespace UI {
	class View;
}

struct InputState;

enum DialogResult {
	DR_OK,
	DR_CANCEL,
	DR_YES,
	DR_NO,
	DR_BACK,
};

class ScreenManager;
class UIContext;
class Thin3DContext;

class Screen {
public:
	Screen() : screenManager_(0) { }
	virtual ~Screen() {
		screenManager_ = 0;
	}

	virtual void onFinish(DialogResult reason) {}
	virtual void update(InputState &input) {}
	virtual void preRender() {}
	virtual void render() {}
	virtual void postRender() {}
	virtual void deviceLost() {}
	virtual void deviceRestore() {}
	virtual void resized() {}
	virtual void dialogFinished(const Screen *dialog, DialogResult result) {}
	virtual bool touch(const TouchInput &touch) { return false;  }
	virtual bool key(const KeyInput &key) { return false; }
	virtual bool axis(const AxisInput &touch) { return false; }
	virtual void sendMessage(const char *msg, const char *value) {}

	virtual void RecreateViews() {}

	ScreenManager *screenManager() { return screenManager_; }
	void setScreenManager(ScreenManager *sm) { screenManager_ = sm; }

	// This one is icky to use because you can't know what's in it until you know
	// what screen it is.
	virtual void *dialogData() { return 0; }

	virtual std::string tag() const { return std::string(""); }

	virtual bool isTransparent() const { return false; }
	virtual bool isTopLevel() const { return false; }

private:
	ScreenManager *screenManager_;
	DISALLOW_COPY_AND_ASSIGN(Screen);
};

class Transition {
public:
	Transition() {}
};

enum {
	LAYER_SIDEMENU = 1,
	LAYER_TRANSPARENT = 2,
};

class ScreenManager {
public:
	ScreenManager();
	virtual ~ScreenManager();

	void switchScreen(Screen *screen);
	void update(InputState &input);

	void setUIContext(UIContext *context) { uiContext_ = context; }
	UIContext *getUIContext() { return uiContext_; }

	void setThin3DContext(Thin3DContext *context) { thin3DContext_ = context; }
	Thin3DContext *getThin3DContext() { return thin3DContext_; }

	void render();
	void resized();
	void deviceLost();
	void deviceRestore();
	void shutdown();

	// Push a dialog box in front. Currently 1-level only.
	void push(Screen *screen, int layerFlags = 0);

	// Recreate all views
	void RecreateAllViews();

	// Pops the dialog away.
	void finishDialog(Screen *dialog, DialogResult result = DR_OK);

	// Instant touch, separate from the update() mechanism.
	bool touch(const TouchInput &touch);
	bool key(const KeyInput &key);
	bool axis(const AxisInput &touch);

	// Generic facility for gross hacks :P
	void sendMessage(const char *msg, const char *value);

	Screen *topScreen() const;

private:
	void pop();
	void switchToNext();
	void processFinishDialog();

	Screen *nextScreen_;
	UIContext *uiContext_;
	Thin3DContext *thin3DContext_;

	const Screen *dialogFinished_;
	DialogResult dialogResult_;

	struct Layer {
		Screen *screen;
		int flags;  // From LAYER_ enum above
		UI::View *focusedView;  // TODO: save focus here. Going for quick solution now to reset focus.
	};

	// Dialog stack. These are shown "on top" of base screens and the Android back button works as expected.
	// Used for options, in-game menus and other things you expect to be able to back out from onto something.
	std::vector<Layer> stack_;
};
