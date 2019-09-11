#include <algorithm>
#include "base/display.h"

int dp_xres;
int dp_yres;

int pixel_xres;
int pixel_yres;

float g_dpi = 1.0f;  // will be overwritten with a value that makes sense.
float g_dpi_scale_x = 1.0f;
float g_dpi_scale_y = 1.0f;
float g_dpi_scale_real_x = 1.0f;
float g_dpi_scale_real_y = 1.0f;
float pixel_in_dps_x = 1.0f;
float pixel_in_dps_y = 1.0f;
float display_hz = 60.0f;

DisplayRotation g_display_rotation;
Matrix4x4 g_display_rot_matrix;

void RotateRectToDisplay(FRect &rect, float curRTWidth, float curRTHeight) {
	switch (g_display_rotation) {
	case DisplayRotation::ROTATE_180:
		rect.x = curRTWidth - rect.w - rect.x;
		rect.y = curRTHeight - rect.h - rect.y;
		break;
	case DisplayRotation::ROTATE_90: {
		// Note that curRTWidth_ and curRTHeight_ are "swapped"!
		float origX = rect.x;
		float origY = rect.y;
		float rtw = curRTHeight;
		float rth = curRTWidth;
		rect.x = rth - rect.h - origY;
		rect.y = origX;
		std::swap(rect.w, rect.h);
		break;
	}
	case DisplayRotation::ROTATE_270: {
		float origX = rect.x;
		float origY = rect.y;
		float rtw = curRTHeight;
		float rth = curRTWidth;
		rect.x = origY;
		rect.y = rtw - rect.w - origX;
		std::swap(rect.w, rect.h);
		break;
	}
	case DisplayRotation::ROTATE_0:
	default:
		break;
	}
}
