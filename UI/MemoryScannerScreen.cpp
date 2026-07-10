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
#include <utility>
#include "UI/MemoryScannerScreen.h"
#include "Common/UI/Context.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/ScrollView.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/TabHolder.h"
#include "Common/Data/Text/I18n.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Common/StringUtils.h"

MemoryScannerScreen::MemoryScannerScreen(Path gamePath) : UIDialogScreen(), gamePath_(std::move(gamePath)), resultsList_(nullptr) {
}

MemoryScannerScreen::~MemoryScannerScreen() = default;

void MemoryScannerScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto se = GetI18NCategory(I18NCat::SEARCH);

	root_ = new LinearLayout(ORIENT_VERTICAL);

	root_->Add(new ItemHeader(se->T("Memory Scanner")));

	LinearLayout *tabRow = root_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	ChoiceStrip *tabs = tabRow->Add(new ChoiceStrip(ORIENT_HORIZONTAL, new LinearLayoutParams(1.0f)));
	for (int i = 0; i < g_MemoryScanner.GetCount(); ++i) {
		tabs->AddChoice(StringFromFormat("%d", i + 1));
	}
	// Make all choices in the ChoiceStrip expand equally.
	for (int i = 0; i < tabs->GetNumSubviews(); ++i) {
		View *v = tabs->GetViewByIndex(i);
		if (v) {
			v->ReplaceLayoutParams(new LinearLayoutParams(1.0f));
		}
	}
	tabs->SetSelection(g_MemoryScanner.GetActiveIndex(), false);
	tabs->OnChoice.Add([this](EventParams &e) {
		g_MemoryScanner.SetActiveIndex((int)e.a);
		RecreateViews();
	});

	tabRow->Add(new Button("+", new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)))->OnClick.Add([this](EventParams &e) {
		g_MemoryScanner.AddScanner();
		RecreateViews();
	});
	if (g_MemoryScanner.GetCount() > 1) {
		tabRow->Add(new Button("-", new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)))->OnClick.Add([this](EventParams &e) {
			g_MemoryScanner.RemoveScanner(g_MemoryScanner.GetActiveIndex());
			RecreateViews();
		});
	}

	MemoryScanner *scanner = g_MemoryScanner.GetActive();

	searchValueEdit_ = root_->Add(new TextEdit(scanner->searchValue, se->T("Value to search"), se->T("Enter value"), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	searchValueEdit_->OnTextChange.Add([scanner, this](EventParams &e) {
		scanner->searchValue = searchValueEdit_->GetText();
	});

	LinearLayout *typeRow = root_->Add(new LinearLayout(ORIENT_HORIZONTAL));
	typeRow->Add(new TextView(se->T("Value Type"), new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));

	ChoiceStrip *typeStrip = typeRow->Add(new ChoiceStrip(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
	typeStrip->AddChoice(se->T("8-bit"));
	typeStrip->AddChoice(se->T("16-bit"));
	typeStrip->AddChoice(se->T("32-bit"));
	typeStrip->AddChoice(se->T("Float"));
	typeStrip->SetSelection((int)scanner->valueType, false);
	typeStrip->OnChoice.Add([scanner](EventParams &e) {
		scanner->valueType = (ScanValueType)e.a;
	});

	LinearLayout *buttonRow = root_->Add(new LinearLayout(ORIENT_HORIZONTAL));
	buttonRow->Add(new Button(se->T("First Scan")))->OnClick.Handle(this, &MemoryScannerScreen::OnFirstScan);
	buttonRow->Add(new Button(se->T("Next Scan")))->OnClick.Handle(this, &MemoryScannerScreen::OnNextScan);
	buttonRow->Add(new Button(di->T("Clear")))->OnClick.Handle(this, &MemoryScannerScreen::OnClear);

	countText_ = root_->Add(new TextView(se->T("Results: 0"), new LinearLayoutParams(Margins(10, 10))));
	countText_->SetShadow(true);
	countText_->SetBig(true);

	root_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, 0.0f, 1.0f)))
		->Add(resultsList_ = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	root_->Add(new Button(di->T("Back")))->OnClick.Add([this](EventParams &) { TriggerFinish(DR_BACK); });

	if (scanner->HasScanDone()) {
		RebuildResultsList();
	}
}

void MemoryScannerScreen::update() {
	UIScreen::update();
	MemoryScanner *scanner = g_MemoryScanner.GetActive();
	if (countText_) {
		const auto &results = scanner->GetResults();
		auto se = GetI18NCategory(I18NCat::SEARCH);
		countText_->SetText(StringFromFormat(se->T_cstr("Results: %d"), (int)results.size()));
	}
	if (resultsList_) {
		resultsList_->SetVisibility(scanner->HasScanDone() ? UI::V_VISIBLE : UI::V_GONE);
	}

	// Update live values in the list.
	// Only update if the UI is in sync with the current scanner results.
	const auto &results = scanner->GetResults();
	if (valueChoices_.size() == std::min((size_t)100, results.size())) {
		for (size_t i = 0; i < valueChoices_.size(); ++i) {
			uint32_t addr = resultAddresses_[i];
			ScanValueType type = resultTypes_[i];
			std::string valStr;
			switch (type) {
			case ScanValueType::U8: valStr = StringFromFormat("%d", (int)Memory::Read_U8(addr)); break;
			case ScanValueType::U16: valStr = StringFromFormat("%d", (int)Memory::Read_U16(addr)); break;
			case ScanValueType::U32: valStr = StringFromFormat("%u", Memory::Read_U32(addr)); break;
			case ScanValueType::FLOAT: valStr = StringFromFormat("%f", Memory::Read_Float(addr)); break;
			}
			valueChoices_[i]->SetText(valStr);
		}
	} else if (scanner->HasScanDone()) {
		// If out of sync, trigger a rebuild.
		RebuildResultsList();
	}
}

void MemoryScannerScreen::RebuildResultsList() {
	if (!resultsList_)
		return;
	using namespace UI;
	auto se = GetI18NCategory(I18NCat::SEARCH);
	resultsList_->Clear();
	valueChoices_.clear();
	resultAddresses_.clear();
	resultTypes_.clear();

	MemoryScanner *scanner = g_MemoryScanner.GetActive();
	auto &results = scanner->GetResults();

	if (results.empty())
		return;

	LinearLayout *header = resultsList_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	auto *addrHeader = header->Add(new TextView(se->T("Address"), new LinearLayoutParams(200.0f, WRAP_CONTENT)));
	addrHeader->SetShadow(true);
	addrHeader->SetPadding(Margins(16, 0));
	auto *valHeader = header->Add(new TextView(se->T("Value"), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f)));
	valHeader->SetShadow(true);
	valHeader->SetPadding(Margins(16, 0));
	auto *lockHeader = header->Add(new TextView(se->T("Lock"), new LinearLayoutParams(100.0f, WRAP_CONTENT)));
	lockHeader->SetShadow(true);
	lockHeader->SetPadding(Margins(16, 0));

	int count = 0;
	for (size_t i = 0; i < results.size(); ++i) {
		LinearLayout *row = resultsList_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		uint32_t addr = results[i].address;
		ScanValueType type = results[i].type;

		row->Add(new Choice(StringFromFormat("0x%08X", addr), new LinearLayoutParams(200.0f, WRAP_CONTENT)));

		std::string valStr;
		switch (type) {
		case ScanValueType::U8: valStr = StringFromFormat("%d", (int)Memory::Read_U8(addr)); break;
		case ScanValueType::U16: valStr = StringFromFormat("%d", (int)Memory::Read_U16(addr)); break;
		case ScanValueType::U32: valStr = StringFromFormat("%u", Memory::Read_U32(addr)); break;
		case ScanValueType::FLOAT: valStr = StringFromFormat("%f", Memory::Read_Float(addr)); break;
		}

		Choice *valChoice = row->Add(new Choice(valStr, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f)));
		valChoice->OnClick.Add([this, addr, type](EventParams &e) {
			OnValueClick(addr, type);
		});
		valueChoices_.push_back(valChoice);
		resultAddresses_.push_back(addr);
		resultTypes_.push_back(type);

		auto *lockCheck = row->Add(new CheckBox(&results[i].locked, "", "", new LinearLayoutParams(100.0f, WRAP_CONTENT)));
		lockCheck->OnClick.Add([scanner, i](EventParams &e) {
			if (!scanner->SetLocked(i, scanner->GetResults()[i].locked)) {
				auto se = GetI18NCategory(I18NCat::SEARCH);
				g_OSD.Show(OSDType::MESSAGE_ERROR, se->T("Address already locked"), 3.0f);
			}
		});

		if (++count > 100) break; // Limit UI items
	}
}

