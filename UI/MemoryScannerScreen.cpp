// Copyright (c) 2012- PPSSPP Project.

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
#include <cstdlib>
#include <cstring>
#include <memory>

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
#include "Core/HLE/sceKernelModule.h"
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
// The module for Search in > Globals of one module. Empty means the main module.
static std::string s_moduleName;

static MemoryScanner::SearchIn ConfiguredSearchIn() {
	const int v = g_Config.iMemoryScannerSearchIn;
	if (v < 0 || v >= (int)MemoryScanner::SearchIn::COUNT) {
		return MemoryScanner::SearchIn::UserMemoryNoCode;
	}
	return (MemoryScanner::SearchIn)v;
}

// In MemoryScanner::SearchIn order.
static const char *g_searchInNames[] = {
	"User memory except code",
	"Globals of one module",
	"All user memory",
};
static_assert(ARRAY_SIZE(g_searchInNames) == (int)MemoryScanner::SearchIn::COUNT, "search in names");

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

static void AddModuleOnlyHint(UI::ViewGroup *parent);

// ScanValueEditor

void ScanValueEditor::CreateViews(UI::ViewGroup *parent, ScreenManager *screenManager, RequesterToken token, bool twoFields) {
	using namespace UI;
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);

	screenManager_ = screenManager;
	token_ = token;
	fieldCount_ = twoFields ? 2 : 1;
	if (activeField >= fieldCount_) {
		activeField = 0;
	}

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

	// Big keys, so they're easy to hit with a thumb. Always four to a row, whatever the width of
	// the window, so the pad looks the same everywhere. An empty key is a gap. (FILL_PARENT with a
	// weight shares the row equally; a width of 0 would be a fixed size of 0.)
	auto addRows = [&](ViewGroup *container, const std::vector<const char *> &keys) {
		LinearLayout *row = nullptr;
		for (size_t i = 0; i < keys.size(); i++) {
			if (i % 4 == 0) {
				row = container->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
				row->SetSpacing(4.0f);
			}
			const char *key = keys[i];
			if (!key[0]) {
				row->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f)));
				continue;
			}
			std::string_view label = key;
			if (!strcmp(key, "...")) {
				label = cw->T("Keyboard");
			} else if (!strcmp(key, "Del")) {
				label = cw->T("Del");
			} else if (!strcmp(key, "Clr")) {
				label = cw->T("Clear");
			}
			Choice *c = row->Add(new Choice(label, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f)));
			c->SetCentered(true);
			c->OnClick.Add([this, key](EventParams &) {
				Key(key);
			});
			if (!strcmp(key, "Hex")) {
				hexToggle_ = c;
			} else if (!strcmp(key, ".")) {
				dotKey_ = c;
			}
		}
	};

	LinearLayout *pad = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(0, 4, 0, 0))));
	pad->SetSpacing(4.0f);
	addRows(pad, {
		"7", "8", "9", "Del",
		"4", "5", "6", "-",
		"1", "2", "3", ".",
		"0", "Hex", "Clr", "...",
	});
	// Only shown in hex mode.
	LinearLayout *hexKeys = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(0, 4, 0, 4))));
	hexKeys->SetSpacing(4.0f);
	addRows(hexKeys, {
		"A", "B", "C", "D",
		"E", "F", "", "",
	});
	hexKeys_ = hexKeys;

	UpdateViews();
}

bool ScanValueEditor::ActiveFieldIsHex() const {
	if (activeField < 0 || activeField >= 2 || !fields[activeField]) {
		return false;
	}
	const std::string &s = *fields[activeField];
	size_t pos = (!s.empty() && (s[0] == '-' || s[0] == '+')) ? 1 : 0;
	return s.compare(pos, 2, "0x") == 0 || s.compare(pos, 2, "0X") == 0 || s.compare(pos, 1, "$") == 0;
}

