// main.mm boilerplate
//
// Overview
//
// main.mm: JIT enablement, starting the next step
// AppDelegate.mm: Runs NativeInit, launches the main ViewController
// ViewController.mm: The main application window

#import <UIKit/UIKit.h>
#import <dlfcn.h>
#import <mach/mach.h>
#import <mach-o/loader.h>
#import <mach-o/getsect.h>
#import <pthread.h>
#import <spawn.h>
#import <signal.h>
#import <string>
#import <stdio.h>
#import <stdlib.h>
#import <sys/syscall.h>
#import <sys/utsname.h>
#import <AudioToolbox/AudioToolbox.h>

#import "AppDelegate.h"
#import "PPSSPPUIApplication.h"
#import "ViewController.h"
#import "iOSCoreAudio.h"
#import "IAPManager.h"
#import "SceneDelegate.h"

#include "Common/MemoryUtil.h"
#include "Common/Audio/AudioBackend.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/StringUtils.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/System/Display.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Common/Log.h"
#include "Common/Log/LogManager.h"
#include "UI/DarwinFileSystemServices.h"

// Compile out all the hackery in app store builds.
#if !PPSSPP_PLATFORM(IOS_APP_STORE)

struct cs_blob_index {
	uint32_t type;
	uint32_t offset;
};

struct cs_superblob {
	uint32_t magic;
	uint32_t length;
	uint32_t count;
	struct cs_blob_index index[];
};

struct cs_entitlements {
	uint32_t magic;
	uint32_t length;
	char entitlements[];
};

static int (*csops)(pid_t pid, unsigned int ops, void * useraddr, size_t usersize);
static boolean_t (*exc_server)(mach_msg_header_t *, mach_msg_header_t *);
static int (*ptrace)(int request, pid_t pid, caddr_t addr, int data);

#define CS_OPS_STATUS	0		/* return status */
#define CS_KILL		0x00000200	/* kill process if it becomes invalid */
#define CS_DEBUGGED	0x10000000	/* process is currently or has previously been debugged and allowed to run with invalid pages */
#define PT_TRACE_ME	0		/* child declares it's being traced */
#define PT_SIGEXC	12		/* signals as exceptions for current_proc */
#define ptrace(a, b, c, d) syscall(SYS_ptrace, a, b, c, d)

static void *exception_handler(void *argument) {
	mach_port_t port = *(mach_port_t *)argument;
	mach_msg_server(exc_server, 2048, port, 0);
	return NULL;
}

static NSDictionary *parse_entitlements(const void *entitlements, size_t length) {
	char *copy = (char *)malloc(length);
	memcpy(copy, entitlements, length);
	
	// strip out psychic paper entitlement hiding
	if (@available(iOS 13.5, *)) {
	} else {
		static const char *needle = "<!---><!-->";
		char *found = strnstr(copy, needle, length);
		if (found) {
			memset(found, ' ', strlen(needle));
		}
	}
	NSData *data = [NSData dataWithBytes:copy length:length];
	free(copy);
	
	return [NSPropertyListSerialization propertyListWithData:data
													 options:NSPropertyListImmutable
													  format:nil
													   error:nil];
}

