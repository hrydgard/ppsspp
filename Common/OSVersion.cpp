#include "ppsspp_config.h"

#ifdef _WIN32

#if !PPSSPP_PLATFORM(UWP)
#pragma comment(lib, "version.lib")
#endif

#include <cstdint>
#include <vector>
#include "OSVersion.h"
#include "Common/CommonWindows.h"

struct WindowsReleaseInfo
{
	uint32_t major;
	uint32_t minor;
	uint32_t spMajor;
	uint32_t spMinor;
	uint32_t build;
	bool greater = false;
};

bool GetVersionFromKernel32(uint32_t &major, uint32_t &minor, uint32_t &build) {
#if PPSSPP_PLATFORM(UWP)
	return false;
#else
	DWORD handle = 0;
	DWORD verSize = GetFileVersionInfoSizeA("kernel32.dll", &handle);
	if (verSize == 0)
		return false;

	std::vector<char> verData(verSize);
	if (GetFileVersionInfoW(L"kernel32.dll", 0, verSize, &verData[0]) == 0)
		return false;

	VS_FIXEDFILEINFO *buf = nullptr;
	uint32_t sz = 0;
	if (VerQueryValueW(&verData[0], L"\\", (void **)&buf, &sz) == 0)
		return false;

	major = buf->dwProductVersionMS >> 16;
	minor = buf->dwProductVersionMS & 0xFFFF;
	build = buf->dwProductVersionLS >> 16;
	return true;
#endif
}

bool DoesVersionMatchWindows(uint32_t major, uint32_t minor, uint32_t spMajor, uint32_t spMinor, uint32_t build, bool greater) {
#if !PPSSPP_PLATFORM(UWP)
	if (spMajor == 0 && spMinor == 0) {
		// "Applications not manifested for Windows 10 will return the Windows 8 OS version value (6.2)."
		// Try to use kernel32.dll instead, for Windows 10+.  Doesn't do SP versions.
		uint32_t actualMajor, actualMinor, actualBuild;
		if (GetVersionFromKernel32(actualMajor, actualMinor, actualBuild)) {
			if (greater)
				return actualMajor > major || (major == actualMajor && actualMinor >= minor);

			// To detect Windows 11 we must check build number
			return major == actualMajor && minor == actualMinor && actualBuild >= build;
		}
	}

	uint64_t conditionMask = 0;
	OSVERSIONINFOEX osvi;
	ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));

	osvi.dwOSVersionInfoSize = sizeof(osvi);
	osvi.dwMajorVersion = major;
	osvi.dwMinorVersion = minor;
	osvi.wServicePackMajor = spMajor;
	osvi.wServicePackMinor = spMinor;
	uint32_t op = greater ? VER_GREATER_EQUAL : VER_EQUAL;

	VER_SET_CONDITION(conditionMask, VER_MAJORVERSION, op);
	VER_SET_CONDITION(conditionMask, VER_MINORVERSION, op);
	uint32_t typeMask = VER_MAJORVERSION | VER_MINORVERSION;

	if (spMajor > 0) {
		VER_SET_CONDITION(conditionMask, VER_SERVICEPACKMAJOR, op);
		typeMask |= VER_SERVICEPACKMAJOR;
	}
	if (spMinor > 0) {
		VER_SET_CONDITION(conditionMask, VER_SERVICEPACKMINOR, op);
		typeMask |= VER_SERVICEPACKMINOR;
	}

	return VerifyVersionInfo(&osvi, typeMask, conditionMask) != FALSE;

#else
	if (greater)
		return true;
	return false;
#endif
}

bool IsVistaOrHigher() {
	// Vista is 6.0
	return DoesVersionMatchWindows(6, 0, 0, 0, 0, true);
}

bool IsWin7OrHigher() {
	// Win7 is 6.1
	return DoesVersionMatchWindows(6, 1, 0, 0, 0, true);
}

bool IsWin8OrHigher() {
	// Win8 is 6.2
	return DoesVersionMatchWindows(6, 2, 0, 0, 0, true);
}

std::string GetWindowsVersion() {
	std::vector<std::pair<std::string, WindowsReleaseInfo>> windowsReleases = {
		/* { "Preview text", { major, minor, spMajor, spMinor, build, greater } }, */
		{ "Microsoft Windows XP, Service Pack 2", { 5, 1, 2, 0 } },
		{ "Microsoft Windows XP, Service Pack 3", { 5, 1, 3, 0 } },
		{ "Microsoft Windows Vista", { 6, 0, 0, 0 } },
		{ "Microsoft Windows Vista, Service Pack 1", { 6, 0, 1, 0 } },
		{ "Microsoft Windows Vista, Service Pack 2", { 6, 0, 2, 0 } },
		{ "Microsoft Windows 7", { 6, 1, 0, 0 } },
		{ "Microsoft Windows 7, Service Pack 1", { 6, 1, 1, 0 } },
		{ "Microsoft Windows 8", { 6, 2, 0, 0 } },
		{ "Microsoft Windows 8.1", { 6, 3, 0, 0 } },
		{ "Microsoft Windows 10", { 10, 0, 0, 0 } },
		{ "Microsoft Windows 11", { 10, 0, 0, 0, 22000 } },
	};

	// Start from higher to lower
	for (auto release = rbegin(windowsReleases); release != rend(windowsReleases); ++release) {
		WindowsReleaseInfo releaseInfo = release->second;
		if (DoesVersionMatchWindows(releaseInfo.major, releaseInfo.minor, releaseInfo.spMajor, releaseInfo.spMinor, releaseInfo.build, releaseInfo.greater)) {
			std::string previewText = release->first;
			return previewText;
		}
	}

	return "Unknown version of Microsoft Windows.";
}

std::string GetWindowsSystemArchitecture() {
	SYSTEM_INFO sysinfo;
	ZeroMemory(&sysinfo, sizeof(SYSTEM_INFO));
	GetNativeSystemInfo(&sysinfo);

	switch (sysinfo.wProcessorArchitecture) {
	case PROCESSOR_ARCHITECTURE_INTEL:
		return "(x86)";
	case PROCESSOR_ARCHITECTURE_AMD64:
		return "(x64)";
	case PROCESSOR_ARCHITECTURE_ARM:
		return "(ARM)";
#ifdef PROCESSOR_ARCHITECTURE_ARM64
	case PROCESSOR_ARCHITECTURE_ARM64:
		return "(ARM64)";
#endif
	default:
		return "(Unknown)";
	}
}

#endif
