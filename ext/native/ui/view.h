#pragma once

// More traditional UI framework than ui/ui.h.

// Still very simple to use.

// Works very similarly to Android, there's a Measure pass and a Layout pass which you don't
// really need to care about if you just use the standard containers and widgets.

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/basictypes.h"
#include "gfx/texture_atlas.h"
#include "math/lin/matrix4x4.h"
#include "math/math_util.h"
#include "math/geom2d.h"

#undef small

struct KeyInput;
struct TouchInput;
struct AxisInput;

class DrawBuffer;
class Texture;
class UIContext;

namespace Draw {
	class DrawContext;
	class Texture;
}


// I don't generally like namespaces but I think we do need one for UI, so many potentially-clashing names.
namespace UI {

class View;

// The ONLY global is the currently focused item.
// Can be and often is null.
void EnableFocusMovement(bool enable);
bool IsFocusMovementEnabled();
View *GetFocusedView();
void SetFocusedView(View *view, bool force = false);

enum DrawableType {
	DRAW_NOTHING,
	DRAW_SOLID_COLOR,
	DRAW_4GRID,
	DRAW_STRETCH_IMAGE,
};

enum Visibility {
	V_VISIBLE,
	V_INVISIBLE,  // Keeps position, not drawn or interacted with
	V_GONE,  // Does not participate in layout
};

struct Drawable {
	Drawable() : type(DRAW_NOTHING), image(-1), color(0xFFFFFFFF) {}
	explicit Drawable(uint32_t col) : type(DRAW_SOLID_COLOR), image(-1), color(col) {}
	Drawable(DrawableType t, int img, uint32_t col = 0xFFFFFFFF) : type(t), image(img), color(col) {}

	DrawableType type;
	uint32_t image;
	uint32_t color;
};

struct Style {
	Style() : fgColor(0xFFFFFFFF), background(0xFF303030), image(-1) {}

	uint32_t fgColor;
	Drawable background;
	int image;  // where applicable.
};

struct FontStyle {
	FontStyle() : atlasFont(0), sizePts(0), flags(0) {}
	FontStyle(const char *name, int size) : atlasFont(0), fontName(name), sizePts(size), flags(0) {}
	FontStyle(int atlasFnt, const char *name, int size) : atlasFont(atlasFnt), fontName(name), sizePts(size), flags(0) {}

	int atlasFont;
	// For native fonts:
	std::string fontName;
	int sizePts;
	int flags;
};


// To use with an UI atlas.
struct Theme {
	FontStyle uiFont;
	FontStyle uiFontSmall;
	FontStyle uiFontSmaller;
	int checkOn;
	int checkOff;
	int sliderKnob;
	int whiteImage;
	int dropShadow4Grid;

	Style buttonStyle;
	Style buttonFocusedStyle;
	Style buttonDownStyle;
	Style buttonDisabledStyle;
	Style buttonHighlightedStyle;

	Style itemStyle;
	Style itemDownStyle;
	Style itemFocusedStyle;
	Style itemDisabledStyle;
	Style itemHighlightedStyle;

	Style headerStyle;
	Style infoStyle;

	Style popupTitle;
	Style popupStyle;
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

	G_CENTER = G_HCENTER | G_VCENTER,

	G_VERTMASK = 3 << 2,
};

typedef float Size;  // can also be WRAP_CONTENT or FILL_PARENT.

enum Orientation {
	ORIENT_HORIZONTAL,
	ORIENT_VERTICAL,
};

inline Orientation Opposite(Orientation o) {
	if (o == ORIENT_HORIZONTAL) return ORIENT_VERTICAL; else return ORIENT_HORIZONTAL;
}

inline FocusDirection Opposite(FocusDirection d) {
	switch (d) {
	case FOCUS_UP: return FOCUS_DOWN;
	case FOCUS_DOWN: return FOCUS_UP;
	case FOCUS_LEFT: return FOCUS_RIGHT;
	case FOCUS_RIGHT: return FOCUS_LEFT;
	case FOCUS_PREV: return FOCUS_NEXT;
	case FOCUS_NEXT: return FOCUS_PREV;
	}
	return d;
}

enum MeasureSpecType {
	UNSPECIFIED,
	EXACTLY,
	AT_MOST,
};

// I hope I can find a way to simplify this one day.
enum EventReturn {
	EVENT_DONE,  // Return this when no other view may process this event, for example if you changed the view hierarchy
	EVENT_SKIPPED,  // Return this if you ignored an event
	EVENT_CONTINUE,  // Return this if it's safe to send this event to further listeners. This should normally be the default choice but often EVENT_DONE is necessary.
};

enum FocusFlags {
	FF_LOSTFOCUS = 1,
	FF_GOTFOCUS = 2
};

enum PersistStatus {
	PERSIST_SAVE,
	PERSIST_RESTORE,
};

typedef std::vector<int> PersistBuffer;
typedef std::map<std::string, UI::PersistBuffer> PersistMap;

class ViewGroup;

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

// Should cover all bases.
struct EventParams {
	View *v;
	uint32_t a, b, x, y;
	float f;
	std::string s;
};

struct HandlerRegistration {
	std::function<EventReturn(EventParams&)> func;
};

class Event {
public:
	Event() {}
	~Event() {
		handlers_.clear();
	}
	// Call this from input thread or whatever, it doesn't matter
	void Trigger(EventParams &e);
	// Call this from UI thread
	EventReturn Dispatch(EventParams &e);

