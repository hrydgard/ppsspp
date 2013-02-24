#include "NativeGL.h"

#define TAG "NativeGL"

int engine_gl_init( struct ENGINE* engine, int native_format )
{
	// initialize OpenGL ES and EGL
	EGLint attribs[] =
	{
		EGL_BLUE_SIZE, 5,
		EGL_GREEN_SIZE, 6,
		EGL_RED_SIZE, 5,
		EGL_ALPHA_SIZE, 0,
//		EGL_DEPTH_SIZE, 0,
//		EGL_STENCIL_SIZE, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_NONE
	};

	if( native_format == WINDOW_FORMAT_RGBA_8888 || native_format == WINDOW_FORMAT_RGBX_8888 )
	{
		attribs[1] = 8;
		attribs[3] = 8;
		attribs[5] = 8;
		attribs[7] = 8;
	}

	EGLint w, h, format;
	EGLint numConfigs;
	EGLConfig config;
	EGLSurface surface;
	EGLContext context;

	EGLDisplay display = eglGetDisplay( EGL_DEFAULT_DISPLAY );

	if( display == EGL_NO_DISPLAY )
	{
		LOGW( "!!! NO DISPLAY !!! eglGetDisplay" );
	}

	EGLint major, minor;
	if( eglInitialize( display, &major, &minor ) == EGL_FALSE )
	{
		LOGE( "eglInitialize() failed : %d", eglGetError() );
	}

	/*if( eglBindAPI( EGL_OPENGL_ES_API ) == EGL_FALSE )
	{
		LOGE( "eglBindAPI() failed : %d", eglGetError() );
	}*/

	if( eglChooseConfig( display, attribs, &config, 1, &numConfigs ) == EGL_FALSE )
	{
		LOGE( "eglChooseConfig() failed : %d", eglGetError() );
	}

	eglGetConfigAttrib( display, config, EGL_NATIVE_VISUAL_ID, &format );

	ANativeWindow_setBuffersGeometry( engine->app->window, 0, 0, format );

	surface = eglCreateWindowSurface( display, config, engine->app->window, 0 );
	if( surface == EGL_NO_SURFACE )
	{
		LOGE( "eglCreateWindowSurface() failed : %d ( %x : %x )", eglGetError(), display, engine->app->window );
		return -1;
	}

	static EGLint contextAttrs[] =
	{
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	context = eglCreateContext( display, config, EGL_NO_CONTEXT, contextAttrs );

	if( surface == EGL_NO_CONTEXT )
	{
		LOGE( "eglCreateContext() failed : %d", eglGetError() );
		return -1;
	}

	if( eglMakeCurrent( display, surface, surface, context ) == EGL_FALSE )
	{
		LOGE( "eglMakeCurrent() failed : %d", eglGetError() );
		return -1;
	}

	eglQuerySurface( display, surface, EGL_WIDTH, &w );
	eglQuerySurface( display, surface, EGL_HEIGHT, &h );

	engine->display	= display;
	engine->context	= context;
	engine->surface	= surface;
	engine->width	= w;
	engine->height	= h;

	// Initialize GL state.
	//glDisable( GL_CULL_FACE );
	//glDisable( GL_DEPTH_TEST );
	glViewport( 0, 0, w, h );

	return 0;
}

void engine_gl_swapbuffers( struct ENGINE* engine )
{
	eglSwapBuffers(engine->display,engine->surface);
}

void engine_gl_term( struct ENGINE* engine )
{
	LOGI( "engine_term_display" );

	if( engine->display != EGL_NO_DISPLAY )
	{
		if( eglMakeCurrent( engine->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT ) == EGL_FALSE )
		{
			LOGE( "term eglMakeCurrent() failed : %d", eglGetError() );
		}

		if( engine->context != EGL_NO_CONTEXT )
		{
			if( eglDestroyContext( engine->display, engine->context ) == EGL_FALSE )
			{
				LOGE( "term eglDestroyContext() failed : %d", eglGetError() );
			}
		}

		if( engine->surface != EGL_NO_SURFACE )
		{
			if( eglDestroySurface( engine->display, engine->surface ) == EGL_FALSE )
			{
				LOGE( "term eglDestroySurface() failed : %d", eglGetError() );
			}
		}

		if( eglTerminate( engine->display ) == EGL_FALSE )
		{
			LOGE( "term eglTerminate() failed : %d", eglGetError() );
		}
	}

	engine->render	= 0;
	engine->display	= EGL_NO_DISPLAY;
	engine->context	= EGL_NO_CONTEXT;
	engine->surface	= EGL_NO_SURFACE;
}

