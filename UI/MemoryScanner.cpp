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
#include "UI/MemoryScanner.h"
#include "Common/UI/Context.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/ScrollView.h"
#include "Common/UI/PopupScreens.h"
#include "Common/Data/Text/I18n.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Common/StringUtils.h"

MemoryScanner g_MemoryScanner;

void MemoryScanner::FirstScan(ScanValueType type, const std::string &filter) {
	results_.clear();
	uint32_t val = 0;
	try {
		val = (uint32_t)std::stoul(filter, nullptr, 0);
	} catch (...) {
		return;
	}

	uint32_t startAddr = 0x08000000;
	uint32_t endAddr = startAddr + Memory::g_MemorySize;

	for (uint32_t addr = startAddr; addr < endAddr; ) {
		bool match = false;
		switch (type) {
		case ScanValueType::U8:
			if (Memory::Read_U8(addr) == (uint8_t)val) match = true;
			addr += 1;
			break;
		case ScanValueType::U16:
			if (addr + 2 <= endAddr && Memory::Read_U16(addr) == (uint16_t)val) match = true;
			addr += 1;
			break;
		case ScanValueType::U32:
			if (addr + 4 <= endAddr && Memory::Read_U32(addr) == (uint32_t)val) match = true;
			addr += 1;
			break;
		}

		if (match) {
			results_.emplace_back(addr - 1, type);
			if (results_.size() > 1000) break;
		}
	}
	firstScanDone_ = true;
}

void MemoryScanner::NextScan(ScanValueType type, const std::string &filter) {
	uint32_t val = 0;
	try {
		val = (uint32_t)std::stoul(filter, nullptr, 0);
	} catch (...) {
		return;
	}

	std::vector<ScanResult> nextResults;
	for (const auto &res : results_) {
		bool match = false;
		switch (type) {
		case ScanValueType::U8:
			if (Memory::Read_U8(res.address) == (uint8_t)val) match = true;
			break;
		case ScanValueType::U16:
			if (Memory::Read_U16(res.address) == (uint16_t)val) match = true;
			break;
		case ScanValueType::U32:
			if (Memory::Read_U32(res.address) == (uint32_t)val) match = true;
			break;
		}

		if (match) {
			nextResults.push_back(res);
		}
	}
	results_ = std::move(nextResults);
}

void MemoryScanner::Clear() {
	results_.clear();
	firstScanDone_ = false;
}

void MemoryScanner::Update() {
	for (auto &res : results_) {
		if (res.locked) {
			switch (res.type) {
			case ScanValueType::U8:
				Memory::Write_U8((uint8_t)res.lockValue, res.address);
				break;
			case ScanValueType::U16:
				Memory::Write_U16((uint16_t)res.lockValue, res.address);
				break;
			case ScanValueType::U32:
				Memory::Write_U32(res.lockValue, res.address);
				break;
			}
		}
	}
}

void MemoryScanner::SetLocked(size_t index, bool locked) {
	if (index < results_.size()) {
		results_[index].locked = locked;
		if (results_[index].locked) {
			// Capture current value to lock it to.
			switch (results_[index].type) {
			case ScanValueType::U8:
				results_[index].lockValue = Memory::Read_U8(results_[index].address);
				break;
			case ScanValueType::U16:
				results_[index].lockValue = Memory::Read_U16(results_[index].address);
				break;
			case ScanValueType::U32:
				results_[index].lockValue = Memory::Read_U32(results_[index].address);
				break;
			}
		}
	}
}

MemoryScannerScreen::MemoryScannerScreen(Path gamePath) : UIDialogScreen(), gamePath_(std::move(gamePath)), resultsList_(nullptr) {
}

MemoryScannerScreen::~MemoryScannerScreen() = default;

void MemoryScannerScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);

	root_ = new LinearLayout(ORIENT_VERTICAL);

	root_->Add(new ItemHeader("Memory Scanner"));

	searchValueEdit_ = root_->Add(new TextEdit(g_MemoryScanner.searchValue, "Value to search", "Enter value", new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	searchValueEdit_->OnTextChange.Add([this](EventParams &e) {
		g_MemoryScanner.searchValue = searchValueEdit_->GetText();
	});

	LinearLayout *typeRow = root_->Add(new LinearLayout(ORIENT_HORIZONTAL));
	typeRow->Add(new TextView("Value Type: ", new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));

	typeRow->Add(new Choice("8-bit"))->OnClick.Add([](EventParams &) { g_MemoryScanner.valueType = ScanValueType::U8; });
	typeRow->Add(new Choice("16-bit"))->OnClick.Add([](EventParams &) { g_MemoryScanner.valueType = ScanValueType::U16; });
	typeRow->Add(new Choice("32-bit"))->OnClick.Add([](EventParams &) { g_MemoryScanner.valueType = ScanValueType::U32; });

	LinearLayout *buttonRow = root_->Add(new LinearLayout(ORIENT_HORIZONTAL));
	buttonRow->Add(new Button("First Scan"))->OnClick.Handle(this, &MemoryScannerScreen::OnFirstScan);
	buttonRow->Add(new Button("Next Scan"))->OnClick.Handle(this, &MemoryScannerScreen::OnNextScan);
	buttonRow->Add(new Button("Clear"))->OnClick.Handle(this, &MemoryScannerScreen::OnClear);

	countText_ = root_->Add(new TextView("Results: 0", new LinearLayoutParams(Margins(10, 10))));
	countText_->SetShadow(true);
	countText_->SetBig(true);

	root_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, 0.0f, 1.0f)))
		->Add(resultsList_ = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	root_->Add(new Button(di->T("Back")))->OnClick.Add([this](EventParams &) { TriggerFinish(DR_BACK); });

	if (g_MemoryScanner.HasScanDone()) {
		RebuildResultsList();
	}
}

