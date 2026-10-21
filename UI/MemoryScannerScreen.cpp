// Copyright (c) 2026- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <algorithm>

#include "Common/Data/Text/I18n.h"
#include "Common/File/FileUtil.h"
#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/StringUtils.h"
#include "Common/System/OSD.h"
#include "Common/System/System.h"
#include "Common/TimeUtil.h"
#include "Common/UI/Context.h"
#include "Common/UI/TabHolder.h"
#include "Common/UI/UI.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/ScreenManager.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/CwCheat.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/MemMap.h"
#include "Core/MemoryScanner.h"
#include "Core/RetroAchievements.h"
#include "Core/System.h"
#include "UI/MemoryScannerScreen.h"

// Lists with this many results or fewer are shown, with live values.
static constexpr size_t MAX_LISTED_RESULTS = 20;
static constexpr double REFRESH_INTERVAL = 1.0;

// Kept between openings of the scanner, like the results are.
static int s_type = (int)ScanValueType::U32;
static int s_compareFirst = (int)ScanCompare::Equal;
static int s_compareNext = (int)ScanCompare::Equal;
static std::string s_value;
static std::string s_value2;

static const char *g_typeNames[] = { "8-bit", "16-bit", "32-bit", "Float" };

// In ScanCompare order.
static const char *g_compareNames[] = {
	"Exact value",
	"Not equal to",
	"Bigger than",
	"Smaller than",
	"Value between",
	"Unknown initial value",
	"Increased value",
	"Decreased value",
	"Increased value by",
	"Decreased value by",
	"Changed value",
	"Unchanged value",
};
static_assert(ARRAY_SIZE(g_compareNames) == (int)ScanCompare::COUNT, "compare names");

static std::string CurrentGameID() {
	return g_paramSFO.GetDiscID();
}

// ScanValueEditor

void ScanValueEditor::CreateViews(UI::ViewGroup *parent, ScreenManager *screenManager, RequesterToken token, bool twoFields) {
	using namespace UI;
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);

	screenManager_ = screenManager;
	token_ = token;
	fieldCount_ = twoFields ? 2 : 1;
	if (activeField >= fieldCount_)
		activeField = 0;

	for (int i = 0; i < fieldCount_; i++) {
		fieldViews_[i] = parent->Add(new Choice(""));
		fieldViews_[i]->OnClick.Add([this, i](EventParams &) {
			activeField = i;
			UpdateViews();
		});
	}
	for (int i = fieldCount_; i < 2; i++) {
		fieldViews_[i] = nullptr;
	}

	// Big keys, so they're easy to hit with a thumb.
	GridLayoutSettings settings(80, 52, 4);
	settings.fillCells = true;
	GridLayout *grid = parent->Add(new GridLayoutList(settings, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(0, 4))));
	static const char *keys[] = {
		"7", "8", "9", "Del",
		"4", "5", "6", "-",
		"1", "2", "3", ".",
		"0", "0x", "C", "...",
	};
	for (const char *key : keys) {
		std::string_view label = key;
		if (!strcmp(key, "...")) {
			label = cw->T("Keyboard");
		} else if (!strcmp(key, "Del")) {
			label = cw->T("Del");
		}
		Choice *c = grid->Add(new Choice(label));
		c->SetCentered(true);
		c->OnClick.Add([this, key](EventParams &) {
			Key(key);
		});
	}

	UpdateViews();
}