static NSDictionary *app_entitlements(void) {
	// Inspired by codesign.c in Darwin sources for Security.framework
	
	// Find our mach-o header
	Dl_info dl_info;
	if (dladdr((const void *)app_entitlements, &dl_info) == 0)
		return nil;
	if (dl_info.dli_fbase == NULL)
		return nil;
	char *base = (char *)dl_info.dli_fbase;
	struct mach_header_64 *header = (struct mach_header_64 *)dl_info.dli_fbase;
	if (header->magic != MH_MAGIC_64)
		return nil;
	
	// Simulator executables have fake entitlements in the code signature. The real entitlements can be found in an __entitlements section.
	size_t entitlements_size;
	uint8_t *entitlements_data = getsectiondata(header, "__TEXT", "__entitlements", &entitlements_size);
	if (entitlements_data != NULL) {
		NSData *data = [NSData dataWithBytesNoCopy:entitlements_data
											length:entitlements_size
									  freeWhenDone:NO];
		return [NSPropertyListSerialization propertyListWithData:data
														 options:NSPropertyListImmutable
														  format:nil
														   error:nil];
	}
	
	// Find the LC_CODE_SIGNATURE
	struct load_command *lc = (struct load_command *) (base + sizeof(*header));
	struct linkedit_data_command *cs_lc = NULL;
	for (uint32_t i = 0; i < header->ncmds; i++) {
		if (lc->cmd == LC_CODE_SIGNATURE) {
			cs_lc = (struct linkedit_data_command *) lc;
			break;
		}
		lc = (struct load_command *) ((char *) lc + lc->cmdsize);
	}
	if (cs_lc == NULL)
		return nil;

	// Read the code signature off disk, as it's apparently not loaded into memory
	NSFileHandle *fileHandle = [NSFileHandle fileHandleForReadingFromURL:NSBundle.mainBundle.executableURL error:nil];
	if (fileHandle == nil)
		return nil;
	[fileHandle seekToFileOffset:cs_lc->dataoff];
	NSData *csData = [fileHandle readDataOfLength:cs_lc->datasize];
	[fileHandle closeFile];
	const struct cs_superblob *cs = (const struct cs_superblob *)csData.bytes;
	if (ntohl(cs->magic) != 0xfade0cc0)
		return nil;
	
	// Find the entitlements in the code signature
	for (uint32_t i = 0; i < ntohl(cs->count); i++) {
		struct cs_entitlements *ents = (struct cs_entitlements *) ((char *) cs + ntohl(cs->index[i].offset));
		if (ntohl(ents->magic) == 0xfade7171) {
			return parse_entitlements(ents->entitlements, ntohl(ents->length) - offsetof(struct cs_entitlements, entitlements));
		}
	}
	return nil;
}

static NSDictionary *cached_app_entitlements(void) {
	static NSDictionary *entitlements = nil;
	if (!entitlements) {
		entitlements = app_entitlements();
	}
	return entitlements;
}

bool jb_has_container(void) {
	NSDictionary *entitlements = cached_app_entitlements();
	return ![entitlements[@"com.apple.private.security.no-sandbox"] boolValue];
}

static char *childArgv[] = {NULL, "debugme", NULL};

bool jb_spawn_ptrace_child(int argc, char **argv) {
	int ret; pid_t pid;
	
	if (argc > 1 && strcmp(argv[1], childArgv[1]) == 0) {
		ret = ptrace(PT_TRACE_ME, 0, NULL, 0);
		NSLog(@"child: ptrace(PT_TRACE_ME) %d", ret);
		exit(ret);
	}
	if (jb_has_container()) {
		return false;
	}
	childArgv[0] = argv[0];
	if ((ret = posix_spawnp(&pid, argv[0], NULL, NULL, (char *const *)childArgv, NULL)) != 0) {
		return false;
	}
	return true;
}

bool jb_has_jit_entitlement(void) {
	NSDictionary *entitlements = cached_app_entitlements();
	return [entitlements[@"dynamic-codesigning"] boolValue];
}

bool jb_has_cs_disabled(void) {
	int flags;
	return !csops(getpid(), CS_OPS_STATUS, &flags, sizeof(flags)) && (flags & ~CS_KILL) == flags;
}

#define _COMM_PAGE_START_ADDRESS        (0x0000000FFFFFC000ULL) /* In TTBR0 */
#define _COMM_PAGE_APRR_SUPPORT         (_COMM_PAGE_START_ADDRESS+0x10C)

static bool is_device_A12_or_newer(void) {
	// devices without APRR are definitely < A12
	char aprr_support = *(volatile char *)_COMM_PAGE_APRR_SUPPORT;
	if (aprr_support == 0) {
		return false;
	}
	// we still have A11 devices that support APRR
	struct utsname systemInfo;
	if (uname(&systemInfo) != 0) {
		return false;
	}
	// iPhone 8, 8 Plus, and iPhone X
	if (strncmp("iPhone10,", systemInfo.machine, 9) == 0) {
		return false;
	} else {
		return true;
	}
}

