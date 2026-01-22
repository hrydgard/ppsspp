// Just some string_view and related wrappers.

#include <string_view>
#include "ext/imgui/imgui.h"

namespace ImGui {

inline void TextUnformatted(std::string_view str) {
	TextUnformatted(str.data(), str.data() + str.size());
}

bool RepeatButton(const char *title);
int RepeatButtonShift(const char* label, float repeatRate = 0.05f);

bool CollapsingHeaderWithCount(const char *title, int count, ImGuiTreeNodeFlags flags = 0);

}  // namespace ImGui
