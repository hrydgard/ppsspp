#pragma once

#include <cstdlib>
#include <locale>

#include "ext/imgui/imgui.h"

struct ImConfig;

// Adapted from the ImGui demo.
class ImConsole {
public:
	ImConsole();
	~ImConsole();

	void Draw(ImConfig &cfg);
	void ExecCommand(const char* command_line);

	int TextEditCallback(ImGuiInputTextCallbackData* data);

private:
	char                  InputBuf[256];
	ImVector<const char*> Commands;
	ImVector<char*>       History;
	int                   HistoryPos;    // -1: new line, 0..History.Size-1 browsing history.
	ImGuiTextFilter       Filter;
	bool                  AutoScroll;
	bool                  ScrollToBottom;
};
