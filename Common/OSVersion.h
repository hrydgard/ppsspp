#pragma once

#include <string>

#ifdef _WIN32

bool IsVistaOrHigher();
bool IsWin7OrHigher();
bool IsWin8OrHigher();
bool DoesVersionMatchWindows(uint32_t major, uint32_t minor, uint32_t spMajor, uint32_t spMinor, uint32_t build, bool acceptGreater);
bool GetVersionFromKernel32(uint32_t& major, uint32_t& minor, uint32_t& build);

std::string GetWindowsVersion();
std::string GetWindowsSystemArchitecture();

#endif
