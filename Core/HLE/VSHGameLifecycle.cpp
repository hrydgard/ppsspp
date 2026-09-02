// Copyright (c) 2012- PPSSPP Project.

#include "Core/HLE/VSHGameLifecycle.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "Common/CommonTypes.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/Swap.h"
#include "Common/StringUtils.h"
#include "Common/System/System.h"
#include "Core/Config.h"
#include "Core/System.h"

namespace {

// PSP 6.60/6.61 vshmain accepts either its ordinary 0x20-byte Init block or
// the extended 0x400-byte block used by loadexec/CFW return paths.  Word 16 in
// the extended form selects the no-cold-logo startup while retaining all VSH
// and opening_plugin initialization.
constexpr size_t VSH_MAIN_ARGS_SIZE = 0x400;
constexpr size_t VSH_MAIN_DEFAULT_ARGS_SIZE = 0x20;

bool returnToVsh = false;
bool freshVshBootPending = false;
bool returnedVshActive = false;
Path mountedUmdPath;
std::string lastGuestPath;
u64 transitionSequence = 0;
u64 freshVshBootCount = 0;
bool nativeImposeActive = false;
std::array<u32_le, VSH_MAIN_ARGS_SIZE / sizeof(u32_le)> preservedVshMainArgs{};
size_t preservedVshMainArgsSize = 0;

void ResetPreservedVshMainArgs() {
	preservedVshMainArgs.fill(0);
	preservedVshMainArgs[0] = (u32)VSH_MAIN_DEFAULT_ARGS_SIZE;
	preservedVshMainArgs[1] = (u32)VSH_MAIN_DEFAULT_ARGS_SIZE;
	preservedVshMainArgsSize = VSH_MAIN_DEFAULT_ARGS_SIZE;
}

std::string PathForDiagnostics(std::string_view guestPath) {
	if (startsWithNoCase(guestPath, "ms0:") || startsWithNoCase(guestPath, "fatms0:") ||
		startsWithNoCase(guestPath, "disc0:") || startsWithNoCase(guestPath, "umd0:") ||
		startsWithNoCase(guestPath, "umd1:")) {
		return std::string(guestPath);
	}
	return "<host target>";
}

Path ResolveMemoryStickPath(std::string_view guestPath) {
	std::string relative(guestPath.substr(4));
	while (!relative.empty() && (relative.front() == '/' || relative.front() == '\\')) {
		relative.erase(relative.begin());
	}
	std::replace(relative.begin(), relative.end(), '\\', '/');
	return g_Config.memStickDirectory / relative;
}

bool ResolveGuestBootPath(std::string_view guestPath, Path *hostPath, std::string *errorString) {
	if (guestPath.empty()) {
		*errorString = "empty VSH launch path";
		return false;
	}
	if (startsWithNoCase(guestPath, "ms0:") || startsWithNoCase(guestPath, "fatms0:")) {
		*hostPath = ResolveMemoryStickPath(guestPath.substr(startsWithNoCase(guestPath, "fatms0:") ? 3 : 0));
	} else if (startsWithNoCase(guestPath, "disc0:") || startsWithNoCase(guestPath, "umd0:") || startsWithNoCase(guestPath, "umd1:")) {
		if (mountedUmdPath.empty()) {
			*errorString = "VSH requested a disc launch with no mounted UMD";
			return false;
		}
		*hostPath = mountedUmdPath;
	} else {
		*hostPath = Path(guestPath);
	}

	if (File::IsDirectory(*hostPath)) {
		const Path eboot = *hostPath / "EBOOT.PBP";
		if (File::Exists(eboot)) {
			*hostPath = eboot;
		}
	}
	if (!File::Exists(*hostPath)) {
		*errorString = "resolved VSH launch target does not exist: " + PathForDiagnostics(guestPath);
		return false;
	}
	return true;
}

}  // namespace

void VSHGameLifecycleResetSession() {
	returnToVsh = false;
	freshVshBootPending = false;
	returnedVshActive = false;
	mountedUmdPath = Path();
	lastGuestPath.clear();
	transitionSequence = 0;
	freshVshBootCount = 0;
	nativeImposeActive = false;
	ResetPreservedVshMainArgs();
}

void VSHGameLifecycleSetMountedUmd(const Path &path) {
	mountedUmdPath = path;
}

