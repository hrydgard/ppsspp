// main.mm boilerplate

#import <UIKit/UIKit.h>
#import <string>
#import <stdio.h>
#import <stdlib.h>
#import <sys/syscall.h>
#import <AudioToolbox/AudioToolbox.h>
#import "codesign.h"

#import "AppDelegate.h"
#import "PPSSPPUIApplication.h"
#import "ViewController.h"

#include "base/NativeApp.h"
#include "profiler/profiler.h"

#define CS_OPS_STATUS	0		/* return status */
#define CS_DEBUGGED	0x10000000	/* process is currently or has previously been debugged and allowed to run with invalid pages */
#define PT_ATTACHEXC	14		/* attach to running process with signal exception */
#define PT_DETACH	11		/* stop tracing a process */
int csops(pid_t pid, unsigned int  ops, void * useraddr, size_t usersize);
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
		case SYSPROP_DISPLAY_REFRESH_RATE:
			return 60000;
		case SYSPROP_DEVICE_TYPE:
			return DEVICE_TYPE_MOBILE;
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
	// see https://github.com/hrydgard/ppsspp/issues/11905
	if (!get_debugged()) {
		fprintf(stderr, "Unable to obtain CS_DEBUGGED -  I will probably die now.\n");
	}
	
	PROFILE_INIT();
	
	@autoreleasepool {
		NSString *documentsPath = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) objectAtIndex:0];
		NSString *bundlePath = [[[NSBundle mainBundle] resourcePath] stringByAppendingString:@"/assets/"];
		
		NativeInit(argc, (const char**)argv, documentsPath.UTF8String, bundlePath.UTF8String, NULL);
		
		return UIApplicationMain(argc, argv, NSStringFromClass([PPSSPPUIApplication class]), NSStringFromClass([AppDelegate class]));
	}
}
