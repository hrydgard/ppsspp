#pragma once

// More traditional UI framework than ui/ui.h. 

// Still very simple to use.

// Works very similarly to Android, there's a Measure pass and a Layout pass which you don't
// really need to care about if you just use the standard containers and widgets.

#include <vector>
#include <functional>
#include <cmath>

// <functional> fix
#if defined(IOS) || defined(MACGNUSTD)
#include <tr1/functional>
namespace std {
	using tr1::bind;
}
#endif

#include "base/mutex.h"
#include "base/basictypes.h"
#include "base/scoped_ptr.h"
#include "math/lin/matrix4x4.h"
#include "math/math_util.h"
#include "math/geom2d.h"

struct TouchInput;
struct InputState;

class DrawBuffer;
class DrawContext;

// I don't generally like namespaces but I think we do need one for UI, so many potentially-clashing names.
namespace UI {

class View;

// The ONLY global is the currently focused item.
// Can be and often is null.
void EnableFocusMovement(bool enable);
bool IsFocusMovementEnabled();
View *GetFocusedView();
void SetFocusedView(View *view);

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
	int whiteImage;

	Style buttonStyle;
	Style buttonFocusedStyle;
	Style buttonDownStyle;

	Style itemDownStyle;
	Style itemFocusedStyle;
};

// The four cardinal directions should be enough, plus Prev/Next in "element order".
enum FocusDirection {
	FOCUS_UP,
	FOCUS_DOWN,
	FOCUS_LEFT,
	FOCUS_RIGHT,
	FOCUS_NEXT,
	FOCUS_PREV,
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
	explicit Margins(uint8_t horiz, uint8_t vert) : top(vert), bottom(vert), left(horiz), right(horiz) {}
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
	virtual void Update(const InputState &input_state) = 0;

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

	virtual bool SetFocus() {
		if (CanBeFocused()) {
			SetFocusedView(this);
			return true;
		}
		return false;
	}

	virtual bool CanBeFocused() const { return true; }
	virtual bool SubviewFocused(View *view) { return false; }
	bool HasFocus() const {
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
	virtual bool CanBeFocused() const { return false; }
	virtual void Update(const InputState &input_state) {}
};


// All these light up their background when touched, or have focus.
class Clickable : public View {
public:
	Clickable(LayoutParams *layoutParams)
		: View(layoutParams), downCountDown_(0), down_(false), dragging_(false) {}

	virtual void Touch(const TouchInput &input);
	virtual void Update(const InputState &input_state);

	Event OnClick;

protected:
	// Internal method that fires on a click. Default behaviour is to trigger
	// the event.
	// Use it for checking/unchecking checkboxes, etc.
	virtual void Click();

	int downCountDown_;
	bool dragging_;
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
};

// Basic button that modifies a bitfield based on the pressed status. Supports multitouch.
// Suitable for controller simulation (ABXY etc).
class TriggerButton : public View {
public:
	TriggerButton(uint32_t *bitField, uint32_t bit, int imageBackground, int imageForeground, LayoutParams *layoutParams)
		: View(layoutParams), down_(0.0), bitField_(bitField), bit_(bit), imageBackground_(imageBackground), imageForeground_(imageForeground) {}

	virtual void Touch(const TouchInput &input);
	virtual void Draw(DrawContext &dc);
	virtual void GetContentDimensions(const DrawContext &dc, float &w, float &h) const;

private:
	int down_;  // bitfield of pressed fingers, translates into bitField

	uint32_t *bitField_;
	uint32_t bit_;

	int imageBackground_;
	int imageForeground_;
};


// The following classes are mostly suitable as items in ListView which
// really is just a LinearLayout in a ScrollView, possibly with some special optimizations.

class Item : public InertView {
public:
	Item(LayoutParams *layoutParams) : InertView(layoutParams) {
		layoutParams_->width = FILL_PARENT;
		layoutParams_->height = 80;
	}

	virtual void GetContentDimensions(const DrawContext &dc, float &w, float &h) const {
		w = 0.0f;
		h = 0.0f;
	}
};

class ClickableItem : public Clickable {
public:
	ClickableItem(LayoutParams *layoutParams) : Clickable(layoutParams) {
		layoutParams_->width = FILL_PARENT;
		layoutParams_->height = 80;
	}

	virtual void GetContentDimensions(const DrawContext &dc, float &w, float &h) const {
		w = 0.0f;
		h = 0.0f;
	}

	// Draws the item background.
	virtual void Draw(DrawContext &dc);
};

// Use to trigger something or open a submenu screen.
class Choice : public ClickableItem {
public:
	Choice(const std::string &text, const std::string &smallText = "", LayoutParams *layoutParams = 0)
		: ClickableItem(layoutParams), text_(text), smallText_(smallText) {}

	virtual void Draw(DrawContext &dc);

private:
	std::string text_;
	std::string smallText_;
};

class InfoItem : public Item {
public:
	InfoItem(const std::string &text, const std::string &rightText, LayoutParams *layoutParams = 0)
		: Item(layoutParams), text_(text), rightText_(rightText) {}

	virtual void Draw(DrawContext &dc);

private:
	std::string text_;
	std::string rightText_;
};

class ItemHeader : public Item {
public:
	ItemHeader(const std::string &text, LayoutParams *layoutParams = 0)
		: Item(layoutParams), text_(text) {
		layoutParams_->width = FILL_PARENT;
		layoutParams_->height = 26;
	}
	virtual void Draw(DrawContext &dc);
private:
	std::string text_;
};

class CheckBox : public ClickableItem {
public:
	CheckBox(bool *toggle, const std::string &text, const std::string &smallText = "", LayoutParams *layoutParams = 0)
		: ClickableItem(layoutParams), text_(text), smallText_(smallText) {
		OnClick.Add(std::bind(&CheckBox::OnClicked, this, std::placeholders::_1));
	}

	virtual void Draw(DrawContext &dc);

	EventReturn OnClicked(const EventParams &e) {
		if (toggle_)
			*toggle_ = !(*toggle_);
		return EVENT_DONE;
	}

private:
	bool *toggle_;
	std::string text_;
	std::string smallText_;
};


// These are for generic use.

class Spacer : public InertView {
public:
	Spacer(LayoutParams *layoutParams = 0)
		: InertView(layoutParams) {}
	virtual void GetContentDimensions(const DrawContext &dc, float &w, float &h) {
		w = 0.0f; h = 0.0f;
	}
	virtual void Draw(DrawContext &dc) {}
};

class TextView : public InertView {
public:
	TextView(int font, const std::string &text, LayoutParams *layoutParams = 0)
		: InertView(layoutParams), font_(font), text_(text) {}

	virtual void GetContentDimensions(const DrawContext &dc, float &w, float &h) const;
	virtual void Draw(DrawContext &dc);

private:
	std::string text_;
	int font_;
};

enum ImageSizeMode {
	IS_DEFAULT,
};

class ImageView : public InertView {
public:
	ImageView(int atlasImage, ImageSizeMode sizeMode, LayoutParams *layoutParams = 0)
		: InertView(layoutParams), atlasImage_(atlasImage), sizeMode_(sizeMode) {}

	virtual void GetContentDimensions(const DrawContext &dc, float &w, float &h) const;
	virtual void Draw(DrawContext &dc);

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

void MeasureBySpec(Size sz, float contentWidth, MeasureSpec spec, float *measured);

}  // namespace