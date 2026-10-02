#pragma once

#include <XInput.h>
#include <set>
#include "Common/Input/InputState.h"
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
	void UpdateRefinedRumble(int pad, int inputId, bool down, const InputMapping &mapping);

	struct PadData {
		bool connected = false;
		int checkDelayUpdates = 0;
		XINPUT_STATE prevState{};
		XINPUT_VIBRATION prevVibration{};
		u16 vendorId = 0;
		u16 productId = 0;
		float prevAxisValue[6]{};
		u32 prevButtons = 0;
		std::set<int> activeRumbleInputs;
		double prevVibrationTime = 0.0;
	};
	PadData padData_[XUSER_MAX_COUNT];
};
