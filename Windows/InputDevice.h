#include "../Common/CommonTypes.h"
#include "../Core/HLE/sceCtrl.h"

#pragma once
class InputDevice
{
public:
	virtual int UpdateState() = 0;
};

