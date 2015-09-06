#include "base/display.h"

int dp_xres;
int dp_yres;

int pixel_xres;
int pixel_yres;

int g_dpi = 1;  // will be overwritten
float g_dpi_scale = 1.0f;
float pixel_in_dps = 1.0f;
float display_hz = 60.0f;