void ScanValueEditor::Key(const char *key) {
	if (activeField < 0 || activeField >= 2 || !fields[activeField]) {
		return;
	}
	std::string &s = *fields[activeField];

	if (!strcmp(key, "Del")) {
		if (!s.empty()) {
			s.pop_back();
		}
	} else if (!strcmp(key, "Clr")) {
		s.clear();
	} else if (!strcmp(key, "-")) {
		// Toggle the sign.
		if (!s.empty() && s[0] == '-') {
			s.erase(0, 1);
		} else {
			s.insert(0, "-");
		}
	} else if (!strcmp(key, ".")) {
		// A hex value is raw bits, even for floats, so no decimal point.
		if (!ActiveFieldIsHex() && s.find('.') == std::string::npos) {
			s += s.empty() || s == "-" ? "0." : ".";
		}
	} else if (!strcmp(key, "Hex")) {
		// Toggle hex: converts the typed number to the other base, and shows or hides the A-F keys.
		const bool wasHex = ActiveFieldIsHex();
		const bool negative = !s.empty() && s[0] == '-';
		size_t pos = (!s.empty() && (s[0] == '-' || s[0] == '+')) ? 1 : 0;
		std::string digits = s.substr(pos);
		if (wasHex) {
			digits.erase(0, digits[0] == '$' ? 1 : 2);
		} else {
			// A float's fraction can't carry over.
			size_t dot = digits.find('.');
			if (dot != std::string::npos) {
				digits.erase(dot);
			}
		}
		const bool valid = !digits.empty() && digits.size() <= 16 &&
			digits.find_first_not_of(wasHex ? "0123456789abcdefABCDEF" : "0123456789") == std::string::npos;
		std::string sign = negative ? "-" : "";
		if (!valid) {
			// Nothing (or nothing usable) typed yet, just switch the mode.
			s = sign + (wasHex ? "" : "0x");
		} else {
			unsigned long long v = strtoull(digits.c_str(), nullptr, wasHex ? 16 : 10);
			s = sign + (wasHex ? StringFromFormat("%llu", v) : StringFromFormat("0x%llX", v));
		}
	} else if (key[0] >= 'A' && key[0] <= 'F' && key[1] == '\0') {
		if (!ActiveFieldIsHex()) {
			return;
		}
		if (s.size() < 24) {
			s += key;
		}
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
		if (!field) {
			return false;
		}
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
	hexKeys_ = nullptr;
	hexToggle_ = nullptr;
	dotKey_ = nullptr;
	fieldCount_ = 0;
	activeField = 0;
}

void ScanValueEditor::UpdateViews() {
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	for (int i = 0; i < fieldCount_; i++) {
		if (!fieldViews_[i] || !fields[i]) {
			continue;
		}
		std::string text = fields[i]->empty() ? std::string("_") : *fields[i];
		std::string label = fieldLabels[i] ? std::string(cw->T(fieldLabels[i])) : std::string(cw->T("Value"));
		fieldViews_[i]->SetText(StringFromFormat("%s%s: %s", fieldCount_ > 1 && i == activeField ? "> " : "", label.c_str(), text.c_str()));
		fieldViews_[i]->SetSelectedIndicator(fieldCount_ > 1 && i == activeField);
	}
	const bool hex = ActiveFieldIsHex();
	if (hexKeys_) {
		hexKeys_->SetVisibility(hex ? UI::V_VISIBLE : UI::V_GONE);
	}
	if (hexToggle_) {
		// Says what pressing it switches to.
		hexToggle_->SetText(hex ? cw->T("Dec") : cw->T("Hex"));
	}
	if (dotKey_) {
		dotKey_->SetEnabled(!hex);
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

	watches_.Clear();
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

	// Wrapped, with some room at the sides: the range didn't fit the column in one line.
	parent->Add(new TextView(AreaText(), FLAG_WRAP_TEXT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 4, 12, 4))));

	if (!searching) {
		parent->Add(new ItemHeader(cw->T("Value type")));
		// Not a ChoiceStrip: its choices are created without layout params, which makes them
		// FILL_PARENT, so in a full-width row the first one took all the space and the others
		// couldn't be clicked. Equal weights instead.
		LinearLayout *types = parent->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		types->SetSpacing(0.0f);
		auto typeChoices = std::make_shared<std::vector<StickyChoice *>>();
		for (int i = 0; i < (int)ARRAY_SIZE(g_typeNames); i++) {
			StickyChoice *choice = types->Add(new StickyChoice(cw->T(g_typeNames[i]), "", new LinearLayoutParams(1.0f)));
			choice->SetCentered(true);
			if (i == s_type) {
				choice->Press();
			}
			choice->OnClick.Add([i, typeChoices](EventParams &) {
				s_type = i;
				for (int j = 0; j < (int)typeChoices->size(); j++) {
					if (j == i) {
						(*typeChoices)[j]->Press();
					} else {
						(*typeChoices)[j]->Release();
					}
				}
			});
			typeChoices->push_back(choice);
		}
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
		if (!allowed) {
			compareChoice->HideChoice(i);
		}
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

	const PointerScanner &pointers = GetPointerScanner();
	if (pointers.HasResults() && pointers.GameID() == CurrentGameID()) {
		std::string countStr = StringFromFormat("%llu", (unsigned long long)pointers.Results().size());
		parent->Add(new Choice(ApplySafeSubstitutions(cw->T("Pointer scan results (%1)"), countStr), ImageID("I_SEARCH")))->OnClick.Add([this](EventParams &e) {
			const PointerScanner &pointers = GetPointerScanner();
			PointerScanScreen *screen = new PointerScanScreen(pointers.Target(), pointers.Type());
			if (e.v) {
				screen->SetPopupOrigin(e.v);
			}
			screenManager()->push(screen);
		});
	}

	parent->Add(new Choice(cw->T("Scan options"), ImageID("I_GEAR")))->OnClick.Add([this](EventParams &e) {
		ScanOptionsScreen *options = new ScanOptionsScreen();
		if (e.v) {
			options->SetPopupOrigin(e.v);
		}
		screenManager()->push(options);
	});

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
			watches_.AddRow(parent, this, f.where, f.type, WatchList::Style::Frozen);
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
		if (scanner.SearchArea().searchIn == MemoryScanner::SearchIn::ModuleGlobals) {
			AddModuleOnlyHint(parent);
		}
		return;
	}

	if (count > MAX_LISTED_RESULTS) {
		std::string countStr = StringFromFormat("%llu", (unsigned long long)count);
		parent->Add(new TextView(ApplySafeSubstitutions(cw->T("%1 addresses found, please continue search after changing value in-game to limit number of results"), countStr), FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(4))));
		return;
	}

	TextView *hint = parent->Add(new TextView(cw->T("Tick Freeze to hold a value. Tap an address to change its value, save it as a cheat or find pointers to it"), FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(4))));
	hint->SetSmall(true);

	const ScanValueType type = scanner.Type();
	for (u32 addr : scanner.GetResults(MAX_LISTED_RESULTS)) {
		watches_.AddRow(parent, this, PointerChain::Direct(addr), type, WatchList::Style::SearchResult);
	}
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
	MemoryScanner::Error error = MemoryScanner::Error::None;
	if (first) {
		scanner.SetGameID(CurrentGameID());
		ok = scanner.NewScan(type, compare, value, value2, ConfiguredSearchIn(), s_moduleName, &error);
	} else {
		ok = scanner.NextScan(compare, value, value2, &error);
	}
	const int ms = (int)((time_now_d() - startTime) * 1000.0);

	if (!ok) {
		switch (error) {
		case MemoryScanner::Error::NoModule:
			statusText_ = cw->T("The chosen module isn't loaded. Pick another one in Scan options.");
			break;
		case MemoryScanner::Error::AreaChanged:
			statusText_ = cw->T("The module moved or was unloaded since the search started. Start a new search.");
			break;
		default:
			statusText_ = cw->T("Scan failed, game memory isn't available");
			break;
		}
		statusIsError_ = true;
	} else {
		std::string countStr = StringFromFormat("%llu", (unsigned long long)scanner.ResultCount());
		statusText_ = ApplySafeSubstitutions(cw->T("%1 addresses found (%2 ms)"), countStr, StringFromFormat("%d", ms));
		statusIsError_ = scanner.ResultCount() == 0;
	}
	RecreateViews();
}

