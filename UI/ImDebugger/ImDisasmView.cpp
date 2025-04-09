#include "ext/imgui/imgui_internal.h"
#include "ext/imgui/imgui_extras.h"
#include "ext/imgui/imgui_impl_thin3d.h"

#include "Common/StringUtils.h"
#include "Common/Log.h"
#include "Common/Math/geom2d.h"
#include "Core/Core.h"
#include "Core/HLE/HLE.h"
#include "Core/Debugger/DebugInterface.h"
#include "Core/Debugger/DisassemblyManager.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MemMap.h"
#include "Common/System/Request.h"

#include "Core/System.h"
#include "UI/ImDebugger/ImDisasmView.h"
#include "UI/ImDebugger/ImDebugger.h"

ImDisasmView::ImDisasmView() {
	curAddress_ = 0;
	showHex_ = false;
	hasFocus_ = false;
	keyTaken = false;

	matchAddress_ = -1;
	searching_ = false;
	searchQuery_.clear();
	windowStart_ = curAddress_;
	displaySymbols_ = true;
}

ImDisasmView::~ImDisasmView() {
	g_disassemblyManager.clear();
}

void ImDisasmView::ScanVisibleFunctions() {
	g_disassemblyManager.analyze(windowStart_, g_disassemblyManager.getNthNextAddress(windowStart_, visibleRows_) - windowStart_);
}

static ImColor scaleColor(ImColor color, float factor) {
	if (factor <= 0.0f) {
		return color;
	}
	color.Value.x = std::min(color.Value.x * factor, 1.0f);
	color.Value.y = std::min(color.Value.y * factor, 1.0f);
	color.Value.z = std::min(color.Value.z * factor, 1.0f);
	return color;
}

bool ImDisasmView::getDisasmAddressText(u32 address, char* dest, bool abbreviateLabels, bool showData) {
	if (PSP_GetBootState() != BootState::Complete)
		return false;

	return GetDisasmAddressText(address, dest, abbreviateLabels, showData, displaySymbols_);
}

void ImDisasmView::assembleOpcode(u32 address, const std::string &defaultText) {
	/*
	if (!Core_IsStepping()) {
		MessageBox(wnd, L"Cannot change code while the core is running!", L"Error", MB_OK);
		return;
	}
	std::string op;
	bool result = InputBox_GetString(MainWindow::GetHInstance(), wnd, L"Assemble opcode", defaultText, op, InputBoxFlags::Default);
	if (!result) {
		return;
	}

	// check if it changes registers first
	auto separator = op.find('=');
	if (separator != std::string::npos)
	{
		std::string registerName = trimString(op.substr(0, separator));
		std::string expression = trimString(op.substr(separator + 1));

		u32 value;
		if (parseExpression(expression.c_str(), debugger, value) == true)
		{
			for (int cat = 0; cat < debugger->GetNumCategories(); cat++)
			{
				for (int reg = 0; reg < debugger->GetNumRegsInCategory(cat); reg++)
				{
					if (strcasecmp(debugger->GetRegName(cat, reg).c_str(), registerName.c_str()) == 0)
					{
						debugger->SetRegValue(cat, reg, value);
						Reporting::NotifyDebugger();
						SendMessage(GetParent(wnd), WM_DEB_UPDATE, 0, 0);
						return;
					}
				}
			}
		}

		// try to assemble the input if it failed
	}

	result = MIPSAsm::MipsAssembleOpcode(op.c_str(), debugger, address);
	Reporting::NotifyDebugger();
	if (result == true)
	{
		ScanVisibleFunctions();

		if (address == curAddress)
			gotoAddr(g_disassemblyManager.getNthNextAddress(curAddress, 1));

		redraw();
	} else {
		std::wstring error = ConvertUTF8ToWString(MIPSAsm::GetAssembleError());
		MessageBox(wnd, error.c_str(), L"Error", MB_OK);
	}
	*/
}

void ImDisasmView::drawBranchLine(ImDrawList *drawList, Bounds rect, std::map<u32, float> &addressPositions, const BranchLine &line) {
	u32 windowEnd = g_disassemblyManager.getNthNextAddress(windowStart_, visibleRows_);

	float topY;
	float bottomY;
	if (line.first < windowStart_) {
		topY = -1;
	} else if (line.first >= windowEnd) {
		topY = rect.y2() + 1.0f;
	} else {
		topY = (float)addressPositions[line.first] + rowHeight_ / 2;
	}

	if (line.second < windowStart_) {
		bottomY = -1;
	} else if (line.second >= windowEnd) {
		bottomY = rect.y2()  + 1.0f;
	} else {
		bottomY = (float)addressPositions[line.second] + rowHeight_ / 2;
	}

	if ((topY < 0 && bottomY < 0) || (topY > rect.y2() && bottomY > rect.y2())) {
		return;
	}

	ImColor pen;

	// highlight line in a different color if it affects the currently selected opcode
	// TODO: Color line differently if forward or backward too!
	if (line.first == curAddress_ || line.second == curAddress_) {
		pen = ImColor(0xFF257AFA);
	} else {
		pen = ImColor(0xFFFF3020);
	}

	float x = (float)pixelPositions_.arrowsStart + (float)line.laneIndex * 8.0f;

	float curX, curY;
	auto moveTo = [&](float x, float y) {
		curX = x;
		curY = y;
	};
	auto lineTo = [&](float x, float y) {
		drawList->AddLine(ImVec2(rect.x + curX, rect.y + curY), ImVec2(rect.x + (float)x, rect.y + (float)y), pen, 1.0f);
		curX = x;
		curY = y;
	};

	if (topY < 0) { // first is not visible, but second is
		moveTo(x - 2.f, bottomY);
		lineTo(x + 2.f, bottomY);
		lineTo(x + 2.f, 0.0f);

		if (line.type == LINE_DOWN) {
			moveTo(x, bottomY - 4.f);
			lineTo(x - 4.f, bottomY);
			lineTo(x + 1.f, bottomY + 5.f);
		}
	} else if (bottomY > rect.y2()) {// second is not visible, but first is
		moveTo(x - 2.f, topY);
		lineTo(x + 2.f, topY);
		lineTo(x + 2.f, rect.y2());

		if (line.type == LINE_UP) {
			moveTo(x, topY - 4.f);
			lineTo(x - 4.f, topY);
			lineTo(x + 1.f, topY + 5.f);
		}
	} else { // both are visible
		if (line.type == LINE_UP) {
			moveTo(x - 2.f, bottomY);
			lineTo(x + 2.f, bottomY);
			lineTo(x + 2.f, topY);
			lineTo(x - 4.f, topY);

			moveTo(x, topY - 4.f);
			lineTo(x - 4.f, topY);
			lineTo(x + 1.f, topY + 5.f);
		} else {
			moveTo(x - 2.f, topY);
			lineTo(x + 2.f, topY);
			lineTo(x + 2.f, bottomY);
			lineTo(x - 4.f, bottomY);

			moveTo(x, bottomY - 4.f);
			lineTo(x - 4.f, bottomY);
			lineTo(x + 1.f, bottomY + 5.f);
		}
	}
}

