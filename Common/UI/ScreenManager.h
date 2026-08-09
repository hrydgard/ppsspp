#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <deque>
#include <unordered_map>
#include <vector>

#include "Common/Common.h"
#include "Common/Input/InputState.h"
#include "Common/System/System.h"
#include "Common/UI/Screen.h"

class Screen;
class UIContext;


/*
class Transition {
public:
	Transition() = default;
};
*/

enum {
	LAYER_TRANSPARENT = 2,
};

typedef void (*PostRenderCallback)(UIContext *ui, void *userdata);

class ScreenManager {
public:
	virtual ~ScreenManager();

	void switchScreen(Screen *screen);
	void ProcessScreenSwitches();
	void ProcessInputEvent(const QueuedEvent &event);
	void Update();

	void setUIContext(UIContext *context) { uiContext_ = context; }
	UIContext *getUIContext() { return uiContext_; }
	Draw::DrawContext *getDrawContext() { return draw_; }

	void setPostRenderCallback(PostRenderCallback cb, void *userdata) {
		postRenderCb_ = cb;
		postRenderUserdata_ = userdata;
	}

	ScreenRenderFlags Render(std::function<void()> afterPreRender);
	void resized();
	void shutdown();

	void deviceLost();
	void deviceRestored(Draw::DrawContext *draw);

	// Push a dialog box in front. Currently 1-level only.
	void push(Screen *screen, int layerFlags = 0);

	// Recreate all views
	void RecreateAllViews();

	// Pops the dialog away.
	void finishDialog(Screen *dialog, DialogResult result = DR_OK);
	Screen *dialogParent(const Screen *dialog) const;

	void sendMessage(UIMessage message, const char *value);

	const Screen *topScreen() const {
		return stack_.empty() ? nullptr : stack_.back().screen;
	}
	Screen *topScreen() {
		return stack_.empty() ? nullptr : stack_.back().screen;
	}

	void cancelScreensAbove(Screen *screen);

	void getFocusPosition(float &x, float &y, float &z);

	// Will delete any existing overlay screen.
	void SetBackgroundOverlayScreens(Screen *backgroundScreen, Screen *overlayScreen);

	InputMode PassInputToMapper() const {
		return passInputToMapper_;
	}

private:
	void pop();
	void switchToNext();
	void processFinishDialog();

	UIContext *uiContext_ = nullptr;
	Draw::DrawContext *draw_ = nullptr;

	PostRenderCallback postRenderCb_ = nullptr;
	void *postRenderUserdata_ = nullptr;

	const Screen *dialogFinished_ = nullptr;
	DialogResult dialogResult_{};

	Screen *backgroundScreen_ = nullptr;
	Screen *overlayScreen_ = nullptr;

	Screen *cancelScreensAbove_ = nullptr;

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

	InputMode passInputToMapper_ = InputMode::None;
};
