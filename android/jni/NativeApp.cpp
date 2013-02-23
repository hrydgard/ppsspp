// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "NativeApp.h"
#include "NativeGL.h"
#include "NativeJNI.h"

#include <sched.h>

#include <errno.h>

#include "base/basictypes.h"
#include "base/display.h"
#include "base/NativeApp.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "file/zip_read.h"
#include "input/input_state.h"
#include "audio/mixer.h"
#include "math/math_util.h"
#include "net/resolve.h"
#include "android/native_audio.h"

#define TAG "PPSSPP"

#define APP_STATE_NONE		0
#define APP_STATE_START		1
#define APP_STATE_RESUME	2
#define APP_STATE_PAUSE		3
#define APP_STATE_STOP		4

#define LOOPER_ID_INPUT		1

#define MSG_APP_START				1
#define MSG_APP_RESUME				2
#define MSG_APP_PAUSE				3
#define MSG_APP_SAVEINSTANCESTATE	4
#define MSG_APP_STOP				5
#define MSG_APP_DESTROYED			6
#define MSG_APP_CONFIGCHANGED		7
#define MSG_APP_LOWMEMORY			8
#define MSG_WINDOW_FOCUSCHANGED		9
#define MSG_WINDOW_CREATED			10
#define MSG_WINDOW_DESTROYED		11
#define MSG_INPUTQUEUE_CREATED		12
#define MSG_INPUTQUEUE_DESTROYED	13

float dp_xscale;
float dp_yscale;

InputState input_state;

static bool renderer_inited = false;
static bool first_lost = true;
static bool use_native_audio = true;

static uint32_t pad_buttons_async_set;
static uint32_t pad_buttons_async_clear;

static int engine_init_display( struct ENGINE* engine, int native_format )
{
	LOGI("engine_init_display()");
	if (!renderer_inited) {

		engine_gl_init(engine, native_format);

		// We default to 240 dpi and all UI code is written to assume it. (DENSITY_HIGH, like Nexus S).
		// Note that we don't compute dp_xscale and dp_yscale until later! This is so that NativeGetAppInfo
		// can change the dp resolution if it feels like it.

		g_dpi = getDPI(); //TODO: JNI this for a real value
		g_dpi_scale = (float)240.0f / (float)g_dpi;
		pixel_xres = ANativeWindow_getWidth(engine->app->window);
		pixel_yres = ANativeWindow_getHeight(engine->app->window);
		pixel_in_dps = (float)pixel_xres / (float)dp_xres;

		dp_xres = pixel_xres * g_dpi_scale;
		dp_yres = pixel_yres * g_dpi_scale;

		LOGI("Calling NativeInitGraphics();	dpi = %i, dp_xres = %i, dp_yres = %i, g_dpi_scale = %f", g_dpi, dp_xres, dp_yres, g_dpi_scale);
		NativeInitGraphics();

		dp_xscale = (float)dp_xres / pixel_xres;
		dp_yscale = (float)dp_yres / pixel_yres;

		renderer_inited = true;

	} else {
		LOGI("Calling NativeDeviceLost();");
		NativeDeviceLost();
	}
	return 0;
}

static void engine_term_display( struct ENGINE* engine )
{
	LOGI( "engine_term_display" );
	if (renderer_inited) {
		NativeShutdownGraphics();
		renderer_inited = false;
	}
	engine->render	= 0;
	engine_gl_term(engine);
}