std::set<std::string> ImDisasmView::getSelectedLineArguments() {
	std::set<std::string> args;
	DisassemblyLineInfo line;
	for (u32 addr = selectRangeStart_; addr < selectRangeEnd_; addr += 4) {
		g_disassemblyManager.getLine(addr, displaySymbols_, line, debugger_);
		size_t p = 0, nextp = line.params.find(',');
		while (nextp != line.params.npos) {
			args.emplace(line.params.substr(p, nextp - p));
			p = nextp + 1;
			nextp = line.params.find(',', p);
		}
		if (p < line.params.size()) {
			args.emplace(line.params.substr(p));
		}
	}
	return args;
}

void ImDisasmView::drawArguments(ImDrawList *drawList, Bounds rc, const DisassemblyLineInfo &line, float x, float y, ImColor textColor, const std::set<std::string> &currentArguments) {
	if (line.params.empty()) {
		return;
	}
	// Don't highlight the selected lines.
	if (isInInterval(selectRangeStart_, selectRangeEnd_ - selectRangeStart_, line.info.opcodeAddress)) {
		drawList->AddText(ImVec2((float)(rc.x + x), (float)(rc.y + y)), textColor, line.params.data(), line.params.data() + line.params.size());
		return;
	}

	ImColor highlightedColor(0xFFaabb00);
	if (textColor == 0xFF0000ff) {
		highlightedColor = ImColor(0xFFaabb77);
	}

	float curX = (float)x, curY = (float)y;

	ImColor curColor = textColor;

	auto Print = [&](std::string_view text) {
		drawList->AddText(ImVec2(rc.x + curX, rc.y + curY), curColor, text.data(), text.data() + text.size());
		ImVec2 sz = ImGui::CalcTextSize(text.data(), text.data() + text.size(), false, -1.0f);
		curX += sz.x;
	};

	size_t p = 0, nextp = line.params.find(',');
	while (nextp != line.params.npos) {
		const std::string arg = line.params.substr(p, nextp - p);
		if (currentArguments.find(arg) != currentArguments.end() && textColor != 0xffffff) {
			curColor = highlightedColor;
		}
		Print(arg);
		curColor = textColor;
		p = nextp + 1;
		nextp = line.params.find(',', p);
		Print(",");
	}
	if (p < line.params.size()) {
		const std::string arg = line.params.substr(p);
		if (currentArguments.find(arg) != currentArguments.end() && textColor != 0xffffff) {
			curColor = highlightedColor;
		}
		Print(arg);
		curColor = textColor;
	}
}

