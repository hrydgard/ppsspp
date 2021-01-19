// Copyright (c) 2018- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <cstdint>
#include <string>

namespace net {
class WebSocketServer;
}

struct InputBroadcaster {
public:
	InputBroadcaster() {
	}

	void Broadcast(net::WebSocketServer *ws);

private:
	struct Analog {
		float x = 0.0f;
		float y = 0.0f;

		bool Equals(const Analog &other) const {
			return x == other.x && y == other.y;
		}
		std::string Event(const char *stick);
	};

	int lastCounter_ = -1;
	uint32_t lastButtons_ = 0;
	Analog lastAnalog_[2];
};