void MemoryScannerScreen::update() {
	UIScreen::update();
	if (countText_) {
		const auto &results = g_MemoryScanner.GetResults();
		countText_->SetText(StringFromFormat("Results: %d", (int)results.size()));
	}
	if (resultsList_) {
		resultsList_->SetVisibility(g_MemoryScanner.HasScanDone() ? UI::V_VISIBLE : UI::V_GONE);
	}

	// Update live values in the list.
	for (size_t i = 0; i < valueChoices_.size(); ++i) {
		uint32_t addr = resultAddresses_[i];
		ScanValueType type = resultTypes_[i];
		std::string valStr;
		switch (type) {
		case ScanValueType::U8: valStr = StringFromFormat("%d", (int)Memory::Read_U8(addr)); break;
		case ScanValueType::U16: valStr = StringFromFormat("%d", (int)Memory::Read_U16(addr)); break;
		case ScanValueType::U32: valStr = StringFromFormat("%u", Memory::Read_U32(addr)); break;
		}
		valueChoices_[i]->SetText(valStr);
	}
}

void MemoryScannerScreen::RebuildResultsList() {
	if (!resultsList_)
		return;
	using namespace UI;
	resultsList_->Clear();
	valueChoices_.clear();
	resultAddresses_.clear();
	resultTypes_.clear();

	auto &results = g_MemoryScanner.GetResults();

	if (results.empty())
		return;

	LinearLayout *header = resultsList_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	auto *addrHeader = header->Add(new TextView("Address", new LinearLayoutParams(200.0f, WRAP_CONTENT)));
	addrHeader->SetShadow(true);
	addrHeader->SetPadding(Margins(16, 0));
	auto *valHeader = header->Add(new TextView("Value", new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f)));
	valHeader->SetShadow(true);
	valHeader->SetPadding(Margins(16, 0));
	auto *lockHeader = header->Add(new TextView("Lock", new LinearLayoutParams(100.0f, WRAP_CONTENT)));
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
		}

		Choice *valChoice = row->Add(new Choice(valStr, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f)));
		valChoice->OnClick.Add([this, addr, type](EventParams &e) {
			OnValueClick(addr, type);
		});
		valueChoices_.push_back(valChoice);
		resultAddresses_.push_back(addr);
		resultTypes_.push_back(type);

		auto *lockCheck = row->Add(new CheckBox(&results[i].locked, "", "", new LinearLayoutParams(100.0f, WRAP_CONTENT)));
		lockCheck->OnClick.Add([i](EventParams &e) {
			g_MemoryScanner.SetLocked(i, g_MemoryScanner.GetResults()[i].locked);
		});

		if (++count > 100) break; // Limit UI items
	}
}

void MemoryScannerScreen::OnFirstScan(UI::EventParams &e) {
	g_MemoryScanner.FirstScan(g_MemoryScanner.valueType, g_MemoryScanner.searchValue);
	RebuildResultsList();
}

void MemoryScannerScreen::OnNextScan(UI::EventParams &e) {
	if (g_MemoryScanner.HasScanDone()) {
		g_MemoryScanner.NextScan(g_MemoryScanner.valueType, g_MemoryScanner.searchValue);
		RebuildResultsList();
	}
}

void MemoryScannerScreen::OnClear(UI::EventParams &e) {
	g_MemoryScanner.Clear();
	RebuildResultsList();
}

void MemoryScannerScreen::OnValueClick(uint32_t addr, ScanValueType type) {
	std::string currentVal;
	switch (type) {
	case ScanValueType::U8: currentVal = StringFromFormat("%d", (int)Memory::Read_U8(addr)); break;
	case ScanValueType::U16: currentVal = StringFromFormat("%d", (int)Memory::Read_U16(addr)); break;
	case ScanValueType::U32: currentVal = StringFromFormat("%u", Memory::Read_U32(addr)); break;
	}

	UI::AskForInput(screenManager(), NON_EPHEMERAL_TOKEN, nullptr, "Edit Value", [addr, type](const std::string &value, bool ok) {
		if (ok) {
			try {
				auto val = (uint32_t)std::stoul(value, nullptr, 0);
				switch (type) {
				case ScanValueType::U8: Memory::Write_U8((uint8_t)val, addr); break;
				case ScanValueType::U16: Memory::Write_U16((uint16_t)val, addr); break;
				case ScanValueType::U32: Memory::Write_U32(val, addr); break;
				}

				// Also update lock value if locked.
				for (auto &res : g_MemoryScanner.GetResults()) {
					if (res.address == addr && res.type == type && res.locked) {
						res.lockValue = val;
					}
				}
			} catch (...) {
				// Ignore invalid input.
			}
		}
	});
}