bool jb_has_cs_execseg_allow_unsigned(void) {
	NSDictionary *entitlements = cached_app_entitlements();
	if (@available(iOS 14.2, *)) {
		if (@available(iOS 14.4, *)) {
			return false; // iOS 14.4 broke it again
		}
		// technically we need to check the Code Directory and make sure
		// CS_EXECSEG_ALLOW_UNSIGNED is set but we assume that it is properly
		// signed, which should reflect the get-task-allow entitlement
		return is_device_A12_or_newer() && [entitlements[@"get-task-allow"] boolValue];
	} else {
		return false;
	}
}

static bool jb_has_debugger_attached(void) {
	int flags;
	return !csops(getpid(), CS_OPS_STATUS, &flags, sizeof(flags)) && flags & CS_DEBUGGED;
}

bool jb_enable_ptrace_hack(void) {
	bool debugged = jb_has_debugger_attached();
	
	// Thanks to this comment: https://news.ycombinator.com/item?id=18431524
	// We use this hack to allow mmap with PROT_EXEC (which usually requires the
	// dynamic-codesigning entitlement) by tricking the process into thinking
	// that Xcode is debugging it. We abuse the fact that JIT is needed to
	// debug the process.
	if (ptrace(PT_TRACE_ME, 0, NULL, 0) < 0) {
		return false;
	}
	
	// ptracing ourselves confuses the kernel and will cause bad things to
	// happen to the system (hangs…) if an exception or signal occurs. Setup
	// some "safety nets" so we can cause the process to exit in a somewhat sane
	// state. We only need to do this if the debugger isn't attached. (It'll do
	// this itself, and if we do it we'll interfere with its normal operation
	// anyways.)
	if (!debugged) {
		// First, ensure that signals are delivered as Mach software exceptions…
		ptrace(PT_SIGEXC, 0, NULL, 0);
		
		// …then ensure that this exception goes through our exception handler.
		// I think it's OK to just watch for EXC_SOFTWARE because the other
		// exceptions (e.g. EXC_BAD_ACCESS, EXC_BAD_INSTRUCTION, and friends)
		// will end up being delivered as signals anyways, and we can get them
		// once they're resent as a software exception.
		mach_port_t port = MACH_PORT_NULL;
		mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port);
		mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND);
		task_set_exception_ports(mach_task_self(), EXC_MASK_SOFTWARE, port, EXCEPTION_DEFAULT, THREAD_STATE_NONE);
		pthread_t thread;
		pthread_create(&thread, NULL, exception_handler, (void *)&port);
	}
	
	return true;
}
#endif

float g_safeInsetLeft = 0.0;
float g_safeInsetRight = 0.0;
float g_safeInsetTop = 0.0;
float g_safeInsetBottom = 0.0;

// We no longer need to judge if jit is usable or not by according to the ios version.
static bool g_jitAvailable = true;
//static int g_iosVersionMinor;

static int g_iosVersionMajor;
static std::string version;

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
		case SYSPROP_NAME:
			return StringFromFormat("iOS %s", version.c_str());
		case SYSPROP_LANGREGION:
			return [[[NSLocale currentLocale] objectForKey:NSLocaleIdentifier] UTF8String];
		case SYSPROP_BUILD_VERSION:
			return PPSSPP_GIT_VERSION;
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

extern "C" {
int Apple_GetCurrentBatteryCapacity();
}

int64_t System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
		case SYSPROP_AUDIO_SAMPLE_RATE:
			return 44100;
		case SYSPROP_DEVICE_TYPE:
			return DEVICE_TYPE_MOBILE;
		case SYSPROP_SYSTEMVERSION:
			return g_iosVersionMajor;
		case SYSPROP_BATTERY_PERCENTAGE:
			return Apple_GetCurrentBatteryCapacity();
		default:
			return -1;
	}
}

float System_GetPropertyFloat(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return 60.f;
	case SYSPROP_DISPLAY_SAFE_INSET_LEFT:
		return g_safeInsetLeft * g_display.dpi_scale_x;
	case SYSPROP_DISPLAY_SAFE_INSET_RIGHT:
		return g_safeInsetRight * g_display.dpi_scale_x;
	case SYSPROP_DISPLAY_SAFE_INSET_TOP:
		return g_safeInsetTop * g_display.dpi_scale_y;
	case SYSPROP_DISPLAY_SAFE_INSET_BOTTOM:
		return g_safeInsetBottom * g_display.dpi_scale_y;
	default:
		return -1;
	}
}

