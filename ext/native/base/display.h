#pragma once

#include "math/lin/matrix4x4.h"

// This is meant to be a framework for handling DPI scaling etc.
// For now, it just consists of these ugly globals.

extern int dp_xres;
extern int dp_yres;
extern int pixel_xres;
extern int pixel_yres;

extern float g_dpi;
extern float g_dpi_scale_x;
extern float g_dpi_scale_y;
extern float g_dpi_scale_real_x;
extern float g_dpi_scale_real_y;
extern float pixel_in_dps_x;
extern float pixel_in_dps_y;
extern float display_hz;

// On some platforms (currently only Windows UWP) we need to manually rotate
// our rendered output to match the display. Use these to do so.
enum class DisplayRotation {
	ROTATE_0 = 0,
	ROTATE_90,
	ROTATE_180,
	ROTATE_270,
};

extern DisplayRotation g_display_rotation;
extern Matrix4x4 g_display_rot_matrix;
