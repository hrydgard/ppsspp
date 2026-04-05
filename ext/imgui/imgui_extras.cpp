// Just some string_view and related wrappers.

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include "ext/imgui/imgui.h"

namespace ImGui {

bool RepeatButton(const char *title) {
	if (ImGui::SmallButton(title)) {
		return true;
	}
	if (ImGui::IsItemActive()) {
		return true;
	}
	return false;
}

int RepeatButtonShift(const char* label, float repeatRate) {
	bool clicked = ImGui::Button(label);

	bool shiftHeld = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
	bool ctrlHeld = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
	bool held = ImGui::IsItemActive() && shiftHeld;

	static float repeatDelay = 0.25f;  // seconds before repeating starts
	static ImGuiID activeId = 0;
	static float holdTimer = 0.0f;

	ImGuiID id = ImGui::GetItemID();
	if (held) {
		if (activeId != id) {
			activeId = id;
			holdTimer = 0.0f;
		} else {
			holdTimer += ImGui::GetIO().DeltaTime;
			if (holdTimer >= repeatDelay) {
				float t = holdTimer - repeatDelay;
				int steps = static_cast<int>(t / repeatRate);
				static int lastStep = -1;
				if (steps != lastStep) {
					int count = steps - lastStep;
					lastStep = steps;
					return count;
				}
			}
		}
	} else {
		// Reset if not holding
		if (activeId == id) {
			activeId = 0;
			holdTimer = 0.0f;
		}
	}

	return clicked ? 1 : 0;
}

bool CollapsingHeaderWithCount(const char *title, int count, ImGuiTreeNodeFlags flags) {
	char temp[256];
	snprintf(temp, sizeof(temp), "%s (%d)##%s", title, count, title);
	return ImGui::CollapsingHeader(temp, flags);
}

}  // namespace ImGui
