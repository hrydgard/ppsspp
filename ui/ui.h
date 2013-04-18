// Simple immediate mode UI implementation.
//
// Heavily inspired by Sol's tutorial at http://sol.gfxile.net/imgui/.
//
// A common pattern is Adapter classes for changing how things are drawn
// in lists, for example.
//
// Immediate UI works great for overlay UI for games, for example, but is actually
// not really a good idea for full app UIs. Also, animations are difficult because
// there's not really any good place to store state.
//
// hrydgard@gmail.com

#pragma once

// Simple ID generators. Absolutely no guarantee of collision avoidance if you implement
// multiple parts of a single screen of UI over multiple files unless you use IMGUI_SRC_ID.
#ifdef IMGUI_SRC_ID
#define GEN_ID (int)((IMGUI_SRC_ID) + (__LINE__))
#define GEN_ID_LOOP(i) (int)((IMGUI_SRC_ID) + (__LINE__) + (i) * 13612)
#else
#define GEN_ID (__LINE__)
#define GEN_ID_LOOP(i) ((__LINE__) + ((int)i) * 13612)
#endif

#include "gfx_es2/draw_buffer.h"

#include <string>
#include <vector>

class Texture;
class UIContext;

class LayoutManager {
public:
	virtual void GetPos(float *w, float *h, float *x, float *y) const = 0;
};

class Pos : public LayoutManager {
public:
	Pos(float x, float y) : x_(x), y_(y) {}
	virtual void GetPos(float *w, float *h, float *x, float *y) const {
		*x = x_;
		*y = y_;
	}
private:
	float x_;
	float y_;
};

class HLinear : public LayoutManager {
public:
	HLinear(float x, float y, float spacing = 2.0f) : x_(x), y_(y), spacing_(spacing) {}
	virtual void GetPos(float *w, float *h, float *x, float *y) const {
		*x = x_;
		*y = y_;
		x_ += *w + spacing_;
	}
	void Space(float x) {
		x_ += x;
	}

private:
	mutable float x_;
	float y_;
	float spacing_;
};

class VLinear : public LayoutManager {
public:
	VLinear(float x, float y, float spacing = 2.0f) : x_(x), y_(y), spacing_(spacing) {}
	virtual void GetPos(float *w, float *h, float *x, float *y) const {
		*x = x_;
		*y = y_;
		y_ += *h + spacing_;
	}

private:
	float x_;
	mutable float y_;
	float spacing_;
};

class HGrid : public LayoutManager {
public:
	HGrid(float x, float y, float xMax, float xSpacing = 2.0f, float ySpacing = 2.0f)
		: x_(x), y_(y), xInit_(x), xMax_(xMax), xSpacing_(xSpacing), ySpacing_(ySpacing) {}
	virtual void GetPos(float *w, float *h, float *x, float *y) const {
		*x = x_;
		*y = y_;
		x_ += *w + xSpacing_;
		if (x_ >= xMax_) {
			x_ = xInit_;
			y_ += *h + ySpacing_;
		}
	}

private:
	mutable float x_;
	mutable float y_;
	float xInit_;
	float xMax_;
	float xSpacing_;
	float ySpacing_;
};

class VGrid : public LayoutManager {
public:
	VGrid(float x, float y, float yMax, float xSpacing = 2.0f, float ySpacing = 2.0f)
		: x_(x), y_(y), yInit_(y), yMax_(yMax), xSpacing_(xSpacing), ySpacing_(ySpacing) {}
	virtual void GetPos(float *w, float *h, float *x, float *y) const {
		*x = x_;
		*y = y_;
		y_ += *h + ySpacing_;
		if (y_ + *h >= yMax_) {
			x_ += *w + xSpacing_;
			y_ = yInit_;
		}
	}

private:
	mutable float x_;
	mutable float y_;
	float yInit_;
	float yMax_;
	float xSpacing_;
	float ySpacing_;
};


#ifndef MAX_POINTERS
#define MAX_POINTERS 8
#endif

// "Mouse" out of habit, applies just as well to touch events.
// TODO: Change to "pointer"
// This struct is zeroed on init, so should be valid at that state.
// Never inherit from this.
struct UIState {
	int mousex[MAX_POINTERS];
	int mousey[MAX_POINTERS];
	bool mousedown[MAX_POINTERS];
	bool mousepressed[MAX_POINTERS];
	short mouseframesdown[MAX_POINTERS];

	int mouseStartX[MAX_POINTERS];
	int mouseStartY[MAX_POINTERS];

	int hotitem[MAX_POINTERS];
	int activeitem[MAX_POINTERS];

	// keyboard focus, not currently used
	int kbdwidget;
	int lastwidget;

	int ui_tick;