void ScanValueEditor::Key(const char *key) {
	if (activeField < 0 || activeField >= 2 || !fields[activeField])
		return;
	std::string &s = *fields[activeField];

	if (!strcmp(key, "Del")) {
		if (!s.empty())
			s.pop_back();
	} else if (!strcmp(key, "C")) {
		s.clear();
	} else if (!strcmp(key, "-")) {
		// Toggle the sign.
		if (!s.empty() && s[0] == '-')
			s.erase(0, 1);
		else
			s.insert(0, "-");
	} else if (!strcmp(key, ".")) {
		if (s.find('.') == std::string::npos)
			s += s.empty() || s == "-" ? "0." : ".";
	} else if (!strcmp(key, "0x")) {
		// Toggle hex.
		size_t pos = (!s.empty() && s[0] == '-') ? 1 : 0;
		if (s.compare(pos, 2, "0x") == 0)
			s.erase(pos, 2);
		else
			s.insert(pos, "0x");
	} else if (!strcmp(key, "...")) {
		// The system keyboard, for hex digits A-F or anything else.
		if (screenManager_) {
			auto cw = GetI18NCategory(I18NCat::CWCHEATS);
			const int index = activeField;
			AskForInput(screenManager_, token_, fieldViews_[index], cw->T("Value"), [this, index](const std::string &text, bool success) {
				if (success && fields[index]) {
					*fields[index] = text;
					UpdateViews();
				}
			});
		}
		return;
	} else if (s.size() < 24) {
		s += key;
	}
	UpdateViews();
}

bool ScanValueEditor::HandleKey(const KeyInput &input) {
	// Typing with a hardware keyboard.
	if (input.flags & KeyInputFlags::CHAR) {
		int c = input.unicodeChar;
		std::string *field = fields[activeField];
		if (!field)
			return false;
		if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == 'x' || c == 'X' || c == '.' || c == '-' || c == '+' || c == '$') {
			if (field->size() < 24) {
				*field += (char)c;
				UpdateViews();
			}
			return true;
		}
		return false;
	}
	if ((input.flags & KeyInputFlags::DOWN) && input.keyCode == NKCODE_DEL) {
		Key("Del");
		return true;
	}
	return false;
}

void ScanValueEditor::Detach() {
	fields[0] = nullptr;
	fields[1] = nullptr;
	fieldViews_[0] = nullptr;
	fieldViews_[1] = nullptr;
	fieldCount_ = 0;
	activeField = 0;
}

void ScanValueEditor::UpdateViews() {
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	for (int i = 0; i < fieldCount_; i++) {
		if (!fieldViews_[i] || !fields[i])
			continue;
		std::string text = fields[i]->empty() ? std::string("_") : *fields[i];
		std::string label = fieldLabels[i] ? std::string(cw->T(fieldLabels[i])) : std::string(cw->T("Value"));
		fieldViews_[i]->SetText(StringFromFormat("%s%s: %s", fieldCount_ > 1 && i == activeField ? "> " : "", label.c_str(), text.c_str()));
		fieldViews_[i]->SetSelectedIndicator(fieldCount_ > 1 && i == activeField);
	}
}

// MemoryScannerScreen

MemoryScannerScreen::MemoryScannerScreen() : PopupScreen(T(I18NCat::CWCHEATS, "Memory Scanner"), T(I18NCat::DIALOG, "Back")) {
	MemoryScanner &scanner = GetMemoryScanner();
	if (scanner.ScanCount() > 0 && scanner.GameID() != CurrentGameID()) {
		scanner.Reset();
	}
	// The type is fixed while a search is going.
	if (scanner.ScanCount() > 0) {
		s_type = (int)scanner.Type();
	}
}

UI::Size MemoryScannerScreen::PopupWidth() const {
	return 820;
}

bool MemoryScannerScreen::WantsTextInput() const {
	return !System_GetPropertyBool(SYSPROP_KEYBOARD_IS_SOFT);
}

void MemoryScannerScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	resultViews_.clear();
	freezeFlags_.clear();
	status_ = nullptr;

	const Bounds bounds = GetLayoutBounds(*screenManager()->getUIContext());
	if (bounds.w >= 640) {
		LinearLayout *columns = parent->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f)));
		ScrollView *left = columns->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(370, FILL_PARENT, 0.0f)));
		LinearLayout *leftColumn = left->Add(new LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		leftColumn->SetSpacing(2.0f);
		CreateControls(leftColumn);

		ScrollView *right = columns->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f, Margins(8, 0, 0, 0))));
		LinearLayout *rightColumn = right->Add(new LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		CreateResults(rightColumn);
	} else {
		// Portrait: one column, results first so they're visible without scrolling.
		ScrollView *scroll = parent->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f)));
		LinearLayout *column = scroll->Add(new LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		CreateResults(column);
		CreateControls(column);
	}

	lastRefresh_ = time_now_d();
}

