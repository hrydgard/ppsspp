#pragma once

#include <string>
#include <list>
#include <mutex>

#include "Common/Math/geom2d.h"
#include "Common/UI/View.h"
#include "Common/UI/UIScreen.h"

class DrawBuffer;

// Data holder. This one is currently global.
class OnScreenMessages {
public:
	void Show(const std::string &message, float duration_s = 1.0f, uint32_t color = 0xFFFFFF, int icon = -1, bool checkUnique = true, const char *id = nullptr);
	void ShowOnOff(const std::string &message, bool b, float duration_s = 1.0f, uint32_t color = 0xFFFFFF, int icon = -1);
	bool IsEmpty() const { return messages_.empty(); }

	void Lock() {
		mutex_.lock();
	}
	void Unlock() {
		mutex_.unlock();
	}

	void Clean();

	struct Message {
		int icon;
		uint32_t color;
		std::string text;
		const char *id;
		double endTime;
		double duration;
	};
	const std::list<Message> &Messages() { return messages_; }

private:
	std::list<Message> messages_;
	std::mutex mutex_;
};

// Infrastructure for rendering overlays.

class OnScreenMessagesView : public UI::InertView {
public:
	OnScreenMessagesView(UI::LayoutParams *layoutParams = nullptr) : UI::InertView(layoutParams) {}
	void Draw(UIContext &dc) override;
	std::string DescribeText() const override;
};

class OSDOverlayScreen : public UIScreen {
public:
	const char *tag() const override { return "OSDOverlayScreen"; }
	void CreateViews() override;
};

extern OnScreenMessages osm;
