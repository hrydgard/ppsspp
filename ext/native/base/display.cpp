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
