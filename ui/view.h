#pragma once

// More traditional UI framework than ui/ui.h. 

// Still very simple to use.

// Works very similarly to Android, there's a Measure pass and a Layout pass which you don't
// really need to care about if you just use the standard containers and widgets.

#include <vector>
#include <functional>

#include "base/mutex.h"
#include "base/basictypes.h"
#include "base/scoped_ptr.h"
#include "math/lin/matrix4x4.h"

struct TouchInput;

class DrawBuffer;
class DrawContext;

// I don't generally like namespaces but I think we do need one for UI, so many potentially-clashing names.
namespace UI {

enum DrawableType {
	DRAW_NOTHING,
	DRAW_SOLID_COLOR,
	DRAW_4GRID,
};

struct Drawable {
	Drawable() : type(DRAW_NOTHING) {}

	DrawableType type;
	uint32_t data;
};

struct Style {
	Style() : fgColor(0xFFFFFFFF), bgColor(0xFF303030) {}

	uint32_t fgColor;
	uint32_t bgColor;
};

// To use with an UI atlas.
struct Theme {
	int uiFont;
	int uiFontSmall;
	int uiFontSmaller;
	int buttonImage;
	int buttonSelected;
	int checkOn;
	int checkOff;
	Style buttonStyle;
};

enum {
	WRAP_CONTENT = -1,
	FILL_PARENT = -2,
};

// Gravity
enum Gravity {
	G_LEFT = 0,
	G_RIGHT = 1,
	G_HCENTER = 2,

	G_HORIZMASK = 3,

	G_TOP = 0,
	G_BOTTOM = 4,
	G_VCENTER = 8,

	G_TOPLEFT = G_TOP | G_LEFT,
	G_TOPRIGHT = G_TOP | G_RIGHT,

	G_BOTTOMLEFT = G_BOTTOM | G_LEFT,
	G_BOTTOMRIGHT = G_BOTTOM | G_RIGHT,

	G_VERTMASK = 3 << 2,
};

typedef float Size;  // can also be WRAP_CONTENT or FILL_PARENT.

enum Orientation {
	ORIENT_HORIZONTAL,
	ORIENT_VERTICAL,
};

enum MeasureSpecType {
	UNSPECIFIED,
	EXACTLY,
	AT_MOST,
};

enum EventReturn {
	EVENT_DONE,
	EVENT_SKIPPED,
};

class ViewGroup;

// Resolved bounds on screen after layout.
struct Bounds {
	bool Contains(float px, float py) const {
		return (px >= x && py >= y && px < x + w && py < y + h);
	}

	float x2() const { return x + w; }
	float y2() const { return y + h; }
	float centerX() const { return x + w * 0.5f; }
	float centerY() const { return y + h * 0.5f; }

	float x;
	float y;
	float w;
	float h;
};


void Fill(DrawContext &dc, const Bounds &bounds, const Drawable &drawable);


struct MeasureSpec {
	MeasureSpec(MeasureSpecType t, float s = 0.0f) : type(t), size(s) {}
	MeasureSpec() : type(UNSPECIFIED), size(0) {}

	MeasureSpec operator -(float amount) {
		// TODO: Check type
		return MeasureSpec(type, size - amount);
	}
	MeasureSpecType type;
	float size;
};

class View;

struct DrawContext {
	DrawBuffer *draw;
	DrawBuffer *drawTop;
	const Theme *theme;
};

// Should cover all bases.
struct EventParams {
	View *v;
	uint32_t a, b, x, y;
	const char *c;
};

struct HandlerRegistration {
	std::function<EventReturn(const EventParams&)> func;
};

class Event {
public:
	Event() : triggered_(false) {}

	void Add(std::function<EventReturn(const EventParams&)> func);

	// Call this from input thread or whatever, it doesn't matter
	void Trigger(const EventParams &e);
	// Call this from UI thread
	void Update();

private:
	recursive_mutex mutex_;
	std::vector<HandlerRegistration> handlers_;
	bool triggered_;
	EventParams eventParams_;

	DISALLOW_COPY_AND_ASSIGN(Event);
};

struct Margins {
	Margins() : top(0), bottom(0), left(0), right(0) {}
	explicit Margins(uint8_t all) : top(all), bottom(all), left(all), right(all) {}
	uint8_t top;
	uint8_t bottom;
	uint8_t left;
	uint8_t right;
};

// Need a virtual destructor so vtables are created, otherwise RTTI can't work
class LayoutParams {
public:
	LayoutParams()
		: width(WRAP_CONTENT), height(WRAP_CONTENT) {}
	LayoutParams(Size w, Size h)
		: width(w), height(h) {}
	virtual ~LayoutParams() {}

	Size width;
	Size height;
private:
};

class LinearLayoutParams : public LayoutParams {
public:
	LinearLayoutParams()
		: LayoutParams(), weight(0.0f), gravity(G_TOPLEFT), hasMargins_(false) {}
	LinearLayoutParams(Size w, Size h, float wgt = 0.0f, Gravity grav = G_TOPLEFT)
		: LayoutParams(w, h), weight(wgt), gravity(grav), hasMargins_(false) {}
	LinearLayoutParams(Size w, Size h, float wgt, Gravity grav, const Margins &mgn)
		: LayoutParams(w, h), weight(wgt), gravity(grav), margins(mgn), hasMargins_(true) {}
	LinearLayoutParams(const Margins &mgn)
		: LayoutParams(WRAP_CONTENT, WRAP_CONTENT), weight(0.0f), gravity(G_TOPLEFT), margins(mgn), hasMargins_(true) {}

	float weight;
	Gravity gravity;
	Margins margins;

