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

#pragma once

#include <vector>
#include <string>

#include "Common/File/Path.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

enum class ScanValueType {
	U8,
	U16,
	U32,
};

struct ScanResult {
	ScanResult(uint32_t addr = 0, ScanValueType t = ScanValueType::U8)
		: address(addr), type(t) {}
	uint32_t address;
	ScanValueType type;
	bool locked = false;
	uint32_t lockValue = 0;
};

class MemoryScanner {
public:
	void FirstScan(ScanValueType type, const std::string &filter);
	void NextScan(ScanValueType type, const std::string &filter);
	void Clear();
	void Update();
	void SetLocked(size_t index, bool locked);

	[[nodiscard]] const std::vector<ScanResult> &GetResults() const { return results_; }
	std::vector<ScanResult> &GetResults() { return results_; }
	[[nodiscard]] bool HasScanDone() const { return firstScanDone_; }

	ScanValueType valueType = ScanValueType::U32;
	std::string searchValue = "0";

private:
	std::vector<ScanResult> results_;
	bool firstScanDone_ = false;
};

extern MemoryScanner g_MemoryScanner;

class MemoryScannerScreen : public UIDialogScreen {
public:
	MemoryScannerScreen(Path gamePath);
	~MemoryScannerScreen() override;

	void CreateViews() override;
	void update() override;
	void RebuildResultsList();

	[[nodiscard]] const char *tag() const override { return "MemoryScanner"; }

private:
	void OnFirstScan(UI::EventParams &e);
	void OnNextScan(UI::EventParams &e);
	void OnClear(UI::EventParams &e);
	void OnValueClick(uint32_t addr, ScanValueType type);

	Path gamePath_;

	UI::TextView *countText_ = nullptr;
	UI::TextEdit *searchValueEdit_ = nullptr;
	UI::LinearLayout *resultsList_ = nullptr;
	std::vector<UI::Choice *> valueChoices_;
	std::vector<uint32_t> resultAddresses_;
	std::vector<ScanValueType> resultTypes_;
};
