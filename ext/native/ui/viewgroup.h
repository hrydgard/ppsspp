#pragma once

#include <vector>
#include <set>

#include "base/logging.h"
#include "base/mutex.h"
#include "math/geom2d.h"
#include "input/gesture_detector.h"
#include "ui/view.h"

namespace UI {

struct NeighborResult {
	NeighborResult() : view(0), score(0) {}
	NeighborResult(View *v, float s) : view(v), score(s) {}

	View *view;
	float score;
};

class ViewGroup : public View {
public:
	ViewGroup(LayoutParams *layoutParams = 0) : View(layoutParams), defaultFocusView_(0), hasDropShadow_(false), clip_(false) {}
	virtual ~ViewGroup();

	// Pass through external events to children.
	virtual bool Key(const KeyInput &input) override;
	virtual void Touch(const TouchInput &input) override;
	virtual void Axis(const AxisInput &input) override;

	// By default, a container will layout to its own bounds.
	virtual void Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) override = 0;
	virtual void Layout() override = 0;
	virtual void Update(const InputState &input_state) override;
	virtual void Query(float x, float y, std::vector<View *> &list) override;

	virtual void Draw(UIContext &dc) override;

	// These should be unused.
	virtual float GetContentWidth() const { return 0.0f; }
	virtual float GetContentHeight() const { return 0.0f; }

	// Takes ownership! DO NOT add a view to multiple parents!
	template <class T>
	T *Add(T *view) {
		lock_guard guard(modifyLock_);
		views_.push_back(view);
		return view;
	}

	virtual bool SetFocus() override;
	virtual bool SubviewFocused(View *view) override;
	virtual void RemoveSubview(View *view);

	void SetDefaultFocusView(View *view) { defaultFocusView_ = view; }
	View *GetDefaultFocusView() { return defaultFocusView_; }

	// Assumes that layout has taken place.
	NeighborResult FindNeighbor(View *view, FocusDirection direction, NeighborResult best);

	virtual bool CanBeFocused() const override { return false; }
	virtual bool IsViewGroup() const override { return true; }

	virtual void SetBG(const Drawable &bg) { bg_ = bg; }

	virtual void Clear();
	void PersistData(PersistStatus status, std::string anonId, PersistMap &storage) override;
	View *GetViewByIndex(int index) { return views_[index]; }
	int GetNumSubviews() const { return (int)views_.size(); }
	void SetHasDropShadow(bool has) { hasDropShadow_ = has; }

	void Lock() { modifyLock_.lock(); }
	void Unlock() { modifyLock_.unlock(); }

	void SetClip(bool clip) { clip_ = clip; }
	std::string Describe() const override { return "ViewGroup: " + View::Describe(); }

protected:
	recursive_mutex modifyLock_;  // Hold this when changing the subviews.
	std::vector<View *> views_;
	View *defaultFocusView_;
	Drawable bg_;
	bool hasDropShadow_;
	bool clip_;
};

// A frame layout contains a single child view (normally).
// It simply centers the child view.
class FrameLayout : public ViewGroup {
public:
	void Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) override;
	void Layout() override;
};

enum {
	NONE = -1,
};

class AnchorLayoutParams : public LayoutParams {
public:
	AnchorLayoutParams(Size w, Size h, float l, float t, float r, float b, bool c = false)
		: LayoutParams(w, h, LP_ANCHOR), left(l), top(t), right(r), bottom(b), center(c) {

	}
	AnchorLayoutParams(Size w, Size h, bool c = false)
		: LayoutParams(w, h, LP_ANCHOR), left(0), top(0), right(NONE), bottom(NONE), center(c) {
	}
	AnchorLayoutParams(float l, float t, float r, float b, bool c = false)
		: LayoutParams(WRAP_CONTENT, WRAP_CONTENT, LP_ANCHOR), left(l), top(t), right(r), bottom(b), center(c) {}

	// These are not bounds, but distances from the container edges.
	// Set to NONE to not attach this edge to the container.
	float left, top, right, bottom;
	bool center;  // If set, only two "sides" can be set, and they refer to the center, not the edge, of the view being layouted.

	static LayoutParamsType StaticType() {
		return LP_ANCHOR;
	}
};

class AnchorLayout : public ViewGroup {
public:
	AnchorLayout(LayoutParams *layoutParams = 0) : ViewGroup(layoutParams), overflow_(true) {}
	void Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) override;
	void Layout() override;
	void Overflow(bool allow) {
		overflow_ = allow;
	}
	std::string Describe() const override { return "AnchorLayout: " + View::Describe(); }