bool VSHGameLifecycleRequestBoot(std::string_view guestPath, std::string *errorString,
	const void *vshMainArgs, size_t vshMainArgsSize) {
	Path hostPath;
	if (!ResolveGuestBootPath(guestPath, &hostPath, errorString)) {
		return false;
	}
	returnToVsh = true;
	freshVshBootPending = false;
	returnedVshActive = false;
	nativeImposeActive = false;
	// This models Init's chunk 0 for the new game generation.  A real explicit
	// VSHMAIN payload is copied before the asynchronous host boot is posted.
	ResetPreservedVshMainArgs();
	VSHGameLifecyclePreserveVshMainArgs(vshMainArgs, vshMainArgsSize);
	lastGuestPath = PathForDiagnostics(guestPath);
	transitionSequence++;
	NOTICE_LOG(Log::System, "Direct VSH lifecycle #%llu: shut down VSH and boot %s",
		(unsigned long long)transitionSequence, lastGuestPath.c_str());
	System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, hostPath.ToString());
	return true;
}

bool VSHGameLifecycleRequestReturn(std::string *errorString) {
	if (!returnToVsh) {
		*errorString = "current game was not launched by Direct VSH";
		return false;
	}
	const Path vshPath = VSHGameLifecycleVshPath();
	if (!File::Exists(vshPath)) {
		*errorString = "Direct VSH firmware is no longer available";
		return false;
	}
	transitionSequence++;
	freshVshBootPending = true;
	returnedVshActive = true;
	NOTICE_LOG(Log::System, "Direct VSH lifecycle #%llu: shut down %s and start a fresh VSH",
		(unsigned long long)transitionSequence, lastGuestPath.empty() ? "game" : lastGuestPath.c_str());
	System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, vshPath.ToString());
	return true;
}

Path VSHGameLifecycleVshPath() {
	return g_Config.nandRootDirectory / "flash0/vsh/module/vshmain.prx";
}

bool VSHGameLifecycleShouldReturn() {
	return returnToVsh;
}

bool VSHGameLifecycleFreshVshBootPending() {
	return freshVshBootPending;
}

void VSHGameLifecyclePreserveVshMainArgs(const void *args, size_t size) {
	if (!args || size == 0) {
		return;
	}
	const size_t copySize = std::min(size, VSH_MAIN_ARGS_SIZE);
	preservedVshMainArgs.fill(0);
	memcpy(preservedVshMainArgs.data(), args, copySize);
	preservedVshMainArgsSize = copySize;
}

const void *VSHGameLifecyclePrepareReturnVshMainArgs(size_t *size) {
	if (!returnedVshActive) {
		*size = 0;
		return nullptr;
	}
	if (preservedVshMainArgsSize == 0) {
		ResetPreservedVshMainArgs();
	}

	// This is the public vshmain startup contract used independently by ARK,
	// PRO CFW, and Adrenaline.  It changes the guest boot reason; it does not
	// patch, omit, accelerate, or synthesize completion for opening_plugin.
	preservedVshMainArgs[0] = (u32)VSH_MAIN_ARGS_SIZE;
	preservedVshMainArgs[1] = (u32)VSH_MAIN_DEFAULT_ARGS_SIZE;
	preservedVshMainArgs[16] = 1;
	*size = VSH_MAIN_ARGS_SIZE;
	return preservedVshMainArgs.data();
}

void VSHGameLifecycleSetNativeImposeActive(bool active) {
	nativeImposeActive = active;
}

bool VSHGameLifecycleNativeImposeActive() {
	return nativeImposeActive;
}

void VSHGameLifecycleNotifyVshBooted() {
	const bool returnedFromGame = freshVshBootPending;
	if (returnedFromGame) {
		freshVshBootCount++;
		NOTICE_LOG(Log::System, "Direct VSH lifecycle: fresh return VSH boot #%llu started",
			(unsigned long long)freshVshBootCount);
	}
	returnToVsh = false;
	freshVshBootPending = false;
	if (!returnedFromGame) {
		returnedVshActive = false;
	}
	nativeImposeActive = false;
	lastGuestPath.clear();
}

std::string VSHGameLifecycleStatus() {
	return StringFromFormat("sequence=%llu fresh-vsh-boots=%llu return=%d return-boot=%d returned-vsh=%d vsh-args=%u native-impose=%d umd=%s game=%s",
		(unsigned long long)transitionSequence, (unsigned long long)freshVshBootCount, returnToVsh,
		freshVshBootPending, returnedVshActive, (unsigned int)preservedVshMainArgsSize, nativeImposeActive,
		mountedUmdPath.empty() ? "none" : "mounted", lastGuestPath.empty() ? "none" : lastGuestPath.c_str());
}
