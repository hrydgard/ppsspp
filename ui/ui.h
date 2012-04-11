// Simple immediate mode UI implementation.
//
// Heavily inspired by Sol's tutorial at http://sol.gfxile.net/imgui/.
//
// A common pattern is Adapter classes for changing how things are drawn
// in lists, for example.
//
// hrydgard@gmail.com

#pragma once

// Simple ID generators. Absolutely no guarantee of collision avoidance if you implement
// multiple parts of a single screen of UI over multiple files unless you use IMGUI_SRC_ID.
#ifdef IMGUI_SRC_ID
#define GEN_ID ((IMGUI_SRC_ID) + (__LINE__))
#else
#define GEN_ID (__LINE__)
#endif

#include "gfx_es2/draw_buffer.h"

#include <string>
#include <vector>

// Mouse out of habit, applies just as well to touch events.
// UI does not yet support multitouch.
struct UIState {
  int mousex;
  int mousey;
  int mousedown;
	int mousepressed;
	int mouseStartX;
	int mouseStartY;

	int lastx;
	int lasty;

	int deltax;
	int deltay;

  int hotitem;
  int activeitem;

	// keyboard focus, not currently used
	int kbdwidget;
	int lastwidget;

	// Used by controls that need to keep track of the initial value for drags, for example.
	// Should probably be indexed by finger - would be neat to be able to move two knobs at the same time.
	float tempfloat;
};

// This needs to be extern so that additional UI controls can be developed outside this file.
extern UIState uistate;

struct Atlas;

// This is the drawbuffer used for UI. Remember to flush it at the end of the frame.
// TODO: One should probably pass it in through UIInit.
extern DrawBuffer ui_draw2d;

void UIInit(const Atlas *atlas, int uiFont, int buttonImage, int checkOn, int checkOff);

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
	StringVectorListAdapter(std::vector<std::string> *items) : items_(items) {}
	virtual size_t getCount() const { return items_->size(); }
	virtual void drawItem(int item, int x, int y, int w, int h, bool active) const;

private:
	std::vector<std::string> *items_;
};

struct UIListState {
	UIListState() : scrollY(0.0f), selected(-1) {}
	float scrollY;
	int selected;
};


// Utility functions, useful when implementing your own controls
bool UIRegionHit(int x, int y, int w, int h, int margin);

// Call at start of frame
void UIBegin();

void UIUpdateMouse(float x, float y, int buttons);

// Returns 1 if clicked
int UIButton(int id, int x, int y, int w, const char *text, int button_align);

// Returns 1 if clicked, puts the value in *value (where it also gets the current state).
int UICheckBox(int id, int x, int y, const char *text, int align, bool *value);

// Just like a button, but with an image, doh.
int UIImageButton(int id, int x, int y, int w, int image_id, int button_align);  // uses current UI atlas for fetching images.

// Vertical slider. Not yet working.
int UIVSlider(int id, int x, int y, int h, int max, int *value);

// Horizontal slider. Not yet working.
int UIHSlider(int id, int x, int y, int w, int max, int *value);

// Draws static text, that does not participate in any focusing scheme etc, it just is.
void UIText(int x, int y, const char *text, uint32_t color, float scale = 1.0f, int align = ALIGN_TOPLEFT);

// Slide choice, like the Angry Birds level selector. Not yet working.
void UISlideChoice(int id, int y, const SlideItem *items, int numItems, UISlideState *state);

// List view.
// return -1 = no selection
int UIList(int id, int x, int y, int w, int h, UIListAdapter *adapter, UIListState *state);

// Call at end of frame.
// Do this afterwards (or similar):
// ShaderManager::SetUIProgram();
// ui_draw2d.Flush(ShaderManager::Program());
void UIEnd();