void MemoryScannerScreen::CreateControls(UI::ViewGroup *parent) {
	using namespace UI;
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	MemoryScanner &scanner = GetMemoryScanner();
	const bool searching = scanner.ScanCount() > 0;

	u32 start, size;
	MemoryScanner::GetPSPScanRange(&start, &size);
	TextView *range = parent->Add(new TextView(StringFromFormat("%s: 0x%08X - 0x%08X (%u MB)", cw->T_cstr("User memory"), start, start + size, size >> 20)));
	range->SetSmall(true);

	if (!searching) {
		parent->Add(new ItemHeader(cw->T("Value type")));
		ChoiceStrip *types = parent->Add(new ChoiceStrip(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		for (const char *name : g_typeNames) {
			types->AddChoice(cw->T(name));
		}
		types->SetSelection(s_type, false);
		types->OnChoice.Add([types](EventParams &) {
			s_type = types->GetSelection();
		});
	} else {
		parent->Add(new InfoItem(cw->T("Value type"), cw->T(g_typeNames[s_type])));
		parent->Add(new InfoItem(cw->T("Scans"), scanner.ScanCount()));
	}

	// The compare mode can be changed before every scan.
	int *compare = searching ? &s_compareNext : &s_compareFirst;
	PopupMultiChoice *compareChoice = parent->Add(new PopupMultiChoice(compare, cw->T("Scan type"), g_compareNames, 0, ARRAY_SIZE(g_compareNames), I18NCat::CWCHEATS, screenManager()));
	for (int i = 0; i < (int)ScanCompare::COUNT; i++) {
		const ScanCompare c = (ScanCompare)i;
		const bool allowed = searching ? c != ScanCompare::Unknown : MemoryScanner::CompareAllowedOnFirstScan(c);
		if (!allowed)
			compareChoice->HideChoice(i);
	}
	compareChoice->OnChoice.Add([this](EventParams &) {
		RecreateViews();
	});

	const ScanCompare mode = (ScanCompare)*compare;
	if (MemoryScanner::CompareNeedsValue(mode)) {
		editor_.fields[0] = &s_value;
		editor_.fields[1] = &s_value2;
		const bool between = MemoryScanner::CompareNeedsSecondValue(mode);
		editor_.fieldLabels[0] = between ? "From" : "Value";
		editor_.fieldLabels[1] = "To";
		editor_.CreateViews(parent, screenManager(), GetRequesterToken(), between);
	} else {
		editor_.Detach();
	}

	LinearLayout *buttons = parent->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	if (!searching) {
		Choice *first = buttons->Add(new Choice(cw->T("First Scan"), ImageID("I_SEARCH"), new LinearLayoutParams(1.0f)));
		first->OnClick.Add([this](EventParams &) { DoScan(true); });
	} else {
		Choice *next = buttons->Add(new Choice(cw->T("Next Scan"), ImageID("I_SEARCH"), new LinearLayoutParams(1.0f)));
		next->OnClick.Add([this](EventParams &) { DoScan(false); });
		Choice *undo = buttons->Add(new Choice(cw->T("Undo"), new LinearLayoutParams(0.6f)));
		undo->SetEnabled(scanner.CanUndo());
		undo->OnClick.Add([this](EventParams &) {
			GetMemoryScanner().Undo();
			auto cw = GetI18NCategory(I18NCat::CWCHEATS);
			statusText_ = ApplySafeSubstitutions(cw->T("%1 addresses found"), (int)std::min<size_t>(GetMemoryScanner().ResultCount(), 0x7FFFFFFF));
			statusIsError_ = false;
			RecreateViews();
		});
		parent->Add(new Choice(cw->T("New Search")))->OnClick.Add([this](EventParams &) {
			GetMemoryScanner().Reset();
			statusText_.clear();
			RecreateViews();
		});
	}

	parent->Add(new CheckBox(&g_Config.bMemoryScannerPause, cw->T("Pause emulation while the scanner is open")));

	status_ = parent->Add(new TextView(statusText_, FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(4))));
	if (statusIsError_) {
		status_->SetTextColor(0xFF4040FF);
	}
	if (statusText_.empty()) {
		status_->SetVisibility(V_GONE);
	}
}

void MemoryScannerScreen::CreateResults(UI::ViewGroup *parent) {
	using namespace UI;
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);

	MemoryScanner &scanner = GetMemoryScanner();

	// Frozen values first, so they can be watched and unfrozen whatever the search shows.
	if (!scanner.Freezes().empty()) {
		parent->Add(new ItemHeader(cw->T("Frozen values")));
		// Copy, the rows can change the list.
		const std::vector<MemoryScanner::Freeze> freezes = scanner.Freezes();
		for (const auto &f : freezes) {
			AddResultRow(parent, f.addr, f.type, false);
		}
		parent->Add(new Choice(cw->T("Unfreeze all")))->OnClick.Add([this](EventParams &) {
			GetMemoryScanner().ClearFreezes();
			RecreateViews();
		});
	}

	parent->Add(new ItemHeader(cw->T("Results")));

	if (scanner.ScanCount() == 0) {
		parent->Add(new TextView(cw->T("MemoryScannerHelp", "Pick the value type, enter the value you see in-game and press First Scan. If you can't see the number (like a health bar), use 'Unknown initial value', then scan for increased or decreased values."), FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(4))));
		return;
	}

	const size_t count = scanner.ResultCount();
	if (count == 0) {
		parent->Add(new TextView(cw->T("No addresses found. Start a new search."), FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(4))));
		return;
	}

	if (count > MAX_LISTED_RESULTS) {
		std::string countStr = StringFromFormat("%llu", (unsigned long long)count);
		parent->Add(new TextView(ApplySafeSubstitutions(cw->T("%1 addresses found, please continue search after changing value in-game to limit number of results"), countStr), FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(4))));
		return;
	}

	TextView *hint = parent->Add(new TextView(cw->T("Tick Freeze to hold a value. Tap an address to change its value or save it as a cheat"), FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(4))));
	hint->SetSmall(true);

	const ScanValueType type = scanner.Type();
	for (u32 addr : scanner.GetResults(MAX_LISTED_RESULTS)) {
		AddResultRow(parent, addr, type, true);
	}
}

