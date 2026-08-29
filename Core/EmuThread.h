// Copyright (c) 2013- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <functional>
#include <string>
#include <thread>

#include "Common/System/Application.h"

// Utilities to manage Emu and Render threads.
// TODO: Use across platforms, currently Windows-only.

bool MainThread_Ready();

class GraphicsContext;
struct WindowDesc;

// Doesn't take ownership of the graphicsContext, you have to delete it.
// This should be used by platforms that launch a separate thread and doesn't
// need to run a polling loop in it.
// NOTE: Does take ownership over Application (which is just a wrapper for NativeInitGraphics/NativeShutdownGraphics/NativeFrame).
// On failure (which currently only means the graphics surface couldn't be initialized), errorMessage
// gets the reason - pass it on to the user, it's the only place it's available.
bool MainThreadFunc(GraphicsContext * graphicsContext, Application *application, const WindowDesc &windowDesc, std::function<bool(GraphicsContext *)> frame, std::string *errorMessage);

// If you're not using MainThreadFunc, you can at least use these to manage a spinning EmuThread (that calls NativeFrame),
// whether your graphics context requires multithreading or not. Then use RunMainLoop to implement your main loop for
// the case where a separate EmuThread is not needed.
// NOTE: Does take ownership over Application (which is just a wrapper for NativeInitGraphics/NativeShutdownGraphics/NativeFrame).
std::thread EmuThread_Start(GraphicsContext *graphicsContext, Application *application, std::function<bool(GraphicsContext *)> frame);
void EmuThread_RequestExit();  // Useful when the render thread is in control like on Android.
void EmuThread_Join(GraphicsContext *graphicsContext, std::thread &emuThread);

// Call from the main thread.
// NOTE: Does take ownership over Application (which is just a wrapper for NativeInitGraphics/NativeShutdownGraphics/NativeFrame).
bool RunMainLoop(GraphicsContext *graphicsContext, Application *application, std::function<bool(GraphicsContext *)> frame);
