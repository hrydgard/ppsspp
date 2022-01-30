// Copyright (c) 2012- PPSSPP Project.

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

#include <algorithm>
#include <mutex>
#include <vector>
#include "Core/HW/Display.h"

// Called when vblank happens (like an internal interrupt.)  Not part of state, should be static.
static std::mutex listenersLock;
static std::vector<VblankCallback> vblankListeners;
typedef std::pair<FlipCallback, void *> FlipListener;
static std::vector<FlipListener> flipListeners;

void DisplayFireVblank() {
	std::vector<VblankCallback> toCall = [] {
		std::lock_guard<std::mutex> guard(listenersLock);
		return vblankListeners;
	}();

	for (VblankCallback cb : toCall) {
		cb();
	}
}

void DisplayFireFlip() {
	std::vector<FlipListener> toCall = [] {
		std::lock_guard<std::mutex> guard(listenersLock);
		return flipListeners;
	}();

	for (FlipListener cb : toCall) {
		cb.first(cb.second);
	}
}

void __DisplayListenVblank(VblankCallback callback) {
	std::lock_guard<std::mutex> guard(listenersLock);
	vblankListeners.push_back(callback);
}

void __DisplayListenFlip(FlipCallback callback, void *userdata) {
	std::lock_guard<std::mutex> guard(listenersLock);
	flipListeners.push_back(std::make_pair(callback, userdata));
}

void __DisplayForgetFlip(FlipCallback callback, void *userdata) {
	std::lock_guard<std::mutex> guard(listenersLock);
	flipListeners.erase(std::remove_if(flipListeners.begin(), flipListeners.end(), [&](FlipListener item) {
		return item.first == callback && item.second == userdata;
	}), flipListeners.end());
}

void DisplayHWInit() {

}

void DisplayHWShutdown() {
	std::lock_guard<std::mutex> guard(listenersLock);
	vblankListeners.clear();
	flipListeners.clear();
}