void MemoryScannerScreen::AddResultRow(UI::ViewGroup *parent, u32 addr, ScanValueType type, bool showPrevious) {
	using namespace UI;
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);

	LinearLayout *row = parent->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	row->SetSpacing(4.0f);

	freezeFlags_.push_back(GetMemoryScanner().IsFrozen(addr, type));
	bool *flag = &freezeFlags_.back();
	CheckBox *freeze = row->Add(new CheckBox(flag, cw->T("Freeze"), "", new LinearLayoutParams(150, WRAP_CONTENT, 0.0f)));
	freeze->OnClick.Add([this, addr, type, flag](EventParams &) {
		// Hold the value it has right now.
		u32 bits = 0;
		if (MemoryScanner::ReadPSP(type, addr, &bits)) {
			GetMemoryScanner().SetFrozen(addr, type, bits, *flag);
		}
		RecreateViews();
	});

	Choice *c = row->Add(new Choice(ResultText(addr, type, showPrevious), new LinearLayoutParams(1.0f)));
	c->OnClick.Add([this, addr, type](EventParams &e) {
		MemoryScannerEditScreen *edit = new MemoryScannerEditScreen(addr, type);
		if (e.v)
			edit->SetPopupOrigin(e.v);
		screenManager()->push(edit);
	});
	resultViews_.push_back(ResultView{ addr, type, showPrevious, c });
}

