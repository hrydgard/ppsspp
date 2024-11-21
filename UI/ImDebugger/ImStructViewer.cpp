#include <regex>
#include <sstream>
#include <unordered_map>
#include <cctype>

#include "ext/imgui/imgui.h"

#include "Common/System/Request.h"
#include "Core/MemMap.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MIPS/MIPSDebugInterface.h"

#include "UI/ImDebugger/ImStructViewer.h"

static auto COLOR_GRAY = ImVec4(0.45f, 0.45f, 0.45f, 1);
static auto COLOR_RED = ImVec4(1, 0, 0, 1);

enum class BuiltInType {
	Bool,
	Char,
	Int8,
	Int16,
	Int32,
	Int64,
	TerminatedString,
	Float,
	Void,
};

struct BuiltIn {
	BuiltInType type;
	ImGuiDataType imGuiType;
	const char* hexFormat;
};

static const std::unordered_map<std::string, BuiltIn> knownBuiltIns = {
	{"/bool", {BuiltInType::Bool, ImGuiDataType_U8, "%hhx"}},

	{"/char", {BuiltInType::Char, ImGuiDataType_S8, "%hhx"}},
	{"/uchar", {BuiltInType::Char, ImGuiDataType_U8, "%hhx"}},

	{"/byte", {BuiltInType::Int8, ImGuiDataType_U8, "%hhx"}},
	{"/sbyte", {BuiltInType::Int8, ImGuiDataType_S8, "%hhx"}},
	{"/undefined1", {BuiltInType::Int8, ImGuiDataType_U8, "%hhx"}},

	{"/word", {BuiltInType::Int16, ImGuiDataType_U16, "%hx"}},
	{"/sword", {BuiltInType::Int16, ImGuiDataType_S16, "%hx"}},
	{"/ushort", {BuiltInType::Int16, ImGuiDataType_U16, "%hx"}},
	{"/short", {BuiltInType::Int16, ImGuiDataType_S16, "%hx"}},
	{"/undefined2", {BuiltInType::Int16, ImGuiDataType_U16, "%hx"}},

	{"/dword", {BuiltInType::Int32, ImGuiDataType_U32, "%lx"}},
	{"/sdword", {BuiltInType::Int32, ImGuiDataType_S32, "%lx"}},
	{"/uint", {BuiltInType::Int32, ImGuiDataType_U32, "%lx"}},
	{"/int", {BuiltInType::Int32, ImGuiDataType_S32, "%lx"}},
	{"/ulong", {BuiltInType::Int32, ImGuiDataType_U32, "%lx"}},
	{"/long", {BuiltInType::Int32, ImGuiDataType_S32, "%lx"}},
	{"/undefined4", {BuiltInType::Int32, ImGuiDataType_U32, "%lx"}},

	{"/qword", {BuiltInType::Int64, ImGuiDataType_U64, "%llx"}},
	{"/sqword", {BuiltInType::Int64, ImGuiDataType_S64, "%llx"}},
	{"/ulonglong", {BuiltInType::Int64, ImGuiDataType_U64, "%llx"}},
	{"/longlong", {BuiltInType::Int64, ImGuiDataType_S64, "%llx"}},
	{"/undefined8", {BuiltInType::Int64, ImGuiDataType_U64, "%llx"}},

	{"/TerminatedCString", {BuiltInType::TerminatedString, -1, nullptr}},

	{"/float", {BuiltInType::Float, ImGuiDataType_Float, nullptr}},
	{"/float4", {BuiltInType::Float, ImGuiDataType_Float, nullptr}},

	{"/void", {BuiltInType::Void, -1, nullptr}},
};