bool System_GetPropertyBool(SystemProperty prop) {
	switch (prop) {
		case SYSPROP_HAS_FILE_BROWSER:
			return true;
		case SYSPROP_HAS_FOLDER_BROWSER:
			return true;
		case SYSPROP_HAS_IMAGE_BROWSER:
			return true;
		case SYSPROP_HAS_OPEN_DIRECTORY:
			return false;
		case SYSPROP_HAS_BACK_BUTTON:
			return false;
		case SYSPROP_HAS_ACCELEROMETER:
			return true;
		case SYSPROP_HAS_KEYBOARD:
			return true;
		case SYSPROP_SUPPORTS_SHARE_TEXT:
			return true;
		case SYSPROP_KEYBOARD_IS_SOFT:
			// If a hardware keyboard is connected, and we add support, we could return false here.
			return true;
		case SYSPROP_APP_GOLD:
#ifdef GOLD
			// This is deprecated.
			return true;
#elif PPSSPP_PLATFORM(IOS_APP_STORE)
			// Check the IAP status.
			return [[IAPManager sharedIAPManager] isGoldUnlocked];
#else
			return false;
#endif
		case SYSPROP_USE_IAP:
#if PPSSPP_PLATFORM(IOS_APP_STORE) && defined(USE_IAP)
			return true;
#else
			return false;
#endif
		case SYSPROP_CAN_JIT:
			return g_jitAvailable;
		case SYSPROP_LIMITED_FILE_BROWSING:
#if PPSSPP_PLATFORM(IOS_APP_STORE)
			return true;
#else
			return false;  // But will return true in app store builds.
#endif
#ifndef HTTPS_NOT_AVAILABLE
		case SYSPROP_SUPPORTS_HTTPS:
			return true;
#endif
		case SYSPROP_CAN_READ_BATTERY_PERCENTAGE:
			return true;

		default:
			return false;
	}
}

void System_Notify(SystemNotification notification) {
	switch (notification) {
	case SystemNotification::APP_SWITCH_MODE_CHANGED:
		dispatch_async(dispatch_get_main_queue(), ^{
			if (sharedViewController) {
				[sharedViewController appSwitchModeChanged];
			}
		});
		break;
	case SystemNotification::UI_STATE_CHANGED:
		dispatch_async(dispatch_get_main_queue(), ^{
			if (sharedViewController) {
				[sharedViewController uiStateChanged];
			}
		});
		break;
	case SystemNotification::AUDIO_MODE_CHANGED:
		dispatch_async(dispatch_get_main_queue(), ^{
			iOSCoreAudioUpdateSession();
		});
		break;
	case SystemNotification::ROTATE_UPDATED:
	    dispatch_async(dispatch_get_main_queue(), ^{
			if (sharedViewController) {
				// [sharedViewController setNeedsUpdateOfSupportedInterfaceOrientations];
				INFO_LOG(Log::System, "Requesting device orientation update");
				[UIViewController attemptRotationToDeviceOrientation];
			}
		});
	default:
		break;
	}
}

