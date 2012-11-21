#include "../Common/CommonTypes.h"
#include "../Core/HLE/sceCtrl.h"

#pragma once
class InputDevice
{
public:
	virtual int UpdateState() = 0;
};

#include <windows.h>
#include <list>
#include <memory>
std::list<std::shared_ptr<InputDevice>> getInputDevices();