void ImDisasmView::Draw(ImDrawList *drawList, ImControl &control) {
	if (!debugger_->isAlive()) {
		return;
	}

	// TODO: Don't need to do these every frame.
	ImGui_PushFixedFont();

	rowHeight_ = ImGui::GetTextLineHeightWithSpacing();
	charWidth_ = ImGui::CalcTextSize("W", nullptr, false, -1.0f).x;

	ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();      // ImDrawList API uses screen coordinates!
	ImVec2 canvas_sz = ImGui::GetContentRegionAvail();   // Resize canvas to what's available
	const ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

	// This will catch our interactions
	bool pressed = ImGui::InvisibleButton("canvas", canvas_sz, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	const bool is_hovered = ImGui::IsItemHovered(); // Hovered
	const bool is_active = ImGui::IsItemActive();   // Held

	if (pressed) {
		// INFO_LOG(Log::System, "Pressed");
	}
	ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);

	drawList->PushClipRect(canvas_p0, canvas_p1, true);
	drawList->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(25, 25, 25, 255));
	if (is_active) {
		drawList->AddRect(canvas_p0, canvas_p1, IM_COL32(255, 255, 255, 255));
	}

	Bounds bounds;
	bounds.x = canvas_p0.x;
	bounds.y = canvas_p0.y;
	bounds.w = canvas_p1.x - canvas_p0.x;
	bounds.h = canvas_p1.y - canvas_p0.y;

	calculatePixelPositions();

	visibleRows_ = (int)((bounds.h + rowHeight_ - 1.f) / rowHeight_);

	unsigned int address = windowStart_;
	std::map<u32, float> addressPositions;

	const std::set<std::string> currentArguments = getSelectedLineArguments();
	DisassemblyLineInfo line;

	const u32 pc = debugger_->GetPC();

	for (int i = 0; i < visibleRows_; i++) {
		g_disassemblyManager.getLine(address, displaySymbols_, line, debugger_);

		float rowY1 = rowHeight_ * i;
		float rowY2 = rowHeight_ * (i + 1);

		addressPositions[address] = rowY1;

		// draw background
		ImColor backgroundColor = ImColor(0xFF000000 | debugger_->getColor(address, true));
		ImColor textColor = 0xFFFFFFFF;

		if (isInInterval(address, line.totalSize, pc)) {
			backgroundColor = scaleColor(backgroundColor, 1.3f);
		}

		if (address >= selectRangeStart_ && address < selectRangeEnd_ && searching_ == false) {
			if (hasFocus_) {
				backgroundColor = ImColor(address == curAddress_ ? 0xFFFF8822 : 0xFFFF9933);
				textColor = ImColor(0xFF000000);
			} else {
				backgroundColor = ImColor(0xFF606060);
			}
		}

		drawList->AddRectFilled(ImVec2(bounds.x, bounds.y + rowY1), ImVec2(bounds.x2(), bounds.y + rowY1 + rowHeight_), backgroundColor);

		// display breakpoint, if any
		bool enabled;
		if (g_breakpoints.IsAddressBreakPoint(address, &enabled)) {
			ImColor breakColor = 0xFF0000FF;
			if (!enabled)
				breakColor = 0xFF909090;
			float yOffset = std::max(-1.0f, (rowHeight_ - 14.f + 1.f) / 2.0f);
			drawList->AddCircleFilled(ImVec2(canvas_p0.x + rowHeight_ * 0.5f, canvas_p0.y + rowY1 + rowHeight_ * 0.5f), rowHeight_ * 0.4f, breakColor, 12);
		}

		char addressText[64];
		getDisasmAddressText(address, addressText, true, line.type == DISTYPE_OPCODE);
		drawList->AddText(ImVec2(bounds.x + pixelPositions_.addressStart, bounds.y + rowY1 + 2), textColor, addressText);

		if (isInInterval(address, line.totalSize, pc)) {
			// Show the current PC with a little triangle.
			drawList->AddTriangleFilled(
				ImVec2(canvas_p0.x + pixelPositions_.opcodeStart - rowHeight_ * 0.7f, canvas_p0.y + rowY1 + 2),
				ImVec2(canvas_p0.x + pixelPositions_.opcodeStart - rowHeight_ * 0.7f, canvas_p0.y + rowY1 + rowHeight_ - 2),
				ImVec2(canvas_p0.x + pixelPositions_.opcodeStart - 4, canvas_p0.y + rowY1 + rowHeight_ * 0.5f),
				0xFFFFFFFF);
		}

		// display whether the condition of a branch is met
		if (line.info.isConditional && address == pc) {
			line.params += line.info.conditionMet ? "  ; true" : "  ; false";
		}

		drawArguments(drawList, bounds, line, pixelPositions_.argumentsStart, rowY1 + 2.f, textColor, currentArguments);

		// The actual opcode.
		// Should be bold!
		drawList->AddText(ImVec2(bounds.x + pixelPositions_.opcodeStart, bounds.y + rowY1 + 2.f), textColor, line.name.c_str());

		address += line.totalSize;
	}

	std::vector<BranchLine> branchLines = g_disassemblyManager.getBranchLines(windowStart_, address - windowStart_);
	for (size_t i = 0; i < branchLines.size(); i++) {
		drawBranchLine(drawList, bounds, addressPositions, branchLines[i]);
	}

	ImGuiIO& io = ImGui::GetIO();
	ImVec2 mousePos = ImVec2(io.MousePos.x - canvas_p0.x, io.MousePos.y - canvas_p0.y);
	if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		// INFO_LOG(Log::System, "Mousedown %f,%f active:%d hover:%d", mousePos.x, mousePos.y, is_active, is_hovered);
		onMouseDown(mousePos.x, mousePos.y, 1);
	}
	if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
		// INFO_LOG(Log::CPU, "Mousedown %f,%f active:%d hover:%d", mousePos.x, mousePos.y, is_active, is_hovered);
		onMouseDown(mousePos.x, mousePos.y, 2);
	}
	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		// INFO_LOG(Log::System, "Mouseup %f,%f active:%d hover:%d", mousePos.x, mousePos.y, is_active, is_hovered);
		if (is_hovered) {
			onMouseUp(mousePos.x, mousePos.y, 1);
		}
	}
	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		// INFO_LOG(Log::System, "Mousedrag %f,%f active:%d hover:%d", mousePos.x, mousePos.y, is_active, is_hovered);
		if (is_hovered) {
			onMouseMove(mousePos.x, mousePos.y, 1);
		}
	}

	if (is_hovered) {
		if (io.MouseWheel > 0.0f) {  // TODO: Scale steps by the value.
			windowStart_ = g_disassemblyManager.getNthPreviousAddress(windowStart_, 4);
		} else if (io.MouseWheel < 0.0f) {
			windowStart_ = g_disassemblyManager.getNthNextAddress(windowStart_, 4);
		}
	}

	ProcessKeyboardShortcuts(ImGui::IsItemFocused());

	if (pressed) {
		// INFO_LOG(Log::System, "Clicked %f,%f", mousePos.x, mousePos.y);
		if (mousePos.x < rowHeight_) {  // Left column
			// Toggle breakpoint at dragAddr_.
			debugger_->toggleBreakpoint(curAddress_);
			bpPopup_ = true;
		} else {
			// disasmView_.selectedAddr_ = dragAddr_;
			bpPopup_ = false;
		}
	}

	ImGui_PopFont();

	ImGui::OpenPopupOnItemClick("context", ImGuiPopupFlags_MouseButtonRight);
	PopupMenu(control);

	drawList->PopClipRect();
}

void ImDisasmView::NotifyStep() {
	if (followPC_) {
		GotoPC();
	}
}

void ImDisasmView::ScrollRelative(int amount) {
	if (amount > 0) {
		windowStart_ = g_disassemblyManager.getNthNextAddress(windowStart_, amount);
	} else if (amount < 0) {
		windowStart_ = g_disassemblyManager.getNthPreviousAddress(windowStart_, amount);
	}
	ScanVisibleFunctions();
}

// Follows branches and jumps.
void ImDisasmView::FollowBranch() {
	DisassemblyLineInfo line;
	g_disassemblyManager.getLine(curAddress_, true, line, debugger_);

	if (line.type == DISTYPE_OPCODE || line.type == DISTYPE_MACRO) {
		if (line.info.isBranch) {
			jumpStack_.push_back(curAddress_);
			gotoAddr(line.info.branchTarget);
		} else if (line.info.hasRelevantAddress) {
			// well, not exactly a branch, but we can do something anyway
			// SendMessage(GetParent(wnd), WM_DEB_GOTOHEXEDIT, line.info.relevantAddress, 0);
			// SetFocus(wnd);
		}
	} else if (line.type == DISTYPE_DATA) {
		// jump to the start of the current line
		// SendMessage(GetParent(wnd), WM_DEB_GOTOHEXEDIT, curAddress, 0);
		// SetFocus(wnd);
	}
}

void ImDisasmView::onChar(int c) {
	if (keyTaken)
		return;

	char str[2];
	str[0] = c;
	str[1] = 0;
	assembleOpcode(curAddress_, str);
}


void ImDisasmView::editBreakpoint(ImConfig &cfg) {
	/*
	BreakpointWindow win(wnd, debugger);

	bool exists = false;
	if (g_breakpoints.IsAddressBreakPoint(curAddress)) {
		auto breakpoints = g_breakpoints.GetBreakpoints();
		for (size_t i = 0; i < breakpoints.size(); i++) {
			if (breakpoints[i].addr == curAddress) {
				win.loadFromBreakpoint(breakpoints[i]);
				exists = true;
				break;
			}
		}
	}

	if (!exists) {
		win.initBreakpoint(curAddress);
	}

	if (win.exec()) {
		if (exists)
			g_breakpoints.RemoveBreakPoint(curAddress);
		win.addBreakpoint();
	}
	*/
}