void MemoryScannerScreen::OnFirstScan(UI::EventParams &e) {
	MemoryScanner *scanner = g_MemoryScanner.GetActive();
	scanner->FirstScan(scanner->valueType, scanner->searchValue);
	RebuildResultsList();
}

void MemoryScannerScreen::OnNextScan(UI::EventParams &e) {
	MemoryScanner *scanner = g_MemoryScanner.GetActive();
	if (scanner->HasScanDone()) {
		scanner->NextScan(scanner->valueType, scanner->searchValue);
		RebuildResultsList();
	}
}

void MemoryScannerScreen::OnClear(UI::EventParams &e) {
	g_MemoryScanner.GetActive()->Clear();
	RebuildResultsList();
}

void MemoryScannerScreen::OnValueClick(uint32_t addr, ScanValueType type) {
	auto se = GetI18NCategory(I18NCat::SEARCH);

	// Global Respect: If the address is locked in ANY tab, we should be careful.
	// If it's locked in the CURRENT tab, we allow editing but must update the lock value.
	// If it's locked in ANOTHER tab, we should block it to respect that lock.
	MemoryScanner *scanner = g_MemoryScanner.GetActive();
	bool lockedHere = false;
	for (const auto &res : scanner->GetResults()) {
		if (res.address == addr && res.locked) {
			lockedHere = true;
			break;
		}
	}

	if (!lockedHere && g_MemoryScanner.IsAddressLocked(addr)) {
		g_OSD.Show(OSDType::MESSAGE_ERROR, se->T("Address locked in another tab"), 3.0f);
		return;
	}

	std::string currentVal;
	switch (type) {
	case ScanValueType::U8: currentVal = StringFromFormat("%d", (int)Memory::Read_U8(addr)); break;
	case ScanValueType::U16: currentVal = StringFromFormat("%d", (int)Memory::Read_U16(addr)); break;
	case ScanValueType::U32: currentVal = StringFromFormat("%u", Memory::Read_U32(addr)); break;
	case ScanValueType::FLOAT: currentVal = StringFromFormat("%f", Memory::Read_Float(addr)); break;
	}

	UI::AskForInput(screenManager(), NON_EPHEMERAL_TOKEN, nullptr, se->T("Edit Value"), [addr, type](const std::string &value, bool ok) {
		if (ok) {
			try {
				if (type == ScanValueType::FLOAT) {
					float fval = std::stof(value);
					Memory::Write_Float(fval, addr);

					// Also update lock value if locked in the active scanner.
					MemoryScanner *scanner = g_MemoryScanner.GetActive();
					for (auto &res : scanner->GetResults()) {
						if (res.address == addr && res.locked) {
							memcpy(&res.lockValue, &fval, 4);
						}
					}
				} else {
					auto val = (uint32_t)std::stoul(value, nullptr, 0);
					switch (type) {
					case ScanValueType::U8: Memory::Write_U8((uint8_t)val, addr); break;
					case ScanValueType::U16: Memory::Write_U16((uint16_t)val, addr); break;
					case ScanValueType::U32: Memory::Write_U32(val, addr); break;
					}

					// Also update lock value if locked in the active scanner.
					MemoryScanner *scanner = g_MemoryScanner.GetActive();
					for (auto &res : scanner->GetResults()) {
						if (res.address == addr && res.locked) {
							res.lockValue = val;
						}
					}
				}
			} catch (...) {
				// Ignore invalid input.
			}
		}
	});
}