void MemoryScannerScreen::update() {
	PopupScreen::update();
	if (!watches_.Empty() && time_now_d() - lastRefresh_ >= REFRESH_INTERVAL) {
		watches_.Refresh();
		lastRefresh_ = time_now_d();
	}
}

bool MemoryScannerScreen::key(const KeyInput &input) {
	if (editor_.fields[0] && editor_.HandleKey(input)) {
		return true;
	}
	return PopupScreen::key(input);
}

void MemoryScannerScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	// Values may have been set or frozen in the edit popup, or pointer chains found.
	RecreateViews();
}

// Shown when a search in one module's globals found nothing.
static void AddModuleOnlyHint(UI::ViewGroup *parent) {
	using namespace UI;
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	TextView *hint = parent->Add(new TextView(cw->T("ModuleOnlyHint", "Only one module's globals were searched. The value may be elsewhere: set Search in to 'User memory except code' in Scan options and start a new search."), FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(4))));
	hint->SetTextColor(0xFF40C0FF);
}

std::string MemoryScannerScreen::AreaText() const {
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	const MemoryScanner &scanner = GetMemoryScanner();
	MemoryScanner::Area area;
	if (scanner.ScanCount() > 0) {
		area = scanner.SearchArea();
	} else {
		// What a new search would cover.
		MemoryScanner::Error error;
		if (!MemoryScanner::GetPSPScanArea(ConfiguredSearchIn(), s_moduleName, &area, &error)) {
			if (error == MemoryScanner::Error::NoModule) {
				return std::string(cw->T("The chosen module isn't loaded. Pick another one in Scan options."));
			}
			return std::string(cw->T("Game memory isn't available"));
		}
	}
	switch (area.searchIn) {
	case MemoryScanner::SearchIn::ModuleGlobals:
		return StringFromFormat("%s %s: 0x%08X - 0x%08X (%u KB)", cw->T_cstr("Globals of"), area.moduleName.c_str(), area.start, area.start + area.size, area.size >> 10);
	case MemoryScanner::SearchIn::UserMemoryNoCode:
		return StringFromFormat("%s: 0x%08X - 0x%08X (%u MB), %s", cw->T_cstr("User memory"), area.start, area.start + area.size, area.size >> 20, cw->T_cstr("code skipped"));
	default:
		return StringFromFormat("%s: 0x%08X - 0x%08X (%u MB)", cw->T_cstr("User memory"), area.start, area.start + area.size, area.size >> 20);
	}
}

