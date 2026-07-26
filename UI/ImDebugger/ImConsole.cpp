#include <cstdlib>

#include "ext/imgui/imgui.h"

#include "UI/ImDebugger/ImDebugger.h"
#include "UI/ImDebugger/ImConsole.h"
#include "Core/LuaContext.h"
#include "Common/StringUtils.h"

ImConsole::ImConsole() {
	memset(inputBuf_, 0, sizeof(inputBuf_));

	historyPos_ = -1;

	// "CLASSIFY" is here to provide the test case where "C"+[tab] completes to "CL" and display multiple matches.
	autoScroll_ = true;
	scrollToBottom_ = false;
}

ImConsole::~ImConsole() {
	for (int i = 0; i < history_.Size; i++)
		ImGui::MemFree(history_[i]);
}

// Portable helpers
static int   Stricmp(const char* s1, const char* s2) { int d; while ((d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; } return d; }
static int   Strnicmp(const char* s1, const char* s2, int n) { int d = 0; while (n > 0 && (d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; n--; } return d; }
static char* Strdup(const char* s) { IM_ASSERT(s); size_t len = strlen(s) + 1; void* buf = ImGui::MemAlloc(len); IM_ASSERT(buf); return (char*)memcpy(buf, (const void*)s, len); }
static void  Strtrim(char* s) { char* str_end = s + strlen(s); while (str_end > s && str_end[-1] == ' ') str_end--; *str_end = 0; }

// In C++11 you'd be better off using lambdas for this sort of forwarding callbacks
static int TextEditCallbackStub(ImGuiInputTextCallbackData* data) {
	ImConsole* console = (ImConsole*)data->UserData;
	return console->TextEditCallback(data);
}

void ImConsole::Draw(ImConfig &cfg) {
	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Lua Console", &cfg.luaConsoleOpen)) {
		ImGui::End();
		return;
	}

	if (ImGui::SmallButton("Clear")) {
		g_lua.Clear();
	}

	ImGui::SameLine();

	bool copy_to_clipboard = ImGui::SmallButton("Copy");
	ImGui::Separator();

	// Options menu
	if (ImGui::BeginPopup("Options")) {
		ImGui::Checkbox("Auto-scroll", &autoScroll_);
		ImGui::EndPopup();
	}

	// Options, Filter
	//ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_Tooltip);
	if (ImGui::Button("Options"))
		ImGui::OpenPopup("Options");
	ImGui::SameLine();
	filter_.Draw("Filter (\"incl,-excl\") (\"error\")", 180);
	ImGui::Separator();

	// Reserve enough left-over height for 1 separator + 1 input text
	const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiChildFlags_NavFlattened)) {
		if (ImGui::BeginPopupContextWindow()) {
			if (ImGui::Selectable("Clear"))
				g_lua.Clear();
			ImGui::EndPopup();
		}

		// Display every line as a separate entry so we can change their color or add custom widgets.
		// If you only want raw text you can use ImGui::TextUnformatted(log.begin(), log.end());
		// NB- if you have thousands of entries this approach may be too inefficient and may require user-side clipping
		// to only process visible items. The clipper will automatically measure the height of your first item and then
		// "seek" to display only items in the visible area.
		// To use the clipper we can replace your standard loop:
		//      for (int i = 0; i < Items.Size; i++)
		//   With:
		//      ImGuiListClipper clipper;
		//      clipper.Begin(Items.Size);
		//      while (clipper.Step())
		//         for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
		// - That your items are evenly spaced (same height)
		// - That you have cheap random access to your elements (you can access them given their index,
		//   without processing all the ones before)
		// You cannot this code as-is if a filter is active because it breaks the 'cheap random-access' property.
		// We would need random-access on the post-filtered list.
		// A typical application wanting coarse clipping and filtering may want to pre-compute an array of indices
		// or offsets of items that passed the filtering test, recomputing this array when user changes the filter,
		// and appending newly elements as they are inserted. This is left as a task to the user until we can manage
		// to improve this example code!
		// If your items are of variable height:
		// - Split them into same height items would be simpler and facilitate random-seeking into your list.
		// - Consider using manual call to IsRectVisible() and skipping extraneous decoration from your items.
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing
		if (copy_to_clipboard)
			ImGui::LogToClipboard();
		for (const auto &item : g_lua.GetLines()) {
			if (!filter_.PassFilter(item.line.c_str()))
				continue;
			ImVec4 color;
			bool has_color = true;

			switch (item.type) {
			case LogLineType::Cmd:      color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); break;
			case LogLineType::Error:    color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); break;
			case LogLineType::External: color = ImVec4(0.8f, 0.8f, 1.0f, 1.0f); break;
			case LogLineType::Integer:  color = ImVec4(1.0f, 1.0f, 0.8f, 1.0f); break;
			case LogLineType::Float:    color = ImVec4(1.0f, 1.0f, 0.8f, 1.0f); break;
			case LogLineType::String:   color = ImVec4(0.8f, 1.0f, 0.8f, 1.0f); break;
			default:
				has_color = false;
				break;
			}

			if (has_color)
				ImGui::PushStyleColor(ImGuiCol_Text, color);
			switch (item.type) {
			case LogLineType::Url:
				if (ImGui::TextLink(item.line.c_str())) {
					System_LaunchUrl(LaunchUrlType::BROWSER_URL, item.line.c_str());
				}
				break;
			default:
				ImGui::TextUnformatted(item.line.data(), item.line.data() + item.line.size());
				break;
			}
			if (has_color)
				ImGui::PopStyleColor();
		}
		if (copy_to_clipboard)
			ImGui::LogFinish();

		// Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
		// Using a scrollbar or mouse-wheel will take away from the bottom edge.
		if (scrollToBottom_ || (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
			ImGui::SetScrollHereY(1.0f);
		scrollToBottom_ = false;

		ImGui::PopStyleVar();
	}
	ImGui::EndChild();
	ImGui::Separator();

	// Detect first frame when the window becomes active
	if (ImGui::IsWindowAppearing()) {
		// Tell ImGui to focus the next input widget
		ImGui::SetKeyboardFocusHere();
	}

	// Command-line
	bool reclaim_focus = false;
	ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
	if (ImGui::InputText("Input", inputBuf_, IM_ARRAYSIZE(inputBuf_), input_text_flags, &TextEditCallbackStub, (void*)this)) {
		char* s = inputBuf_;
		Strtrim(s);
		if (s[0])
			ExecCommand(s);
		strcpy(s, "");
		reclaim_focus = true;
	}

	// Auto-focus on window apparition
	ImGui::SetItemDefaultFocus();
	if (reclaim_focus)
		ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget

	ImGui::End();
}

