#pragma once

#include <string>

#ifdef _MSC_VER

bool IsVistaOrHigher();
bool DoesVersionMatchWindows(uint32_t major, uint32_t minor, uint32_t spMajor, uint32_t spMinor, bool acceptGreater);
std::string GetWindowsVersion();
std::string GetWindowsSystemArchitecture();

#endif