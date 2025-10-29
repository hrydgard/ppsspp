#pragma once

#include <cstdlib>
#include <locale>

#include "ext/imgui/imgui.h"

struct ImConfig;

// Adapted from the ImGui demo.
// Used to interact with Lua contexts.

// TODO: Add a selector to choose different Lua contexts (e.g. for different plugins or the global one).
// Though loading multiple different plugins seems like a recipe for confusion.
class ImConsole {
public:
	ImConsole();
	~ImConsole();

	void Draw(ImConfig &cfg);
	void ExecCommand(const char *command_line);

	int TextEditCallback(ImGuiInputTextCallbackData *data);

private:
	char inputBuf_[512];
	ImVector<char*> history_;
	int historyPos_;    // -1: new line, 0..History.Size-1 browsing history.
	ImGuiTextFilter filter_;
	bool autoScroll_;
	bool scrollToBottom_;
};
