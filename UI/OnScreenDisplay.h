#pragma once

#include <string>
#include <list>

#include "base/basictypes.h"
#include "Common/StdMutex.h"

class DrawBuffer;

class OnScreenMessages {
public:
	void Show(const std::string &message, float duration_s = 1.0f, uint32_t color = 0xFFFFFF, int icon = -1, bool checkUnique = false);
	void ShowOnOff(const std::string &message, bool b, float duration_s = 1.0f, uint32_t color = 0xFFFFFF, int icon = -1);
	void Draw(DrawBuffer &draw);
	bool IsEmpty() const { return messages_.empty(); }

private:
	struct Message {
		int icon;
		uint32_t color;
		std::string text;
		double endTime;
		double duration;
	};

	std::list<Message> messages_;
	std::recursive_mutex mutex_;
};

extern OnScreenMessages osm;
