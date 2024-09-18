#pragma once

#include "Common/Math/lin/vec3.h"

namespace TiltEventProcessor {

// generates a tilt in the correct coordinate system based on
// calibration. x, y, z is the current accelerometer reading (with no conversion).
void ProcessTilt(bool landscape, const float calibrationAngle, float x, float y, float z, bool invertX, bool invertY, float xSensitivity, float ySensitivity);
void ResetTiltEvents();

float GetCurrentYAngle();

// Lets you preview the amount of tilt in TiltAnalogSettingsScreen.
extern float rawTiltAnalogX;
extern float rawTiltAnalogY;

}  // namespace

namespace MouseEventProcessor {

void ProcessDelta(double now, float dx, float dy);
void MouseDeltaToAxes(double now, float *mx, float *my);

}  // namespace