// ScanOptionsScreen

ScanOptionsScreen::ScanOptionsScreen() : PopupScreen(T(I18NCat::CWCHEATS, "Scan options"), T(I18NCat::DIALOG, "Back")) {}

void ScanOptionsScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);

	ScrollView *scroll = parent->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f)));
	LinearLayout *items = scroll->Add(new LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	items->Add(new CheckBox(&g_Config.bMemoryScannerPause, cw->T("Pause emulation while the scanner is open")));

	items->Add(new ItemHeader(cw->T("Search in")));
	g_Config.iMemoryScannerSearchIn = (int)ConfiguredSearchIn();
	PopupMultiChoice *searchIn = items->Add(new PopupMultiChoice(&g_Config.iMemoryScannerSearchIn, cw->T("Search in"), g_searchInNames, 0, ARRAY_SIZE(g_searchInNames), I18NCat::CWCHEATS, screenManager()));
	searchIn->OnChoice.Add([this](EventParams &) {
		RecreateViews();
	});

	std::string_view help;
	switch (ConfiguredSearchIn()) {
	case MemoryScanner::SearchIn::UserMemoryNoCode:
		help = cw->T("SearchInNoCodeHelp", "Everywhere a value can be, but not in the game's code, where it almost never is. Values found in allocated memory may move later: use a pointer scan for those.");
		break;
	case MemoryScanner::SearchIn::ModuleGlobals:
		help = cw->T("SearchInModuleHelp", "Only the globals (.data and .bss) of one module, which never move: plain cheats from these keep working. Faster, but many games keep their values elsewhere. Pointer scans only start from this module too.");
		break;
	default:
		help = cw->T("SearchInAllHelp", "Everything, including code and read-only data.");
		break;
	}
	items->Add(new TextView(help, FLAG_WRAP_TEXT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 4))));

	if (ConfiguredSearchIn() == MemoryScanner::SearchIn::ModuleGlobals) {
		// The game's modules, main first.
		std::vector<ModuleRange> modules;
		for (const ModuleRange &m : __KernelModuleRanges()) {
			if (!m.kernel && m.dataSize != 0) {
				modules.push_back(m);
			}
		}
		std::stable_sort(modules.begin(), modules.end(), [](const ModuleRange &a, const ModuleRange &b) { return a.isMain && !b.isMain; });
		std::vector<std::string> names;
		std::vector<std::string> labels;
		int selected = 0;
		for (const ModuleRange &m : modules) {
			if (m.name == s_moduleName) {
				selected = (int)names.size();
			}
			names.push_back(m.name);
			const std::string mainTag = m.isMain ? StringFromFormat(" (%s)", cw->T_cstr("main")) : std::string();
			labels.push_back(StringFromFormat("%s%s  0x%08X - 0x%08X (%u KB)", m.name.c_str(), mainTag.c_str(), m.dataStart, m.dataStart + m.dataSize, m.dataSize >> 10));
		}
		if (names.empty()) {
			items->Add(new TextView(cw->T("No game modules loaded"), FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 4))));
		} else {
			if (!s_moduleName.empty() && std::find(names.begin(), names.end(), s_moduleName) == names.end()) {
				// Not loaded right now. The main module is used until it is.
				s_moduleName.clear();
			}
			const std::string current = labels[selected];
			Choice *module = items->Add(new Choice(cw->T("Module"), current));
			module->OnClick.Add([this, names, labels, selected](EventParams &e) {
				auto cw = GetI18NCategory(I18NCat::CWCHEATS);
				ListPopupScreen *list = new ListPopupScreen(cw->T("Module"), labels, selected, [this, names](int index) {
					if (index >= 0 && index < (int)names.size()) {
						s_moduleName = names[index];
					}
					RecreateViews();
				});
				if (e.v) {
					list->SetPopupOrigin(e.v);
				}
				screenManager()->push(list);
			});
		}
	}

	TextView *note = items->Add(new TextView(cw->T("Search in applies from the next new search."), FLAG_WRAP_TEXT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 4))));
	note->SetTextColor(0xFFC0C0C0);

	// Where each module is, which is what the Search in areas come from.
	items->Add(new ItemHeader(cw->T("Game modules")));
	for (const ModuleRange &m : __KernelModuleRanges()) {
		std::string text = m.name;
		if (m.isMain) {
			text += StringFromFormat(" (%s)", cw->T_cstr("main"));
		} else if (m.kernel) {
			text += StringFromFormat(" (%s)", cw->T_cstr("kernel"));
		}
		auto range = [](u32 start, u32 size) {
			return size != 0 ? StringFromFormat("0x%08X - 0x%08X", start, start + size) : std::string("-");
		};
		text += StringFromFormat("\n%s: %s\n%s: %s\n%s: %s",
			cw->T_cstr("Module"), range(m.blockStart, m.blockSize).c_str(),
			cw->T_cstr("Code"), range(m.codeStart, m.codeSize).c_str(),
			cw->T_cstr("Globals"), range(m.dataStart, m.dataSize).c_str());
		items->Add(new TextView(text, FLAG_WRAP_TEXT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 4))));
	}
}

