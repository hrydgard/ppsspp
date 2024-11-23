#pragma once

#include "ext/imgui/imgui.h"

#include "Common/GhidraClient.h"
#include "Core/MIPS/MIPSDebugInterface.h"

// Struct viewer visualizes objects data in game memory using types and symbols fetched from a Ghidra project.
// It also allows to set memory breakpoints and edit field values which is helpful when reverse engineering unknown
// types.
//
// To use this you will need to install an unofficial Ghidra extension "ghidra-rest-api" by Kotcrab.
// (available at https://github.com/kotcrab/ghidra-rest-api). After installing the extension and starting the API
// server in Ghidra you can open the Struct viewer window and press the "Connect" button to start using it.
//
// See the original pull request https://github.com/hrydgard/ppsspp/pull/19629 for a screenshot and how to test this
// without the need to set up Ghidra.
class ImStructViewer {
	struct Watch {
		int id = 0;
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
	bool fetchedAtLeastOnce_ = false; // True if fetched from Ghidra successfully at least once

	std::vector<Watch> watches_;
	int nextWatchId_ = 0; // ID value to use when creating new watch entry
	int removeWatchId_ = -1; // Watch entry id to be removed on next draw
	Watch addWatch_; // Temporary variable to store watch entry added from the Globals tab
	NewWatch newWatch_; // State for the new watch entry UI

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
		int watchId,
		ImGuiTreeNodeFlags extraTreeNodeFlags = 0);

	void DrawIndexedMembers(
		u32 base,
		u32 offset,
		const std::string& typePathName,
		const char* name,
		u32 elementCount,
		int elementLength,
		bool openFirst);

	void DrawContextMenu(
		u32 base,
		u32 offset,
		int length,
		const std::string& typePathName,
		const char* name,
		int watchId,
		const u64* value);
};
