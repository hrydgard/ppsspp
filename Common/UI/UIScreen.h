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
	void deviceRestored(Draw::DrawContext *draw) override;

	virtual void touch(const TouchInput &touch);
	virtual bool key(const KeyInput &key);
	virtual void axis(const AxisInput &axis);

	bool UnsyncTouch(const TouchInput &touch) override;
	bool UnsyncKey(const KeyInput &key) override;
	void UnsyncAxis(const AxisInput *axes, size_t count) override;

	TouchInput transformTouch(const TouchInput &touch) override;

	virtual void TriggerFinish(DialogResult result);

	// Some useful default event handlers
	void OnOK(UI::EventParams &e);
	void OnCancel(UI::EventParams &e);
	void OnBack(UI::EventParams &e);

	virtual UI::Margins RootMargins() const { return UI::Margins(0); }

protected:
	virtual void CreateViews() = 0;

	void RecreateViews() override { recreateViews_ = true; }
	DeviceOrientation GetDeviceOrientation() const;

	UI::ViewGroup *root_ = nullptr;
	Vec3 translation_ = Vec3(0.0f);
	Vec3 scale_ = Vec3(1.0f);
	float alpha_ = 1.0f;
	bool ignoreInsets_ = false;
	bool ignoreBottomInset_ = false;
	bool ignoreInput_ = false;

protected:
	virtual void DrawBackground(UIContext &ui) {}
	virtual void DrawForeground(UIContext &ui) {}

	void DoRecreateViews();

	bool recreateViews_ = true;
	DeviceOrientation lastOrientation_ = DeviceOrientation::Landscape;

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