// WatchList

void WatchList::Clear() {
	rows_.clear();
	flags_.clear();
}

static const char *TypeName(ScanValueType type) {
	return g_typeNames[(int)type];
}

std::string WatchList::RowText(const PointerChain &where, ScanValueType type, Style style) {
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	std::string text;
	u32 addr = 0;
	const bool resolved = where.Resolve(&addr);
	if (where.IsPointer()) {
		text = where.ToString() + "  ->  ";
		if (!resolved) {
			text += cw->T("invalid");
			return text;
		}
	}
	u32 bits = 0;
	if (!resolved || !MemoryScanner::ReadPSP(type, addr, &bits)) {
		return text + StringFromFormat("0x%08X  ?", addr);
	}
	text += StringFromFormat("0x%08X  =  %s", addr, MemoryScanner::FormatValue(type, bits).c_str());

	switch (style) {
	case Style::Frozen:
		text += StringFromFormat("   [%s]", cw->T_cstr(TypeName(type)));
		break;
	case Style::SearchResult:
	{
		const u32 prev = GetMemoryScanner().PreviousValue(addr);
		if (prev != bits) {
			text += StringFromFormat("   (%s %s)", cw->T_cstr("was"), MemoryScanner::FormatValue(type, prev).c_str());
		}
		break;
	}
	case Style::Pointer:
		break;
	}
	return text;
}

void WatchList::AddRow(UI::ViewGroup *parent, Screen *owner, const PointerChain &where, ScanValueType type, Style style) {
	using namespace UI;
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);

	LinearLayout *row = parent->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	row->SetSpacing(4.0f);

	flags_.push_back(GetMemoryScanner().IsFrozen(where, type));
	bool *flag = &flags_.back();
	CheckBox *freeze = row->Add(new CheckBox(flag, cw->T("Freeze"), "", new LinearLayoutParams(150, WRAP_CONTENT, 0.0f)));
	freeze->OnClick.Add([owner, where, type, flag](EventParams &) {
		if (!*flag) {
			GetMemoryScanner().SetFrozen(where, type, 0, false);
		} else {
			// Hold the value it has right now.
			u32 addr = 0;
			u32 bits = 0;
			if (where.Resolve(&addr) && MemoryScanner::ReadPSP(type, addr, &bits)) {
				GetMemoryScanner().SetFrozen(where, type, bits, true);
			} else {
				*flag = false;
			}
		}
		owner->RecreateViews();
	});

	Choice *c = row->Add(new Choice(RowText(where, type, style), new LinearLayoutParams(1.0f)));
	c->OnClick.Add([owner, where, type](EventParams &e) {
		MemoryScannerEditScreen *edit = new MemoryScannerEditScreen(where, type);
		if (e.v) {
			edit->SetPopupOrigin(e.v);
		}
		owner->screenManager()->push(edit);
	});
	rows_.push_back(Row{ where, type, style, c });
}

void WatchList::Refresh() {
	for (Row &row : rows_) {
		row.view->SetText(RowText(row.where, row.type, row.style));
	}
}

// MemoryScannerEditScreen

static std::string DefaultCheatName(const PointerChain &where) {
	if (where.IsPointer()) {
		return StringFromFormat("Scanner pointer 0x%07X", where.base - PSP_GetUserMemoryBase());
	}
	return StringFromFormat("Scanner 0x%07X", where.base - PSP_GetUserMemoryBase());
}