	// This is suggested for use in most cases. Autobinds, allowing for neat syntax.
	template<class T>
	T *Handle(T *thiz, EventReturn (T::* theCallback)(EventParams &e)) {
		Add(std::bind(theCallback, thiz, std::placeholders::_1));
		return thiz;
	}

	// Sometimes you have an already-bound function<>, just use this then.
	void Add(std::function<EventReturn(EventParams&)> func);

private:
	std::vector<HandlerRegistration> handlers_;
	DISALLOW_COPY_AND_ASSIGN(Event);
};

struct Margins {
	Margins() : top(0), bottom(0), left(0), right(0) {}
	explicit Margins(int8_t all) : top(all), bottom(all), left(all), right(all) {}
	Margins(int8_t horiz, int8_t vert) : top(vert), bottom(vert), left(horiz), right(horiz) {}
	Margins(int8_t l, int8_t t, int8_t r, int8_t b) : top(t), bottom(b), left(l), right(r) {}

	int horiz() const {
		return left + right;
	}
	int vert() const {
		return top + bottom;
	}

	int8_t top;
	int8_t bottom;
	int8_t left;
	int8_t right;
};

struct Padding {
	Padding() : top(0), bottom(0), left(0), right(0) {}
	explicit Padding(float all) : top(all), bottom(all), left(all), right(all) {}
	Padding(float horiz, float vert) : top(vert), bottom(vert), left(horiz), right(horiz) {}
	Padding(float l, float t, float r, float b) : top(t), bottom(b), left(l), right(r) {}

	float horiz() const {
		return left + right;
	}
	float vert() const {
		return top + bottom;
	}

	float top;
	float bottom;
	float left;
	float right;
};

enum LayoutParamsType {
	LP_PLAIN = 0,
	LP_LINEAR = 1,
	LP_ANCHOR = 2,
};

// Need a virtual destructor so vtables are created, otherwise RTTI can't work
class LayoutParams {
public:
	LayoutParams(LayoutParamsType type = LP_PLAIN)
		: width(WRAP_CONTENT), height(WRAP_CONTENT), type_(type) {}
	LayoutParams(Size w, Size h, LayoutParamsType type = LP_PLAIN)
		: width(w), height(h), type_(type) {}
	virtual ~LayoutParams() {}
	Size width;
	Size height;

	// Fake RTTI
	bool Is(LayoutParamsType type) const { return type_ == type; }

	template <typename T>
	T *As() {
		if (Is(T::StaticType())) {
			return static_cast<T *>(this);
		}
		return nullptr;
	}

	template <typename T>
	const T *As() const {
		if (Is(T::StaticType())) {
			return static_cast<const T *>(this);
		}
		return nullptr;
	}

	static LayoutParamsType StaticType() {
		return LP_PLAIN;
	}

private:
	LayoutParamsType type_;
};

View *GetFocusedView();

class Tween;
class CallbackColorTween;

class View {
public:
	View(LayoutParams *layoutParams = 0) : layoutParams_(layoutParams), visibility_(V_VISIBLE), measuredWidth_(0), measuredHeight_(0), enabledPtr_(0), enabled_(true), enabledMeansDisabled_(false) {
		if (!layoutParams)
			layoutParams_.reset(new LayoutParams());
	}
	virtual ~View();

