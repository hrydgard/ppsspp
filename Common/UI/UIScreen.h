#pragma once

#include <mutex>
#include <string>
#include <deque>

#include "Common/Math/lin/vec3.h"
#include "Common/UI/Screen.h"
#include "Common/UI/ViewGroup.h"

using namespace Lin;

class I18NCategory;
namespace Draw {
	class DrawContext;
}

enum class QueuedEventType : u8 {
	KEY,
	AXIS,
	TOUCH,
};

struct QueuedEvent {
	QueuedEventType type;
	union {
		TouchInput touch;
		KeyInput key;
		AxisInput axis;
	};
};

class UIScreen : public Screen {
public:
	UIScreen();
	~UIScreen();

	void update() override;
	ScreenRenderFlags render(ScreenRenderMode mode) override;
	void deviceLost() override;
	void deviceRestored() override;

	virtual void touch(const TouchInput &touch);
	virtual bool key(const KeyInput &key);
	virtual void axis(const AxisInput &axis);

	bool UnsyncTouch(const TouchInput &touch) override;
	bool UnsyncKey(const KeyInput &key) override;
	void UnsyncAxis(const AxisInput *axes, size_t count) override;

	TouchInput transformTouch(const TouchInput &touch) override;

	virtual void TriggerFinish(DialogResult result);

	// Some useful default event handlers
	UI::EventReturn OnOK(UI::EventParams &e);
	UI::EventReturn OnCancel(UI::EventParams &e);
	UI::EventReturn OnBack(UI::EventParams &e);

protected:
	virtual void CreateViews() = 0;

	void RecreateViews() override { recreateViews_ = true; }
	bool UseVerticalLayout() const;

	UI::ViewGroup *root_ = nullptr;
	Vec3 translation_ = Vec3(0.0f);
	Vec3 scale_ = Vec3(1.0f);
	float alpha_ = 1.0f;
	bool ignoreInsets_ = false;
	bool ignoreInput_ = false;

protected:
	virtual void DrawBackground(UIContext &ui) {}
	virtual void DrawForeground(UIContext &ui) {}

	void SetupViewport();
	void DoRecreateViews();

	bool recreateViews_ = true;
	bool lastVertical_;

private:
	std::mutex eventQueueLock_;
	std::deque<QueuedEvent> eventQueue_;
};

class UIDialogScreen : public UIScreen {
public:
	UIDialogScreen() : UIScreen(), finished_(false) {}
	bool key(const KeyInput &key) override;
	void sendMessage(UIMessage message, const char *value) override;

private:
	bool finished_;
};

class PopupScreen : public UIDialogScreen {
public:
	PopupScreen(std::string_view title, std::string_view button1 = "", std::string_view button2 = "");

	virtual void CreatePopupContents(UI::ViewGroup *parent) = 0;
	void CreateViews() override;
	bool isTransparent() const override { return true; }
	void touch(const TouchInput &touch) override;
	bool key(const KeyInput &key) override;

	void TriggerFinish(DialogResult result) override;

	void SetPopupOrigin(const UI::View *view);
	void SetPopupOffset(float y) { offsetY_ = y; }

	void SetAlignTop(bool alignTop) { alignTop_ = alignTop; }

	void SetHasDropShadow(bool has) { hasDropShadow_ = has; }

	// For the postproc param sliders on DisplayLayoutScreen
	bool wantBrightBackground() const override { return !hasDropShadow_; }

protected:
	virtual bool FillVertical() const { return false; }
	virtual UI::Size PopupWidth() const { return 550; }
	virtual bool ShowButtons() const { return true; }
	virtual bool CanComplete(DialogResult result) { return true; }
	virtual void OnCompleted(DialogResult result) {}
	virtual bool HasTitleBar() const { return true; }
	const std::string &Title() { return title_; }

	void update() override;

private:
	UI::LinearLayout *box_ = nullptr;
	UI::Button *defaultButton_ = nullptr;
	std::string title_;
	std::string button1_;
	std::string button2_;

	enum {
		FRAMES_LEAD_IN = 6,
		FRAMES_LEAD_OUT = 4,
	};

	int frames_ = 0;
	int finishFrame_ = -1;
	DialogResult finishResult_ = DR_CANCEL;
	bool hasPopupOrigin_ = false;
	Point2D popupOrigin_;
	float offsetY_ = 0.0f;
	bool alignTop_ = false;

	bool hasDropShadow_ = true;
};