std::string MemoryScannerScreen::ResultText(u32 addr, ScanValueType type, bool showPrevious) const {
	const MemoryScanner &scanner = GetMemoryScanner();
	u32 bits = 0;
	if (!MemoryScanner::ReadPSP(type, addr, &bits)) {
		return StringFromFormat("0x%08X  ?", addr);
	}
	std::string text = StringFromFormat("0x%08X  =  %s", addr, MemoryScanner::FormatValue(type, bits).c_str());
	if (!showPrevious) {
		// A frozen value from the list above: show its type, it may not be the search's.
		auto cw = GetI18NCategory(I18NCat::CWCHEATS);
		static const char *typeNames[] = { "8-bit", "16-bit", "32-bit", "Float" };
		text += StringFromFormat("   [%s]", cw->T_cstr(typeNames[(int)type]));
		return text;
	}
	const u32 prev = scanner.PreviousValue(addr);
	if (prev != bits) {
		auto cw = GetI18NCategory(I18NCat::CWCHEATS);
		text += StringFromFormat("   (%s %s)", cw->T_cstr("was"), MemoryScanner::FormatValue(type, prev).c_str());
	}
	return text;
}

void MemoryScannerScreen::RefreshResultValues() {
	for (auto &rv : resultViews_) {
		rv.view->SetText(ResultText(rv.addr, rv.type, rv.showPrevious));
	}
	lastRefresh_ = time_now_d();
}

void MemoryScannerScreen::DoScan(bool first) {
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	MemoryScanner &scanner = GetMemoryScanner();

	const ScanValueType type = first ? (ScanValueType)s_type : scanner.Type();
	const ScanCompare compare = (ScanCompare)(first ? s_compareFirst : s_compareNext);

	ScanValue value, value2;
	if (MemoryScanner::CompareNeedsValue(compare)) {
		if (!MemoryScanner::ParseValue(type, s_value, &value)) {
			statusText_ = cw->T("Invalid value for this value type");
			statusIsError_ = true;
			RecreateViews();
			return;
		}
		if (MemoryScanner::CompareNeedsSecondValue(compare) && !MemoryScanner::ParseValue(type, s_value2, &value2)) {
			statusText_ = cw->T("Invalid value for this value type");
			statusIsError_ = true;
			RecreateViews();
			return;
		}
	}

	// This runs on the emulation thread, so the game is stopped for the duration of the scan,
	// whether or not the scanner pauses it otherwise.
	const double startTime = time_now_d();
	bool ok;
	if (first) {
		scanner.SetGameID(CurrentGameID());
		ok = scanner.NewScan(type, compare, value, value2);
	} else {
		ok = scanner.NextScan(compare, value, value2);
	}
	const int ms = (int)((time_now_d() - startTime) * 1000.0);

	if (!ok) {
		statusText_ = cw->T("Scan failed, game memory isn't available");
		statusIsError_ = true;
	} else {
		std::string countStr = StringFromFormat("%llu", (unsigned long long)scanner.ResultCount());
		statusText_ = ApplySafeSubstitutions(cw->T("%1 addresses found (%2 ms)"), countStr, StringFromFormat("%d", ms));
		statusIsError_ = false;
	}
	RecreateViews();
}

void MemoryScannerScreen::update() {
	PopupScreen::update();
	if (!resultViews_.empty() && time_now_d() - lastRefresh_ >= REFRESH_INTERVAL) {
		RefreshResultValues();
	}
}

bool MemoryScannerScreen::key(const KeyInput &input) {
	if (editor_.fields[0] && editor_.HandleKey(input)) {
		return true;
	}
	return PopupScreen::key(input);
}

void MemoryScannerScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	// Values may have been set, or frozen, in the edit popup.
	if (dynamic_cast<const MemoryScannerEditScreen *>(dialog)) {
		RecreateViews();
	} else {
		RefreshResultValues();
	}
}

// MemoryScannerEditScreen

MemoryScannerEditScreen::MemoryScannerEditScreen(u32 addr, ScanValueType type)
	: PopupScreen(T(I18NCat::CWCHEATS, "Edit value"), T(I18NCat::DIALOG, "Back")), addr_(addr), type_(type) {
	frozen_ = GetMemoryScanner().IsFrozen(addr_, type_);
	u32 bits = 0;
	if (MemoryScanner::ReadPSP(type_, addr_, &bits)) {
		value_ = MemoryScanner::FormatValue(type_, bits);
		// Drop the "(signed)" part, it isn't a valid value to type.
		size_t space = value_.find(' ');
		if (space != std::string::npos)
			value_.resize(space);
	}
	name_ = StringFromFormat("Scanner 0x%08X", addr_ - PSP_GetUserMemoryBase());
}

