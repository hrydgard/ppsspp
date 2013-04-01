#pragma once

#include <list>
#include <memory>

#include "../Common/CommonTypes.h"

struct InputState;

class InputDevice
{
public:
	enum { UPDATESTATE_SKIP_NEXT = 0x1234};
	virtual int UpdateState(InputState &input_state) = 0;
};

std::list<std::shared_ptr<InputDevice>> getInputDevices();
