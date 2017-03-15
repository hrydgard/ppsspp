#pragma once

#include "InputDevice.h"
#include "Xinput.h"


class XinputDevice : public InputDevice {
public:
	XinputDevice();
	~XinputDevice();
	virtual int UpdateState();
	virtual bool IsPad() { return true; }

private:
	void UpdatePad(int pad, const XINPUT_STATE &state);
	void ApplyButtons(int pad, const XINPUT_STATE &state);
	int check_delay[4]{};
	XINPUT_STATE prevState[4]{};
	u32 prevButtons[4]{};
};
