/*
 * Copyright (c) 2013 Sacha Refshauge
 *
 */
// Blackberry implementation of the framework.
#ifndef BLACKBERRYMAIN_H
#define BLACKBERRYMAIN_H

// Blackberry specific
#include <bps/bps.h>            // Blackberry Platform Services
#include <bps/screen.h>	        // Blackberry Window Manager
#include <bps/navigator.h>      // Invoke Service
#include <bps/virtualkeyboard.h>// Keyboard Service
#include <bps/sensor.h>         // Accelerometer
#include <sys/keycodes.h>
#include <bps/dialog.h>         // Dialog Service (Toast=BB10)
#include <bps/vibration.h>      // Vibrate Service (BB10)

// Display
#include <EGL/egl.h>
#include <screen/screen.h>
#include <sys/platform.h>
#include <GLES2/gl2.h>
#include "Core/System.h"

// Native
#include "base/timeutil.h"
#include "gfx_es2/glsl_program.h"
#include "file/zip_read.h"
#include "base/NativeApp.h"
#include "input/input_state.h"
#include "net/resolve.h"
#include "display.h"

#include "BlackberryAudio.h"

struct dispdata_t {
	int attached;
	int type;
	bool realised;
	int width, height;
};

class BlackberryMain
{
public:
	BlackberryMain(int argc, char *argv[]) :
		emulating(false),
		screen_ui(0), screen_emu(0),
		pad_buttons(0), controller_buttons(0),
		egl_cont(EGL_NO_CONTEXT)
	{
		startMain(argc, argv);
	}
	~BlackberryMain() {
		endMain();
	}
	void  startMain(int argc, char *argv[]);

private:
	void  runMain();
	void  endMain();

	void  handleInput(screen_event_t screen_event);

	char* displayTypeString(int type);
	void  startDisplays();
	void* startDisplay(int idx);
	void  realiseDisplay(int idx);
	void  unrealiseDisplay(int idx);
	void  switchDisplay(int idx);
	void  killDisplays();
	void  killDisplay(int idx, bool killContext);

	BlackberryAudio* audio;
	dispdata_t *displays;
	int dpi;
	float dpi_scale;
	int ndisplays;
	int screen_ui, screen_emu;
	bool emulating;
	int pad_buttons, controller_buttons;
	EGLDisplay* egl_disp;
	EGLSurface* egl_surf;
	EGLContext  egl_cont;
	EGLConfig   egl_conf;
	screen_context_t screen_cxt;
	screen_display_t *screen_dpy;
	screen_window_t  *screen_win;
};

#endif