static void DrawBuiltInEditPopup(const BuiltIn& builtIn, const u32 address) {
	if (builtIn.imGuiType == -1) {
		return;
	}
	ImGui::OpenPopupOnItemClick("edit", ImGuiPopupFlags_MouseButtonRight);
	if (ImGui::BeginPopup("edit")) {
		if (ImGui::Selectable("Set to zero")) {
			switch (builtIn.type) {
				case BuiltInType::Bool:
				case BuiltInType::Char:
				case BuiltInType::Int8:
					Memory::Write_U8(0, address);
					break;
				case BuiltInType::Int16:
					Memory::Write_U16(0, address);
					break;
				case BuiltInType::Int32:
					Memory::Write_U32(0, address);
					break;
				case BuiltInType::Int64:
					Memory::Write_U64(0, address);
					break;
				case BuiltInType::Float:
					Memory::Write_Float(0, address);
					break;
				default:
					break;
			}
		}
		void* data = Memory::GetPointerWriteUnchecked(address);
		if (builtIn.hexFormat) {
			ImGui::DragScalar("Value (hex)", builtIn.imGuiType, data, 0.2f, nullptr, nullptr, builtIn.hexFormat);
		}
		ImGui::DragScalar("Value", builtIn.imGuiType, data, 0.2f);
		ImGui::EndPopup();
	}
}

static void DrawIntBuiltInEditPopup(const u32 address, const u32 length) {
	switch (length) {
		case 1:
			DrawBuiltInEditPopup(knownBuiltIns.at("/byte"), address);
			break;
		case 2:
			DrawBuiltInEditPopup(knownBuiltIns.at("/word"), address);
			break;
		case 4:
			DrawBuiltInEditPopup(knownBuiltIns.at("/dword"), address);
			break;
		case 8:
			DrawBuiltInEditPopup(knownBuiltIns.at("/qword"), address);
			break;
		default:
			break;
	}
}

static void DrawBuiltInContent(const BuiltIn& builtIn, const u32 address) {
	switch (builtIn.type) {
		case BuiltInType::Bool:
			ImGui::Text("= %s", Memory::Read_U8(address) ? "true" : "false");
			break;
		case BuiltInType::Char: {
			const u8 value = Memory::Read_U8(address);
			if (std::isprint(value)) {
				ImGui::Text("= %x '%c'", value, value);
			} else {
				ImGui::Text("= %x", value);
			}
			break;
		}
		case BuiltInType::Int8:
			ImGui::Text("= %x", Memory::Read_U8(address));
			break;
		case BuiltInType::Int16:
			ImGui::Text("= %x", Memory::Read_U16(address));
			break;
		case BuiltInType::Int32:
			ImGui::Text("= %x", Memory::Read_U32(address));
			break;
		case BuiltInType::Int64:
			ImGui::Text("= %llx", Memory::Read_U64(address));
			break;
		case BuiltInType::TerminatedString:
			if (Memory::IsValidNullTerminatedString(address)) {
				ImGui::Text("= \"%s\"", Memory::GetCharPointerUnchecked(address));
			} else {
				ImGui::Text("= %x <invalid string @ %x>", Memory::Read_U8(address), address);
			}
			break;
		case BuiltInType::Float:
			ImGui::Text("= %f", Memory::Read_Float(address));
			break;
		case BuiltInType::Void:
			ImGui::Text("<void type>");
		default:
			return;
	}
	DrawBuiltInEditPopup(builtIn, address);
}

static u64 ReadMemoryInt(const u32 address, const u32 length) {
	switch (length) {
		case 1:
			return Memory::Read_U8(address);
		case 2:
			return Memory::Read_U16(address);
		case 4:
			return Memory::Read_U32(address);
		case 8:
			return Memory::Read_U64(address);
		default:
			return 0;
	}
}

static constexpr int COLUMN_NAME = 0;
static constexpr int COLUMN_TYPE = 1;
static constexpr int COLUMN_CONTENT = 2;

void ImStructViewer::Draw(MIPSDebugInterface* mipsDebug, bool* open) {
	mipsDebug_ = mipsDebug;
	ImGui::SetNextWindowSize(ImVec2(750, 550), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Struct viewer", open) || !mipsDebug->isAlive() || !Memory::IsActive()) {
		ImGui::End();
		return;
	}
	if (ghidraClient_.Ready()) {
		ghidraClient_.UpdateResult();
		if (!fetchedAtLeastOnce_ && !ghidraClient_.Failed()) {
			fetchedAtLeastOnce_ = true;
		}
	}

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
	if (fetchedAtLeastOnce_) {
		DrawStructViewer();
	} else {
		DrawConnectionSetup();
	}
	ImGui::PopStyleVar();

	ImGui::End();
}

