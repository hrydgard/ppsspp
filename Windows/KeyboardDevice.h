#pragma once
#include "InputDevice.h"

class KeyboardDevice : public InputDevice {
public:
	virtual int UpdateState(InputState &input_state);
};
