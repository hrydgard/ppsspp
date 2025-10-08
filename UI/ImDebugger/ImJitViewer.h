#pragma once

#include <cstdint>
#include <vector>
#include "ext/imgui/imgui.h"

#include "Core/Debugger/DebugInterface.h"

struct ImConfig;
struct ImControl;

class ImJitViewerWindow {
public:
	void Draw(ImConfig &cfg, ImControl &control);
	const char *Title() const {
		return "JIT Viewer";
	}
};
