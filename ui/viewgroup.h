#pragma once

#include "ui/view.h"
#include "input/gesture_detector.h"

namespace UI {

struct NeighborResult {
	NeighborResult() : view(0), score(0) {}
	NeighborResult(View *v, float s) : view(v), score(s) {}

	View *view;
	float score;
};

class ViewGroup : public View {
public:
	ViewGroup(LayoutParams *layoutParams = 0) : View(layoutParams) {}
	virtual ~ViewGroup();

	// Pass through external events to children.
	virtual void Touch(const TouchInput &input);

	// By default, a container will layout to its own bounds.
	virtual void Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) = 0;
	virtual void Layout() = 0;
	virtual void Update(const InputState &input_state);

	virtual void Draw(UIContext &dc);

	// These should be unused.
	virtual float GetContentWidth() const { return 0.0f; }
	virtual float GetContentHeight() const { return 0.0f; }

	// Takes ownership! DO NOT add a view to multiple parents!
	void Add(View *view) { views_.push_back(view); }

	virtual bool SetFocus();
	virtual bool SubviewFocused(View *view);

	// Assumes that layout has taken place.
	NeighborResult FindNeighbor(View *view, FocusDirection direction, NeighborResult best);
	
	virtual bool CanBeFocused() const { return false; }

protected:
	std::vector<View *> views_;
};

// A frame layout contains a single child view (normally).
class FrameLayout : public ViewGroup {
public:
	void Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert);
	void Layout();
};


enum {
	NONE = -1,
};

class AnchorLayoutParams : public LayoutParams {
public:
	AnchorLayoutParams(Size w, Size h, float l, float t, float r, float b)
		: LayoutParams(w, h), left(l), top(t), right(r), bottom(b) {}

	AnchorLayoutParams(float l, float t, float r, float b)
		: LayoutParams(WRAP_CONTENT, WRAP_CONTENT), left(l), top(t), right(r), bottom(b) {}

	// These are not bounds, but distances from the container edges.
	// Set to NONE to not attach this edge to the container.
	float left, top, right, bottom;
};

class AnchorLayout : public ViewGroup {
public:
	AnchorLayout(LayoutParams *layoutParams = 0) : ViewGroup(layoutParams) {}
	void Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert);
	void Layout();
};

class LinearLayoutParams : public LayoutParams {
public:
	LinearLayoutParams()
		: LayoutParams(), weight(0.0f), gravity(G_TOPLEFT), hasMargins_(false) {}
	explicit LinearLayoutParams(float wgt)
		: LayoutParams(), weight(wgt), gravity(G_TOPLEFT), hasMargins_(false) {}
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

class LinearLayout : public ViewGroup {
public:
	LinearLayout(Orientation orientation, LayoutParams *layoutParams = 0)
		: ViewGroup(layoutParams), spacing_(5), orientation_(orientation), defaultMargins_(0) {}

	void Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert);
	void Layout();

private:
	Orientation orientation_;
	Margins defaultMargins_;
	float spacing_;
};

// GridLayout is a little different from the Android layout. This one has fixed size
// rows and columns. Items are not allowed to deviate from the set sizes.
// Initially, only horizontal layout is supported.
struct GridLayoutSettings {
	GridLayoutSettings() : orientation(ORIENT_HORIZONTAL), columnWidth(100), rowHeight(50), spacing(5), fillCells(false) {}
	GridLayoutSettings(int colW, int colH, int spac = 5) : orientation(ORIENT_HORIZONTAL), columnWidth(colW), rowHeight(colH), spacing(spac), fillCells(false) {}

	Orientation orientation;
	int columnWidth;
	int rowHeight;
	int spacing;
	bool fillCells;
};

class GridLayout : public ViewGroup {
public:
	GridLayout(GridLayoutSettings settings, LayoutParams *layoutParams = 0)
		: ViewGroup(layoutParams), settings_(settings) {
		if (settings.orientation != ORIENT_HORIZONTAL)
			ELOG("GridLayout: Vertical layouts not yet supported");
	}

	void Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert);
	void Layout();

private:
	GridLayoutSettings settings_;
};

// A scrollview usually contains just a single child - a linear layout or similar.
class ScrollView : public ViewGroup {
public:
	ScrollView(Orientation orientation, LayoutParams *layoutParams = 0) :
		ViewGroup(layoutParams), orientation_(orientation), scrollPos_(0), scrollTarget_(0), scrollToTarget_(false) {}

	void Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert);
	void Layout();

	void Touch(const TouchInput &input);
	void Draw(UIContext &dc);

	void ScrollTo(float newScrollPos);
	void Update(const InputState &input_state);

	// Override so that we can scroll to the active one after moving the focus.
	virtual bool SubviewFocused(View *view);

private:
	GestureDetector gesture_;
	Orientation orientation_;
	float scrollPos_;
	float scrollStart_;
	float scrollMax_;
	float scrollTarget_;
	bool scrollToTarget_;
};

class ViewPager : public ScrollView {
public:
};

void LayoutViewHierarchy(const UIContext &dc, ViewGroup *root);
void UpdateViewHierarchy(const InputState &input_state, ViewGroup *root);

}  // namespace UI