MemoryScannerEditScreen::MemoryScannerEditScreen(const PointerChain &where, ScanValueType type)
	: PopupScreen(T(I18NCat::CWCHEATS, "Edit value"), T(I18NCat::DIALOG, "Back")), where_(where), type_(type) {
	frozen_ = GetMemoryScanner().IsFrozen(where_, type_);
	u32 addr = 0;
	u32 bits = 0;
	if (Resolve(&addr) && MemoryScanner::ReadPSP(type_, addr, &bits)) {
		value_ = MemoryScanner::FormatValue(type_, bits);
		// Drop the "(signed)" part, it isn't a valid value to type.
		size_t space = value_.find(' ');
		if (space != std::string::npos) {
			value_.resize(space);
		}
	}
	name_ = DefaultCheatName(where_);
}

bool MemoryScannerEditScreen::WantsTextInput() const {
	return !System_GetPropertyBool(SYSPROP_KEYBOARD_IS_SOFT);
}

void MemoryScannerEditScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);

	ScrollView *scroll = parent->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f)));
	LinearLayout *items = scroll->Add(new LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	if (where_.IsPointer()) {
		items->Add(new InfoItem(cw->T("Pointer"), where_.ToString()));
	} else {
		items->Add(new InfoItem(cw->T("Address"), StringFromFormat("0x%08X (+0x%07X)", where_.base, where_.base - PSP_GetUserMemoryBase())));
	}
	currentText_ = items->Add(new TextView("", FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 4))));

	editor_.fields[0] = &value_;
	editor_.fieldLabels[0] = "New value";
	editor_.CreateViews(items, screenManager(), GetRequesterToken(), false);

	items->Add(new Choice(cw->T("Set"), ImageID("I_CHECKMARK")))->OnClick.Add([this](EventParams &) {
		OnSet();
	});
	items->Add(new CheckBox(&frozen_, cw->T("Freeze"), cw->T("Keep writing this value while the game runs")))->OnClick.Add([this](EventParams &) {
		if (!frozen_) {
			GetMemoryScanner().SetFrozen(where_, type_, 0, false);
			UpdateTexts();
			return;
		}
		// Hold the typed value if it's valid, otherwise what's in memory now.
		u32 bits = 0;
		u32 addr = 0;
		if (!GetValueToUse(&bits) && !(Resolve(&addr) && MemoryScanner::ReadPSP(type_, addr, &bits))) {
			frozen_ = false;
			return;
		}
		GetMemoryScanner().SetFrozen(where_, type_, bits, true);
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

	if (!where_.IsPointer()) {
		items->Add(new ItemHeader(cw->T("Pointers")));
		const PointerScanner &pointers = GetPointerScanner();
		const bool havePointers = pointers.HasResults() && pointers.GameID() == CurrentGameID();
		std::string_view label = havePointers ? cw->T("Pointer scan (filter with this address)") : cw->T("Find pointers to this address");
		Choice *pointerScan = items->Add(new Choice(label, ImageID("I_SEARCH")));
		pointerScan->OnClick.Add([this](EventParams &e) {
			PointerScanScreen *screen = new PointerScanScreen(where_.base, type_);
			if (e.v) {
				screen->SetPopupOrigin(e.v);
			}
			screenManager()->push(screen);
		});
		TextView *hint = items->Add(new TextView(cw->T("PointerScanHint", "For values that move when the level changes or the game restarts: a pointer cheat finds them again."), FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 4))));
		hint->SetSmall(true);
	}

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
	if (currentText_) {
		u32 addr = 0;
		u32 bits = 0;
		if (!Resolve(&addr)) {
			currentText_->SetText(cw->T("The pointer is invalid right now"));
		} else if (!MemoryScanner::ReadPSP(type_, addr, &bits)) {
			currentText_->SetText(cw->T("Current value: ?"));
		} else if (where_.IsPointer()) {
			currentText_->SetText(StringFromFormat("%s 0x%08X, %s: %s  (%s)", cw->T_cstr("Points to"), addr, cw->T_cstr("Current value"), MemoryScanner::FormatValue(type_, bits).c_str(), MemoryScanner::FormatHex(type_, bits).c_str()));
		} else {
			currentText_->SetText(StringFromFormat("%s: %s  (%s)", cw->T_cstr("Current value"), MemoryScanner::FormatValue(type_, bits).c_str(), MemoryScanner::FormatHex(type_, bits).c_str()));
		}
	}
	if (cheatText_) {
		u32 newBits = 0;
		if (GetValueToUse(&newBits)) {
			std::string code;
			for (const std::string &line : MemoryScanner::MakeCwCheatLines(type_, where_, newBits)) {
				if (!code.empty()) {
					code += "\n";
				}
				code += line;
			}
			cheatText_->SetText(code);
		} else {
			cheatText_->SetText(cw->T("Invalid value for this value type"));
		}
	}
	lastRefresh_ = time_now_d();
}

