#pragma once

#include "ext/imgui/imgui.h"

#include "Common/GhidraClient.h"
#include "Core/MIPS/MIPSDebugInterface.h"

class ImStructViewer {
	struct Watch {
		std::string expression;
		u32 address = 0;
		std::string typePathName;
		std::string name;
	};

	struct NewWatch {
		char name[256] = {};
		std::string typeDisplayName;
		std::string typePathName;
		char expression[256] = {};
		bool dynamic = false;
		std::string error;
		ImGuiTextFilter typeFilter;
	};

public:
	void Draw(MIPSDebugInterface* mipsDebug, bool* open);

private:
	MIPSDebugInterface* mipsDebug_ = nullptr;

	ImGuiTextFilter globalFilter_;
	ImGuiTextFilter watchFilter_;

	GhidraClient ghidraClient_;
	char ghidraHost_[128] = "localhost";
	int ghidraPort_ = 18489;
	bool fetchedAtLeastOnce_ = false;

	std::vector<Watch> watches_;
	int removeWatchIndex_ = -1;
	Watch addWatch_;
	NewWatch newWatch_;

	void DrawConnectionSetup();

	void DrawStructViewer();

	void DrawGlobals();

	void DrawWatch();

	void DrawNewWatchEntry();

	void DrawType(
		u32 base,
		u32 offset,
		const std::string& typePathName,
		const char* typeDisplayNameOverride,
		const char* name,
		int watchIndex,
		ImGuiTreeNodeFlags extraTreeNodeFlags = 0);

	void DrawContextMenu(
		u32 base,
		u32 offset,
		int length,
		const std::string& typePathName,
		const char* name,
		int watchIndex,
		const u64* value);
};