void ImStructViewer::DrawConnectionSetup() {
	ImGui::TextWrapped(R"(Struct viewer visualizes data in game memory using types from your Ghidra project.
It also allows to set memory breakpoints and edit field values which is helpful when reverse engineering unknown types.
To get started:
 1. In Ghidra install the ghidra-rest-api extension by Kotcrab.
 2. After installing the extension enable the RestApiPlugin in the Miscellaneous plugins configuration window.
 3. Open your Ghidra project and click "Start Rest API Server" in the "Tools" menu bar.
 4. Press the connect button below.
)");
	ImGui::Dummy(ImVec2(1, 6));

	ImGui::BeginDisabled(!ghidraClient_.Idle());
	ImGui::PushItemWidth(120);
	ImGui::InputText("Host", ghidraHost_, IM_ARRAYSIZE(ghidraHost_));
	ImGui::SameLine();
	ImGui::InputInt("Port", &ghidraPort_, 0);
	ImGui::SameLine();
	if (ImGui::Button("Connect")) {
		ghidraClient_.FetchAll(ghidraHost_, ghidraPort_);
	}
	ImGui::PopItemWidth();
	ImGui::EndDisabled();

	if (ghidraClient_.Idle() && ghidraClient_.Failed()) {
		ImGui::PushStyleColor(ImGuiCol_Text, COLOR_RED);
		ImGui::TextWrapped("Error: %s", ghidraClient_.result.error.c_str());
		ImGui::PopStyleColor();
	}
}

