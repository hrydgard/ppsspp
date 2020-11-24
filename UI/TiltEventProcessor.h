

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


	Tilt NormalizeTilt(const Tilt &tilt);
	
	//generates a tilt in the correct coordinate system based on 
	//calibration. BaseTilt is the "base" / "zero" tilt. currentTilt is the
	//sensor tilt reading at this moment.
	//NOTE- both base and current tilt *MUST BE NORMALIZED* by calling the NormalizeTilt() function.
	Tilt GenTilt(const Tilt &baseTilt, const Tilt &currentTilt, bool invertX, bool invertY, float deadzone, float xSensitivity, float ySensitivity);

	void TranslateTiltToInput(const Tilt &tilt);

	//the next functions generate tilt events given the current Tilt amount,
	//and the deadzone radius.
	void GenerateAnalogStickEvent(const Tilt &tilt);
	void GenerateDPadEvent(const Tilt &tilt);
	void GenerateActionButtonEvent(const Tilt &tilt);
	void GenerateTriggerButtonEvent(const Tilt &tilt);

	void ResetTiltEvents();


}