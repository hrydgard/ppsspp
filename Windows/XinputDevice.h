#pragma once

#include <XInput.h>
#include "Core/HLE/sceCtrl.h"
#include "Windows/InputDevice.h"

class XinputDevice final : public InputDevice {
public:
	XinputDevice();
	~XinputDevice();
	int UpdateState() override;

private:
	void UpdatePad(int pad, const XINPUT_STATE &state, XINPUT_VIBRATION &vibration);
	void ReleaseAllKeys(int pad);
	void ApplyButtons(int pad, const XINPUT_STATE &state);
	void ApplyVibration(int pad, XINPUT_VIBRATION &vibration);

	bool connected_[4]{};
	int checkDelayUpdates_[4]{};
	XINPUT_STATE prevState_[4]{};
	XINPUT_VIBRATION prevVibration_[4]{};
	double prevVibrationTime_ = 0.0;
	float prevAxisValue_[4][6]{};
	bool notified_[XUSER_MAX_COUNT]{};
	u32 prevButtons_[4]{};
	double newVibrationTime_ = 0.0;
};