bool System_MakeRequest(SystemRequestType type, int requestId, const std::string &param1, const std::string &param2, int64_t param3, int64_t param4) {
	switch (type) {
	case SystemRequestType::RESTART_APP:
		dispatch_async(dispatch_get_main_queue(), ^{
			// Get the connected scenes
			NSSet<UIScene *> *scenes = [UIApplication sharedApplication].connectedScenes;

			// Loop through scenes to find a UIWindowScene that is active
			for (UIScene *scene in scenes) {
				if ([scene isKindOfClass:[UIWindowScene class]]) {
					UIWindowScene *windowScene = (UIWindowScene *)scene;
					SceneDelegate *sceneDelegate = (SceneDelegate *)windowScene.delegate;
					[sceneDelegate restart:param1.c_str()];
					break; // call only on the first active scene
				}
			}
		});
		return true;

	case SystemRequestType::EXIT_APP:
		// NOTE: on iOS, this is considered a crash and not a valid way to exit.
		exit(0);
		// The below seems right, but causes hangs. See #12140.
		// dispatch_async(dispatch_get_main_queue(), ^{
		// [sharedViewController shutdown];
		//	exit(0);
		// });
		break;
	case SystemRequestType::BROWSE_FOR_FILE:
	{
		DarwinDirectoryPanelCallback callback = [requestId] (bool success, Path path) {
			if (success) {
				g_requestManager.PostSystemSuccess(requestId, path.c_str());
			} else {
				g_requestManager.PostSystemFailure(requestId);
			}
		};
		DarwinFileSystemServices::presentDirectoryPanel(callback, /* allowFiles = */ true, /* allowDirectories = */ false);
		return true;
	}
	case SystemRequestType::BROWSE_FOR_FOLDER:
	{
		DarwinDirectoryPanelCallback callback = [requestId] (bool success, Path path) {
			if (success) {
				g_requestManager.PostSystemSuccess(requestId, path.c_str());
			} else {
				g_requestManager.PostSystemFailure(requestId);
			}
		};
		DarwinFileSystemServices::presentDirectoryPanel(callback, /* allowFiles = */ false, /* allowDirectories = */ true);
		return true;
	}
	case SystemRequestType::BROWSE_FOR_IMAGE:
	{
		NSString *filename = [NSString stringWithUTF8String:param2.c_str()];
		dispatch_async(dispatch_get_main_queue(), ^{
			[sharedViewController pickPhoto:filename requestId:requestId];
		});
		return true;
	}
	case SystemRequestType::CAMERA_COMMAND:
		if (!strncmp(param1.c_str(), "startVideo", 10)) {
			int width = 0, height = 0;
			sscanf(param1.c_str(), "startVideo_%dx%d", &width, &height);
			[sharedViewController startVideo:width height:height];
		} else if (!strcmp(param1.c_str(), "stopVideo")) {
			[sharedViewController stopVideo];
		}
		return true;
	case SystemRequestType::GPS_COMMAND:
		if (param1 == "open") {
			[sharedViewController startLocation];
		} else if (param1 == "close") {
			[sharedViewController stopLocation];
		}
		return true;
	case SystemRequestType::SHARE_TEXT:
	{
		NSString *text = [NSString stringWithUTF8String:param1.c_str()];
		[sharedViewController shareText:text];
		return true;
	}
	case SystemRequestType::NOTIFY_UI_EVENT:
	{
		switch ((UIEventNotification)param3) {
		case UIEventNotification::POPUP_CLOSED:
			[sharedViewController hideKeyboard];
			break;
		case UIEventNotification::TEXT_GOTFOCUS:
			[sharedViewController showKeyboard];
			break;
		case UIEventNotification::TEXT_LOSTFOCUS:
			[sharedViewController hideKeyboard];
			break;
		default:
			break;
		}
		return true;
	}
#if PPSSPP_PLATFORM(IOS_APP_STORE)
	case SystemRequestType::IAP_RESTORE_PURCHASES:
	{
		[[IAPManager sharedIAPManager] restorePurchasesWithRequestID:requestId];
		return true;
	}
	case SystemRequestType::IAP_MAKE_PURCHASE:
	{
		[[IAPManager sharedIAPManager] buyGoldWithRequestID:requestId];
		return true;
	}
#endif
/*
	// Not 100% sure the threading is right
	case SystemRequestType::COPY_TO_CLIPBOARD:
	{
		@autoreleasepool {
			[UIPasteboard generalPasteboard].string = @(param1.c_str());
			return 0;
		}
	}
*/
	case SystemRequestType::SET_KEEP_SCREEN_BRIGHT:
		dispatch_async(dispatch_get_main_queue(), ^{
			INFO_LOG(Log::System, "SET_KEEP_SCREEN_BRIGHT: %d", (int)param3);
			[[UIApplication sharedApplication] setIdleTimerDisabled: (param3 ? YES : NO)];
		});
		return true;
	default:
		break;
	}
	return false;
}

void System_Toast(std::string_view text) {}
void System_AskForPermission(SystemPermission permission) {}

void System_LaunchUrl(LaunchUrlType urlType, std::string_view url) {
	std::string strUrl(url);
	NSURL *nsUrl = [NSURL URLWithString:[NSString stringWithCString:strUrl.c_str() encoding:NSStringEncodingConversionAllowLossy]];
	dispatch_async(dispatch_get_main_queue(), ^{
		[[UIApplication sharedApplication] openURL:nsUrl options:@{} completionHandler:nil];
	});
}

