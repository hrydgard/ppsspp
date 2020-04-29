// main.mm boilerplate

#import <UIKit/UIKit.h>
#import <dlfcn.h>
#import <mach/mach.h>
#import <pthread.h>
#import <string>
#import <stdio.h>
#import <stdlib.h>
#import <sys/syscall.h>
#import <AudioToolbox/AudioToolbox.h>

#import "AppDelegate.h"
#import "PPSSPPUIApplication.h"
#import "ViewController.h"

#include "base/NativeApp.h"
#include "profiler/profiler.h"

#define	CS_OPS_STATUS		0	/* return status */
#define CS_DEBUGGED 0x10000000  /* process is currently or has previously been debugged and allowed to run with invalid pages */
#define PT_TRACE_ME     0       /* child declares it's being traced */
#define PT_SIGEXC       12      /* signals as exceptions for current_proc */

static int (*csops)(pid_t pid, unsigned int ops, void * useraddr, size_t usersize);
static boolean_t (*exc_server)(mach_msg_header_t *, mach_msg_header_t *);
static int (*ptrace)(int request, pid_t pid, caddr_t addr, int data);

bool cs_debugged() {
	int flags;
	return !csops(getpid(), CS_OPS_STATUS, &flags, sizeof(flags)) && flags & CS_DEBUGGED;
}

kern_return_t catch_exception_raise(mach_port_t exception_port,
                                    mach_port_t thread,
                                    mach_port_t task,
                                    exception_type_t exception,
                                    exception_data_t code,
                                    mach_msg_type_number_t code_count) {
	return KERN_FAILURE;
}

void *exception_handler(void *argument) {
	auto port = *reinterpret_cast<mach_port_t *>(argument);
	mach_msg_server(exc_server, 2048, port, 0);
	return NULL;
}

static float g_safeInsetLeft = 0.0;
static float g_safeInsetRight = 0.0;
static float g_safeInsetTop = 0.0;
static float g_safeInsetBottom = 0.0;


std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
		case SYSPROP_NAME:
			return "iOS:";
		case SYSPROP_LANGREGION:
			return "en_US";
		default:
			return "";
	}
}

int System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
		case SYSPROP_AUDIO_SAMPLE_RATE:
			return 44100;
		case SYSPROP_DEVICE_TYPE:
			return DEVICE_TYPE_MOBILE;
		default:
			return -1;
	}
}

float System_GetPropertyFloat(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return 60.f;
	case SYSPROP_DISPLAY_SAFE_INSET_LEFT:
		return g_safeInsetLeft;
	case SYSPROP_DISPLAY_SAFE_INSET_RIGHT:
		return g_safeInsetRight;
	case SYSPROP_DISPLAY_SAFE_INSET_TOP:
		return g_safeInsetTop;
	case SYSPROP_DISPLAY_SAFE_INSET_BOTTOM:
		return g_safeInsetBottom;
	default:
		return -1;
	}
}

bool System_GetPropertyBool(SystemProperty prop) {
	switch (prop) {
		case SYSPROP_HAS_BACK_BUTTON:
			return false;
		case SYSPROP_APP_GOLD:
#ifdef GOLD
			return true;
#else
			return false;
#endif
		default:
			return false;
	}
}

void System_SendMessage(const char *command, const char *parameter) {
	if (!strcmp(command, "finish")) {
		exit(0);
		// The below seems right, but causes hangs. See #12140.
		// dispatch_async(dispatch_get_main_queue(), ^{
		// [sharedViewController shutdown];
		//	exit(0);
		// });
	} else if (!strcmp(command, "sharetext")) {
		NSString *text = [NSString stringWithUTF8String:parameter];
		[sharedViewController shareText:text];
	} else if (!strcmp(command, "camera_command")) {
		if (!strncmp(parameter, "startVideo", 10)) {
			int width = 0, height = 0;
			sscanf(parameter, "startVideo_%dx%d", &width, &height);
			setCameraSize(width, height);
			startVideo();
		} else if (!strcmp(parameter, "stopVideo")) {
			stopVideo();
		}
	} else if (!strcmp(command, "gps_command")) {
		if (!strcmp(parameter, "open")) {
			startLocation();
		} else if (!strcmp(parameter, "close")) {
			stopLocation();
		}
	} else if (!strcmp(command, "safe_insets")) {
		float left, right, top, bottom;
		if (4 == sscanf(parameter, "%f:%f:%f:%f", &left, &right, &top, &bottom)) {
			g_safeInsetLeft = left;
			g_safeInsetRight = right;
			g_safeInsetTop = top;
			g_safeInsetBottom = bottom;
		}
	}
}

