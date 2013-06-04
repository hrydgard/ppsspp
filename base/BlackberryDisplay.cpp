/*
 * Copyright (c) 2013 Sacha Refshauge
 *
 */
// Blackberry implementation of the framework.
#include "BlackberryMain.h"

char* BlackberryMain::displayTypeString(int type)
{
	switch (type)
	{
        case SCREEN_DISPLAY_TYPE_INTERNAL:
			return "Internal"; break;
		case SCREEN_DISPLAY_TYPE_COMPOSITE:
			return "Composite"; break;
		case SCREEN_DISPLAY_TYPE_DVI:
			return "DVI"; break;
		case SCREEN_DISPLAY_TYPE_HDMI:
			return "HDMI"; break;
		case SCREEN_DISPLAY_TYPE_DISPLAYPORT:
			return "DisplayPort"; break;
		case SCREEN_DISPLAY_TYPE_OTHER:
		default:
			break;
	}
	return "Unknown Port";
}

void BlackberryMain::startDisplays() {
	int num_configs;
	EGLint attrib_list[]= {
		EGL_RED_SIZE,        8,
		EGL_GREEN_SIZE,      8,
		EGL_BLUE_SIZE,       8,
		EGL_DEPTH_SIZE,      24,
		EGL_STENCIL_SIZE,    8,
		EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE};

	const EGLint attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

	screen_get_context_property_iv(screen_cxt, SCREEN_PROPERTY_DISPLAY_COUNT, &ndisplays);
	egl_disp = (EGLDisplay*)calloc(ndisplays, sizeof(EGLDisplay));
	egl_surf = (EGLSurface*)calloc(ndisplays, sizeof(EGLSurface));
	displays = (dispdata_t*)calloc(ndisplays, sizeof(dispdata_t));
	screen_win = (screen_window_t *)calloc(ndisplays, sizeof(screen_window_t ));
	screen_dpy = (screen_display_t*)calloc(ndisplays, sizeof(screen_display_t));
	screen_get_context_property_pv(screen_cxt, SCREEN_PROPERTY_DISPLAYS, (void **)screen_dpy);

	// Common data
	int usage = SCREEN_USAGE_ROTATION | SCREEN_USAGE_OPENGL_ES2;
	int format = SCREEN_FORMAT_RGBX8888;
	int sensitivity = SCREEN_SENSITIVITY_ALWAYS;

	// Initialise every display
	for (int i = 0; i < ndisplays; i++) {
		screen_get_display_property_iv(screen_dpy[i], SCREEN_PROPERTY_TYPE, &(displays[i].type));
		screen_get_display_property_iv(screen_dpy[i], SCREEN_PROPERTY_ATTACHED, &(displays[i].attached));

		screen_create_window(&screen_win[i], screen_cxt);
		screen_set_window_property_iv(screen_win[i], SCREEN_PROPERTY_FORMAT, &format);
		screen_set_window_property_iv(screen_win[i], SCREEN_PROPERTY_USAGE, &usage);
		screen_set_window_property_iv(screen_win[i], SCREEN_PROPERTY_SENSITIVITY, &sensitivity);
		screen_set_window_property_pv(screen_win[i], SCREEN_PROPERTY_DISPLAY, (void **)&screen_dpy[i]);

		egl_disp[i] = eglGetDisplay((EGLNativeDisplayType)i);
		eglInitialize(egl_disp[i], NULL, NULL);
		if (egl_cont == EGL_NO_CONTEXT) {
			eglChooseConfig(egl_disp[0], attrib_list, &egl_conf, 1, &num_configs);
			egl_cont = eglCreateContext(egl_disp[0], egl_conf, EGL_NO_CONTEXT, attributes);
		}

		fprintf(stderr, "Display %i: %s, %s\n", i, displayTypeString(displays[i].type), displays[i].attached ? "Attached" : "Detached");
		if (displays[i].attached)
			realiseDisplay(i);
	}
	screen_get_display_property_iv(screen_dpy[0], SCREEN_PROPERTY_DPI, &dpi); // Only internal display has DPI
	dpi_scale = ((pixel_xres == pixel_yres) ? 300.0f : 213.6f) / dpi;
	switchDisplay(screen_ui);
}