	// deprecated: tempfloat
	float tempfloat;
};

// This needs to be extern so that additional UI controls can be developed outside this file.
extern UIState uistate;

struct Atlas;

// This is the drawbuffer used for UI. Remember to flush it at the end of the frame.
// TODO: One should probably pass it in through UIInit.
extern DrawBuffer ui_draw2d;
extern DrawBuffer ui_draw2d_front;	// for things that need to be on top of the rest

struct UITheme {
	int uiFont;
	int uiFontSmall;
	int uiFontSmaller;
	int buttonImage;
	int buttonSelected;
	int checkOn;
	int checkOff;
};

// The atlas needs to stick around, the theme is copied.
void UIInit(const Atlas *atlas, const UITheme &theme);

// Between these, UI components won't see pointer events.
void UIDisableBegin();
void UIDisableEnd();

// Just lets you retrieve the theme that was passed into UIInit, for your own controls for example.
UITheme &UIGetTheme();

// TODO: These don't really belong here.
const int UI_SPACE = 32;
const int SMALL_BUTTON_WIDTH = 128;
const int LARGE_BUTTON_WIDTH = 192;
const int BUTTON_HEIGHT = 72;

struct SlideItem {
	const char *text;
	int image;
	uint32_t bgColor;
};

struct UISlideState {
	float scroll;
};

// Implement this interface to style your lists
class UIListAdapter {
public:
	virtual size_t getCount() const = 0;
	virtual void drawItem(int item, int x, int y, int w, int h, bool active) const = 0;
	virtual float itemHeight(int itemIndex) const { return 64; }
	virtual bool itemEnabled(int itemIndex) const { return true; }
};

class StringVectorListAdapter : public UIListAdapter {
public:
	StringVectorListAdapter(const std::vector<std::string> *items) : items_(items) {}
	virtual size_t getCount() const { return items_->size(); }
	virtual void drawItem(int item, int x, int y, int w, int h, bool active) const;

private:
	const std::vector<std::string> *items_;
};


// Utility functions, useful when implementing your own controls
bool UIRegionHit(int pointerId, int x, int y, int w, int h, int margin);

// Call at start of frame
void UIBegin(const GLSLProgram *shader);

// Call at end of frame.

void UIEnd();
void UIFlush();

void UIUpdateMouse(int i, float x, float y, bool down);

// Call when you switch screens
void UIReset();

// Returns 1 if clicked
int UIButton(int id, const LayoutManager &layout, float w, float h, const char *text, int button_align);
int UIImageButton(int id, const LayoutManager &layout, float w, int image_id, int button_align);	// uses current UI atlas for fetching images.
int UITextureButton(UIContext *ctx, int id, const LayoutManager &layout, float w, float h, Texture *texture, int button_align, uint32_t color, int drop_shadow=0);  // uses current UI atlas for fetching images.
// Returns 1 if clicked, puts the value in *value (where it also gets the current state).
int UICheckBox(int id, int x, int y, const char *text, int align, bool *value);

// Vertical slider. Not yet working.
// int UIVSlider(int id, int x, int y, int h, int max, int *value);

// Horizontal slider. Not yet working.
int UIHSlider(int id, int x, int y, int w, int max, int *value);

// Draws static text, that does not participate in any focusing scheme etc, it just is.
void UIText(int font, int x, int y, const char *text, uint32_t color, float scale = 1.0f, int align = ALIGN_TOPLEFT);
void UIText(int x, int y, const char *text, uint32_t color, float scale = 1.0f, int align = ALIGN_TOPLEFT);
void UIText(int font, const LayoutManager &layout, const char *text, uint32_t color, float scale = 1.0f, int align = ALIGN_TOPLEFT);

// Slide choice, like the Angry Birds level selector. Not yet working.
void UISlideChoice(int id, int y, const SlideItem *items, int numItems, UISlideState *state);


class UIList {
public:
	UIList();

	bool scrolling;
	int activePointer;
	float startScrollY;
	float scrollY;
	float lastX;
	float lastY;
	float startDragY;
	float movedDistanceX;
	float movedDistanceY;
	float inertiaY;

	int dragFinger;
	int selected;
	// List view.
	// return -1 = no selection
	int Do(int id, int x, int y, int w, int h, UIListAdapter *adapter);

	// Call this when the content has changed, to reset scroll position etc.
	void contentChanged() {
		scrollY = 0.0f;
		inertiaY = 0.0f;
	}
private:
	// TODO: Migrate to using these directly.
	void pointerDown(int pointer, float x, float y);
	void pointerUp(int pointer, float x, float y, bool inside);
	void pointerMove(int pointer, float x, float y, bool inside);

	DISALLOW_COPY_AND_ASSIGN(UIList);
};