void MemoryScannerEditScreen::OnSet() {
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	u32 bits = 0;
	u32 addr = 0;
	if (!GetValueToUse(&bits)) {
		status_->SetText(cw->T("Invalid value for this value type"));
		status_->SetTextColor(0xFF4040FF);
	} else if (!Resolve(&addr) || !MemoryScanner::WritePSP(type_, addr, bits)) {
		status_->SetText(cw->T("The pointer is invalid right now"));
		status_->SetTextColor(0xFF4040FF);
	} else {
		// A frozen value is held at the new value from now on.
		GetMemoryScanner().UpdateFrozenValue(where_, type_, bits);
		status_->SetText(cw->T("Value written to game memory"));
		status_->SetTextColor(0xFF40FF40);
	}
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
		name = DefaultCheatName(where_);
	}
	std::string error;
	if (AppendCheatToGameIni(name, MemoryScanner::MakeCwCheatLines(type_, where_, bits), &error)) {
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

void MemoryScannerEditScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	// The pointer scan may have frozen this value.
	frozen_ = GetMemoryScanner().IsFrozen(where_, type_);
	UpdateTexts();
}

// PointerScanScreen

static int s_pointerDepth = 2;
static int s_pointerOffsetIndex = 2;
static const u32 g_pointerOffsets[] = { 0x100, 0x400, 0x1000, 0x4000, 0x10000 };
static const char *g_pointerOffsetNames[] = { "0x100", "0x400", "0x1000", "0x4000", "0x10000" };
static_assert(ARRAY_SIZE(g_pointerOffsets) == ARRAY_SIZE(g_pointerOffsetNames), "pointer offsets");

PointerScanScreen::PointerScanScreen(u32 target, ScanValueType type)
	: PopupScreen(T(I18NCat::CWCHEATS, "Pointer scan"), T(I18NCat::DIALOG, "Back")), target_(target), type_(type) {
	PointerScanner &pointers = GetPointerScanner();
	if (pointers.HasResults() && pointers.GameID() != CurrentGameID()) {
		pointers.Reset();
	}
}

void PointerScanScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	watches_.Clear();

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
		ScrollView *scroll = parent->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f)));
		LinearLayout *column = scroll->Add(new LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		CreateResults(column);
		CreateControls(column);
	}

	lastRefresh_ = time_now_d();
}

void PointerScanScreen::CreateControls(UI::ViewGroup *parent) {
	using namespace UI;
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);

	const PointerScanner &pointers = GetPointerScanner();

	parent->Add(new InfoItem(cw->T("Address"), StringFromFormat("0x%08X", target_)));
	parent->Add(new InfoItem(cw->T("Value type"), cw->T(TypeName(pointers.HasResults() ? pointers.Type() : type_))));

	parent->Add(new ItemHeader(cw->T("New pointer scan")));
	parent->Add(new PopupSliderChoice(&s_pointerDepth, 1, MAX_POINTER_DEPTH, 2, cw->T("Max pointer depth"), screenManager()));
	parent->Add(new PopupMultiChoice(&s_pointerOffsetIndex, cw->T("Max offset"), g_pointerOffsetNames, 0, ARRAY_SIZE(g_pointerOffsetNames), I18NCat::NONE, screenManager()));
	parent->Add(new Choice(cw->T("Scan for pointers to this address"), ImageID("I_SEARCH")))->OnClick.Add([this](EventParams &) {
		DoScan();
	});

	if (pointers.HasResults()) {
		parent->Add(new ItemHeader(cw->T("Narrow down")));
		parent->Add(new InfoItem(cw->T("Scans"), pointers.Scans()));
		parent->Add(new Choice(cw->T("Filter with this address")))->OnClick.Add([this](EventParams &) {
			DoFilter();
		});
		parent->Add(new Choice(cw->T("Clear pointer results")))->OnClick.Add([this](EventParams &) {
			GetPointerScanner().Reset();
			statusText_.clear();
			RecreateViews();
		});
	}

	TextView *help = parent->Add(new TextView(cw->T("PointerScanHelp", "After a scan, change level or restart the game, find the value again with a normal search, tap its address and use Pointer scan > Filter with this address. Repeat until only a few chains are left, then add one as a cheat."), FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(4))));
	help->SetSmall(true);

	if (!statusText_.empty()) {
		TextView *status = parent->Add(new TextView(statusText_, FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(4))));
		if (statusIsError_) {
			status->SetTextColor(0xFF4040FF);
		}
	}
}

