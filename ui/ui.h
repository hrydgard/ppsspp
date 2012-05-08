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
#define GEN_ID_LOOP(i) ((IMGUI_SRC_ID) + (__LINE__) + (i) * 13612)
#else
#define GEN_ID (__LINE__)
#define GEN_ID_LOOP(i) ((__LINE__) + (i) * 13612)
#endif

#include "gfx_es2/draw_buffer.h"

#include <string>
#include <vector>


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

#ifndef MAX_POINTERS
#define MAX_POINTERS 8
#endif

// Mouse out of habit, applies just as well to touch events.
// UI does not yet support multitouch.
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
extern DrawBuffer ui_draw2d_front;  // for things that need to be on top of the rest

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
bool UIRegionHit(int pointerId, int x, int y, int w, int h, int margin);

// Call at start of frame
void UIBegin();

void UIUpdateMouse(int i, float x, float y, bool down);

// Returns 1 if clicked
int UIButton(int id, const LayoutManager &layout, float w, const char *text, int button_align);
int UIImageButton(int id, const LayoutManager &layout, float w, int image_id, int button_align);  // uses current UI atlas for fetching images.

// Returns 1 if clicked, puts the value in *value (where it also gets the current state).
int UICheckBox(int id, int x, int y, const char *text, int align, bool *value);

// Vertical slider. Not yet working.
// int UIVSlider(int id, int x, int y, int h, int max, int *value);

// Horizontal slider. Not yet working.
int UIHSlider(int id, int x, int y, int w, int max, int *value);

// Draws static text, that does not participate in any focusing scheme etc, it just is.
void UIText(int font, int x, int y, const char *text, uint32_t color, float scale = 1.0f, int align = ALIGN_TOPLEFT);
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
// ui_draw2d_front.Flush(ShaderManager::Program());

void UIEnd();
