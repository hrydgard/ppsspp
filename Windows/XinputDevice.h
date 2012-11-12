#pragma once
#include "InputDevice.h"
#include "Xinput.h"

class XinputDevice :
	public InputDevice
{
public:
	XinputDevice();
	virtual int UpdateState();
private:
	void ApplyDiff(XINPUT_STATE &state);
	int gamepad_idx;
	int check_delay;
	XINPUT_STATE prevState;
};