void PointerScanScreen::CreateResults(UI::ViewGroup *parent) {
	using namespace UI;
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);

	const PointerScanner &pointers = GetPointerScanner();
	parent->Add(new ItemHeader(cw->T("Pointer chains")));

	if (!pointers.HasResults()) {
		parent->Add(new TextView(cw->T("PointerScanIntro", "Finds pointers in the game's static data that lead to this address. The game finds moving values the same way, so a cheat using such a pointer keeps working when the value moves."), FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(4))));
		return;
	}

	const std::vector<PointerChain> &chains = pointers.Results();
	if (chains.empty()) {
		parent->Add(new TextView(cw->T("No pointer chains found. Try a larger depth or offset."), FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(4))));
		if (!pointers.LastSettings().moduleName.empty()) {
			AddModuleOnlyHint(parent);
		}
		return;
	}

	if (pointers.Truncated()) {
		TextView *warn = parent->Add(new TextView(cw->T("Too many chains, some were left out. Use a smaller depth or offset."), FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(4))));
		warn->SetTextColor(0xFF40C0FF);
	}

	if (chains.size() > MAX_LISTED_RESULTS) {
		std::string countStr = StringFromFormat("%llu", (unsigned long long)chains.size());
		parent->Add(new TextView(ApplySafeSubstitutions(cw->T("%1 pointer chains found. Change level or restart the game, then filter with the value's new address to keep the ones that still work. The shortest are listed below."), countStr), FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(4))));
	}

	const ScanValueType type = pointers.Type();
	for (size_t i = 0; i < chains.size() && i < MAX_LISTED_RESULTS; i++) {
		watches_.AddRow(parent, this, chains[i], type, WatchList::Style::Pointer);
	}
}

void PointerScanScreen::DoScan() {
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	PointerScanner &pointers = GetPointerScanner();

	// With Search in set to one module, chains only start from its globals.
	std::string moduleName;
	if (ConfiguredSearchIn() == MemoryScanner::SearchIn::ModuleGlobals) {
		MemoryScanner::Area area;
		MemoryScanner::Error error;
		if (!MemoryScanner::GetPSPScanArea(MemoryScanner::SearchIn::ModuleGlobals, s_moduleName, &area, &error)) {
			statusText_ = cw->T("The chosen module isn't loaded. Pick another one in Scan options.");
			statusIsError_ = true;
			RecreateViews();
			return;
		}
		moduleName = area.moduleName;
	}
	if (PointerScanner::GetPSPStaticRanges(moduleName).empty()) {
		statusText_ = cw->T("Couldn't find the game's static data");
		statusIsError_ = true;
		RecreateViews();
		return;
	}

	PointerScanner::Settings settings;
	settings.maxDepth = s_pointerDepth;
	settings.maxOffset = g_pointerOffsets[std::max(0, std::min(s_pointerOffsetIndex, (int)ARRAY_SIZE(g_pointerOffsets) - 1))];
	settings.moduleName = moduleName;

	// Like value scans, this runs on the emulation thread, so the game is stopped meanwhile.
	const double startTime = time_now_d();
	pointers.SetGameID(CurrentGameID());
	const bool ok = pointers.Scan(target_, type_, settings);
	const int ms = (int)((time_now_d() - startTime) * 1000.0);

	if (!ok) {
		statusText_ = cw->T("Scan failed, game memory isn't available");
		statusIsError_ = true;
	} else {
		std::string countStr = StringFromFormat("%llu", (unsigned long long)pointers.Results().size());
		statusText_ = ApplySafeSubstitutions(cw->T("%1 pointer chains found (%2 ms)"), countStr, StringFromFormat("%d", ms));
		statusIsError_ = pointers.Results().empty();
	}
	RecreateViews();
}

void PointerScanScreen::DoFilter() {
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	const size_t left = GetPointerScanner().Filter(target_);
	std::string countStr = StringFromFormat("%llu", (unsigned long long)left);
	statusText_ = ApplySafeSubstitutions(cw->T("%1 pointer chains still lead to this address"), countStr);
	statusIsError_ = false;
	RecreateViews();
}

void PointerScanScreen::update() {
	PopupScreen::update();
	if (!watches_.Empty() && time_now_d() - lastRefresh_ >= REFRESH_INTERVAL) {
		watches_.Refresh();
		lastRefresh_ = time_now_d();
	}
}

void PointerScanScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	// A chain may have been frozen or edited.
	RecreateViews();
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
			if (line.size() >= 3 && (u8)line[0] == 0xEF && (u8)line[1] == 0xBB && (u8)line[2] == 0xBF) {
				line = std::string(StripSpaces(line.substr(3)));
			}
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