PermissionStatus System_GetPermissionStatus(SystemPermission permission) {
	 return PERMISSION_STATUS_GRANTED;
}

#if !PPSSPP_PLATFORM(IOS_APP_STORE)
FOUNDATION_EXTERN void AudioServicesPlaySystemSoundWithVibration(unsigned long, objc_object*, NSDictionary*);
#endif

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

void System_Vibrate(int mode) {
	if (SupportsTaptic()) {
		PPSSPPUIApplication* app = (PPSSPPUIApplication*)[UIApplication sharedApplication];
		if(app.feedbackGenerator == nil)
		{
			app.feedbackGenerator = [[UISelectionFeedbackGenerator alloc] init];
			[app.feedbackGenerator prepare];
		}
		[app.feedbackGenerator selectionChanged];
	} else {
#if !PPSSPP_PLATFORM(IOS_APP_STORE)
		NSMutableDictionary *dictionary = [NSMutableDictionary dictionary];
		NSArray *pattern = @[@YES, @30, @NO, @2];

		dictionary[@"VibePattern"] = pattern;
		dictionary[@"Intensity"] = @2;

		AudioServicesPlaySystemSoundWithVibration(kSystemSoundID_Vibrate, nil, dictionary);
#endif
	}
}

AudioBackend *System_CreateAudioBackend() {
	// Use legacy mechanisms.
	return nullptr;
}

int main(int argc, char *argv[]) {
	version = [[[UIDevice currentDevice] systemVersion] UTF8String];
	if (1 != sscanf(version.c_str(), "%d", &g_iosVersionMajor)) {
		// Just set it to 14.0 if the parsing fails for whatever reason.
		g_iosVersionMajor = 14;
	}

	TimeInit();

	g_logManager.EnableOutput(LogOutput::Stdio);

#if PPSSPP_PLATFORM(IOS_APP_STORE)
	g_jitAvailable = false;
#else
	// Hacky hacks to try to enable JIT by pretending to be a debugger.
	csops = reinterpret_cast<decltype(csops)>(dlsym(dlopen(nullptr, RTLD_LAZY), "csops"));
	exc_server = reinterpret_cast<decltype(exc_server)>(dlsym(dlopen(NULL, RTLD_LAZY), "exc_server"));
	ptrace = reinterpret_cast<decltype(ptrace)>(dlsym(dlopen(NULL, RTLD_LAZY), "ptrace"));
	// see https://github.com/hrydgard/ppsspp/issues/11905

	if (jb_spawn_ptrace_child(argc, argv)) {
		INFO_LOG(Log::System, "JIT: ptrace() child spawn trick\n");
	} else if (jb_has_jit_entitlement()) {
		INFO_LOG(Log::System, "JIT: found entitlement\n");
	} else if (jb_has_cs_disabled()) {
		INFO_LOG(Log::System, "JIT: CS_KILL disabled\n");
	} else if (jb_has_cs_execseg_allow_unsigned()) {
		INFO_LOG(Log::System, "JIT: CS_EXECSEG_ALLOW_UNSIGNED set\n");
	} else if (jb_enable_ptrace_hack()) {
		INFO_LOG(Log::System, "JIT: ptrace() hack supported\n");
	} else {
		INFO_LOG(Log::System, "JIT: ptrace() hack failed\n");
		g_jitAvailable = false;
	}

	// Tried checking for JIT support here with AllocateExecutableMemory and ProtectMemoryPages,
	// but it just succeeds, and then fails when you try to execute from it.

	// So, we'll just resort to a version check.
	// TODO: This seems outdated.
/*
	if (g_iosVersionMajor > 14 || (g_iosVersionMajor == 14 && g_iosVersionMinor >= 4)) {
		g_jitAvailable = false;
	} else {
		g_jitAvailable = true;
	}
*/
#endif

	// Ignore sigpipe.
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		perror("Unable to ignore SIGPIPE");
	}
	PROFILE_INIT();

	@autoreleasepool {
		return UIApplicationMain(argc, argv, NSStringFromClass([PPSSPPUIApplication class]), NSStringFromClass([AppDelegate class]));
	}
}