void BlackberryMain::realiseDisplay(int idx) {
	const EGLint egl_surfaceAttr[] = { EGL_RENDER_BUFFER, EGL_BACK_BUFFER, EGL_NONE };

	int size[2] = { atoi(getenv("WIDTH")), atoi(getenv("HEIGHT")) };
	if (idx != 0)
		screen_get_display_property_iv(screen_dpy[idx], SCREEN_PROPERTY_SIZE, size);
	displays[idx].width = size[0];
	displays[idx].height = size[1];
	screen_set_window_property_iv(screen_win[idx], SCREEN_PROPERTY_BUFFER_SIZE, size);
	screen_create_window_buffers(screen_win[idx], 2); // Double buffered
	fprintf(stderr, "Display %i realised with %ix%i\n", idx, size[0], size[1]);

	egl_surf[idx] = eglCreateWindowSurface(egl_disp[idx], egl_conf, screen_win[idx], egl_surfaceAttr);

	// Only enable for devices with hardware QWERTY, 1:1 aspect ratio
	if ((pixel_xres == pixel_yres) && displays[idx].type != SCREEN_DISPLAY_TYPE_INTERNAL)
	{
		screen_emu = idx;
		if (emulating)
			switchDisplay(idx);
	}

	displays[idx].realised = true;
}

void BlackberryMain::unrealiseDisplay(int idx) {
	if (displays[idx].type != SCREEN_DISPLAY_TYPE_INTERNAL) // Always true, only external can unrealise
	{
		screen_emu = screen_ui;
		if (emulating)
			switchDisplay(screen_ui);
	}
	killDisplay(idx, false);
	displays[idx].realised = false;
}

void BlackberryMain::switchDisplay(int idx) {
	static int screen_curr = -1;
	if (idx != screen_curr) {
		pixel_xres = displays[idx].width;
		pixel_yres = displays[idx].height;
		dp_xres = (int)(pixel_xres * dpi_scale);
		dp_yres = (int)(pixel_yres * dpi_scale);
		screen_curr = idx;
		eglMakeCurrent(egl_disp[idx], egl_surf[idx], egl_surf[idx], egl_cont);
	}
	if (emulating) {
		PSP_CoreParameter().pixelWidth   = pixel_xres;
		PSP_CoreParameter().pixelHeight  = pixel_yres;
		PSP_CoreParameter().outputWidth  = dp_xres;
		PSP_CoreParameter().outputHeight = dp_yres;
	}
}

void BlackberryMain::killDisplays() {
	for (int i = 0; i < ndisplays; i++) {
		killDisplay(i, true);
	}
	if (egl_cont != EGL_NO_CONTEXT) {
		eglDestroyContext(egl_disp[0], egl_cont);
		egl_cont = EGL_NO_CONTEXT;
	}
	free(egl_disp);
	free(egl_surf);
	eglReleaseThread();
	free(screen_dpy);
}

void BlackberryMain::killDisplay(int idx, bool killContext) {
	if (egl_disp[idx] != EGL_NO_DISPLAY) {
		if (killContext)
			eglMakeCurrent(egl_disp[idx], EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (egl_surf[idx] != EGL_NO_SURFACE) {
			eglDestroySurface(egl_disp[idx], egl_surf[idx]);
			egl_surf[idx] = EGL_NO_SURFACE;
		}
		if (killContext)
		{
			eglTerminate(egl_disp[idx]);
			egl_disp[idx] = EGL_NO_DISPLAY;
		}
	}
	if (killContext && screen_win[idx] != NULL) {
		screen_destroy_window(screen_win[idx]);
		screen_win[idx] = NULL;
	}
	screen_destroy_window_buffers(screen_win[idx]);
}