	bool HasMargins() const { return hasMargins_; }

private:
	bool hasMargins_;
};

View *GetFocusedView();

class View {
public:
	View(LayoutParams *layoutParams = 0) : layoutParams_(layoutParams) {
		if (!layoutParams)
			layoutParams_.reset(new LayoutParams());
	}

	virtual ~View() {}

	// Please note that Touch is called ENTIRELY asynchronously from drawing!
	// Can even be called on a different thread! This is to really minimize latency, and decouple
	// touch response from the frame rate.
	virtual void Touch(const TouchInput &input) = 0;

	void Move(Bounds bounds) {
		bounds_ = bounds;
	}
	
	// Views don't do anything here in Layout, only containers implement this.
	virtual void Measure(const DrawContext &dc, MeasureSpec horiz, MeasureSpec vert);
	virtual void Layout() {}
	virtual void Draw(DrawContext &dc) {}

	virtual float GetMeasuredWidth() const { return measuredWidth_; }
	virtual float GetMeasuredHeight() const { return measuredHeight_; }

	// Override this for easy standard behaviour. No need to override Measure.
	virtual void GetContentDimensions(const DrawContext &dc, float &w, float &h) const;

	// Called when the layout is done.
	void SetBounds(Bounds bounds) { bounds_ = bounds; }
	virtual const LayoutParams *GetLayoutParams() const { return layoutParams_.get(); }
	const Bounds &GetBounds() const { return bounds_; }

	virtual bool CanBeFocused() const { return true; }

	bool Focused() const {
		return GetFocusedView() == this;
	}

protected:
	// Inputs to layout
	scoped_ptr<LayoutParams> layoutParams_;
	
	// Results of measure pass. Set these in Measure.
	float measuredWidth_;
	float measuredHeight_;

	// Outputs of layout. X/Y are absolute screen coordinates, hierarchy is "gone" here.
	Bounds bounds_;

	scoped_ptr<Matrix4x4> transform_;

private:
	DISALLOW_COPY_AND_ASSIGN(View);
};

// These don't do anything when touched.
class InertView : public View {
public:
	InertView(LayoutParams *layoutParams)
		: View(layoutParams) {}

	virtual void Touch(const TouchInput &input) {}
};


// All these light up their background when touched, or have focus.
class Clickable : public View {
public:
	Clickable(LayoutParams *layoutParams)
		: View(layoutParams) {}

	virtual void Touch(const TouchInput &input);

	Event OnClick;

protected:
	// Internal method that fires on a click. Default behaviour is to trigger
	// the event.
	// Use it for checking/unchecking checkboxes, etc.
	virtual void Click();

	bool down_;
};

class Button : public Clickable {
public:
	Button(const std::string &text, LayoutParams *layoutParams = 0)
		: Clickable(layoutParams), text_(text) {}
	
	virtual void Draw(DrawContext &dc);
	virtual void GetContentDimensions(const DrawContext &dc, float &w, float &h) const;

private:
	Style style_;
	std::string text_;
	DISALLOW_COPY_AND_ASSIGN(Button);
};

// The following classes are mostly suitable as items in ListView which
// really is just a LinearLayout in a ScrollView, possibly with some special optimizations.

// Use to trigger something or open a submenu screen.
class Choice : public Clickable {
public:
	Choice(const std::string &text, const std::string &smallText = "", LayoutParams *layoutParams = 0)
		: Clickable(layoutParams), text_(text), smallText_(smallText) {}

	virtual void Draw(DrawContext &dc);
	virtual void GetContentDimensions(const DrawContext &dc, float &w, float &h) const;

private:
	std::string text_;
	std::string smallText_;
};

class CheckBox : public Clickable {
public:
	CheckBox(const std::string &text, const std::string &smallText = "", LayoutParams *layoutParams = 0)
		: Clickable(layoutParams), text_(text), smallText_(smallText) {}

	virtual void GetContentDimensions(const DrawContext &dc, float &w, float &h) const;
	virtual void Draw(DrawContext &dc);

private:
	std::string text_;
	std::string smallText_;
};

enum ImageSizeMode {

};

class TextView : public InertView {
public:
	TextView(int font, const std::string &text, LayoutParams *layoutParams = 0)
		: InertView(layoutParams), font_(font), text_(text) {}

	virtual void GetContentDimensions(const DrawContext &dc, float &w, float &h) const;
	virtual void Draw(DrawContext &dc);
	virtual bool CanBeFocused() const { return false; }

private:
	std::string text_;
	int font_;
};

class ImageView : public InertView {
public:
	ImageView(int atlasImage, ImageSizeMode sizeMode, LayoutParams *layoutParams = 0)
		: InertView(layoutParams), atlasImage_(atlasImage), sizeMode_(sizeMode) {}

	virtual void GetContentDimensions(const DrawContext &dc, float &w, float &h) const;
	virtual void Draw(DrawContext &dc);
	virtual bool CanBeFocused() const { return false; }

private:
	int atlasImage_;
	ImageSizeMode sizeMode_;
};

// This tab strip is a little special.
/*
class TabStrip : public View {
public:
	TabStrip();

	virtual void Touch(const TouchInput &input);
	virtual void Draw(DrawContext &dc);

	void AddTab(const std::string &title, uint32_t color) {
		Tab tab;
		tab.title = title;
		tab.color = color;
		tabs_.push_back(tab);
	}

private:
	int selected_;
	struct Tab {
		std::string title;
		uint32_t color;
	};
	std::vector<Tab> tabs_;
};*/

// The ONLY global is the currently focused item.
// Can be and often is null.

View *GetFocusedView();
void SetFocusedView(View *view);
void MeasureBySpec(Size sz, float contentWidth, MeasureSpec spec, float *measured);

}  // namespace