void ImStructViewer::DrawStructViewer() {
	ImGui::BeginDisabled(!ghidraClient_.Idle());
	if (ImGui::Button("Refresh data types")) {
		ghidraClient_.FetchAll(ghidraHost_, ghidraPort_);
	}
	ImGui::EndDisabled();

	if (ghidraClient_.Idle() && ghidraClient_.Failed()) {
		ImGui::PushStyleColor(ImGuiCol_Text, COLOR_RED);
		ImGui::SameLine();
		ImGui::TextWrapped("Error: %s", ghidraClient_.result.error.c_str());
		ImGui::PopStyleColor();
	}

	if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_Reorderable)) {
		if (ImGui::BeginTabItem("Globals")) {
			DrawGlobals();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Watch")) {
			DrawWatch();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

void ImStructViewer::DrawGlobals() {
	globalFilter_.Draw();
	if (ImGui::BeginTable("##globals", 3,
	                      ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
	                      ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Field");
		ImGui::TableSetupColumn("Type");
		ImGui::TableSetupColumn("Content");
		ImGui::TableHeadersRow();

		for (const auto& symbol : ghidraClient_.result.symbols) {
			if (!symbol.label || !symbol.userDefined || symbol.dataTypePathName.empty()) {
				continue;
			}
			if (!globalFilter_.PassFilter(symbol.name.c_str())) {
				continue;
			}
			DrawType(symbol.address, 0, symbol.dataTypePathName, nullptr, symbol.name.c_str(), -1);
		}

		ImGui::EndTable();
	}
}

void ImStructViewer::DrawWatch() {
	DrawNewWatchEntry();
	ImGui::Dummy(ImVec2(1, 6));

	watchFilter_.Draw();
	if (ImGui::BeginTable("##watch", 3,
	                      ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
	                      ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Field");
		ImGui::TableSetupColumn("Type");
		ImGui::TableSetupColumn("Content");
		ImGui::TableHeadersRow();

		int watchIndex = -1;
		for (const auto& watch : watches_) {
			watchIndex++;
			if (!watchFilter_.PassFilter(watch.name.c_str())) {
				continue;
			}
			u32 address = 0;
			if (!watch.expression.empty()) {
				u32 val;
				PostfixExpression postfix;
				if (mipsDebug_->initExpression(watch.expression.c_str(), postfix)
				    && mipsDebug_->parseExpression(postfix, val)) {
					address = val;
				}
			} else {
				address = watch.address;
			}
			DrawType(address, 0, watch.typePathName, nullptr, watch.name.c_str(), watchIndex);
		}
		if (removeWatchIndex_ != -1) {
			watches_.erase(watches_.begin() + removeWatchIndex_);
			removeWatchIndex_ = -1;
		}
		if (addWatch_.address != 0) {
			watches_.push_back(addWatch_);
			addWatch_ = Watch();
		}

		ImGui::EndTable();
	}
}

void ImStructViewer::DrawNewWatchEntry() {
	ImGui::PushItemWidth(150);
	ImGui::InputText("Name", newWatch_.name, IM_ARRAYSIZE(newWatch_.name));

	ImGui::SameLine();
	if (ImGui::BeginCombo("Type", newWatch_.typeDisplayName.c_str())) {
		if (ImGui::IsWindowAppearing()) {
			ImGui::SetKeyboardFocusHere(0);
		}
		newWatch_.typeFilter.Draw();
		for (const auto& entry : ghidraClient_.result.types) {
			const auto& type = entry.second;
			if (newWatch_.typeFilter.PassFilter(type.displayName.c_str())) {
				ImGui::PushID(type.pathName.c_str());
				if (ImGui::Selectable(type.displayName.c_str(), newWatch_.typePathName == type.pathName)) {
					newWatch_.typePathName = type.pathName;
					newWatch_.typeDisplayName = type.displayName;
				}
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && ImGui::BeginTooltip()) {
					ImGui::Text("%s (%s)", type.displayName.c_str(), type.pathName.c_str());
					ImGui::Text("Length: %x (aligned: %x)", type.length, type.alignedLength);
					ImGui::EndTooltip();
				}
				ImGui::PopID();
			}
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	ImGui::InputText("Expression", newWatch_.expression, IM_ARRAYSIZE(newWatch_.expression));
	ImGui::SameLine();
	ImGui::Checkbox("Dynamic", &newWatch_.dynamic);
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_DelayNormal)) {
		ImGui::SetTooltip("When checked the expression will be\nre-evaluated on each frame.");
	}

	ImGui::PopItemWidth();

	ImGui::SameLine();
	if (ImGui::Button("Add watch")) {
		u32 val;
		PostfixExpression postfix;
		if (newWatch_.typePathName.empty()) {
			newWatch_.error = "type can't be empty";
		} else if (!mipsDebug_->initExpression(newWatch_.expression, postfix)
		           || !mipsDebug_->parseExpression(postfix, val)) {
			newWatch_.error = "invalid expression";
		} else {
			std::string watchName = newWatch_.name;
			if (watchName.empty()) {
				watchName = "<watch>";
			}
			watches_.emplace_back(Watch{
				newWatch_.dynamic ? newWatch_.expression : "",
				newWatch_.dynamic ? 0 : val,
				newWatch_.typePathName,
				newWatch_.dynamic ? watchName + " (" + newWatch_.expression + ")" : watchName
			});
			memset(newWatch_.name, 0, sizeof(newWatch_.name));
			memset(newWatch_.expression, 0, sizeof(newWatch_.name));
			newWatch_.dynamic = false;
			newWatch_.error = "";
			newWatch_.typeFilter.Clear();
			// Not clearing the actual selected type on purpose here, user will have to reselect one anyway and
			// maybe there is a chance they will reuse the current one
		}
	}
	if (!newWatch_.error.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, COLOR_RED);
		ImGui::TextWrapped("Error: %s", newWatch_.error.c_str());
		ImGui::PopStyleColor();
	}
}

static void DrawTypeColumn(
	const std::string& format,
	const std::string& typeDisplayName,
	const u32 base,
	const u32 offset
) {
	ImGui::TableSetColumnIndex(COLUMN_TYPE);
	ImGui::Text(format.c_str(), typeDisplayName.c_str());
	ImGui::PushStyleColor(ImGuiCol_Text, COLOR_GRAY);
	ImGui::SameLine();
	if (offset != 0) {
		ImGui::Text("@ %x+%x", base, offset);
	} else {
		ImGui::Text("@ %x", base);
	}
	ImGui::PopStyleColor();
}

static void DrawArrayContent(
	const std::unordered_map<std::string, GhidraType>& types,
	const GhidraType& type,
	const u32 address
) {
	if (type.arrayElementLength != 1 || !types.count(type.arrayTypePathName)) {
		return;
	}
	const auto& arrayType = types.at(type.arrayTypePathName);
	bool charElement = false;
	if (arrayType.kind == TYPEDEF && types.count(arrayType.typedefBaseTypePathName)) {
		const auto& baseArrayType = types.at(arrayType.typedefBaseTypePathName);
		charElement = baseArrayType.pathName == "/char";
	} else {
		charElement = arrayType.pathName == "/char";
	}
	if (!charElement) {
		return;
	}
	const char* charPointer = Memory::GetCharPointerUnchecked(address);
	std::string text(charPointer, charPointer + type.arrayElementCount);
	text = std::regex_replace(text, std::regex("\n"), "\\n");
	ImGui::Text("= \"%s\"", text.c_str());
}

static void DrawPointerText(const u32 value) {
	if (Memory::IsValidAddress(value)) {
		ImGui::Text("* %x", value);
		return;
	}
	ImGui::PushStyleColor(ImGuiCol_Text, COLOR_GRAY);
	if (value == 0) {
		ImGui::Text("* NULL");
	} else {
		ImGui::Text("* <invalid pointer: %x>", value);
	}
	ImGui::PopStyleColor();
}

static void DrawPointerContent(
	const std::unordered_map<std::string, GhidraType>& types,
	const GhidraType& type,
	const u32 value
) {
	if (!types.count(type.pointerTypePathName)) {
		DrawPointerText(value);
		return;
	}
	const auto& pointedType = types.at(type.pointerTypePathName);
	bool charPointerElement = false;
	if (pointedType.kind == TYPEDEF && types.count(pointedType.typedefBaseTypePathName)) {
		const auto& basePointedType = types.at(pointedType.typedefBaseTypePathName);
		charPointerElement = basePointedType.pathName == "/char";
	} else {
		charPointerElement = pointedType.pathName == "/char";
	}
	if (!charPointerElement || !Memory::IsValidNullTerminatedString(value)) {
		DrawPointerText(value);
		return;
	}
	const char* charPointer = Memory::GetCharPointerUnchecked(value);
	std::string text(charPointer);
	text = std::regex_replace(text, std::regex("\n"), "\\n");
	ImGui::Text("= \"%s\"", text.c_str());
}

// Formatting enum value to a nice string as it would look in code with 'or' operator (e.g. "ALIGN_TOP | ALIGN_LEFT")
static std::string FormatEnumValue(const std::vector<GhidraEnumMember>& enumMembers, const u64 value) {
	std::stringstream ss;
	bool hasPrevious = false;
	for (const auto& member : enumMembers) {
		if (value & member.value) {
			if (hasPrevious) {
				ss << " | ";
			}
			ss << member.name;
			hasPrevious = true;
		}
	}
	return ss.str();
}

// This will be potentially called a lot of times in a frame so not using string here
static void FormatIndexedMember(char* buffer, const size_t bufferSize, const std::string& name, const u32 index) {
	snprintf(buffer, bufferSize, "%s[%x]", name.c_str(), index);
}

void ImStructViewer::DrawType(
	const u32 base,
	const u32 offset,
	const std::string& typePathName,
	const char* typeDisplayNameOverride,
	const char* name,
	const int watchIndex,
	const ImGuiTreeNodeFlags extraTreeNodeFlags
) {
	const auto& types = ghidraClient_.result.types;
	// Generic pointer is not included in the type listing, need to resolve it manually to void*
	if (typePathName == "/pointer") {
		DrawType(base, offset, "/void *", "pointer", name, watchIndex);
		return;
	}
	// Undefined itself doesn't exist as a type, let's just display first byte in that case
	if (typePathName == "/undefined") {
		DrawType(base, offset, "/undefined1", "undefined", name, watchIndex);
		return;
	}

	const bool hasType = types.count(typePathName) != 0;

	// Resolve typedefs as early as possible
	if (hasType) {
		const auto& type = types.at(typePathName);
		if (type.kind == TYPEDEF) {
			DrawType(base, offset, type.typedefBaseTypePathName, type.displayName.c_str(), name,
			         watchIndex);
			return;
		}
	}

	const u32 address = base + offset;
	ImGui::PushID(static_cast<int>(address));
	ImGui::PushID(watchIndex); // We push watch index too as it's possible to have multiple watches on the same address

	// Text and Tree nodes are less high than framed widgets, using AlignTextToFramePadding() we add vertical spacing
	// to make the tree lines equal high.
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(COLUMN_NAME);
	ImGui::AlignTextToFramePadding();
	// Flags used for nodes that can't be further opened
	const ImGuiTreeNodeFlags leafFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
	                                     extraTreeNodeFlags;

	// Type is missing in fetched types, this can happen e.g. if type used for watch is removed from Ghidra
	if (!hasType) {
		ImGui::TreeNodeEx("Field", leafFlags, "%s", name);
		DrawContextMenu(base, offset, 0, typePathName, name, watchIndex, nullptr);
		ImGui::PushStyleColor(ImGuiCol_Text, COLOR_RED);
		DrawTypeColumn("<missing type: %s>", typePathName, base, offset);
		ImGui::PopStyleColor();
		ImGui::PopID();
		ImGui::PopID();
		return;
	}

	const auto& type = types.at(typePathName);
	const std::string typeDisplayName =
			typeDisplayNameOverride == nullptr ? type.displayName : typeDisplayNameOverride;

	// Handle cases where pointers or expressions point to invalid memory
	if (!Memory::IsValidAddress(address)) {
		ImGui::TreeNodeEx("Field", leafFlags, "%s", name);
		DrawContextMenu(base, offset, type.alignedLength, typePathName, name, watchIndex, nullptr);
		DrawTypeColumn("%s", typeDisplayName, base, offset);
		ImGui::TableSetColumnIndex(COLUMN_CONTENT);
		ImGui::PushStyleColor(ImGuiCol_Text, COLOR_GRAY);
		ImGui::Text("<invalid address: %x>", address);
		ImGui::PopStyleColor();
		ImGui::PopID();
		ImGui::PopID();
		return;
	}

	// For each type we create tree node with the field name and fill type column
	// Content column and edit popup is only set for types where it makes sense
	switch (type.kind) {
		case ENUM: {
			ImGui::TreeNodeEx("Enum", leafFlags, "%s", name);
			const u64 enumValue = ReadMemoryInt(address, type.length);
			DrawContextMenu(base, offset, type.alignedLength, typePathName, name, watchIndex, &enumValue);
			DrawTypeColumn("%s", typeDisplayName, base, offset);

			ImGui::TableSetColumnIndex(COLUMN_CONTENT);
			const std::string enumString = FormatEnumValue(type.enumMembers, enumValue);
			ImGui::Text("= %llx (%s)", enumValue, enumString.c_str());
			DrawIntBuiltInEditPopup(address, type.length);
			break;
		}
		case POINTER: {
			const bool nodeOpen = ImGui::TreeNodeEx("Pointer", extraTreeNodeFlags, "%s", name);
			const u32 pointer = Memory::Read_U32(address);
			const u64 pointer64 = pointer;
			DrawContextMenu(base, offset, type.alignedLength, typePathName, name, watchIndex, &pointer64);
			DrawTypeColumn("%s", typeDisplayName, base, offset);

			ImGui::TableSetColumnIndex(COLUMN_CONTENT);
			DrawPointerContent(types, type, pointer);

			if (nodeOpen) {
				if (types.count(type.pointerTypePathName)) {
					const auto& pointedType = types.at(type.pointerTypePathName);

					const auto countStateId = ImGui::GetID("PointerElementCount");
					const int pointerElementCount = ImGui::GetStateStorage()->GetInt(countStateId, 1);

					// A pointer to unsized type (e.g. function or void) can't have more than one element
					if (pointedType.alignedLength > 0) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(COLUMN_NAME);
						if (ImGui::Button("Show more")) {
							ImGui::GetStateStorage()->SetInt(countStateId, pointerElementCount + 1);
						}
						if (pointerElementCount > 1) {
							ImGui::SameLine();
							ImGui::Text("(showing %x)", pointerElementCount);
						}
					}

					for (int i = 0; i < pointerElementCount; i++) {
						char nameBuffer[256];
						FormatIndexedMember(nameBuffer, sizeof(nameBuffer), name, i);
						// A pointer always creates extra node in the tree so using DefaultOpen to spare user one click
						DrawType(pointer, i * pointedType.alignedLength, type.pointerTypePathName,
						         nullptr, nameBuffer, -1, ImGuiTreeNodeFlags_DefaultOpen);
					}
				}
				ImGui::TreePop();
			}
			break;
		}
		case ARRAY: {
			const bool nodeOpen = ImGui::TreeNodeEx("Array", extraTreeNodeFlags, "%s", name);
			DrawContextMenu(base, offset, type.alignedLength, typePathName, name, watchIndex, nullptr);
			DrawTypeColumn("%s", typeDisplayName, base, offset);

			ImGui::TableSetColumnIndex(COLUMN_CONTENT);
			DrawArrayContent(types, type, address);

			if (nodeOpen) {
				for (int i = 0; i < type.arrayElementCount; i++) {
					char nameBuffer[256];
					FormatIndexedMember(nameBuffer, sizeof(nameBuffer), name, i);
					DrawType(base, offset + i * type.arrayElementLength, type.arrayTypePathName,
					         nullptr, nameBuffer, -1);
				}
				ImGui::TreePop();
			}
			break;
		}
		case STRUCTURE:
		case UNION: {
			const bool nodeOpen = ImGui::TreeNodeEx("Composite", extraTreeNodeFlags, "%s", name);
			DrawContextMenu(base, offset, type.alignedLength, typePathName, name, watchIndex, nullptr);
			DrawTypeColumn("%s", typeDisplayName, base, offset);

			if (nodeOpen) {
				for (const auto& member : type.compositeMembers) {
					DrawType(base, offset + member.offset, member.typePathName, nullptr,
					         member.fieldName.c_str(), -1);
				}
				ImGui::TreePop();
			}
			break;
		}
		case FUNCTION_DEFINITION:
			ImGui::TreeNodeEx("Field", leafFlags, "%s", name);
			DrawContextMenu(base, offset, type.alignedLength, typePathName, name, watchIndex, nullptr);
			DrawTypeColumn("%s", typeDisplayName, base, offset);

			ImGui::TableSetColumnIndex(COLUMN_CONTENT);
			ImGui::Text("<function definition>"); // TODO could be go to in disassembler here
			break;
		case BUILT_IN: {
			ImGui::TreeNodeEx("Field", leafFlags, "%s", name);

			if (knownBuiltIns.count(typePathName)) {
				// This will copy float as int, but we can live with that for now
				const u64 value = ReadMemoryInt(address, type.alignedLength);
				DrawContextMenu(base, offset, type.alignedLength, typePathName, name, watchIndex, &value);
				DrawTypeColumn("%s", typeDisplayName, base, offset);
				ImGui::TableSetColumnIndex(COLUMN_CONTENT);
				DrawBuiltInContent(knownBuiltIns.at(typePathName), address);
			} else {
				// Some built in types are rather obscure so we don't handle every possible one
				ImGui::PushStyleColor(ImGuiCol_Text, COLOR_RED);
				DrawTypeColumn("<unsupported built in: %s>", typePathName, base, offset);
				ImGui::PopStyleColor();
			}
			break;
		}
		default: {
			// At this point there is most likely some issue in the Ghidra extension and the type wasn't
			// classified to any category
			ImGui::TreeNodeEx("Field", leafFlags, "%s", name);
			DrawContextMenu(base, offset, type.alignedLength, typePathName, name, watchIndex, nullptr);
			ImGui::PushStyleColor(ImGuiCol_Text, COLOR_RED);
			DrawTypeColumn("<not implemented type: %s>", typeDisplayName, base, offset);
			ImGui::PopStyleColor();
			break;
		}
	}

	ImGui::PopID();
	ImGui::PopID();
}

static void CopyHexNumberToClipboard(const u64 value) {
	std::stringstream ss;
	ss << std::hex << value;
	const std::string valueString = ss.str();
	System_CopyStringToClipboard(valueString);
}

void ImStructViewer::DrawContextMenu(
	const u32 base,
	const u32 offset,
	const int length,
	const std::string& typePathName,
	const char* name,
	const int watchIndex,
	const u64* value
) {
	ImGui::OpenPopupOnItemClick("context", ImGuiPopupFlags_MouseButtonRight);
	if (ImGui::BeginPopup("context")) {
		const u32 address = base + offset;

		if (ImGui::MenuItem("Copy address")) {
			CopyHexNumberToClipboard(address);
		}
		if (value && ImGui::MenuItem("Copy value")) {
			CopyHexNumberToClipboard(*value);
		}

		// This might be called when iterating over existing watches so can't modify the watch vector directly here
		if (watchIndex < 0) {
			if (ImGui::MenuItem("Add watch")) {
				addWatch_.address = address;
				addWatch_.typePathName = typePathName;
				addWatch_.name = name;
			}
		} else if (watchIndex < watches_.size()) {
			if (ImGui::MenuItem("Remove watch")) {
				removeWatchIndex_ = watchIndex;
			}
		}

		// if (ImGui::MenuItem("Go to in memory view")) {
		// TODO imgui memory view not yet implemented
		// }

		// Memory breakpoints are only possible for sized types
		if (length > 0) {
			const u32 end = address + length;
			MemCheck memCheck;
			const bool hasMemCheck = CBreakPoints::GetMemCheck(address, end, &memCheck);
			if (hasMemCheck) {
				if (ImGui::MenuItem("Remove memory breakpoint")) {
					CBreakPoints::RemoveMemCheck(address, end);
				}
			}
			const bool canAddRead = !hasMemCheck || !(memCheck.cond & MEMCHECK_READ);
			const bool canAddWrite = !hasMemCheck || !(memCheck.cond & MEMCHECK_WRITE);
			const bool canAddWriteOnChange = !hasMemCheck || !(memCheck.cond & MEMCHECK_WRITE_ONCHANGE);
			if ((canAddRead || canAddWrite || canAddWriteOnChange) && ImGui::BeginMenu("Add memory breakpoint")) {
				if (canAddRead && canAddWrite && ImGui::MenuItem("Read/Write")) {
					constexpr auto cond = static_cast<MemCheckCondition>(MEMCHECK_READ | MEMCHECK_WRITE);
					CBreakPoints::AddMemCheck(address, end, cond, BREAK_ACTION_PAUSE);
				}
				if (canAddRead && ImGui::MenuItem("Read")) {
					CBreakPoints::AddMemCheck(address, end, MEMCHECK_READ, BREAK_ACTION_PAUSE);
				}
				if (canAddWrite && ImGui::MenuItem("Write")) {
					CBreakPoints::AddMemCheck(address, end, MEMCHECK_WRITE, BREAK_ACTION_PAUSE);
				}
				if (canAddWriteOnChange && ImGui::MenuItem("Write Change")) {
					constexpr auto cond = static_cast<MemCheckCondition>(MEMCHECK_WRITE | MEMCHECK_WRITE_ONCHANGE);
					CBreakPoints::AddMemCheck(address, end, cond, BREAK_ACTION_PAUSE);
				}
				ImGui::EndMenu();
			}
		}

		ImGui::EndPopup();
	}
}
