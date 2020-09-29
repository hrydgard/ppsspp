#pragma once

#include <string>

// Generic function to get last error message.
// Call directly after the command or use the error num.
// This function might change the error code.
// Defined in Misc.cpp.
std::string GetLastErrorMsg();
std::string GetStringErrorMsg(int errCode);