	// Please note that Touch is called ENTIRELY asynchronously from drawing!
	// Can even be called on a different thread! This is to really minimize latency, and decouple
	// touch response from the frame rate. Same with Key and Axis.
	virtual bool Key(const KeyInput &input) { return false; }
	virtual void Touch(const TouchInput &input) {}
	virtual void Axis(const AxisInput &input) {}
	virtual void Update();

	virtual void DeviceLost() {}
	virtual void DeviceRestored(Draw::DrawContext *draw) {}

	// If this view covers these coordinates, it should add itself and its children to the list.
	virtual void Query(float x, float y, std::vector<View *> &list);
	virtual std::string Describe() const;

	virtual void FocusChanged(int focusFlags) {}
	virtual void PersistData(PersistStatus status, std::string anonId, PersistMap &storage);

	void Move(Bounds bounds) {
		bounds_ = bounds;
	}

	// Views don't do anything here in Layout, only containers implement this.
	virtual void Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert);
	virtual void Layout() {}
	virtual void Draw(UIContext &dc) {}

	virtual float GetMeasuredWidth() const { return measuredWidth_; }
	virtual float GetMeasuredHeight() const { return measuredHeight_; }

	// Override this for easy standard behaviour. No need to override Measure.
	virtual void GetContentDimensions(const UIContext &dc, float &w, float &h) const;
	virtual void GetContentDimensionsBySpec(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert, float &w, float &h) const;

	// Called when the layout is done.
	void SetBounds(Bounds bounds) { bounds_ = bounds; }
	virtual const LayoutParams *GetLayoutParams() const { return layoutParams_.get(); }
	virtual void ReplaceLayoutParams(LayoutParams *newLayoutParams) { layoutParams_.reset(newLayoutParams); }
	const Bounds &GetBounds() const { return bounds_; }

	virtual bool SetFocus();

	virtual bool CanBeFocused() const { return true; }
	virtual bool SubviewFocused(View *view) { return false; }

	bool HasFocus() const {
		return GetFocusedView() == this;
	}

	void SetEnabled(bool enabled) { enabled_ = enabled; enabledMeansDisabled_ = false; }
	bool IsEnabled() const {
		if (enabledPtr_)
			return *enabledPtr_ != enabledMeansDisabled_;
		else
			return enabled_ != enabledMeansDisabled_;
	}
	void SetEnabledPtr(bool *enabled) { enabledPtr_ = enabled; enabledMeansDisabled_ = false; }
	void SetDisabledPtr(bool *disabled) { enabledPtr_ = disabled; enabledMeansDisabled_ = true;  }

	virtual void SetVisibility(Visibility visibility) { visibility_ = visibility; }
	Visibility GetVisibility() const { return visibility_; }

	const std::string &Tag() const { return tag_; }
	void SetTag(const std::string &str) { tag_ = str; }

	// Fake RTTI
	virtual bool IsViewGroup() const { return false; }

	Point GetFocusPosition(FocusDirection dir);

	template <class T>
	T *AddTween(T *t) {
		tweens_.push_back(t);
		return t;
	}

protected:
	// Inputs to layout
	std::unique_ptr<LayoutParams> layoutParams_;

	std::string tag_;
	Visibility visibility_;

	// Results of measure pass. Set these in Measure.
	float measuredWidth_;
	float measuredHeight_;

	// Outputs of layout. X/Y are absolute screen coordinates, hierarchy is "gone" here.
	Bounds bounds_;

	std::vector<Tween *> tweens_;

private:
	bool *enabledPtr_;
	bool enabled_;
	bool enabledMeansDisabled_;

	DISALLOW_COPY_AND_ASSIGN(View);
};

// These don't do anything when touched.
class InertView : public View {
public:
	InertView(LayoutParams *layoutParams)
		: View(layoutParams) {}

	bool Key(const KeyInput &input) override { return false; }
	void Touch(const TouchInput &input) override {}
	bool CanBeFocused() const override { return false; }
};


// All these light up their background when touched, or have focus.
class Clickable : public View {
public:
	Clickable(LayoutParams *layoutParams);

	bool Key(const KeyInput &input) override;
	void Touch(const TouchInput &input) override;

	void FocusChanged(int focusFlags) override;

	Event OnClick;

protected:
	// Internal method that fires on a click. Default behaviour is to trigger
	// the event.
	// Use it for checking/unchecking checkboxes, etc.
	virtual void Click();
	void DrawBG(UIContext &dc, const Style &style);

