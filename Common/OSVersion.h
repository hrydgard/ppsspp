#pragma once

#include <string>

#ifdef _WIN32

bool IsVistaOrHigher();
bool IsWin7OrHigher();
bool DoesVersionMatchWindows(uint32_t major, uint32_t minor, uint32_t spMajor, uint32_t spMinor, bool acceptGreater);

std::string GetWindowsVersion();

// Platforms like UWP has their own API, 
// so we use versionInfo to pass version info from outside
// on Win32 it will use kernel (normal behavior) when versionInfo is 0
std::string GetWindowsVersion(uint64_t versionInfo);
std::string GetWindowsVersion(uint32_t& outMajor, uint32_t& outMinor, uint32_t& outBuild);
std::string GetWindowsVersion(uint64_t versionInfo, uint32_t& outMajor, uint32_t& outMinor, uint32_t& outBuild);

std::string GetWindowsSystemArchitecture();

#endif
