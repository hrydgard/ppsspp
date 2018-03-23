#pragma once

#include <string>

#ifdef _WIN32

bool IsVistaOrHigher();
bool DoesVersionMatchWindows(uint32_t major, uint32_t minor, uint32_t spMajor, uint32_t spMinor, bool acceptGreater);
std::string GetWindowsVersion();
std::string GetWindowsSystemArchitecture();

#endif
