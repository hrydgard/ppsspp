// Copyright (c) 2012- PPSSPP Project.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"

enum class PSPRegistryValueType : u8 {
	DIRECTORY = 1,
	INTEGER = 2,
	STRING = 3,
	BINARY = 4,
};

struct PSPRegistryFileValue {
	PSPRegistryValueType type = PSPRegistryValueType::BINARY;
	std::vector<u8> data;
};

struct PSPRegistryFileCategory {
	std::vector<std::string> order;
	std::map<std::string, PSPRegistryFileValue> values;
};

// Parser/updater for the real flash1:/registry/system.ireg and system.dreg
// pair. IREG owns the category/block map; DREG owns keys and values. This
// class intentionally updates only existing keys. Rebuilding category/block
// allocation is a separate operation and must never be approximated with a
// second private registry format.
class PSPRegistryFile {
public:
	bool Load(const std::vector<u8> &ireg, const std::vector<u8> &dreg, std::string *error);

	const std::map<std::string, PSPRegistryFileCategory> &Categories() const { return categories_; }
	const std::vector<u8> &IregData() const { return ireg_; }
	const std::vector<u8> &DregData() const { return dreg_; }

	bool HasValue(const std::string &path, const std::string &name) const;
	bool SetValue(const std::string &path, const std::string &name, PSPRegistryValueType type,
		const std::vector<u8> &data, std::string *error);

private:
	struct Block {
		std::vector<u16> sectors;
		u16 keyCount = 0;
	};

	struct ValueLocation {
		PSPRegistryValueType type = PSPRegistryValueType::BINARY;
		u16 keyEntry = 0;
		u16 dataEntry = 0;
		u32 capacity = 0;
	};

	bool ReadBlock(const Block &block, std::vector<u8> *data) const;
	bool WriteBlock(const Block &block, const std::vector<u8> &data);
	bool UpdateBlockChecksum(std::vector<u8> *data) const;
	static std::string ValueIdentity(const std::string &path, const std::string &name);

	std::vector<u8> ireg_;
	std::vector<u8> dreg_;
	std::map<std::string, PSPRegistryFileCategory> categories_;
	std::map<std::string, Block> blocks_;
	std::map<std::string, ValueLocation> locations_;
};

bool PSPRegistryFileTestRoundTrip(std::string *error);