void ImDisasmView::ProcessKeyboardShortcuts(bool focused) {
	if (!focused) {
		return;
	}

	u32 windowEnd = g_disassemblyManager.getNthNextAddress(windowStart_, visibleRows_);
	keyTaken = true;

	ImGuiIO& io = ImGui::GetIO();

	if (io.KeyMods & ImGuiMod_Ctrl) {
		if (ImGui::IsKeyPressed(ImGuiKey_F)) {
			// Toggle the find popup
			// ImGui::OpenPopup("disSearch");
		}
		if (ImGui::IsKeyPressed(ImGuiKey_C) || ImGui::IsKeyPressed(ImGuiKey_Insert)) {
			CopyInstructions(selectRangeStart_, selectRangeEnd_, CopyInstructionsMode::DISASM);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_X)) {
			// disassembleToFile();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_A)) {
			// assembleOpcode(curAddress, "");
		}
		if (ImGui::IsKeyPressed(ImGuiKey_G)) {
			// Goto. should just focus on the goto input?
			// u32 addr;
			// if (executeExpressionWindow(wnd, debugger, addr) == false) return;
			// gotoAddr(addr);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_E)) {
			// editBreakpoint();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_D)) {
			toggleBreakpoint(true);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
			ScrollRelative(-1);
			ScanVisibleFunctions();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
			ScrollRelative(1);
			ScanVisibleFunctions();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
			setCurAddress(g_disassemblyManager.getNthPreviousAddress(windowEnd, 1), (io.KeyMods & ImGuiMod_Shift) != 0);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
			setCurAddress(windowStart_, ImGui::IsKeyDown(ImGuiKey_LeftShift));
		}
	} else {
		if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
			windowStart_ = g_disassemblyManager.getNthNextAddress(windowStart_, visibleRows_);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
			windowStart_ = g_disassemblyManager.getNthPreviousAddress(windowStart_, visibleRows_);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_F3)) {
			SearchNext(!ImGui::IsKeyPressed(ImGuiKey_LeftShift));
		}
		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
			setCurAddress(g_disassemblyManager.getNthNextAddress(curAddress_, 1), (io.KeyMods & ImGuiMod_Shift) != 0);
			scrollAddressIntoView();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
			setCurAddress(g_disassemblyManager.getNthPreviousAddress(curAddress_, 1), (io.KeyMods & ImGuiMod_Shift) != 0);
			scrollAddressIntoView();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
			FollowBranch();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
			if (jumpStack_.empty()) {
				GotoPC();
			} else {
				u32 addr = jumpStack_[jumpStack_.size() - 1];
				jumpStack_.pop_back();
				gotoAddr(addr);
			}
			return;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_F9)) {
			toggleBreakpoint();
		}
	}

	/*
	if (KeyDownAsync(VK_CONTROL)) {
	} else {
		switch (wParam & 0xFFFF) {
		case VK_NEXT:
			if (g_disassemblyManager.getNthNextAddress(curAddress, 1) != windowEnd && curAddressIsVisible()) {
				setCurAddress(g_disassemblyManager.getNthPreviousAddress(windowEnd, 1), KeyDownAsync(VK_SHIFT));
				scrollAddressIntoView();
			} else {
				setCurAddress(g_disassemblyManager.getNthNextAddress(windowEnd, visibleRows_ - 1), KeyDownAsync(VK_SHIFT));
				scrollAddressIntoView();
			}
			break;
		case VK_PRIOR:
			if (curAddress_ != windowStart_ && curAddressIsVisible()) {
				setCurAddress(windowStart_, KeyDownAsync(VK_SHIFT));
				scrollAddressIntoView();
			} else {
				setCurAddress(g_disassemblyManager.getNthPreviousAddress(windowStart_, visibleRows_), KeyDownAsync(VK_SHIFT));
				scrollAddressIntoView();
			}
			break;
		case VK_TAB:
			displaySymbols_ = !displaySymbols_;
			break;
		default:
			keyTaken = false;
			return;
		}
	}
	*/
}

void ImDisasmView::scrollAddressIntoView() {
	u32 windowEnd = g_disassemblyManager.getNthNextAddress(windowStart_, visibleRows_);

	if (curAddress_ < windowStart_)
		windowStart_ = curAddress_;
	else if (curAddress_ >= windowEnd)
		windowStart_ = g_disassemblyManager.getNthPreviousAddress(curAddress_, visibleRows_ - 1);

	ScanVisibleFunctions();
}

bool ImDisasmView::curAddressIsVisible() {
	u32 windowEnd = g_disassemblyManager.getNthNextAddress(windowStart_, visibleRows_);
	return curAddress_ >= windowStart_ && curAddress_ < windowEnd;
}

void ImDisasmView::toggleBreakpoint(bool toggleEnabled) {
	bool enabled;
	if (g_breakpoints.IsAddressBreakPoint(curAddress_, &enabled)) {
		if (!enabled) {
			// enable disabled breakpoints
			g_breakpoints.ChangeBreakPoint(curAddress_, true);
		} else if (!toggleEnabled && g_breakpoints.GetBreakPointCondition(curAddress_) != nullptr) {
			// don't just delete a breakpoint with a custom condition
			/*
			int ret = MessageBox(wnd, L"This breakpoint has a custom condition.\nDo you want to remove it?", L"Confirmation", MB_YESNO);
			if (ret == IDYES)
				g_breakpoints.RemoveBreakPoint(curAddress);
			*/
		} else if (toggleEnabled) {
			// disable breakpoint
			g_breakpoints.ChangeBreakPoint(curAddress_, false);
		} else {
			// otherwise just remove breakpoint
			g_breakpoints.RemoveBreakPoint(curAddress_);
		}
	} else {
		g_breakpoints.AddBreakPoint(curAddress_);
	}
}

void ImDisasmView::onMouseDown(float x, float y, int button) {
	u32 newAddress = yToAddress(y);
	bool extend = ImGui::IsKeyDown(ImGuiKey_LeftShift);
	if (button == 1) {
		if (newAddress == curAddress_ && hasFocus_) {
			toggleBreakpoint();
		}
	} else if (button == 2) {
		// Maintain the current selection if right clicking into it.
		if (newAddress >= selectRangeStart_ && newAddress < selectRangeEnd_)
			extend = true;
	}
	setCurAddress(newAddress, extend);
}

