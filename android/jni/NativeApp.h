#pragma once

#include <poll.h>
#include <pthread.h>

#include <android/configuration.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/input.h>
#include <android/log.h>

#define LOGI(...) ((void)__android_log_print( ANDROID_LOG_INFO, TAG, __VA_ARGS__ ))
#define LOGW(...) ((void)__android_log_print( ANDROID_LOG_WARN, TAG, __VA_ARGS__ ))
#define LOGE(...) ((void)__android_log_print( ANDROID_LOG_ERROR, TAG, __VA_ARGS__ ))

struct APP_MSG
{
	char	msg;
	char	arg1;
};

struct APP_INSTANCE
{
	// The application can place a pointer to its own state object
	// here if it likes.
	void*				userData;

	// The ANativeActivity object instance that this app is running in.
	ANativeActivity*	activity;

	// The current configuration the app is running in.
	AConfiguration*		config;

	// The ALooper associated with the app's thread.
	ALooper*			looper;

	// When non-NULL, this is the input queue from which the app will
	// receive user input events.
	AInputQueue*		inputQueue;
	AInputQueue*		pendingInputQueue;

	// When non-NULL, this is the window surface that the app can draw in.
	ANativeWindow*		window;
	ANativeWindow*		pendingWindow;

	//Activity's current state: APP_STATE_*
	int					activityState;
	int					pendingActivityState;

	//
	struct APP_MSG		msgQueue[512];
	int					msgQueueLength;

	//used to synchronize between callbacks and game-thread
	pthread_mutex_t		mutex;
	pthread_cond_t		cond;

	pthread_t			thread;

	int					running;
	int					destroyed;
	int					redrawNeeded;
};
