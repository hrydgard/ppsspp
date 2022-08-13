// main.mm boilerplate

#import <UIKit/UIKit.h>
#import <dlfcn.h>
#import <mach/mach.h>
#import <pthread.h>
#import <signal.h>
#import <string>
#import <stdio.h>
#import <stdlib.h>
#import <sys/syscall.h>
#import <AudioToolbox/AudioToolbox.h>

#import "AppDelegate.h"
#import "PPSSPPUIApplication.h"
#import "ViewController.h"

#include "Common/MemoryUtil.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/StringUtils.h"
#include "Common/Profiler/Profiler.h"

static int (*csops)(pid_t pid, unsigned int ops, void * useraddr, size_t usersize);
static boolean_t (*exc_server)(mach_msg_header_t *, mach_msg_header_t *);
static int (*ptrace)(int request, pid_t pid, caddr_t addr, int data);

#define CS_OPS_STATUS	0		/* return status */
#define CS_DEBUGGED	0x10000000	/* process is currently or has previously been debugged and allowed to run with invalid pages */
#define PT_ATTACHEXC	14		/* attach to running process with signal exception */
#define PT_DETACH	11		/* stop tracing a process */
#define ptrace(a, b, c, d) syscall(SYS_ptrace, a, b, c, d)

bool get_debugged() {
	int flags;
	int rv = csops(getpid(), CS_OPS_STATUS, &flags, sizeof(flags));
	if (rv==0 && flags&CS_DEBUGGED) return true;

	pid_t pid = fork();
	if (pid > 0) {
		int st,rv,i=0;
		do {
			usleep(500);
			rv = waitpid(pid, &st, 0);
		} while (rv<0 && i++<10);
		if (rv<0) fprintf(stderr, "Unable to wait for child?\n");
	} else if (pid == 0) {
		pid_t ppid = getppid();
		int rv = ptrace(PT_ATTACHEXC, ppid, 0, 0);
		if (rv) {
			perror("Unable to attach to process");
			exit(1);
		}
		for (int i=0; i<100; i++) {
			usleep(1000);
			errno = 0;
			rv = ptrace(PT_DETACH, ppid, 0, 0);
			if (rv==0) break;
		}
		if (rv) {
			perror("Unable to detach from process");
			exit(1);
		}
		exit(0);
	} else {
		perror("Unable to fork");
	}

	rv = csops(getpid(), CS_OPS_STATUS, &flags, sizeof(flags));
	if (rv==0 && flags&CS_DEBUGGED) return true;

	return false;
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

static bool g_jitAvailable = false;
static int g_iosVersionMajor;
static int g_iosVersionMinor;

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
		case SYSPROP_NAME:
			return StringFromFormat("iOS %d.%d", g_iosVersionMajor, g_iosVersionMinor);
		case SYSPROP_LANGREGION:
			return "en_US";
		default:
			return "";
	}
}

std::vector<std::string> System_GetPropertyStringVec(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_TEMP_DIRS:
	default:
		return std::vector<std::string>();
	}
}

int System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
		case SYSPROP_AUDIO_SAMPLE_RATE:
			return 44100;
		case SYSPROP_DEVICE_TYPE:
			return DEVICE_TYPE_MOBILE;
		case SYSPROP_SYSTEMVERSION:
			return g_iosVersionMajor;
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
		case SYSPROP_CAN_JIT:
			return g_jitAvailable;

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

void System_Toast(const char *text) {}
void System_AskForPermission(SystemPermission permission) {}

PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }

FOUNDATION_EXTERN void AudioServicesPlaySystemSoundWithVibration(unsigned long, objc_object*, NSDictionary*);

BOOL SupportsTaptic() {
	// we're on an iOS version that cannot instantiate UISelectionFeedbackGenerator, so no.
	if(!NSClassFromString(@"UISelectionFeedbackGenerator")) {
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
	if (SupportsTaptic()) {
		PPSSPPUIApplication* app = (PPSSPPUIApplication*)[UIApplication sharedApplication];
		if(app.feedbackGenerator == nil)
		{
			app.feedbackGenerator = [[UISelectionFeedbackGenerator alloc] init];
			[app.feedbackGenerator prepare];
		}
		[app.feedbackGenerator selectionChanged];
	} else {
		NSMutableDictionary *dictionary = [NSMutableDictionary dictionary];
		NSArray *pattern = @[@YES, @30, @NO, @2];

		dictionary[@"VibePattern"] = pattern;
		dictionary[@"Intensity"] = @2;

		AudioServicesPlaySystemSoundWithVibration(kSystemSoundID_Vibrate, nil, dictionary);
	}
}

int main(int argc, char *argv[])
{
	// Hacky hacks to try to enable JIT by pretending to be a debugger.
	csops = reinterpret_cast<decltype(csops)>(dlsym(dlopen(nullptr, RTLD_LAZY), "csops"));
	exc_server = reinterpret_cast<decltype(exc_server)>(dlsym(dlopen(NULL, RTLD_LAZY), "exc_server"));
	ptrace = reinterpret_cast<decltype(ptrace)>(dlsym(dlopen(NULL, RTLD_LAZY), "ptrace"));
	// see https://github.com/hrydgard/ppsspp/issues/11905

	// Tried checking for JIT support here with AllocateExecutableMemory and ProtectMemoryPages,
	// but it just succeeds, and then fails when you try to execute from it.

	// So, we'll just resort to a version check.

	std::string version = [[UIDevice currentDevice].systemVersion UTF8String];
	if (2 != sscanf(version.c_str(), "%d.%d", &g_iosVersionMajor, &g_iosVersionMinor)) {
		// Just set it to 14.0 if the parsing fails for whatever reason.
		g_iosVersionMajor = 14;
		g_iosVersionMinor = 0;
	}

	g_jitAvailable = get_debugged();
/*
	if (g_iosVersionMajor > 14 || (g_iosVersionMajor == 14 && g_iosVersionMinor >= 4)) {
		g_jitAvailable = false;
	} else {
		g_jitAvailable = true;
	}
*/

	PROFILE_INIT();

	// Ignore sigpipe.
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		perror("Unable to ignore SIGPIPE");
	}

	@autoreleasepool {
		return UIApplicationMain(argc, argv, NSStringFromClass([PPSSPPUIApplication class]), NSStringFromClass([AppDelegate class]));
	}
}