bool MemoryScannerEditScreen::WantsTextInput() const {
	return !System_GetPropertyBool(SYSPROP_KEYBOARD_IS_SOFT);
}

void MemoryScannerEditScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);

	ScrollView *scroll = parent->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f)));
	LinearLayout *items = scroll->Add(new LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	items->Add(new InfoItem(cw->T("Address"), StringFromFormat("0x%08X (+0x%07X)", addr_, addr_ - PSP_GetUserMemoryBase())));
	currentText_ = items->Add(new TextView("", new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 4))));

	editor_.fields[0] = &value_;
	editor_.fieldLabels[0] = "New value";
	editor_.CreateViews(items, screenManager(), GetRequesterToken(), false);

	items->Add(new Choice(cw->T("Set"), ImageID("I_CHECKMARK")))->OnClick.Add([this](EventParams &) {
		OnSet();
	});
	items->Add(new CheckBox(&frozen_, cw->T("Freeze"), cw->T("Keep writing this value while the game runs")))->OnClick.Add([this](EventParams &) {
		// Hold the typed value if it's valid, otherwise what's in memory now.
		u32 bits = 0;
		if (!GetValueToUse(&bits) && !MemoryScanner::ReadPSP(type_, addr_, &bits)) {
			return;
		}
		GetMemoryScanner().SetFrozen(addr_, type_, bits, frozen_);
		UpdateTexts();
	});

	items->Add(new ItemHeader(cw->T("Save as cheat")));
	items->Add(new PopupTextInputChoice(GetRequesterToken(), &name_, cw->T("Cheat name"), "", 64, screenManager()));
	cheatText_ = items->Add(new TextView("", new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 4))));
	cheatText_->SetSmall(true);
	items->Add(new Choice(cw->T("Add"), ImageID("I_PLUS")))->OnClick.Add([this](EventParams &) {
		OnAdd();
	});

	status_ = items->Add(new TextView("", FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 4))));
	status_->SetVisibility(V_GONE);

	UpdateTexts();
}

bool MemoryScannerEditScreen::GetValueToUse(u32 *bits) {
	ScanValue v;
	if (MemoryScanner::ParseValue(type_, value_, &v)) {
		*bits = v.bits;
		return true;
	}
	return false;
}

void MemoryScannerEditScreen::UpdateTexts() {
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	u32 bits = 0;
	if (currentText_) {
		if (MemoryScanner::ReadPSP(type_, addr_, &bits)) {
			currentText_->SetText(StringFromFormat("%s: %s  (%s)", cw->T_cstr("Current value"), MemoryScanner::FormatValue(type_, bits).c_str(), MemoryScanner::FormatHex(type_, bits).c_str()));
		} else {
			currentText_->SetText(cw->T("Current value: ?"));
		}
	}
	if (cheatText_) {
		u32 newBits = 0;
		if (GetValueToUse(&newBits)) {
			cheatText_->SetText(MemoryScanner::MakeCwCheatLine(type_, addr_, newBits));
		} else {
			cheatText_->SetText(cw->T("Invalid value for this value type"));
		}
	}
	lastRefresh_ = time_now_d();
}

void MemoryScannerEditScreen::OnSet() {
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	u32 bits = 0;
	bool ok = GetValueToUse(&bits) && MemoryScanner::WritePSP(type_, addr_, bits);
	if (ok) {
		// A frozen address is held at the new value from now on.
		GetMemoryScanner().UpdateFrozenValue(addr_, type_, bits);
	}
	status_->SetText(ok ? cw->T("Value written to game memory") : cw->T("Invalid value for this value type"));
	status_->SetTextColor(ok ? 0xFF40FF40 : 0xFF4040FF);
	status_->SetVisibility(UI::V_VISIBLE);
	UpdateTexts();
}