void ImConsole::ExecCommand(const char* command_line) {
	// Insert into history. First find match and delete it so it can be pushed to the back.
	// This isn't trying to be smart or optimal.
	historyPos_ = -1;
	for (int i = history_.Size - 1; i >= 0; i--) {
		if (Stricmp(history_[i], command_line) == 0) {
			ImGui::MemFree(history_[i]);
			history_.erase(history_.begin() + i);
			break;
		}
	}
	history_.push_back(Strdup(command_line));

	g_lua.Print(LogLineType::Cmd, std::string(command_line));

	// Process command
	if (Stricmp(command_line, "clear") == 0) {
		g_lua.Clear();
	} else if (Stricmp(command_line, "help") == 0) {
		g_lua.Print("For Lua help:");
		g_lua.Print(LogLineType::Url, "https://www.lua.org/manual/5.3/");
		// TODO: Also print available Lua commands.
	} else if (Stricmp(command_line, "history") == 0) {
		int first = history_.Size - 10;
		for (int i = first > 0 ? first : 0; i < history_.Size; i++)
			g_lua.Print(StringFromFormat("%3d: %s", i, history_[i]));
	} else {
		g_lua.ExecuteConsoleCommand(command_line);
	}

	// On command input, we scroll to bottom even if AutoScroll==false
	scrollToBottom_ = true;
}

int ImConsole::TextEditCallback(ImGuiInputTextCallbackData* data) {
	//AddLog("cursor: %d, selection: %d-%d", data->CursorPos, data->SelectionStart, data->SelectionEnd);
	switch (data->EventFlag) {
	case ImGuiInputTextFlags_CallbackCompletion:
	{
		// Example of TEXT COMPLETION

		// Locate beginning of current word
		const char* word_end = data->Buf + data->CursorPos;
		const char* word_start = word_end;
		while (word_start > data->Buf)
		{
			const char c = word_start[-1];
			if (c == ' ' || c == '\t' || c == ',' || c == ';')
				break;
			word_start--;
		}

		// Build a list of candidates
		ImVector<const char*> candidates;

		// TODO: Add lua globals to candidates!
		std::vector<std::string> luaCandidates = g_lua.AutoComplete(std::string_view(word_start, word_end - word_start));
		for (const auto &s : luaCandidates) {
			candidates.push_back(s.c_str());
		}

		if (candidates.Size == 0) {
			// No match. TODO: Match against lua globals.
			g_lua.Print(StringFromFormat("No match for \"%.*s\"!", (int)(word_end - word_start), word_start));
		} else if (candidates.Size == 1) {
			// Single match. Delete the beginning of the word and replace it entirely so we've got nice casing.
			data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
			data->InsertChars(data->CursorPos, candidates[0]);
			data->InsertChars(data->CursorPos, " ");
		} else {
			// Multiple matches. Complete as much as we can..
			// So inputing "C"+Tab will complete to "CL" then display "CLEAR" and "CLASSIFY" as matches.
			int match_len = (int)(word_end - word_start);
			for (;;) {
				int c = 0;
				bool all_candidates_matches = true;
				for (int i = 0; i < candidates.Size && all_candidates_matches; i++)
					if (i == 0)
						c = toupper(candidates[i][match_len]);
					else if (c == 0 || c != toupper(candidates[i][match_len]))
						all_candidates_matches = false;
				if (!all_candidates_matches)
					break;
				match_len++;
			}

			if (match_len > 0) {
				data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
				data->InsertChars(data->CursorPos, candidates[0], candidates[0] + match_len);
			}

			// List matches
			g_lua.Print("Possible matches:");
			std::sort(candidates.begin(), candidates.end(), [](const char* a, const char* b) {
				return Stricmp(a, b) < 0;
			});
			for (int i = 0; i < candidates.Size; i++) {
				g_lua.Print(StringFromFormat("- %s", candidates[i]));
			}
		}

		break;
	}
	case ImGuiInputTextFlags_CallbackHistory:
	{
		// Example of HISTORY
		const int prev_history_pos = historyPos_;
		if (data->EventKey == ImGuiKey_UpArrow) {
			if (historyPos_ == -1)
				historyPos_ = history_.Size - 1;
			else if (historyPos_ > 0)
				historyPos_--;
		} else if (data->EventKey == ImGuiKey_DownArrow) {
			if (historyPos_ != -1)
				if (++historyPos_ >= history_.Size)
					historyPos_ = -1;
		}

		// A better implementation would preserve the data on the current input line along with cursor position.
		if (prev_history_pos != historyPos_) {
			const char* history_str = (historyPos_ >= 0) ? history_[historyPos_] : "";
			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, history_str);
		}
	}
	}
	return 0;
}