private:
	bool overflow_;
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
	LinearLayoutParams(Size w, Size h, float wgt, const Margins &mgn)
		: LayoutParams(w, h, LP_LINEAR), weight(wgt), gravity(G_TOPLEFT), margins(mgn), hasMargins_(true) {}
	LinearLayoutParams(const Margins &mgn)
		: LayoutParams(WRAP_CONTENT, WRAP_CONTENT, LP_LINEAR), weight(0.0f), gravity(G_TOPLEFT), margins(mgn), hasMargins_(true) {}

	float weight;
	Gravity gravity;
	Margins margins;

	bool HasMargins() const { return hasMargins_; }

	static LayoutParamsType StaticType() {
		return LP_LINEAR;
	}

private:
	bool hasMargins_;
};

class LinearLayout : public ViewGroup {
public:
	LinearLayout(Orientation orientation, LayoutParams *layoutParams = 0)
		: ViewGroup(layoutParams), orientation_(orientation), defaultMargins_(0), spacing_(10) {}

	void Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) override;
	void Layout() override;
	void SetSpacing(float spacing) {
		spacing_ = spacing;
	}
	std::string Describe() const override { return (orientation_ == ORIENT_HORIZONTAL ? "LinearLayoutHoriz: " : "LinearLayoutVert: ") + View::Describe(); }

protected:
	Orientation orientation_;
private:
	Margins defaultMargins_;
	float spacing_;
};

// GridLayout is a little different from the Android layout. This one has fixed size
// rows and columns. Items are not allowed to deviate from the set sizes.
// Initially, only horizontal layout is supported.
struct GridLayoutSettings {
	GridLayoutSettings() : orientation(ORIENT_HORIZONTAL), columnWidth(100), rowHeight(50), spacing(5), fillCells(false) {}
	GridLayoutSettings(int colW, int colH, int spac = 5)
		: orientation(ORIENT_HORIZONTAL), columnWidth(colW), rowHeight(colH), spacing(spac), fillCells(false) {}

	Orientation orientation;
	int columnWidth;
	int rowHeight;
	int spacing;
	bool fillCells;
};

class GridLayout : public ViewGroup {
public:
	GridLayout(GridLayoutSettings settings, LayoutParams *layoutParams = 0)
		: ViewGroup(layoutParams), settings_(settings), numColumns_(1) {
		if (settings.orientation != ORIENT_HORIZONTAL)
			ELOG("GridLayout: Vertical layouts not yet supported");
	}

	void Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) override;
	void Layout() override;
	std::string Describe() const override { return "GridLayout: " + View::Describe(); }

private:
	GridLayoutSettings settings_;
	int numColumns_;
};

// A scrollview usually contains just a single child - a linear layout or similar.
class ScrollView : public ViewGroup {
public:
	ScrollView(Orientation orientation, LayoutParams *layoutParams = 0) :
		ViewGroup(layoutParams),
		orientation_(orientation),
		scrollPos_(0),
		scrollStart_(0),
		scrollTarget_(0),
		scrollToTarget_(false),
		inertia_(0.0f),
		pull_(0.0f),
		lastViewSize_(0.0f),
		scrollToTopOnSizeChange_(false) {}

	void Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) override;
	void Layout() override;

	bool Key(const KeyInput &input) override;
	void Touch(const TouchInput &input) override;
	void Draw(UIContext &dc) override;
	std::string Describe() const override { return "ScrollView: " + View::Describe(); }

	void ScrollTo(float newScrollPos);
	void ScrollToBottom();
	void ScrollRelative(float distance);
	bool CanScroll() const;
	void Update(const InputState &input_state) override;

	// Override so that we can scroll to the active one after moving the focus.
	bool SubviewFocused(View *view) override;
	void PersistData(PersistStatus status, std::string anonId, PersistMap &storage) override;
	void SetVisibility(Visibility visibility) override;

	// Quick hack to prevent scrolling to top in some lists
	void SetScrollToTop(bool t) { scrollToTopOnSizeChange_ = t; }

private:
	float ClampedScrollPos(float pos);

	GestureDetector gesture_;
	Orientation orientation_;
	float scrollPos_;
	float scrollStart_;
	float scrollTarget_;
	bool scrollToTarget_;
	float inertia_;
	float pull_;
	float lastViewSize_;
	bool scrollToTopOnSizeChange_;
};

class ViewPager : public ScrollView {
public:
};


class ChoiceStrip : public LinearLayout {
public:
	ChoiceStrip(Orientation orientation, LayoutParams *layoutParams = 0);

	void AddChoice(const std::string &title);
	void AddChoice(ImageID buttonImage);

	int GetSelection() const { return selected_; }
	void SetSelection(int sel);

	void HighlightChoice(unsigned int choice);

	bool Key(const KeyInput &input) override;

