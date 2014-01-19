#pragma once

#include <vector>
#include "base/mutex.h"
#include "InputDevice.h"

extern std::map<int, int> windowsTransTable;

class KeyboardDevice : public InputDevice {
public:
	virtual int UpdateState(InputState &input_state);
	virtual bool IsPad() { return false; }
	
private:
};