void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }

FOUNDATION_EXTERN void AudioServicesPlaySystemSoundWithVibration(unsigned long, objc_object*, NSDictionary*);

BOOL SupportsTaptic()
{
	// we're on an iOS version that cannot instantiate UISelectionFeedbackGenerator, so no.
	if(!NSClassFromString(@"UISelectionFeedbackGenerator"))
	{
		return NO;
	}

	// http://www.mikitamanko.com/blog/2017/01/29/haptic-feedback-with-uifeedbackgenerator/
	// use private API against UIDevice to determine the haptic stepping
	// 2 - iPhone 7 or above, full taptic feedback
	// 1 - iPhone 6S, limited taptic feedback
	// 0 - iPhone 6 or below, no taptic feedback
	NSNumber* val = (NSNumber*)[[UIDevice currentDevice] valueForKey:@"feedbackSupportLevel"];
	return [val intValue] >= 2;
}

void Vibrate(int mode) {

	if(SupportsTaptic())
	{
		PPSSPPUIApplication* app = (PPSSPPUIApplication*)[UIApplication sharedApplication];
		if(app.feedbackGenerator == nil)
		{
			app.feedbackGenerator = [[UISelectionFeedbackGenerator alloc] init];
			[app.feedbackGenerator prepare];
		}
		[app.feedbackGenerator selectionChanged];
	}
	else
	{
		NSMutableDictionary *dictionary = [NSMutableDictionary dictionary];
		NSArray *pattern = @[@YES, @30, @NO, @2];

		dictionary[@"VibePattern"] = pattern;
		dictionary[@"Intensity"] = @2;

		AudioServicesPlaySystemSoundWithVibration(kSystemSoundID_Vibrate, nil, dictionary);
	}
}

int main(int argc, char *argv[])
{
	csops = reinterpret_cast<decltype(csops)>(dlsym(dlopen(nullptr, RTLD_LAZY), "csops"));
	exc_server = reinterpret_cast<decltype(exc_server)>(dlsym(dlopen(NULL, RTLD_LAZY), "exc_server"));
	ptrace = reinterpret_cast<decltype(ptrace)>(dlsym(dlopen(NULL, RTLD_LAZY), "ptrace"));
	// see https://github.com/hrydgard/ppsspp/issues/11905
	if (!cs_debugged()) {
		pid_t pid = fork();
		if (pid == 0) {
			ptrace(PT_TRACE_ME, 0, nullptr, 0);
			exit(0);
		} else if (pid < 0) {
			perror("Unable to fork");

			ptrace(PT_TRACE_ME, 0, nullptr, 0);
			ptrace(PT_SIGEXC, 0, nullptr, 0);
			
			mach_port_t port = MACH_PORT_NULL;
			mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port);
			mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND);
			task_set_exception_ports(mach_task_self(), EXC_MASK_SOFTWARE, port, EXCEPTION_DEFAULT, THREAD_STATE_NONE);
			pthread_t thread;
			pthread_create(&thread, nullptr, exception_handler, reinterpret_cast<void *>(&port));
		}
	}

	PROFILE_INIT();

	@autoreleasepool {
		return UIApplicationMain(argc, argv, NSStringFromClass([PPSSPPUIApplication class]), NSStringFromClass([AppDelegate class]));
	}
}
