#pragma once

#include <cstdlib>
#include <locale>

#include "ext/imgui/imgui.h"

// Adapted from the ImGui demo.
struct ImConsole {
	char                  InputBuf[256];
	ImVector<char*>       Items;
	ImVector<const char*> Commands;
	ImVector<char*>       History;
	int                   HistoryPos;    // -1: new line, 0..History.Size-1 browsing history.
	ImGuiTextFilter       Filter;
	bool                  AutoScroll;
	bool                  ScrollToBottom;

	ImConsole();
	~ImConsole();

	void ClearLog();

	void AddLog(const char* fmt, ...) IM_FMTARGS(2);

	void Draw(ImConfig &cfg);
	void ExecCommand(const char* command_line);

	int TextEditCallback(ImGuiInputTextCallbackData* data);
};