	void SetTopTabs(bool tabs) { topTabs_ = tabs; }
	void Draw(UIContext &dc) override;

	std::string Describe() const override { return "ChoiceStrip: " + View::Describe(); }

	Event OnChoice;

private:
	StickyChoice *Choice(int index);
	EventReturn OnChoiceClick(EventParams &e);

	int selected_;
	bool topTabs_;  // Can be controlled with L/R.
};


class TabHolder : public LinearLayout {
public:
	TabHolder(Orientation orientation, float stripSize, LayoutParams *layoutParams = 0);

	template <class T>
	T *AddTab(const std::string &title, T *tabContents) {
		tabContents->ReplaceLayoutParams(new LinearLayoutParams(1.0f));
		tabs_.push_back(tabContents);
		tabStrip_->AddChoice(title);
		Add(tabContents);
		if (tabs_.size() > 1)
			tabContents->SetVisibility(V_GONE);
		return tabContents;
	}

	void SetCurrentTab(int tab) {
		if (tab != currentTab_) {
			tabs_[currentTab_]->SetVisibility(V_GONE);
			currentTab_ = tab;
			tabs_[currentTab_]->SetVisibility(V_VISIBLE);
		}
		tabStrip_->SetSelection(tab);
	}

	int GetCurrentTab() const { return currentTab_; }
	std::string Describe() const override { return "TabHolder: " + View::Describe(); }

	void PersistData(PersistStatus status, std::string anonId, PersistMap &storage) override;

private:
	EventReturn OnTabClick(EventParams &e);

	ChoiceStrip *tabStrip_;
	ScrollView *tabScroll_;

	float stripSize_;
	int currentTab_;
	std::vector<View *> tabs_;
};

// Yes, this feels a bit Java-ish...
class ListAdaptor {
public:
	virtual ~ListAdaptor() {}
	virtual View *CreateItemView(int index) = 0;
	virtual int GetNumItems() = 0;
	virtual bool AddEventCallback(View *view, std::function<EventReturn(EventParams&)> callback) { return false; }
	virtual std::string GetTitle(int index) const { return ""; }
	virtual void SetSelected(int sel) { }
	virtual int GetSelected() { return -1; }
};

class ChoiceListAdaptor : public ListAdaptor {
public:
	ChoiceListAdaptor(const char *items[], int numItems) : items_(items), numItems_(numItems) {}
	virtual View *CreateItemView(int index);
	virtual int GetNumItems() { return numItems_; }
	virtual bool AddEventCallback(View *view, std::function<EventReturn(EventParams&)> callback);

private:
	const char **items_;
	int numItems_;
};


// The "selected" item is what was previously selected (optional). This items will be drawn differently.
class StringVectorListAdaptor : public ListAdaptor {
public:
	StringVectorListAdaptor() : selected_(-1) {}
	StringVectorListAdaptor(const std::vector<std::string> &items, int selected = -1) : items_(items), selected_(selected) {}
	virtual View *CreateItemView(int index) override;
	virtual int GetNumItems() override { return (int)items_.size(); }
	virtual bool AddEventCallback(View *view, std::function<EventReturn(EventParams&)> callback) override;
	void SetSelected(int sel) override { selected_ = sel; }
	virtual std::string GetTitle(int index) const override { return items_[index]; }
	virtual int GetSelected() override { return selected_; }

private:
	std::vector<std::string> items_;
	int selected_;
};

// A list view is a scroll view with autogenerated items.
// In the future, it might be smart and load/unload items as they go, but currently not.
class ListView : public ScrollView {
public:
	ListView(ListAdaptor *a, std::set<int> hidden = std::set<int>(), LayoutParams *layoutParams = 0);

	int GetSelected() { return adaptor_->GetSelected(); }
	virtual void Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) override;
	virtual void SetMaxHeight(float mh) { maxHeight_ = mh; }
	Event OnChoice;
	std::string Describe() const override { return "ListView: " + View::Describe(); }

private:
	void CreateAllItems();
	EventReturn OnItemCallback(int num, EventParams &e);
	ListAdaptor *adaptor_;
	LinearLayout *linLayout_;
	float maxHeight_;
	std::set<int> hidden_;
};

void LayoutViewHierarchy(const UIContext &dc, ViewGroup *root);
void UpdateViewHierarchy(const InputState &input_state, ViewGroup *root);
// Hooks arrow keys for navigation
bool KeyEvent(const KeyInput &key, ViewGroup *root);
bool TouchEvent(const TouchInput &touch, ViewGroup *root);
bool AxisEvent(const AxisInput &axis, ViewGroup *root);

void CaptureDrag(int id);
void ReleaseDrag(int id);
bool IsDragCaptured(int id);

}  // namespace UI
