#pragma once

#include <list>
#include <memory>

#include "../Common/CommonTypes.h"
#include "../Core/HLE/sceCtrl.h"

struct InputState;

class InputDevice
{
public:
	virtual int UpdateState(InputState &input_state) = 0;
};

std::list<std::shared_ptr<InputDevice>> getInputDevices();
