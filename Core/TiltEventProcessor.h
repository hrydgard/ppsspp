#pragma once

#include "Common/Math/lin/vec3.h"

namespace TiltEventProcessor {

// generates a tilt in the correct coordinate system based on
// calibration. x, y, z is the current accelerometer reading (with no conversion).
void ProcessTilt(bool landscape, const float calibrationAngle, float x, float y, float z, bool invertX, bool invertY, float xSensitivity, float ySensitivity);
void ResetTiltEvents();

// Steadies the gravity vector by integrating gyroscope rates, and only slowly pulling the result toward the
// accelerometer. Both use the same device axes. Rates are in rad/s, the timestamp in seconds.
// ProcessGyro returns false until there's an accelerometer reading to start from.
void ProcessAccelerometerForFusion(float x, float y, float z);
bool ProcessGyro(float x, float y, float z, double timestamp, Lin::Vec3 *gravity);
// True if gyro data arrived recently, in which case the accelerometer shouldn't drive tilt directly.
bool GyroActive();

float GetCurrentYAngle();

// Lets you preview the amount of tilt in TiltAnalogSettingsScreen.
extern float rawTiltAnalogX;
extern float rawTiltAnalogY;

}  // namespace

namespace MouseEventProcessor {

void ProcessDelta(double now, float dx, float dy);
void MouseDeltaToAxes(double now, float *mx, float *my);

}  // namespace
