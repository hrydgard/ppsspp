#include "base/display.h"
#include "math/math_util.h"

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

template<class T>
void RotateRectToDisplayImpl(DisplayRect<T> &rect, T curRTWidth, T curRTHeight) {
	switch (g_display_rotation) {
	case DisplayRotation::ROTATE_180:
		rect.x = curRTWidth - rect.w - rect.x;
		rect.y = curRTHeight - rect.h - rect.y;
		break;
	case DisplayRotation::ROTATE_90: {
		// Note that curRTWidth_ and curRTHeight_ are "swapped"!
		T origX = rect.x;
		T origY = rect.y;
		T rtw = curRTHeight;
		T rth = curRTWidth;
		rect.x = clamp_value(rth - rect.h - origY, T{}, curRTHeight);
		rect.y = origX;
		T temp = rect.w;
		rect.w = rect.h;
		rect.h = temp;
		break;
	}
	case DisplayRotation::ROTATE_270: {
		T origX = rect.x;
		T origY = rect.y;
		T rtw = curRTHeight;
		T rth = curRTWidth;
		rect.x = origY;
		rect.y = clamp_value(rtw - rect.w - origX, T{}, curRTWidth);
		T temp = rect.w;
		rect.w = rect.h;
		rect.h = temp;
		break;
	}
	case DisplayRotation::ROTATE_0:
	default:
		break;
	}
}

void RotateRectToDisplay(DisplayRect<int> &rect, int curRTWidth, int curRTHeight) {
	RotateRectToDisplayImpl<int>(rect, curRTWidth, curRTHeight);
}

void RotateRectToDisplay(DisplayRect<float> &rect, float curRTWidth, float curRTHeight) {
	RotateRectToDisplayImpl<float>(rect, curRTWidth, curRTHeight);
}
