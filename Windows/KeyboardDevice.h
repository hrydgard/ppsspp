#pragma once

#include <map>
#include "InputDevice.h"

extern std::map<int, int> windowsTransTable;

class KeyboardDevice : public InputDevice {
public:
	virtual int UpdateState();
	virtual bool IsPad() { return false; }
	
private:
};
