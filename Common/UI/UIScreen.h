#pragma once

#include <mutex>
#include <string>
#include <deque>

#include "Common/Math/lin/vec3.h"
#include "Common/UI/Screen.h"
#include "Common/UI/ViewGroup.h"

using namespace Lin;

enum class ViewLayoutMode;

class I18NCategory;
namespace Draw {
	class DrawContext;
}

class UIScreen : public Screen {
public:
	UIScreen();
	~UIScreen();

	void update() override;
	ScreenRenderFlags render(ScreenRenderMode mode) override;
	void deviceLost() override;
	void deviceRestored(Draw::DrawContext *draw) override;

	bool touch(const TouchInput &touch) override;
	bool key(const KeyInput &key) override;
	void axis(const AxisInput &axis) override;

	TouchInput transformTouch(const TouchInput &touch) override;

	virtual void TriggerFinish(DialogResult result);

	// Some useful default event handlers
	void OnOK(UI::EventParams &e);
	void OnCancel(UI::EventParams &e);
	void OnBack(UI::EventParams &e);

	virtual UI::Margins RootMargins() const { return UI::Margins(0); }

	virtual void focusChanged(ScreenFocusChange focusChange) override {
		Screen::focusChanged(focusChange);
	}

	virtual bool AllowKeyboardNavigation() const { return true; }

protected:
	virtual void CreateViews() = 0;

	Bounds GetLayoutBounds(UIContext &dc) const;

	void RecreateViews() override { recreateViews_ = true; }
	DeviceOrientation GetDeviceOrientation() const;
	bool IsOnTop() const;
	virtual ViewLayoutMode LayoutMode() const { return ViewLayoutMode::ApplyInsets; }
	virtual bool UseImmersiveMode() const { return false; }

	UI::ViewGroup *root_ = nullptr;
	Vec3 translation_ = Vec3(0.0f);
	Vec3 scale_ = Vec3(1.0f);
	float alpha_ = 1.0f;
	bool ignoreInput_ = false;

protected:
	virtual void DrawBackground(UIContext &ui) {}
	virtual void DrawForeground(UIContext &ui) {}

	void DoRecreateViews();

	bool recreateViews_ = true;
	DeviceOrientation lastOrientation_ = DeviceOrientation::Landscape;
};

class UIDialogScreen : public UIScreen {
public:
	UIDialogScreen() : UIScreen(), finished_(false) {}
	~UIDialogScreen() override;
	void update() override {
		UIScreen::update();
		firstFrame_ = false;
	}
	bool key(const KeyInput &key) override;
	void sendMessage(UIMessage message, const char *value) override;

protected:
	bool firstFrame_ = true;  // Since back button can toggle this screen, we need to make sure we don't immediately pop it on the first frame.
private:
	bool finished_;
};
