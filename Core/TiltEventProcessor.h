#pragma once

namespace TiltEventProcessor {

// generates a tilt in the correct coordinate system based on
// calibration. x, y, z is the current accelerometer reading (with no conversion).
void ProcessAxisInput(const AxisInput *axes, size_t count);
void ResetTiltEvents();

// Lets you preview the amount of tilt in TiltAnalogSettingsScreen.
extern float rawTiltAnalogX;
extern float rawTiltAnalogY;

}  // namespace
