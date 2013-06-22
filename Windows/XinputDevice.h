#pragma once
#include "InputDevice.h"
#include "Xinput.h"

struct RawInputState;

class XinputDevice :
	public InputDevice
{
public:
	XinputDevice();
	virtual int UpdateState(InputState &input_state);
	virtual bool IsPad() { return true; }
	int UpdateRawStateSingle(RawInputState &rawState);
private:
	void ApplyDiff(XINPUT_STATE &state, InputState &input_state);
	int gamepad_idx;
	int check_delay;
	XINPUT_STATE prevState;
};