static void engine_draw_frame( struct ENGINE* engine )
{
	if (renderer_inited) {
		{
			lock_guard guard(input_state.lock);
			input_state.pad_buttons |= pad_buttons_async_set;
			input_state.pad_buttons &= ~pad_buttons_async_clear;
			UpdateInputState(&input_state);
		}

		{
			lock_guard guard(input_state.lock);
			NativeUpdate(input_state);
		}

		{
			lock_guard guard(input_state.lock);
			EndInputState(&input_state);
		}

		NativeRender();
		engine_gl_swapbuffers(engine);

		time_update();
	} else {
		ELOG("Ended up in nativeRender even though app has quit.%s", "");
		// Shouldn't really get here.
		glClearColor(1.0, 0.0, 1.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}
}


static void AsyncDown(int padbutton)
{
	pad_buttons_async_set |= padbutton;
	pad_buttons_async_clear &= ~padbutton;
}

static void AsyncUp(int padbutton)
{
	pad_buttons_async_set &= ~padbutton;
	pad_buttons_async_clear |= padbutton;
}

static int32_t engine_handle_input( struct APP_INSTANCE* app, AInputEvent* event )
{
	struct ENGINE* engine = (struct ENGINE*)app->userData;
	if( AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION ) //Touchpad or Touchscreen
	{
		int nPointerCount	= AMotionEvent_getPointerCount( event );
		int nSourceId		= AInputEvent_getSource( event );
		int n;

		for( n = 0 ; n < nPointerCount ; ++n )
		{
			int nPointerId	= AMotionEvent_getPointerId( event, n );
			int nAction		= AMOTION_EVENT_ACTION_MASK & AMotionEvent_getAction( event );
			//struct TOUCHSTATE *touchstate = 0;

			if( nSourceId == AINPUT_SOURCE_TOUCHPAD )
			{
				//LOGI("Boop.");
				if (nAction == AMOTION_EVENT_ACTION_POINTER_DOWN)
				{
					lock_guard guard(input_state.lock);
					input_state.pad_lstick_x = AMotionEvent_getX( event, n );
					input_state.pad_lstick_y = AMotionEvent_getY( event, n );
				}
			}
			else
			{
				if( nAction == AMOTION_EVENT_ACTION_POINTER_DOWN || nAction == AMOTION_EVENT_ACTION_POINTER_UP )
				{
					int nPointerIndex = (AMotionEvent_getAction( event ) & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
					nPointerId = AMotionEvent_getPointerId( event, nPointerIndex );
				}

				float scaledX = (int)(AMotionEvent_getX( event, n ) * dp_xscale);	// why the (int) cast?
				float scaledY = (int)(AMotionEvent_getY( event, n ) * dp_yscale);
				input_state.pointer_x[nPointerId] = scaledX;
				input_state.pointer_y[nPointerId] = scaledY;

				if( nAction == AMOTION_EVENT_ACTION_DOWN || nAction == AMOTION_EVENT_ACTION_POINTER_DOWN )
				{
					input_state.pointer_down[nPointerId] = true;
					NativeTouch(nPointerId, scaledX, scaledY, 0, TOUCH_DOWN);
				}
				else if( nAction == AMOTION_EVENT_ACTION_UP || nAction == AMOTION_EVENT_ACTION_POINTER_UP || nAction == AMOTION_EVENT_ACTION_CANCEL )
				{
					input_state.pointer_down[nPointerId] = false;
					NativeTouch(nPointerId, scaledX, scaledY, 0, TOUCH_UP);
				}
				else {
					NativeTouch(nPointerId, scaledX, scaledY, 0, TOUCH_MOVE);
				}
			}
		}

		return 1;
	}
	else if( AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY ) //Physical button press
	{
		int nAction	= AKeyEvent_getAction( event );
		int metaState = AKeyEvent_getMetaState( event );
		if(nAction == AKEY_EVENT_ACTION_DOWN)
		{
			switch(AKeyEvent_getKeyCode(event))
			{
				case AKEYCODE_BACK:
					if(((metaState & AMETA_ALT_ON) == AMETA_ALT_ON))
						AsyncDown(PAD_BUTTON_B);
					else
						AsyncDown(PAD_BUTTON_BACK);
					break; 	// Back and O
				case AKEYCODE_MENU:	AsyncDown(PAD_BUTTON_MENU); break;  // Menu
				case AKEYCODE_SEARCH:	AsyncDown(PAD_BUTTON_A); break; // Search
				case AKEYCODE_DPAD_CENTER: AsyncDown(PAD_BUTTON_A); break;
				case AKEYCODE_BUTTON_X: AsyncDown(PAD_BUTTON_X); break;
				case AKEYCODE_BUTTON_Y: AsyncDown(PAD_BUTTON_Y); break;
				case AKEYCODE_DPAD_LEFT: AsyncDown(PAD_BUTTON_LEFT); break;
				case AKEYCODE_DPAD_UP: AsyncDown(PAD_BUTTON_UP); break;
				case AKEYCODE_DPAD_RIGHT: AsyncDown(PAD_BUTTON_RIGHT); break;
				case AKEYCODE_DPAD_DOWN: AsyncDown(PAD_BUTTON_DOWN); break;
				case AKEYCODE_BUTTON_L1: AsyncDown(PAD_BUTTON_LBUMPER); break;
				case AKEYCODE_BUTTON_R1: AsyncDown(PAD_BUTTON_RBUMPER); break;
				case AKEYCODE_BUTTON_START: AsyncDown(PAD_BUTTON_START); break;
				case AKEYCODE_BUTTON_SELECT: AsyncDown(PAD_BUTTON_SELECT); break;

				case AKEYCODE_VOLUME_UP:
				case AKEYCODE_VOLUME_DOWN:
					return 0; //Let the system handle volume changes
			}
		}
		else if(nAction == AKEY_EVENT_ACTION_UP)
		{
			switch(AKeyEvent_getKeyCode(event))
			{
				case AKEYCODE_BACK:
					if(((metaState & AMETA_ALT_ON) == AMETA_ALT_ON))
						AsyncUp(PAD_BUTTON_B);
					else
						AsyncUp(PAD_BUTTON_BACK);
					break; 	// Back and O
				case AKEYCODE_MENU:	AsyncUp(PAD_BUTTON_MENU); break;  // Menu
				case AKEYCODE_SEARCH:	AsyncUp(PAD_BUTTON_A); break; // Search
				case AKEYCODE_DPAD_CENTER: AsyncUp(PAD_BUTTON_A); break;
				case AKEYCODE_BUTTON_X: AsyncUp(PAD_BUTTON_X); break;
				case AKEYCODE_BUTTON_Y: AsyncUp(PAD_BUTTON_Y); break;
				case AKEYCODE_DPAD_LEFT: AsyncUp(PAD_BUTTON_LEFT); break;
				case AKEYCODE_DPAD_UP: AsyncUp(PAD_BUTTON_UP); break;
				case AKEYCODE_DPAD_RIGHT: AsyncUp(PAD_BUTTON_RIGHT); break;
				case AKEYCODE_DPAD_DOWN: AsyncUp(PAD_BUTTON_DOWN); break;
				case AKEYCODE_BUTTON_L1: AsyncUp(PAD_BUTTON_LBUMPER); break;
				case AKEYCODE_BUTTON_R1: AsyncUp(PAD_BUTTON_RBUMPER); break;
				case AKEYCODE_BUTTON_START: AsyncUp(PAD_BUTTON_START); break;
				case AKEYCODE_BUTTON_SELECT: AsyncUp(PAD_BUTTON_SELECT); break;

				case AKEYCODE_VOLUME_UP:
				case AKEYCODE_VOLUME_DOWN:
					return 0; //Let the system handle volume changes
			}
		}
		return 1;
	}
	return 0;
}

void
app_lock_queue( struct APP_INSTANCE* state )
{
	pthread_mutex_lock( &state->mutex );
}

void
app_unlock_queue( struct APP_INSTANCE* state )
{
	pthread_cond_broadcast( &state->cond );
	pthread_mutex_unlock( &state->mutex );
}

static void InitMain(struct APP_INSTANCE* app_instance)
{
	memset(&input_state, 0, sizeof(input_state));
	renderer_inited = false;
	first_lost = true;

	pad_buttons_async_set = 0;
	pad_buttons_async_clear = 0;

	std::string apkPath = getApkPath();
	std::string externalDir = getExternalDir();
	std::string user_data_path = getUserDataPath();
	std::string library_path = getLibraryPath();
	std::string installID = getInstallID();

	LOGI("APK path: %s", apkPath.c_str());
	VFSRegister("", new ZipAssetReader(apkPath.c_str(), "assets/"));

	LOGI("External storage path: %s", externalDir.c_str());

	std::string app_name;
	std::string app_nice_name;
	bool landscape;

	net::Init();
	//LOGI("net::Init() done");

	NativeGetAppInfo(&app_name, &app_nice_name, &landscape);

	LOGI("NativeInit()");
	const char *argv[2] = {app_name.c_str(), 0};
	NativeInit(1, argv, user_data_path.c_str(), externalDir.c_str(), installID.c_str());

	if (use_native_audio) {
		AndroidAudio_Init(&NativeMix, library_path);
	}
}

void
instance_app_main( struct APP_INSTANCE* app_instance )
{
	LOGI( "main entering." );

	struct ENGINE engine;

	memset( &engine, 0, sizeof(engine) );
	app_instance->userData	= &engine;
	engine.app				= app_instance;

	int run = 1;

	//run real main
	InitMain( app_instance );

	// our 'main loop'
	while( run == 1 )
	{
		// Read all pending events.
		int msg_index;
		int ident;
		int events;
		struct android_poll_source* source;

		app_lock_queue( app_instance );

		for( msg_index = 0; msg_index < app_instance->msgQueueLength; ++msg_index )
		{
			switch( app_instance->msgQueue[msg_index].msg )
			{
				case MSG_APP_START:
				{
					app_instance->activityState = app_instance->pendingActivityState;
				}
				break;
				case MSG_APP_RESUME:
				{
					app_instance->activityState = app_instance->pendingActivityState;
					ILOG("NativeResume");
					if (use_native_audio) {
						AndroidAudio_Resume();
					}
				}
				break;
				case MSG_APP_PAUSE:
				{
					app_instance->activityState = app_instance->pendingActivityState;
					ILOG("NativePause");
					engine.render = 0;
					if (use_native_audio) {
						AndroidAudio_Pause();
					}
				}
				break;
				case MSG_APP_STOP:
				{
					app_instance->activityState = app_instance->pendingActivityState;
					LOGI("NativeShutdown.");
					if (use_native_audio) {
						AndroidAudio_Shutdown();
					}
					NativeShutdown();
					LOGI("VFSShutdown.");
					VFSShutdown();
					net::Shutdown();
				}
				break;
				case MSG_APP_SAVEINSTANCESTATE:
				{
				}
				break;
				case MSG_APP_LOWMEMORY:
				{
				}
				break;
				case MSG_APP_CONFIGCHANGED:
				{
				}
				break;
				case MSG_APP_DESTROYED:
				{
					run = 0;
				}
				break;
				case MSG_WINDOW_FOCUSCHANGED:
				{
					engine.render = app_instance->msgQueue[msg_index].arg1;
				}
				break;
				case MSG_WINDOW_CREATED:
				{
					app_instance->window = app_instance->pendingWindow;

					int nWidth	= ANativeWindow_getWidth( app_instance->window );
					int nHeight	= ANativeWindow_getHeight( app_instance->window );
					int nFormat	= ANativeWindow_getFormat( app_instance->window );

					unsigned int nHexFormat = 0x00000000;
					if( nFormat == WINDOW_FORMAT_RGBA_8888 )
						nHexFormat = 0x8888;
					else if(nFormat == WINDOW_FORMAT_RGBX_8888)
						nHexFormat = 0x8880;
					else
						nHexFormat = 0x0565;

					LOGI("Window Created : Width(%d) Height(%d) Format(%04x)", nWidth, nHeight, nHexFormat);

					engine_init_display( &engine, nFormat );
					engine.render = 1;
				}
				break;
				case MSG_WINDOW_DESTROYED:
				{
					engine_term_display(&engine);
					app_instance->window = NULL;
				}
				break;
				case MSG_INPUTQUEUE_CREATED:
				case MSG_INPUTQUEUE_DESTROYED:
				{
					if( app_instance->inputQueue != NULL )
						AInputQueue_detachLooper( app_instance->inputQueue );

					app_instance->inputQueue = app_instance->pendingInputQueue;
					if( app_instance->inputQueue != NULL )
					{
						AInputQueue_attachLooper( app_instance->inputQueue, app_instance->looper, LOOPER_ID_INPUT, NULL, NULL );
					}
				}
				break;
			};
		}

		app_instance->msgQueueLength = 0;

		app_unlock_queue( app_instance );

		if (!run)
			break;

		// If not rendering, we will block forever waiting for events.
		// If rendering, we loop until all events are read, then continue
		// to draw the next frame.
		while( (ident = ALooper_pollAll( 0, NULL, &events, (void**)&source )) >= 0 )
		{
			if( ident == LOOPER_ID_INPUT )
			{
				AInputEvent* event = NULL;
				if (AInputQueue_getEvent( app_instance->inputQueue, &event ) >= 0)
				{
					if( AInputQueue_preDispatchEvent( app_instance->inputQueue, event ) )
						continue;

					int handled = engine_handle_input( app_instance, event );

					AInputQueue_finishEvent( app_instance->inputQueue, event, handled );
				}
			}
		}

		if( engine.render )
		{
			engine_draw_frame( &engine );
		}
	}

	LOGI( "main exiting." );
}

///////////////////////
///////////////////////
///////////////////////
///////////////////////
///////////////////////



///////////////
static
void
OnDestroy( ANativeActivity* activity )
{
	LOGI( "NativeActivity destroy: %p\n", activity );

	struct APP_INSTANCE* app_instance = (struct APP_INSTANCE*)activity->instance;

	pthread_mutex_lock( &app_instance->mutex );

	app_instance->msgQueue[ app_instance->msgQueueLength++ ].msg = MSG_APP_DESTROYED;

	while( !app_instance->destroyed )
	{
		LOGI( "NativeActivity destroy waiting on app thread" );
		pthread_cond_wait(&app_instance->cond, &app_instance->mutex);
	}

	pthread_mutex_unlock( &app_instance->mutex );
}

static
void
OnStart( ANativeActivity* activity )
{
	LOGI( "NativeActivity start: %p\n", activity );

	struct APP_INSTANCE* app_instance = (struct APP_INSTANCE*)activity->instance;

	pthread_mutex_lock( &app_instance->mutex );

	app_instance->pendingActivityState = APP_STATE_START;
	app_instance->msgQueue[ app_instance->msgQueueLength++ ].msg = MSG_APP_START;

	while( app_instance->activityState != app_instance->pendingActivityState )
	{
		pthread_cond_wait(&app_instance->cond, &app_instance->mutex);
	}

	app_instance->pendingActivityState = APP_STATE_NONE;

	pthread_mutex_unlock( &app_instance->mutex );
}

static
void
OnResume( ANativeActivity* activity )
{
	LOGI( "NativeActivity resume: %p\n", activity );

	struct APP_INSTANCE* app_instance = (struct APP_INSTANCE*)activity->instance;

	pthread_mutex_lock( &app_instance->mutex );

	app_instance->pendingActivityState = APP_STATE_RESUME;
	app_instance->msgQueue[ app_instance->msgQueueLength++ ].msg = MSG_APP_RESUME;

	while( app_instance->activityState != app_instance->pendingActivityState )
	{
		pthread_cond_wait(&app_instance->cond, &app_instance->mutex);
	}

	app_instance->pendingActivityState = APP_STATE_NONE;

	pthread_mutex_unlock( &app_instance->mutex );
}

static
void*
OnSaveInstanceState( ANativeActivity* activity, size_t* out_lentch )
{
	LOGI( "NativeActivity save instance state: %p\n", activity );

	return 0;
}

static
void
OnPause( ANativeActivity* activity )
{
	LOGI( "NativeActivity pause: %p\n", activity );

	struct APP_INSTANCE* app_instance = (struct APP_INSTANCE*)activity->instance;

	pthread_mutex_lock( &app_instance->mutex );

	app_instance->pendingActivityState = APP_STATE_PAUSE;
	app_instance->msgQueue[ app_instance->msgQueueLength++ ].msg = MSG_APP_PAUSE;

	while( app_instance->activityState != app_instance->pendingActivityState )
	{
		pthread_cond_wait(&app_instance->cond, &app_instance->mutex);
	}

	app_instance->pendingActivityState = APP_STATE_NONE;

	pthread_mutex_unlock( &app_instance->mutex );
}

static
void
OnStop( ANativeActivity* activity )
{
	LOGI( "NativeActivity stop: %p\n", activity );

	struct APP_INSTANCE* app_instance = (struct APP_INSTANCE*)activity->instance;

	pthread_mutex_lock( &app_instance->mutex );

	app_instance->pendingActivityState = APP_STATE_STOP;
	app_instance->msgQueue[ app_instance->msgQueueLength++ ].msg = MSG_APP_STOP;

	while( app_instance->activityState != app_instance->pendingActivityState )
	{
		pthread_cond_wait(&app_instance->cond, &app_instance->mutex);
	}

	app_instance->pendingActivityState = APP_STATE_NONE;

	pthread_mutex_unlock( &app_instance->mutex );
}

static
void
OnConfigurationChanged( ANativeActivity* activity )
{
	LOGI( "NativeActivity configuration changed: %p\n", activity );

	struct APP_INSTANCE* app_instance = (struct APP_INSTANCE*)activity->instance;

	pthread_mutex_lock( &app_instance->mutex );

	app_instance->msgQueue[ app_instance->msgQueueLength++ ].msg = MSG_APP_CONFIGCHANGED;

	pthread_mutex_unlock( &app_instance->mutex );
}

static
void
OnLowMemory( ANativeActivity* activity )
{
	LOGI( "NativeActivity low memory: %p\n", activity );

	struct APP_INSTANCE* app_instance = (struct APP_INSTANCE*)activity->instance;

	pthread_mutex_lock( &app_instance->mutex );

	app_instance->msgQueue[ app_instance->msgQueueLength++ ].msg = MSG_APP_CONFIGCHANGED;

	pthread_mutex_unlock( &app_instance->mutex );
}

static
void
OnWindowFocusChanged( ANativeActivity* activity, int focused )
{
	LOGI( "NativeActivity window focus changed: %p -- %d\n", activity, focused );

	struct APP_INSTANCE* app_instance = (struct APP_INSTANCE*)activity->instance;

	pthread_mutex_lock( &app_instance->mutex );

	app_instance->msgQueue[ app_instance->msgQueueLength ].msg		= MSG_WINDOW_FOCUSCHANGED;
	app_instance->msgQueue[ app_instance->msgQueueLength++ ].arg1	= focused;

	pthread_mutex_unlock( &app_instance->mutex );
}

static
void
OnNativeWindowCreated( ANativeActivity* activity, ANativeWindow* window )
{
	LOGI( "NativeActivity native window created: %p -- %p\n", activity, window );
	
	struct APP_INSTANCE* app_instance = (struct APP_INSTANCE*)activity->instance;

	pthread_mutex_lock( &app_instance->mutex );

	app_instance->pendingWindow = window;
	app_instance->msgQueue[ app_instance->msgQueueLength++ ].msg = MSG_WINDOW_CREATED;

	while( app_instance->window != app_instance->pendingWindow )
	{
		pthread_cond_wait(&app_instance->cond, &app_instance->mutex);
	}

	app_instance->pendingWindow = NULL;

	pthread_mutex_unlock( &app_instance->mutex );
}

static
void
OnNativeWindowDestroyed( ANativeActivity* activity, ANativeWindow* window )
{
	LOGI( "NativeActivity native window destroyed: %p -- %p\n", activity, window );

	struct APP_INSTANCE* app_instance = (struct APP_INSTANCE*)activity->instance;

	pthread_mutex_lock( &app_instance->mutex );

	app_instance->pendingWindow = NULL;
	app_instance->msgQueue[ app_instance->msgQueueLength++ ].msg = MSG_WINDOW_DESTROYED;

	while( app_instance->window != app_instance->pendingWindow )
	{
		pthread_cond_wait(&app_instance->cond, &app_instance->mutex);
	}

	pthread_mutex_unlock( &app_instance->mutex );
}

static
void
OnInputQueueCreated( ANativeActivity* activity, AInputQueue* queue )
{
	LOGI( "NativeActivity input queue created: %p -- %p\n", activity, queue );

	struct APP_INSTANCE* app_instance = (struct APP_INSTANCE*)activity->instance;

	pthread_mutex_lock( &app_instance->mutex );

	app_instance->pendingInputQueue = queue;
	app_instance->msgQueue[ app_instance->msgQueueLength++ ].msg = MSG_INPUTQUEUE_CREATED;

	while( app_instance->inputQueue != app_instance->pendingInputQueue )
	{
		pthread_cond_wait(&app_instance->cond, &app_instance->mutex);
	}

	app_instance->pendingInputQueue = NULL;

	pthread_mutex_unlock( &app_instance->mutex );
}

static
void
OnInputQueueDestroyed(ANativeActivity* activity, AInputQueue* queue )
{
	LOGI( "NativeActivity input queue destroyed: %p -- %p\n", activity, queue );

	struct APP_INSTANCE* app_instance = (struct APP_INSTANCE*)activity->instance;

	pthread_mutex_lock( &app_instance->mutex );

	app_instance->pendingInputQueue = NULL;
	app_instance->msgQueue[ app_instance->msgQueueLength++ ].msg = MSG_INPUTQUEUE_DESTROYED;

	while( app_instance->inputQueue != app_instance->pendingInputQueue )
	{
		pthread_cond_wait(&app_instance->cond, &app_instance->mutex);
	}

	pthread_mutex_unlock( &app_instance->mutex );
}
///////////////

static
void*
app_thread_entry( void* param )
{
	LOGI( "NativeActivity entered application thread" );

	struct APP_INSTANCE* app_instance = (struct APP_INSTANCE*)param;

	setupJNI(app_instance);

	app_instance->config = AConfiguration_new();
	AConfiguration_fromAssetManager( app_instance->config, app_instance->activity->assetManager );

	//create/get a looper
	ALooper* looper			= ALooper_prepare( ALOOPER_PREPARE_ALLOW_NON_CALLBACKS );
	app_instance->looper	= looper;

	//tell the thread which created this one that we are now up and running
	pthread_mutex_lock( &app_instance->mutex );
	app_instance->running = 1;
	pthread_cond_broadcast( &app_instance->cond );
	pthread_mutex_unlock( &app_instance->mutex );

	//run instance
	instance_app_main( app_instance );

	pthread_mutex_lock( &app_instance->mutex );
	
	AConfiguration_delete(app_instance->config);
	
	if( app_instance->inputQueue != NULL )
	{
		AInputQueue_detachLooper(app_instance->inputQueue);
	}

	app_instance->destroyed = 1;

	pthread_cond_broadcast( &app_instance->cond );
	pthread_mutex_unlock( &app_instance->mutex );

	free( app_instance );

	LOGI( "NativeActivity exting application thread" );

	return NULL;
}

//
static
struct APP_INSTANCE*
app_instance_create( ANativeActivity* activity, void* saved_state, size_t saved_state_size )
{
    struct APP_INSTANCE* app_instance = (struct APP_INSTANCE*)malloc( sizeof(struct APP_INSTANCE) );
    memset(app_instance, 0, sizeof(struct APP_INSTANCE));
    app_instance->activity = activity;

    pthread_mutex_init( &app_instance->mutex, NULL );
    pthread_cond_init( &app_instance->cond, NULL );

    pthread_attr_t attr; 
    pthread_attr_init( &attr );
    pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
    pthread_create( &app_instance->thread, &attr, app_thread_entry, app_instance );

    // Wait for thread to start.
    pthread_mutex_lock( &app_instance->mutex );
    while( !app_instance->running )
    {
        pthread_cond_wait( &app_instance->cond, &app_instance->mutex );
    }
    pthread_mutex_unlock( &app_instance->mutex );

    return app_instance;
}

//entry point from android/nativeactivity
void
ANativeActivity_onCreate( ANativeActivity* activity, void* saved_state, size_t saved_state_size )
{
	LOGI( "NativeActivity creating: %p\n", activity );

	activity->callbacks->onDestroy					= OnDestroy;
	activity->callbacks->onStart					= OnStart;
	activity->callbacks->onResume					= OnResume;
	activity->callbacks->onSaveInstanceState		= OnSaveInstanceState;
	activity->callbacks->onPause					= OnPause;
	activity->callbacks->onStop						= OnStop;
	activity->callbacks->onConfigurationChanged		= OnConfigurationChanged;
	activity->callbacks->onLowMemory				= OnLowMemory;
	activity->callbacks->onWindowFocusChanged		= OnWindowFocusChanged;
	activity->callbacks->onNativeWindowCreated		= OnNativeWindowCreated;
	activity->callbacks->onNativeWindowDestroyed	= OnNativeWindowDestroyed;
	activity->callbacks->onInputQueueCreated		= OnInputQueueCreated;
	activity->callbacks->onInputQueueDestroyed		= OnInputQueueDestroyed;

	activity->instance = app_instance_create( activity, saved_state, saved_state_size );
}