void ImDisasmView::onMouseMove(float x, float y, int button) {
	if ((button & 1) != 0) {
		setCurAddress(yToAddress(y), ImGui::IsKeyDown(ImGuiKey_LeftShift));
	}
}

void ImDisasmView::onMouseUp(float x, float y, int button) {
	if (button == 1) {
		if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
			setCurAddress(yToAddress(y), true);
		}
	}
}

void ImDisasmView::CopyInstructions(u32 startAddr, u32 endAddr, CopyInstructionsMode mode) {
	_assert_msg_((startAddr & 3) == 0, "readMemory() can't handle unaligned reads");

	if (mode != CopyInstructionsMode::DISASM) {
		int instructionSize = debugger_->getInstructionSize(0);
		int count = (endAddr - startAddr) / instructionSize;
		int space = count * 32;
		char *temp = new char[space];

		char *p = temp, *end = temp + space;
		for (u32 pos = startAddr; pos < endAddr && p < end; pos += instructionSize) {
			u32 data = mode == CopyInstructionsMode::OPCODES ? debugger_->readMemory(pos) : pos;
			p += snprintf(p, end - p, "%08X", data);

			// Don't leave a trailing newline.
			if (pos + instructionSize < endAddr && p < end)
				p += snprintf(p, end - p, "\r\n");
		}
		System_CopyStringToClipboard(temp);
		delete[] temp;
	} else {
		std::string disassembly = disassembleRange(startAddr, endAddr - startAddr);
		System_CopyStringToClipboard(disassembly);
	}
}

