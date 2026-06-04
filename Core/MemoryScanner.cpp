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
#include <cmath>

#include "Core/MemoryScanner.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/HLE/sceKernelModule.h"
#include "Common/StringUtils.h"

MemoryScannerManager g_MemoryScanner;

struct FloatRange {
	float lower = 0.0f;
	float upper = 0.0f;
	bool valid = false;

	[[nodiscard]] bool Match(float val) const {
		if (!valid) return false;
		if (lower <= upper) {
			return val >= lower && val < upper;
		} else {
			// Negative range: e.g. lower = -12, upper = -13
			return val <= lower && val > upper;
		}
	}
};

static FloatRange ParseFloatRange(const std::string &filter) {
	FloatRange range;
	if (filter.size() < 2 || filter.substr(filter.size() - 2) != ".*") {
		return range;
	}
	std::string prefix = filter.substr(0, filter.size() - 2);
	if (prefix.empty()) return range;
	if (prefix == "-") return range;

	try {
		range.lower = std::stof(prefix);
		float inc = 1.0f;
		size_t dotPos = prefix.find('.');
		if (dotPos != std::string::npos) {
			size_t digits = prefix.size() - dotPos - 1;
			inc = powf(0.1f, (float)digits);
		}
		if (prefix[0] == '-') {
			range.upper = range.lower - inc;
		} else {
			range.upper = range.lower + inc;
		}
		range.valid = true;
	} catch (...) {
	}
	return range;
}

void MemoryScanner::FirstScan(ScanValueType type, const std::string &filter) {
	results_.clear();
	uint32_t val = 0;
	float fval = 0.0f;
	FloatRange frange;
	bool isWildcard = false;
	try {
		if (type == ScanValueType::FLOAT) {
			if (filter.size() >= 2 && filter.substr(filter.size() - 2) == ".*") {
				frange = ParseFloatRange(filter);
				if (frange.valid) {
					isWildcard = true;
				} else {
					return;
				}
			} else {
				fval = std::stof(filter);
			}
		} else {
			val = (uint32_t)std::stoul(filter, nullptr, 0);
		}
	} catch (...) {
		return;
	}

	uint32_t startAddr = 0x08000000;
	uint32_t endAddr = startAddr + Memory::g_MemorySize;

	SceUID mainModuleID = __KernelGetMainModuleId();
	if (mainModuleID != 0) {
		u32 error;
		auto *mainModule = kernelObjects.Get<PSPModule>(mainModuleID, error);
		if (mainModule) {
			startAddr = mainModule->GetDataAddr();
			endAddr = startAddr + mainModule->nm.data_size + mainModule->nm.bss_size;
		}
	}

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
		case ScanValueType::FLOAT:
			if (addr + 4 <= endAddr) {
				float v = Memory::Read_Float(addr);
				if (isWildcard) {
					if (frange.Match(v)) match = true;
				} else {
					if (v == fval) match = true;
				}
			}
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
	float fval = 0.0f;
	FloatRange frange;
	bool isWildcard = false;
	try {
		if (type == ScanValueType::FLOAT) {
			if (filter.size() >= 2 && filter.substr(filter.size() - 2) == ".*") {
				frange = ParseFloatRange(filter);
				if (frange.valid) {
					isWildcard = true;
				} else {
					return;
				}
			} else {
				fval = std::stof(filter);
			}
		} else {
			val = (uint32_t)std::stoul(filter, nullptr, 0);
		}
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
		case ScanValueType::FLOAT:
			{
				float v = Memory::Read_Float(res.address);
				if (isWildcard) {
					if (frange.Match(v)) match = true;
				} else {
					if (v == fval) match = true;
				}
			}
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
			case ScanValueType::FLOAT:
				{
					float f;
					memcpy(&f, &res.lockValue, 4);
					Memory::Write_Float(f, res.address);
				}
				break;
			}
		}
	}
}

bool MemoryScanner::SetLocked(size_t index, bool locked) {
	if (index < results_.size()) {
		if (locked) {
			if (g_MemoryScanner.HasLockedAddress(results_[index].address, this, index)) {
				results_[index].locked = false;
				return false;
			}
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
			case ScanValueType::FLOAT:
				{
					float f = Memory::Read_Float(results_[index].address);
					memcpy(&results_[index].lockValue, &f, 4);
				}
				break;
			}
		}
		results_[index].locked = locked;
		return true;
	}
	return false;
}

MemoryScannerManager::MemoryScannerManager() {
	scanners_.push_back(new MemoryScanner());
}

void MemoryScannerManager::Update() {
	for (auto scanner : scanners_) {
		scanner->Update();
	}
}

void MemoryScannerManager::SetActiveIndex(int index) {
	if (index >= 0 && index < (int)scanners_.size()) {
		activeScanner_ = index;
	}
}

bool MemoryScannerManager::HasLockedAddress(uint32_t addr, const MemoryScanner *excludeScanner, size_t excludeIndex) const {
	for (auto scanner : scanners_) {
		const auto &results = scanner->GetResults();
		for (size_t i = 0; i < results.size(); ++i) {
			if (scanner == excludeScanner && i == excludeIndex)
				continue;
			if (results[i].locked && results[i].address == addr)
				return true;
		}
	}
	return false;
}

void MemoryScannerManager::AddScanner() {
	scanners_.push_back(new MemoryScanner());
	activeScanner_ = (int)scanners_.size() - 1;
}

void MemoryScannerManager::RemoveScanner(int index) {
	if (scanners_.size() <= 1) {
		return;
	}
	if (index >= 0 && index < (int)scanners_.size()) {
		delete scanners_[index];
		scanners_.erase(scanners_.begin() + index);
		if (activeScanner_ >= (int)scanners_.size()) {
			activeScanner_ = (int)scanners_.size() - 1;
		}
	}
}
