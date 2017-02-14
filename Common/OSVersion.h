#pragma once

#include <string>

#ifdef _MSC_VER

bool DoesVersionMatchWindows(uint32_t major, uint32_t minor, uint32_t spMajor = 0, uint32_t spMinor = 0);
std::string GetWindowsVersion();
std::string GetWindowsSystemArchitecture();

#endif