#pragma once

#include <vector>
#include "base/mutex.h"
#include "InputDevice.h"

class KeyboardDevice : public InputDevice {
public:
	virtual int UpdateState(InputState &input_state);
	virtual bool IsPad() { return false; }
	
	virtual void KeyDown(int keycode) {
		downKeys.push_back(keycode);
	}
	virtual void KeyUp(int keycode) {
		upKeys.push_back(keycode);
	}

private:
	std::vector<int> downKeys;
	std::vector<int> upKeys;
};
