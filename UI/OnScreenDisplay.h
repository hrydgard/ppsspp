#pragma once

#include <string>
#include <list>

#include "base/basictypes.h"
#include "math/geom2d.h"
#include "Common/StdMutex.h"

#include "ui/view.h"

class DrawBuffer;

class OnScreenMessages {
public:
	void Show(const std::string &message, float duration_s = 1.0f, uint32_t color = 0xFFFFFF, int icon = -1, bool checkUnique = true);
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
		double endTime;
		double duration;
	};
	const std::list<Message> &Messages() { return messages_; }

private:

	std::list<Message> messages_;
	std::recursive_mutex mutex_;
};

class OnScreenMessagesView : public UI::InertView {
public:
	OnScreenMessagesView(UI::LayoutParams *layoutParams = nullptr) : UI::InertView(layoutParams) {}
	void Draw(UIContext &dc);
};

extern OnScreenMessages osm;