	CallbackColorTween *bgColor_ = nullptr;
	float bgColorLast_ = 0.0f;
	int downCountDown_ = 0;
	bool dragging_ = false;
	bool down_ = false;
};

class Button : public Clickable {
public:
	Button(const std::string &text, LayoutParams *layoutParams = 0)
		: Clickable(layoutParams), text_(text), imageID_(-1) {}
	Button(ImageID imageID, LayoutParams *layoutParams = 0)
		: Clickable(layoutParams), imageID_(imageID) {}
	Button(const std::string &text, ImageID imageID, LayoutParams *layoutParams = 0)
		: Clickable(layoutParams), text_(text), imageID_(imageID) {}

	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
	const std::string &GetText() const { return text_; }
	void SetPadding(int w, int h) {
		paddingW_ = w;
		paddingH_ = h;
	}

private:
	Style style_;
	std::string text_;
	ImageID imageID_;
	int paddingW_ = 16;
	int paddingH_ = 8;
};

class Slider : public Clickable {
public:
	Slider(int *value, int minValue, int maxValue, LayoutParams *layoutParams = 0)
		: Clickable(layoutParams), value_(value), showPercent_(false), minValue_(minValue), maxValue_(maxValue), paddingLeft_(5), paddingRight_(70), step_(1), repeat_(-1) {}

	Slider(int *value, int minValue, int maxValue, int step = 1, LayoutParams *layoutParams = 0)
		: Clickable(layoutParams), value_(value), showPercent_(false), minValue_(minValue), maxValue_(maxValue), paddingLeft_(5), paddingRight_(70), repeat_(-1) {
		step_ = step <= 0 ? 1 : step;
	}
	void Draw(UIContext &dc) override;
	bool Key(const KeyInput &input) override;
	void Touch(const TouchInput &input) override;
	void Update() override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
	void SetShowPercent(bool s) { showPercent_ = s; }

	// OK to call this from the outside after having modified *value_
	void Clamp();

	Event OnChange;

private:
	bool ApplyKey(int keyCode);

	int *value_;
	bool showPercent_;
	int minValue_;
	int maxValue_;
	float paddingLeft_;
	float paddingRight_;
	int step_;
	int repeat_;
	int repeatCode_;
};

class SliderFloat : public Clickable {
public:
	SliderFloat(float *value, float minValue, float maxValue, LayoutParams *layoutParams = 0)
		: Clickable(layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), paddingLeft_(5), paddingRight_(70), repeat_(-1) {}
	void Draw(UIContext &dc) override;
	bool Key(const KeyInput &input) override;
	void Touch(const TouchInput &input) override;
	void Update() override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;

	// OK to call this from the outside after having modified *value_
	void Clamp();

	Event OnChange;

private:
	bool ApplyKey(int keyCode);

	float *value_;
	float minValue_;
	float maxValue_;
	float paddingLeft_;
	float paddingRight_;
	int repeat_;
	int repeatCode_;
};

// Basic button that modifies a bitfield based on the pressed status. Supports multitouch.
// Suitable for controller simulation (ABXY etc).
class TriggerButton : public View {
public:
	TriggerButton(uint32_t *bitField, uint32_t bit, int imageBackground, int imageForeground, LayoutParams *layoutParams)
		: View(layoutParams), down_(0.0), bitField_(bitField), bit_(bit), imageBackground_(imageBackground), imageForeground_(imageForeground) {}

	void Touch(const TouchInput &input) override;
	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;

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
	Item(LayoutParams *layoutParams);
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
};

class ClickableItem : public Clickable {
public:
	ClickableItem(LayoutParams *layoutParams);
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;

	// Draws the item background.
	void Draw(UIContext &dc) override;
};

// Use to trigger something or open a submenu screen.
class Choice : public ClickableItem {
public:
	Choice(const std::string &text, LayoutParams *layoutParams = nullptr)
		: Choice(text, std::string(), false, layoutParams) {}
	Choice(const std::string &text, const std::string &smallText, bool selected = false, LayoutParams *layoutParams = nullptr)
		: ClickableItem(layoutParams), text_(text), smallText_(smallText), atlasImage_(-1), iconImage_(-1), centered_(false), highlighted_(false), selected_(selected) {}
	Choice(ImageID image, LayoutParams *layoutParams = nullptr)
		: ClickableItem(layoutParams), atlasImage_(image), iconImage_(-1), centered_(false), highlighted_(false), selected_(false) {}

