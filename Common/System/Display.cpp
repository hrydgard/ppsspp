#include <cstdio>

#include "Common/System/Display.h"
#include "Common/Math/math_util.h"
#include "Common/GPU/MiscTypes.h"

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

bool DisplayProperties::Recalculate(int new_pixel_xres, int new_pixel_yres, float new_scale, float customScale) {
	bool px_changed = false;
	if (new_pixel_xres > 0 && pixel_xres != new_pixel_xres) {
		pixel_xres = new_pixel_xres;
		px_changed = true;
	}
	if (new_pixel_yres > 0 && pixel_yres != new_pixel_yres) {
		pixel_yres = new_pixel_yres;
		px_changed = true;
	}

	if (new_scale > 0) {
		dpi_scale_real = new_scale;
	}
	dpi_scale = dpi_scale_real / customScale;
	pixel_in_dps = 1.0f / dpi_scale;

	int new_dp_xres = (int)(pixel_xres * dpi_scale);
	int new_dp_yres = (int)(pixel_yres * dpi_scale);
	if (new_dp_xres != dp_xres || new_dp_yres != dp_yres || px_changed) {
		dp_xres = new_dp_xres;
		dp_yres = new_dp_yres;
		return true;
	} else {
		return false;
	}
}

void DisplayProperties::Print() {
	printf("dp_xres/yres: %d, %d\n", dp_xres, dp_yres);
	printf("pixel_xres/yres: %d, %d\n", pixel_xres, pixel_yres);

	printf("dpi_scale: %f\n", dpi_scale);
	printf("pixel_in_dps: %f\n", pixel_in_dps);

	printf("dpi_real: %f\n", dpi_scale_real);
	printf("display_hz: %f\n", display_hz);

	printf("rotation: %d\n", (int)rotation);
	rot_matrix.print();
}

Lin::Matrix4x4 ComputeOrthoMatrix(float xres, float yres, CoordConvention coordConvention) {
	using namespace Lin;
	// TODO: Should be able to share the y-flip logic here with the one in postprocessing/presentation, for example.
	Matrix4x4 ortho;
	switch (coordConvention) {
	case CoordConvention::Vulkan:
		ortho.setOrthoD3D(0.0f, xres, 0, yres, -1.0f, 1.0f);
		break;
	case CoordConvention::Direct3D9:
	{
		ortho.setOrthoD3D(0.0f, xres, yres, 0.0f, -1.0f, 1.0f);
		// Account for the small window adjustment.
		float half_pixel = -0.5f * g_display.dpi_scale / g_display.dpi_scale_real;
		Matrix4x4 translation;
		translation.setTranslation(Vec3(half_pixel, half_pixel, 0.0f));
		ortho = translation * ortho;
		break;
	}
	case CoordConvention::Direct3D11:
		ortho.setOrthoD3D(0.0f, xres, yres, 0.0f, -1.0f, 1.0f);
		break;
	case CoordConvention::OpenGL:
	default:
		ortho.setOrtho(0.0f, xres, yres, 0.0f, -1.0f, 1.0f);
		break;
	}
	// Compensate for rotated display if needed.
	if (g_display.rotation != DisplayRotation::ROTATE_0) {
		ortho = ortho * g_display.rot_matrix;
	}
	return ortho;
}
