#pragma once

#include "base/logging.h"
#include "ui/view.h"
#include "math/geom2d.h"
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
	template <class T>
	T *Add(T *view) { views_.push_back(view); return view; }

	virtual bool SetFocus();
	virtual bool SubviewFocused(View *view);

	// Assumes that layout has taken place.
	NeighborResult FindNeighbor(View *view, FocusDirection direction, NeighborResult best);
	
	virtual bool CanBeFocused() const { return false; }
	virtual bool IsViewGroup() const { return true; }

protected:
	std::vector<View *> views_;
};

// A frame layout contains a single child view (normally).
// It simply centers the child view.
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
		: LayoutParams(w, h, LP_ANCHOR), left(l), top(t), right(r), bottom(b) {

	}

	AnchorLayoutParams(float l, float t, float r, float b)
		: LayoutParams(WRAP_CONTENT, WRAP_CONTENT, LP_ANCHOR), left(l), top(t), right(r), bottom(b) {}

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
		: LayoutParams(LP_LINEAR), weight(0.0f), gravity(G_TOPLEFT), hasMargins_(false) {}
	explicit LinearLayoutParams(float wgt, Gravity grav = G_TOPLEFT)
		: LayoutParams(LP_LINEAR), weight(wgt), gravity(grav), hasMargins_(false) {}
	LinearLayoutParams(float wgt, const Margins &mgn)
		: LayoutParams(LP_LINEAR), weight(wgt), gravity(G_TOPLEFT), margins(mgn), hasMargins_(true) {}
	LinearLayoutParams(Size w, Size h, float wgt = 0.0f, Gravity grav = G_TOPLEFT)
		: LayoutParams(w, h, LP_LINEAR), weight(wgt), gravity(grav), hasMargins_(false) {}
	LinearLayoutParams(Size w, Size h, float wgt, Gravity grav, const Margins &mgn)
		: LayoutParams(w, h, LP_LINEAR), weight(wgt), gravity(grav), margins(mgn), hasMargins_(true) {}
	LinearLayoutParams(Size w, Size h, const Margins &mgn)
		: LayoutParams(w, h, LP_LINEAR), weight(0.0f), gravity(G_TOPLEFT), margins(mgn), hasMargins_(true) {}
	LinearLayoutParams(const Margins &mgn)
		: LayoutParams(WRAP_CONTENT, WRAP_CONTENT, LP_LINEAR), weight(0.0f), gravity(G_TOPLEFT), margins(mgn), hasMargins_(true) {}

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
		: ViewGroup(layoutParams), orientation_(orientation), defaultMargins_(0), spacing_(10) {}

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
		ViewGroup(layoutParams), 
		orientation_(orientation),
		scrollPos_(0),
		scrollStart_(0),
		scrollMax_(0),
		scrollTarget_(0),
		scrollToTarget_(false) {}

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

class TabHolder : public LinearLayout {
public:
	TabHolder(Orientation orientation, float stripSize, LayoutParams *layoutParams = 0)
		: LinearLayout(Opposite(orientation), layoutParams),
		  orientation_(orientation), stripSize_(stripSize), currentTab_(0) {
		tabStrip_ = new LinearLayout(orientation, new LinearLayoutParams(stripSize, WRAP_CONTENT));
		Add(tabStrip_);
	}

	template <class T>
	T *AddTab(const std::string &title, T *tabContents) {
		tabContents->ReplaceLayoutParams(new LinearLayoutParams(1.0f));
		tabTitles_.push_back(title);
		tabs_.push_back(tabContents);
		Choice *choice = new Choice(title);
		tabStrip_->Add(choice)->OnClick.Handle(this, &TabHolder::OnTabClick);
		tabChoices_.push_back(choice);
		Add(tabContents);
		if (tabs_.size() > 1)
			tabContents->SetVisibility(V_GONE);
		return tabContents;
	}

	void SetCurrentTab(int tab) {
		tabs_[currentTab_]->SetVisibility(V_GONE);
		currentTab_ = tab;
		tabs_[currentTab_]->SetVisibility(V_VISIBLE);
	}

private:
	EventReturn OnTabClick(EventParams &e);

	ViewGroup *tabStrip_;

	Orientation orientation_;
	float stripSize_;
	int currentTab_;
	std::vector<std::string> tabTitles_;
	std::vector<Choice *> tabChoices_;
	std::vector<View *> tabs_;
};

// Yes, this feels a bit Java-ish...
class ListAdaptor {
public:
	virtual ~ListAdaptor() {}
	virtual View *CreateItemView(int index) = 0;
	virtual int GetNumItems() = 0;
	virtual bool AddEventCallback(View *view, std::function<EventReturn(EventParams&)> callback) { return false; }
};

class ChoiceListAdaptor {
public:
	ChoiceListAdaptor(const char *items[], int numItems) : items_(items), numItems_(numItems) {}
	virtual View *CreateItemView(int index);
	virtual int GetNumItems() { return numItems_; }
	virtual bool AddEventCallback(View *view, std::function<EventReturn(EventParams&)> callback);

private:
	const char **items_;
	int numItems_;
};

// A list view is a scroll view with autogenerated items.
// In the future, it might be smart and load/unload items as they go, but currently not.
class ListView : public ScrollView {
public:
	ListView(ListAdaptor *a, LayoutParams *layoutParams);
	
	Event OnChoice;

private:
	EventReturn OnItemCallback(int num, EventParams &e);
	ListAdaptor *adaptor_;
};

void LayoutViewHierarchy(const UIContext &dc, ViewGroup *root);
void UpdateViewHierarchy(const InputState &input_state, ViewGroup *root);

}  // namespace UI