void MemoryScannerEditScreen::OnAdd() {
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	u32 bits = 0;
	if (!GetValueToUse(&bits)) {
		status_->SetText(cw->T("Invalid value for this value type"));
		status_->SetTextColor(0xFF4040FF);
		status_->SetVisibility(UI::V_VISIBLE);
		return;
	}
	std::string name(StripSpaces(name_));
	if (name.empty()) {
		name = StringFromFormat("Scanner 0x%08X", addr_ - PSP_GetUserMemoryBase());
	}
	std::string error;
	if (AppendCheatToGameIni(name, { MemoryScanner::MakeCwCheatLine(type_, addr_, bits) }, &error)) {
		status_->SetText(ApplySafeSubstitutions(cw->T("Cheat '%1' added, enable it in the Cheats menu"), name));
		status_->SetTextColor(0xFF40FF40);
		g_OSD.Show(OSDType::MESSAGE_SUCCESS, ApplySafeSubstitutions(cw->T("Cheat '%1' added, enable it in the Cheats menu"), name), 3.0f);
	} else {
		status_->SetText(error);
		status_->SetTextColor(0xFF4040FF);
	}
	status_->SetVisibility(UI::V_VISIBLE);
}

void MemoryScannerEditScreen::update() {
	PopupScreen::update();
	if (time_now_d() - lastRefresh_ >= REFRESH_INTERVAL) {
		UpdateTexts();
	}
}

bool MemoryScannerEditScreen::key(const KeyInput &input) {
	if (editor_.HandleKey(input)) {
		UpdateTexts();
		return true;
	}
	return PopupScreen::key(input);
}

// Cheat file

static std::string FormatGameIDForCheat(const std::string &id) {
	// "ULUS10041" -> "ULUS-10041", as cheat files write it.
	if (id.size() == 9 && id.find('-') == std::string::npos) {
		return id.substr(0, 4) + "-" + id.substr(4);
	}
	return id;
}

bool AppendCheatToGameIni(const std::string &name, const std::vector<std::string> &lines, std::string *error) {
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	const std::string gameID = CurrentGameID();
	if (gameID.empty()) {
		*error = cw->T("No game running");
		return false;
	}

	CWCheatEngine engine(gameID);
	engine.CreateCheatFile();
	const Path &filename = engine.CheatFilename();

	// Find out which game section the end of the file is in.
	std::string contents;
	File::ReadTextFileToString(filename, &contents);
	const std::string wanted = ReplaceAll(gameID, "-", "");
	bool hasAnySection = false;
	bool lastSectionIsOurs = false;
	{
		std::vector<std::string_view> fileLines;
		SplitString(contents, '\n', fileLines);
		for (std::string_view l : fileLines) {
			std::string line(StripSpaces(l));
			// Skip a BOM.
			if (line.size() >= 3 && (u8)line[0] == 0xEF && (u8)line[1] == 0xBB && (u8)line[2] == 0xBF)
				line = std::string(StripSpaces(line.substr(3)));
			if (line.size() >= 2 && line[0] == '_' && line[1] == 'S') {
				hasAnySection = true;
				lastSectionIsOurs = ReplaceAll(std::string(StripSpaces(line.substr(2))), "-", "") == wanted;
			}
		}
	}

	std::string out;
	if (!contents.empty() && contents.back() != '\n') {
		out += '\n';
	}
	if (!hasAnySection || !lastSectionIsOurs) {
		out += "_S " + FormatGameIDForCheat(gameID) + "\n";
		std::string title = g_paramSFO.GetValueString("TITLE");
		if (!title.empty()) {
			out += "_G " + title + "\n";
		}
	}
	// Disabled, so it's there to turn on later in the Cheats menu.
	out += "_C0 " + name + "\n";
	for (const std::string &line : lines) {
		out += line + "\n";
	}

	FILE *f = File::OpenCFile(filename, "ab");
	if (!f) {
		*error = cw->T("Unable to write the cheat file");
		return false;
	}
	bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
	fclose(f);
	if (!ok) {
		*error = cw->T("Unable to write the cheat file");
		return false;
	}

	g_Config.bReloadCheats = true;
	return true;
}
