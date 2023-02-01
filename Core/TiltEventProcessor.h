#pragma once

namespace TiltEventProcessor {

enum TiltTypes{
	TILT_NULL = 0,
	TILT_ANALOG,
	TILT_DPAD,
	TILT_ACTION_BUTTON,
	TILT_TRIGGER_BUTTON,
};


//Represents a generic Tilt event
struct Tilt {
	Tilt() : x_(0), y_(0) {}
	Tilt(const float x, const float y) : x_(x), y_(y) {}

	float x_, y_;
};
	
// generates a tilt in the correct coordinate system based on
// calibration. x, y, z is the current accelerometer reading.
// NOTE- both base and current tilt *MUST BE NORMALIZED* by calling the NormalizeTilt() function.
Tilt GenTilt(bool landscape, const float calibrationAngle, float x, float y, float z, bool invertX, bool invertY, float deadzone, float xSensitivity, float ySensitivity);

void TranslateTiltToInput(const Tilt &tilt);

void ResetTiltEvents();

// Lets you preview the amount of tilt in TiltAnalogSettingsScreen.
extern float rawTiltAnalogX;
extern float rawTiltAnalogY;

}  // namespace
