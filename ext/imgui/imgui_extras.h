// Just some string_view and related wrappers.

#include <string_view>
#include "ext/imgui/imgui.h"

namespace ImGui {

inline void TextUnformatted(std::string_view str) {
	TextUnformatted(str.data(), str.data() + str.size());
}

}  // namespace ImGui
