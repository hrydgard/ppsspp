#pragma once
#include "InputDevice.h"
#include "Xinput.h"

class XinputDevice :
	public InputDevice
{
public:
	XinputDevice();
	virtual int UpdateState(InputState &input_state);
private:
	void ApplyDiff(XINPUT_STATE &state, InputState &input_state);
	int gamepad_idx;
	int check_delay;
	XINPUT_STATE prevState;
};