	virtual void HighlightChanged(bool highlighted);
	void GetContentDimensionsBySpec(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert, float &w, float &h) const override;
	void Draw(UIContext &dc) override;
	virtual void SetCentered(bool c) {
		centered_ = c;
	}
	virtual void SetIcon(ImageID iconImage) {
		iconImage_ = iconImage;
	}

protected:
	// hackery
	virtual bool IsSticky() const { return false; }
	virtual float CalculateTextScale(const UIContext &dc, float availWidth) const;

	std::string text_;
	std::string smallText_;
	ImageID atlasImage_;
	ImageID iconImage_;  // Only applies for text, non-centered
	Padding textPadding_;
	bool centered_;
	bool highlighted_;

private:
	bool selected_;
};

// Different key handling.
class StickyChoice : public Choice {
public:
	StickyChoice(const std::string &text, const std::string &smallText = "", LayoutParams *layoutParams = 0)
		: Choice(text, smallText, false, layoutParams) {}
	StickyChoice(ImageID buttonImage, LayoutParams *layoutParams = 0)
		: Choice(buttonImage, layoutParams) {}

	bool Key(const KeyInput &key) override;
	void Touch(const TouchInput &touch) override;
	void FocusChanged(int focusFlags) override;

	void Press() { down_ = true; dragging_ = false;  }
	void Release() { down_ = false; dragging_ = false; }
	bool IsDown() { return down_; }

protected:
	// hackery
	bool IsSticky() const override { return true; }
};

class InfoItem : public Item {
public:
	InfoItem(const std::string &text, const std::string &rightText, LayoutParams *layoutParams = nullptr);

	void Draw(UIContext &dc) override;

	// These are focusable so that long lists of them can be keyboard scrolled.
	bool CanBeFocused() const override { return true; }

	void SetText(const std::string &text) {
		text_ = text;
	}
	const std::string &GetText() const {
		return text_;
	}
	void SetRightText(const std::string &text) {
		rightText_ = text;
	}

private:
	CallbackColorTween *bgColor_ = nullptr;
	CallbackColorTween *fgColor_ = nullptr;

	std::string text_;
	std::string rightText_;
};

class ItemHeader : public Item {
public:
	ItemHeader(const std::string &text, LayoutParams *layoutParams = 0);
	void Draw(UIContext &dc) override;
	void GetContentDimensionsBySpec(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert, float &w, float &h) const override;

private:
	std::string text_;
};

class PopupHeader : public Item {
public:
	PopupHeader(const std::string &text, LayoutParams *layoutParams = 0)
		: Item(layoutParams), text_(text) {
			layoutParams_->width = FILL_PARENT;
			layoutParams_->height = 64;
	}
	void Draw(UIContext &dc) override;
private:
	std::string text_;
};

class CheckBox : public ClickableItem {
public:
	CheckBox(bool *toggle, const std::string &text, const std::string &smallText = "", LayoutParams *layoutParams = 0)
		: ClickableItem(layoutParams), toggle_(toggle), text_(text), smallText_(smallText) {
		OnClick.Handle(this, &CheckBox::OnClicked);
	}

	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;

	EventReturn OnClicked(EventParams &e);
	//allow external agents to toggle the checkbox
	void Toggle();
private:
	float CalculateTextScale(const UIContext &dc, float availWidth) const;

	bool *toggle_;
	std::string text_;
	std::string smallText_;
};

// These are for generic use.

class Spacer : public InertView {
public:
	Spacer(LayoutParams *layoutParams = 0)
		: InertView(layoutParams), size_(0.0f) {}
	Spacer(float size, LayoutParams *layoutParams = 0)
		: InertView(layoutParams), size_(size) {}
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		w = size_; h = size_;
	}
	void Draw(UIContext &dc) override {}
private:
	float size_;
};

class TextView : public InertView {
public:
	TextView(const std::string &text, LayoutParams *layoutParams = 0)
		: InertView(layoutParams), text_(text), textAlign_(0), textColor_(0xFFFFFFFF), small_(false), shadow_(false), focusable_(false), clip_(true) {}

	TextView(const std::string &text, int textAlign, bool small, LayoutParams *layoutParams = 0)
		: InertView(layoutParams), text_(text), textAlign_(textAlign), textColor_(0xFFFFFFFF), small_(small), shadow_(false), focusable_(false), clip_(true) {}

	void GetContentDimensionsBySpec(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert, float &w, float &h) const override;
	void Draw(UIContext &dc) override;

