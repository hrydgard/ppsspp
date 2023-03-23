#pragma once

namespace TiltEventProcessor {

// generates a tilt in the correct coordinate system based on
// calibration. x, y, z is the current accelerometer reading (with no conversion).
void ProcessTilt(bool landscape, const float calibrationAngle, float x, float y, float z, bool invertX, bool invertY, float xSensitivity, float ySensitivity);
void ResetTiltEvents();

// Lets you preview the amount of tilt in TiltAnalogSettingsScreen.
extern float rawTiltAnalogX;
extern float rawTiltAnalogY;

}  // namespace
