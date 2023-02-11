#pragma once

namespace TiltEventProcessor {

// Represents a generic Tilt event
struct Tilt {
	Tilt() : x_(0), y_(0) {}
	Tilt(const float x, const float y) : x_(x), y_(y) {}
	float x_, y_;
};

// generates a tilt in the correct coordinate system based on
// calibration. x, y, z is the current accelerometer reading (with no conversion).
Tilt GenTilt(bool landscape, const float calibrationAngle, float x, float y, float z, bool invertX, bool invertY, float xSensitivity, float ySensitivity);

void TranslateTiltToInput(const Tilt &tilt);
void ResetTiltEvents();

// Lets you preview the amount of tilt in TiltAnalogSettingsScreen.
extern float rawTiltAnalogX;
extern float rawTiltAnalogY;

}  // namespace