void ImDisasmView::PopupMenu(ImControl &control) {
	bool renameFunctionPopup = false;
	if (ImGui::BeginPopup("context")) {
		ImGui::Text("Address: %08x", curAddress_);
		if (ImGui::MenuItem("Toggle breakpoint", "F9")) {
			toggleBreakpoint();
		}
		ShowInMemoryViewerMenuItem(curAddress_, control);
		if (ImGui::MenuItem("Copy address")) {
			CopyInstructions(selectRangeStart_, selectRangeEnd_, CopyInstructionsMode::ADDRESSES);
		}
		if (ImGui::MenuItem("Copy instruction (disasm)")) {
			CopyInstructions(selectRangeStart_, selectRangeEnd_, CopyInstructionsMode::DISASM);
		}
		if (ImGui::MenuItem("Copy instruction (hex)")) {
			CopyInstructions(selectRangeStart_, selectRangeEnd_, CopyInstructionsMode::OPCODES);
		}
		ImGui::Separator();

		if (ImGui::MenuItem("Set PC to here")) {
			debugger_->SetPC(curAddress_);
		}
		if (ImGui::MenuItem("Follow branch")) {
			FollowBranch();
		}
		if (ImGui::MenuItem("Run to here")) {
			g_breakpoints.AddBreakPoint(curAddress_, true);
			g_breakpoints.SetSkipFirst(curAddress_);
			if (Core_IsStepping()) {
				Core_Resume();
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Assemble")) {
			assembleOpcode(curAddress_, "");
		}
		if (ImGui::MenuItem("NOP instructions (destructive)")) {
			for (u32 addr = selectRangeStart_; addr < selectRangeEnd_; addr += 4) {
				Memory::Write_U32(0, addr);
			}
			if (currentMIPS) {
				currentMIPS->InvalidateICache(selectRangeStart_, selectRangeEnd_ - selectRangeStart_);
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Rename function")) {
			funcBegin_ = g_symbolMap->GetFunctionStart(curAddress_);
			if (funcBegin_ != -1) {
				truncate_cpy(funcNameTemp_, g_symbolMap->GetLabelString(funcBegin_).c_str());
				renameFunctionPopup = true;
				statusBarText_ = funcNameTemp_;
			} else {
				statusBarText_ = "No function here";
			}
		}
		if (ImGui::MenuItem("Remove function")) {
			u32 funcBegin = g_symbolMap->GetFunctionStart(curAddress_);
			if (funcBegin != -1) {
				u32 prevBegin = g_symbolMap->GetFunctionStart(funcBegin - 1);
				if (prevBegin != -1)
				{
					u32 expandedSize = g_symbolMap->GetFunctionSize(prevBegin) + g_symbolMap->GetFunctionSize(funcBegin);
					g_symbolMap->SetFunctionSize(prevBegin, expandedSize);
				}

				g_symbolMap->RemoveFunction(funcBegin, true);
				g_symbolMap->SortSymbols();
				g_disassemblyManager.clear();

				mapReloaded_ = true;
			} else {
				statusBarText_ = "WARNING: unable to find function symbol here";
			}
		}
		u32 prevBegin = g_symbolMap->GetFunctionStart(curAddress_);
		if (ImGui::MenuItem("Add function")) {
			char statusBarTextBuff[256];
			if (prevBegin != -1) {
				if (prevBegin == curAddress_) {
					snprintf(statusBarTextBuff, 256, "WARNING: There's already a function entry point at this adress");
					statusBarText_ = statusBarTextBuff;
				} else {
					char symname[128];
					u32 prevSize = g_symbolMap->GetFunctionSize(prevBegin);
					u32 newSize = curAddress_ - prevBegin;
					g_symbolMap->SetFunctionSize(prevBegin, newSize);

					newSize = prevSize - newSize;
					snprintf(symname, 128, "u_un_%08X", curAddress_);
					g_symbolMap->AddFunction(symname, curAddress_, newSize);
					g_symbolMap->SortSymbols();
					g_disassemblyManager.clear();

					mapReloaded_ = true;
				}
			} else {
				char symname[128];
				int newSize = selectRangeEnd_ - selectRangeStart_;
				snprintf(symname, 128, "u_un_%08X", selectRangeStart_);
				g_symbolMap->AddFunction(symname, selectRangeStart_, newSize);
				g_symbolMap->SortSymbols();

				mapReloaded_ = true;
			}
		}
		if (prevBegin != -1) {
			u32 prevSize = g_symbolMap->GetFunctionSize(prevBegin);
			ShowInMemoryDumperMenuItem(prevBegin, prevSize, MemDumpMode::Disassembly, control);
		}
		ImGui::EndPopup();
	}

	// Sub popups here
	if (renameFunctionPopup) {
		ImGui::OpenPopup("renameFunction");
	}
	// ImGui::SetNextWindowPos(pos, ImGuiCond_Appearing())
	if (ImGui::BeginPopup("renameFunction")) {
		std::string symName = g_symbolMap->GetDescription(funcBegin_);
		ImGui::Text("Rename function %s", symName.c_str());
		if (ImGui::IsWindowAppearing()) {
			ImGui::SetKeyboardFocusHere();
		}
		if (ImGui::InputText("Name", funcNameTemp_, sizeof(funcNameTemp_), ImGuiInputTextFlags_EnterReturnsTrue) && strlen(funcNameTemp_)) {
			g_symbolMap->SetLabelName(funcNameTemp_, funcBegin_);
			u32 funcSize = g_symbolMap->GetFunctionSize(funcBegin_);
			MIPSAnalyst::RegisterFunction(funcBegin_, funcSize, funcNameTemp_);
			MIPSAnalyst::UpdateHashMap();
			MIPSAnalyst::ApplyHashMap();
			mapReloaded_ = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void ImDisasmView::updateStatusBarText() {
	if (!PSP_IsInited())
		return;

	char text[512];
	DisassemblyLineInfo line;
	g_disassemblyManager.getLine(curAddress_, true, line, debugger_);

	text[0] = 0;
	if (line.type == DISTYPE_OPCODE || line.type == DISTYPE_MACRO) {
		if (line.info.hasRelevantAddress && IsLikelyStringAt(line.info.relevantAddress)) {
			snprintf(text, sizeof(text), "[%08X] = \"%s\"", line.info.relevantAddress, Memory::GetCharPointer(line.info.relevantAddress));
		}

		if (line.info.isDataAccess) {
			if (!Memory::IsValidAddress(line.info.dataAddress)) {
				snprintf(text, sizeof(text), "Invalid address %08X", line.info.dataAddress);
			} else {
				bool isFloat = MIPSGetInfo(line.info.encodedOpcode) & (IS_FPU | IS_VFPU);
				switch (line.info.dataSize) {
				case 1:
					snprintf(text, sizeof(text), "[%08X] = %02X", line.info.dataAddress, Memory::Read_U8(line.info.dataAddress));
					break;
				case 2:
					snprintf(text, sizeof(text), "[%08X] = %04X", line.info.dataAddress, Memory::Read_U16(line.info.dataAddress));
					break;
				case 4:
				{
					u32 dataInt = Memory::Read_U32(line.info.dataAddress);
					u32 dataFloat = Memory::Read_Float(line.info.dataAddress);
					std::string dataString;
					if (isFloat)
						dataString = StringFromFormat("%08X / %f", dataInt, dataFloat);
					else
						dataString = StringFromFormat("%08X", dataInt);

					const std::string addressSymbol = g_symbolMap->GetLabelString(dataInt);
					if (!addressSymbol.empty()) {
						snprintf(text, sizeof(text), "[%08X] = %s (%s)", line.info.dataAddress, addressSymbol.c_str(), dataString.c_str());
					} else {
						snprintf(text, sizeof(text), "[%08X] = %s", line.info.dataAddress, dataString.c_str());
					}
					break;
				}
				case 16:
				{
					uint32_t dataInt[4];
					float dataFloat[4];
					for (int i = 0; i < 4; ++i) {
						dataInt[i] = Memory::Read_U32(line.info.dataAddress + i * 4);
						dataFloat[i] = Memory::Read_Float(line.info.dataAddress + i * 4);
					}
					std::string dataIntString = StringFromFormat("%08X,%08X,%08X,%08X", dataInt[0], dataInt[1], dataInt[2], dataInt[3]);
					std::string dataFloatString = StringFromFormat("%f,%f,%f,%f", dataFloat[0], dataFloat[1], dataFloat[2], dataFloat[3]);

					snprintf(text, sizeof(text), "[%08X] = %s / %s", line.info.dataAddress, dataIntString.c_str(), dataFloatString.c_str());
					break;
				}
				}
			}
		}

		if (line.info.isBranch) {
			const std::string addressSymbol = g_symbolMap->GetLabelString(line.info.branchTarget);
			if (addressSymbol.empty()) {
				snprintf(text, sizeof(text), "%08X", line.info.branchTarget);
			} else {
				snprintf(text, sizeof(text), "%08X = %s", line.info.branchTarget, addressSymbol.c_str());
			}
		}
	} else if (line.type == DISTYPE_DATA) {
		u32 start = g_symbolMap->GetDataStart(curAddress_);
		if (start == -1)
			start = curAddress_;

		u32 diff = curAddress_ - start;
		const std::string label = g_symbolMap->GetLabelString(start);

		if (!label.empty()) {
			if (diff != 0)
				snprintf(text, sizeof(text), "%08X (%s) + %08X", start, label.c_str(), diff);
			else
				snprintf(text, sizeof(text), "%08X (%s)", start, label.c_str());
		} else {
			if (diff != 0)
				snprintf(text, sizeof(text), "%08X + %08X", start, diff);
			else
				snprintf(text, sizeof(text), "%08X", start);
		}
	}

	statusBarText_ = text;

	const std::string label = g_symbolMap->GetLabelString(line.info.opcodeAddress);
	if (!label.empty()) {
		statusBarText_ = label;
	}
}

u32 ImDisasmView::yToAddress(float y) {
	int line = (int)(y / rowHeight_);
	return g_disassemblyManager.getNthNextAddress(windowStart_, line);
}

void ImDisasmView::calculatePixelPositions() {
	pixelPositions_.addressStart = 16;
	pixelPositions_.opcodeStart = pixelPositions_.addressStart + 18 * charWidth_;
	pixelPositions_.argumentsStart = pixelPositions_.opcodeStart + 9 * charWidth_;
	pixelPositions_.arrowsStart = pixelPositions_.argumentsStart + 30 * charWidth_;
}

void ImDisasmView::Search(std::string_view needle) {
	searchQuery_ = needle;
	for (size_t i = 0; i < searchQuery_.size(); i++) {
		searchQuery_[i] = tolower(searchQuery_[i]);
	}
	matchAddress_ = curAddress_;
	SearchNext(true);
}

void ImDisasmView::SearchNext(bool forward) {
	if (searchQuery_.empty()) {
		return;
	}

	// Note: Search will replace matchAddress_ with the current address.
	u32 searchAddress = g_disassemblyManager.getNthNextAddress(matchAddress_, 1);

	// limit address to sensible ranges
	if (searchAddress < 0x04000000)
		searchAddress = 0x04000000;
	if (searchAddress >= 0x04200000 && searchAddress < 0x08000000)
		searchAddress = 0x08000000;
	if (searchAddress >= 0x0A000000) {
		// MessageBox(wnd, L"Not found", L"Search", MB_OK);
		return;
	}

	searching_ = true;

	DisassemblyLineInfo lineInfo;
	while (searchAddress < 0x0A000000) {
		g_disassemblyManager.getLine(searchAddress, displaySymbols_, lineInfo, debugger_);

		char addressText[64];
		getDisasmAddressText(searchAddress, addressText, true, lineInfo.type == DISTYPE_OPCODE);

		const char* opcode = lineInfo.name.c_str();
		const char* arguments = lineInfo.params.c_str();

		char merged[512];
		int mergePos = 0;

		// I'm doing it manually to convert everything to lowercase at the same time
		for (int i = 0; addressText[i] != 0; i++) merged[mergePos++] = tolower(addressText[i]);
		merged[mergePos++] = ' ';
		for (int i = 0; opcode[i] != 0; i++) merged[mergePos++] = tolower(opcode[i]);
		merged[mergePos++] = ' ';
		for (int i = 0; arguments[i] != 0; i++) merged[mergePos++] = tolower(arguments[i]);
		merged[mergePos] = 0;

		// match!
		if (strstr(merged, searchQuery_.c_str()) != NULL) {
			matchAddress_ = searchAddress;
			searching_ = false;
			gotoAddr(searchAddress);
			return;
		}

		// cancel search
		if ((searchAddress % 256) == 0 && ImGui::GetKeyPressedAmount(ImGuiKey_Escape, 0.0f, 0.0f)) {
			searching_ = false;
			return;
		}

		searchAddress = g_disassemblyManager.getNthNextAddress(searchAddress, 1);
		if (searchAddress >= 0x04200000 && searchAddress < 0x08000000) searchAddress = 0x08000000;
	}

	statusBarText_ = "Not found: ";
	statusBarText_.append(searchQuery_);

	searching_ = false;
}

std::string ImDisasmView::disassembleRange(u32 start, u32 size) {
	std::string result;

	// gather all branch targets without labels
	std::set<u32> branchAddresses;
	for (u32 i = 0; i < size; i += debugger_->getInstructionSize(0)) {
		MIPSAnalyst::MipsOpcodeInfo info = MIPSAnalyst::GetOpcodeInfo(debugger_, start + i);

		if (info.isBranch && g_symbolMap->GetLabelString(info.branchTarget).empty()) {
			if (branchAddresses.find(info.branchTarget) == branchAddresses.end()) {
				branchAddresses.insert(info.branchTarget);
			}
		}
	}

	u32 disAddress = start;
	bool previousLabel = true;
	DisassemblyLineInfo line;
	while (disAddress < start + size) {
		char addressText[64], buffer[512];

		g_disassemblyManager.getLine(disAddress, displaySymbols_, line, debugger_);
		bool isLabel = getDisasmAddressText(disAddress, addressText, false, line.type == DISTYPE_OPCODE);

		if (isLabel) {
			if (!previousLabel) result += "\r\n";
			sprintf(buffer, "%s\r\n\r\n", addressText);
			result += buffer;
		} else if (branchAddresses.find(disAddress) != branchAddresses.end()) {
			if (!previousLabel) result += "\r\n";
			sprintf(buffer, "pos_%08X:\r\n\r\n", disAddress);
			result += buffer;
		}

		if (line.info.isBranch && !line.info.isBranchToRegister
			&& g_symbolMap->GetLabelString(line.info.branchTarget).empty()
			&& branchAddresses.find(line.info.branchTarget) != branchAddresses.end())
		{
			sprintf(buffer, "pos_%08X", line.info.branchTarget);
			line.params = line.params.substr(0, line.params.find("0x")) + buffer;
		}

		sprintf(buffer, "\t%s\t%s\r\n", line.name.c_str(), line.params.c_str());
		result += buffer;
		previousLabel = isLabel;
		disAddress += line.totalSize;
	}

	return result;
}

void ImDisasmView::disassembleToFile() { 	// get size
	/*
	u32 size;
	if (executeExpressionWindow(wnd, debugger, size) == false)
		return;
	if (size == 0 || size > 10 * 1024 * 1024) {
		MessageBox(wnd, L"Invalid size!", L"Error", MB_OK);
		return;
	}

	std::string filename;
	if (W32Util::BrowseForFileName(false, nullptr, L"Save Disassembly As...", nullptr, L"All Files\0*.*\0\0", nullptr, filename)) {
		std::wstring fileName = ConvertUTF8ToWString(filename);
		FILE *output = _wfopen(fileName.c_str(), L"wb");
		if (output == nullptr) {
			MessageBox(wnd, L"Could not open file!", L"Error", MB_OK);
			return;
		}

		std::string disassembly = disassembleRange(curAddress, size);
		fprintf(output, "%s", disassembly.c_str());

		fclose(output);
		MessageBox(wnd, L"Finished!", L"Done", MB_OK);
	}
	*/
}

void ImDisasmView::getOpcodeText(u32 address, char* dest, int bufsize) {
	DisassemblyLineInfo line;
	address = g_disassemblyManager.getStartAddress(address);
	g_disassemblyManager.getLine(address, displaySymbols_, line, debugger_);
	snprintf(dest, bufsize, "%s  %s", line.name.c_str(), line.params.c_str());
}

void ImDisasmView::scrollStepping(u32 newPc) {
	u32 windowEnd = g_disassemblyManager.getNthNextAddress(windowStart_, visibleRows_);

	newPc = g_disassemblyManager.getStartAddress(newPc);
	if (newPc >= windowEnd || newPc >= g_disassemblyManager.getNthPreviousAddress(windowEnd, 1))
	{
		windowStart_ = g_disassemblyManager.getNthPreviousAddress(newPc, visibleRows_ - 2);
	}
}

u32 ImDisasmView::getInstructionSizeAt(u32 address) {
	u32 start = g_disassemblyManager.getStartAddress(address);
	u32 next = g_disassemblyManager.getNthNextAddress(start, 1);
	return next - address;
}


void ImDisasmWindow::Draw(MIPSDebugInterface *mipsDebug, ImConfig &cfg, ImControl &control, CoreState coreState) {
	disasmView_.setDebugger(mipsDebug);

	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(Title(), &cfg.disasmOpen)) {
		ImGui::End();
		return;
	}

	if (ImGui::IsWindowFocused()) {
		// Process stepping keyboard shortcuts.
		if (ImGui::IsKeyPressed(ImGuiKey_F10)) {
			u32 stepSize = disasmView_.getInstructionSizeAt(mipsDebug->GetPC());
			Core_RequestCPUStep(CPUStepType::Over, stepSize);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_F11)) {
			u32 stepSize = disasmView_.getInstructionSizeAt(mipsDebug->GetPC());
			Core_RequestCPUStep(CPUStepType::Into, stepSize);
		}
	}

	if (coreState == CORE_STEPPING_GE || coreState == CORE_RUNNING_GE) {
		ImGui::Text("!!! Currently stepping the GE");
		ImGui::SameLine();
		if (ImGui::SmallButton("Open Ge Debugger")) {
			cfg.geDebuggerOpen = true;
			ImGui::SetWindowFocus("GE Debugger");
		}
	}

	ImGui::BeginDisabled(coreState != CORE_STEPPING_CPU);
	if (ImGui::SmallButton("Run")) {
		Core_Resume();
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(coreState != CORE_RUNNING_CPU);
	if (ImGui::SmallButton("Pause")) {
		Core_Break(BreakReason::DebugBreak);
	}
	ImGui::EndDisabled();

	ImGui::BeginDisabled(coreState != CORE_STEPPING_CPU);

	ImGui::SameLine();
	ImGui::Text("Step: ");
	ImGui::SameLine();

	if (ImGui::SmallButton("Into")) {
		u32 stepSize = disasmView_.getInstructionSizeAt(mipsDebug->GetPC());
		Core_RequestCPUStep(CPUStepType::Into, stepSize);
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("F11");
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("Over")) {
		u32 stepSize = disasmView_.getInstructionSizeAt(mipsDebug->GetPC());
		Core_RequestCPUStep(CPUStepType::Over, stepSize);
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("F10");
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("Out")) {
		Core_RequestCPUStep(CPUStepType::Out, 0);
	}

	/*
	ImGui::SameLine();
	if (ImGui::SmallButton("Frame")) {
		Core_RequestCPUStep(CPUStepType::Frame, 0);
	}*/

	ImGui::SameLine();
	if (ImGui::SmallButton("Syscall")) {
		hleDebugBreak();
		Core_Resume();
	}

	ImGui::SameLine();
	if (ImGui::RepeatButton("Skim")) {
		u32 stepSize = disasmView_.getInstructionSizeAt(mipsDebug->GetPC());
		Core_RequestCPUStep(CPUStepType::Into, stepSize);
	}

	ImGui::EndDisabled();

	ImGui::SameLine();
	if (ImGui::SmallButton("Goto PC")) {
		disasmView_.GotoPC();
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Goto RA")) {
		disasmView_.GotoRA();
	}

	if (ImGui::BeginPopup("disSearch")) {
		if (ImGui::IsWindowAppearing()) {
			ImGui::SetKeyboardFocusHere();
		}
		if (ImGui::InputText("Search", searchTerm_, sizeof(searchTerm_), ImGuiInputTextFlags_EnterReturnsTrue)) {
			disasmView_.Search(searchTerm_);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("Search")) {
		// Open a small popup
		ImGui::OpenPopup("disSearch");
		ImGui::Shortcut(ImGuiKey_F | ImGuiMod_Ctrl);
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("Next")) {
		disasmView_.SearchNext(true);
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("Settings")) {
		ImGui::OpenPopup("disSettings");
	}

	if (ImGui::BeginPopup("disSettings")) {
		ImGui::Checkbox("Follow PC", &disasmView_.followPC_);
		ImGui::EndPopup();
	}

	ImGui::SetNextItemWidth(100);
	if (ImGui::InputScalar("Go to addr: ", ImGuiDataType_U32, &gotoAddr_, NULL, NULL, "%08X")) {
		disasmView_.setCurAddress(gotoAddr_);
		disasmView_.scrollAddressIntoView();
	}
	ImGui::SameLine();
	if (ImGui::Button("Go")) {
		disasmView_.setCurAddress(gotoAddr_);
		disasmView_.scrollAddressIntoView();
	}

	BreakReason breakReason = Core_BreakReason();
	ImGui::SameLine();
	ImGui::TextUnformatted(BreakReasonToString(breakReason));

	ImVec2 avail = ImGui::GetContentRegionAvail();
	avail.y -= ImGui::GetTextLineHeightWithSpacing();

	if (ImGui::BeginChild("left", ImVec2(150.0f, avail.y), ImGuiChildFlags_ResizeX)) {
		if (symCache_.empty() || symsDirty_) {
			symCache_ = g_symbolMap->GetAllActiveSymbols(SymbolType::ST_FUNCTION);
			symsDirty_ = false;
		}

		if (selectedSymbol_ >= 0 && selectedSymbol_ < symCache_.size()) {
			auto &sym = symCache_[selectedSymbol_];
			if (ImGui::TreeNode("Edit Symbol", "Edit %s", sym.name.c_str())) {
				if (ImGui::InputText("Name", selectedSymbolName_, sizeof(selectedSymbolName_), ImGuiInputTextFlags_EnterReturnsTrue)) {
					g_symbolMap->SetLabelName(selectedSymbolName_, sym.address);
					symsDirty_ = true;
				}
				ImGui::Text("%08x (size: %0d)", sym.address, sym.size);
				ImGui::TreePop();
			}
		}

		if (ImGui::BeginListBox("##symbols", ImGui::GetContentRegionAvail())) {
			ImGuiListClipper clipper;
			clipper.Begin((int)symCache_.size(), -1);
			while (clipper.Step()) {
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
					if (ImGui::Selectable(symCache_[i].name.c_str(), selectedSymbol_ == i)) {
						disasmView_.gotoAddr(symCache_[i].address);
						disasmView_.scrollAddressIntoView();
						truncate_cpy(selectedSymbolName_, symCache_[i].name.c_str());
						selectedSymbol_ = i;
					}
				}
			}
			clipper.End();
			ImGui::EndListBox();
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();
	if (ImGui::BeginChild("right", ImVec2(0.0f, avail.y))) {
		disasmView_.Draw(ImGui::GetWindowDrawList(), control);
	}
	ImGui::EndChild();

	StatusBar(disasmView_.StatusBarText());
	ImGui::End();
}
