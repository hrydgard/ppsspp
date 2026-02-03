#pragma once

#include "Common/CommonWindows.h"

bool InitializeDualSense(HANDLE handle, int outReportSize);
bool ShutdownDualsense(HANDLE handle, int outReportSize);
bool ReadDualSenseInput(HANDLE handle, HIDControllerState *state, int inReportSize);
