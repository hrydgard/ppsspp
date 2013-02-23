#pragma once

#include "NativeApp.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

/**
 * Shared state for our app.
 */
struct ENGINE
{
	struct APP_INSTANCE* app;

	int			render;
	EGLDisplay	display;
	EGLSurface	surface;
	EGLContext	context;
	int			width;
	int			height;

};

int engine_gl_init( struct ENGINE* engine, int native_format );
void engine_gl_term( struct ENGINE* engine );
void engine_gl_swapbuffers( struct ENGINE* engine );
