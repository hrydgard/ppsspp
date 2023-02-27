#pragma once

#include "Common/Math/lin/matrix4x4.h"

// This is meant to be a framework for handling DPI scaling etc.
// For now, it just consists of these ugly globals.

// On some platforms (currently only Windows UWP) we need to manually rotate
// our rendered output to match the display. Use these to do so.
enum class DisplayRotation {
	ROTATE_0 = 0,
	ROTATE_90,
	ROTATE_180,
	ROTATE_270,
};

struct DisplayProperties {
	int dp_xres;
	int dp_yres;
	int pixel_xres;
	int pixel_yres;

	float dpi = 1.0f;  // will be overwritten with a value that makes sense.
	float dpi_scale_x = 1.0f;
	float dpi_scale_y = 1.0f;

	// pixel_xres/yres in dps
	float pixel_in_dps_x = 1.0f;
	float pixel_in_dps_y = 1.0f;

	// If DPI is overridden (like in small window mode), these are still the original DPI.
	float dpi_scale_real_x = 1.0f;
	float dpi_scale_real_y = 1.0f;

	float display_hz = 60.0f;

	DisplayRotation rotation;
	Lin::Matrix4x4 rot_matrix;

	DisplayProperties();
	void Print();
};

extern DisplayProperties g_display;

template<class T>
struct DisplayRect {
	T x, y, w, h;
};

void RotateRectToDisplay(DisplayRect<float> &rect, float rtWidth, float rtHeight);
void RotateRectToDisplay(DisplayRect<int> &rect, int rtWidth, int rtHeight);
