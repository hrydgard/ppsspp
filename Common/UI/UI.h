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

#include <string>
#include <vector>

#include "Common/Render/DrawBuffer.h"
#include "Common/Input/InputState.h"

class UIContext;

struct Atlas;

// This is the drawbuffer used for UI. Remember to flush it at the end of the frame.
// TODO: One should probably pass it in through UIInit.
extern DrawBuffer ui_draw2d;

// TODO: These don't really belong here.

// Implement this interface to style your lists
class UIListAdapter {
public:
	virtual ~UIListAdapter() {}
	virtual size_t getCount() const = 0;
	virtual void drawItem(int item, int x, int y, int w, int h, bool active) const = 0;
	virtual float itemHeight(int itemIndex) const { return 64; }
	virtual bool itemEnabled(int itemIndex) const { return true; }
};

class StringVectorListAdapter : public UIListAdapter {
public:
	StringVectorListAdapter(const std::vector<std::string> *items) : items_(items) {}
	size_t getCount() const override { return items_->size(); }
	void drawItem(int item, int x, int y, int w, int h, bool active) const override;

private:
	const std::vector<std::string> *items_;
};


// Begins/flushes the two UI drawbuffers together.
void UIBegin(Draw::Pipeline *shaderSet);
void UIFlush();