	void SetText(const std::string &text) { text_ = text; }
	const std::string &GetText() const { return text_; }
	void SetSmall(bool small) { small_ = small; }
	void SetTextColor(uint32_t color) { textColor_ = color; hasTextColor_ = true; }
	void SetShadow(bool shadow) { shadow_ = shadow; }
	void SetFocusable(bool focusable) { focusable_ = focusable; }
	void SetClip(bool clip) { clip_ = clip; }

	bool CanBeFocused() const override { return focusable_; }

private:
	std::string text_;
	int textAlign_;
	uint32_t textColor_;
	bool hasTextColor_ = false;
	bool small_;
	bool shadow_;
	bool focusable_;
	bool clip_;
};

class TextEdit : public View {
public:
	TextEdit(const std::string &text, const std::string &placeholderText, LayoutParams *layoutParams = 0);
	void SetText(const std::string &text) { text_ = text; scrollPos_ = 0; caret_ = (int)text_.size(); }
	void SetTextColor(uint32_t color) { textColor_ = color; hasTextColor_ = true; }
	const std::string &GetText() const { return text_; }
	void SetMaxLen(size_t maxLen) { maxLen_ = maxLen; }
	void SetTextAlign(int align) { align_ = align; }  // Only really useful for setting FLAG_DYNAMIC_ASCII

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
	void Draw(UIContext &dc) override;
	bool Key(const KeyInput &key) override;
	void Touch(const TouchInput &touch) override;

	Event OnTextChange;
	Event OnEnter;

private:
	void InsertAtCaret(const char *text);

	std::string text_;
	std::string undo_;
	std::string placeholderText_;
	uint32_t textColor_;
	bool hasTextColor_ = false;
	int caret_;
	int scrollPos_ = 0;
	size_t maxLen_;
	bool ctrlDown_ = false;  // TODO: Make some global mechanism for this.
	int align_ = 0;
	// TODO: Selections
};

enum ImageSizeMode {
	IS_DEFAULT,
	IS_FIXED,
	IS_KEEP_ASPECT,
};

class ImageView : public InertView {
public:
	ImageView(int atlasImage, ImageSizeMode sizeMode, LayoutParams *layoutParams = 0)
		: InertView(layoutParams), atlasImage_(atlasImage), sizeMode_(sizeMode) {}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
	void Draw(UIContext &dc) override;

private:
	int atlasImage_;
	ImageSizeMode sizeMode_;
};

// TextureView takes a texture that is assumed to be alive during the lifetime
// of the view.
class TextureView : public InertView {
public:
	TextureView(Draw::Texture *texture, ImageSizeMode sizeMode, LayoutParams *layoutParams = 0)
		: InertView(layoutParams), texture_(texture), color_(0xFFFFFFFF), sizeMode_(sizeMode) {}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
	void Draw(UIContext &dc) override;

	void SetTexture(Draw::Texture *texture) { texture_ = texture; }
	void SetColor(uint32_t color) { color_ = color; }

private:
	Draw::Texture *texture_;
	uint32_t color_;
	ImageSizeMode sizeMode_;
};

class ProgressBar : public InertView {
public:
	ProgressBar(LayoutParams *layoutParams = 0)
		: InertView(layoutParams), progress_(0.0) {}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
	void Draw(UIContext &dc) override;

	void SetProgress(float progress) {
		if (progress > 1.0f) {
			progress_ = 1.0f;
		} else if (progress < 0.0f) {
			progress_ = 0.0f;
		} else {
			progress_ = progress;
		}
	}
	float GetProgress() const { return progress_; }

private:
	float progress_;
};

class Spinner : public InertView {
public:
	Spinner(const int *images, int numImages, LayoutParams *layoutParams = 0)
		: InertView(layoutParams), images_(images), numImages_(numImages) {
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
	void Draw(UIContext &dc) override;
	void SetColor(uint32_t color) { color_ = color; }

private:
	const int *images_;
	int numImages_;
	uint32_t color_ = 0xFFFFFFFF;
};

void MeasureBySpec(Size sz, float contentWidth, MeasureSpec spec, float *measured);

void EventTriggered(Event *e, EventParams params);
void DispatchEvents();
bool IsDPadKey(const KeyInput &key);
bool IsAcceptKey(const KeyInput &key);
bool IsEscapeKey(const KeyInput &key);
bool IsTabLeftKey(const KeyInput &key);
bool IsTabRightKey(const KeyInput &key);

}  // namespace
