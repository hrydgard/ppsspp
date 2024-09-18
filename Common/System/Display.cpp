#include <cstdio>

#include "Common/System/Display.h"
#include "Common/Math/math_util.h"

DisplayProperties g_display;

template<class T>
void RotateRectToDisplayImpl(DisplayRect<T> &rect, T curRTWidth, T curRTHeight) {
	switch (g_display.rotation) {
	case DisplayRotation::ROTATE_180:
		rect.x = curRTWidth - rect.w - rect.x;
		rect.y = curRTHeight - rect.h - rect.y;
		break;
	case DisplayRotation::ROTATE_90: {
		// Note that curRTWidth_ and curRTHeight_ are "swapped"!
		T origX = rect.x;
		T origY = rect.y;
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

DisplayProperties::DisplayProperties() {
	rot_matrix.setIdentity();
}

void DisplayProperties::Print() {
	printf("dp_xres/yres: %d, %d\n", dp_xres, dp_yres);
	printf("pixel_xres/yres: %d, %d\n", pixel_xres, pixel_yres);

	printf("dpi, x, y: %f, %f, %f\n", dpi, dpi_scale_x, dpi_scale_y);
	printf("pixel_in_dps: %f, %f\n", pixel_in_dps_x, pixel_in_dps_y);

	printf("dpi_real: %f, %f\n", dpi_scale_real_x, dpi_scale_real_y);
	printf("display_hz: %f\n", display_hz);

	printf("rotation: %d\n", (int)rotation);
	rot_matrix.print();
}
