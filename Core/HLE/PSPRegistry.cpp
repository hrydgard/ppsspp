// Copyright (c) 2012- PPSSPP Project.

#include "Core/HLE/PSPRegistry.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <set>

#include "Common/Crypto/sha1.h"
#include "Common/StringUtils.h"

namespace {

constexpr size_t IREG_HEADER_SIZE = 0x5C;
constexpr size_t IREG_ENTRY_SIZE = 0x3A;
constexpr size_t IREG_ENTRY_COUNT = 256;
constexpr size_t DREG_SECTOR_SIZE = 0x200;
constexpr size_t DREG_ENTRY_SIZE = 0x20;

u16 ReadLE16(const u8 *data) {
	return (u16)data[0] | ((u16)data[1] << 8);
}

void WriteLE16(u8 *data, u16 value) {
	data[0] = (u8)value;
	data[1] = (u8)(value >> 8);
}

std::string FixedString(const u8 *data, size_t size) {
	size_t length = 0;
	while (length < size && data[length] != 0) {
		++length;
	}
	return std::string((const char *)data, length);
}

bool IsSafeName(const std::string &name) {
	if (name.empty()) {
		return false;
	}
	for (char ch : name) {
		if ((u8)ch < 0x20 || ch == '/' || ch == '\\') {
			return false;
		}
	}
	return true;
}

bool ComputeSHA1(std::vector<u8> data, size_t clearOffset, size_t clearSize, u8 digest[20]) {
	if (clearOffset > data.size() || clearSize > data.size() - clearOffset || data.size() > 0x7FFFFFFF) {
		return false;
	}
	std::fill(data.begin() + clearOffset, data.begin() + clearOffset + clearSize, 0);
	sha1(data.data(), (int)data.size(), digest);
	return true;
}

bool UpdateDregChecksum(std::vector<u8> *data) {
	if (data->size() < DREG_ENTRY_SIZE || ((*data)[0] & 0x0F) != 0x0F) {
		return false;
	}
	u8 digest[20]{};
	if (!ComputeSHA1(*data, 14, 4, digest)) {
		return false;
	}
	for (int i = 0; i < 4; ++i) {
		(*data)[14 + i] = digest[i * 5 + 0] ^ digest[i * 5 + 1] ^ digest[i * 5 + 2] ^ digest[i * 5 + 3] ^ digest[i * 5 + 4];
	}
	return true;
}

}  // namespace

std::string PSPRegistryFile::ValueIdentity(const std::string &path, const std::string &name) {
	return path + '\n' + name;
}

bool PSPRegistryFile::ReadBlock(const Block &block, std::vector<u8> *data) const {
	data->resize(block.sectors.size() * DREG_SECTOR_SIZE);
	for (size_t i = 0; i < block.sectors.size(); ++i) {
		const size_t source = (size_t)block.sectors[i] * DREG_SECTOR_SIZE;
		if (source > dreg_.size() || DREG_SECTOR_SIZE > dreg_.size() - source) {
			return false;
		}
		memcpy(data->data() + i * DREG_SECTOR_SIZE, dreg_.data() + source, DREG_SECTOR_SIZE);
	}
	return true;
}

bool PSPRegistryFile::WriteBlock(const Block &block, const std::vector<u8> &data) {
	if (data.size() != block.sectors.size() * DREG_SECTOR_SIZE) {
		return false;
	}
	for (size_t i = 0; i < block.sectors.size(); ++i) {
		const size_t destination = (size_t)block.sectors[i] * DREG_SECTOR_SIZE;
		if (destination > dreg_.size() || DREG_SECTOR_SIZE > dreg_.size() - destination) {
			return false;
		}
		memcpy(dreg_.data() + destination, data.data() + i * DREG_SECTOR_SIZE, DREG_SECTOR_SIZE);
	}
	return true;
}

bool PSPRegistryFile::UpdateBlockChecksum(std::vector<u8> *data) const {
	return UpdateDregChecksum(data);
}

bool PSPRegistryFile::Load(const std::vector<u8> &ireg, const std::vector<u8> &dreg, std::string *error) {
	auto fail = [&](const char *message) {
		if (error) {
			*error = message;
		}
		return false;
	};

	ireg_.clear();
	dreg_.clear();
	categories_.clear();
	blocks_.clear();
	locations_.clear();

	if (ireg.size() != IREG_HEADER_SIZE + IREG_ENTRY_SIZE * IREG_ENTRY_COUNT ||
		dreg.empty() || dreg.size() % DREG_SECTOR_SIZE != 0 || dreg.size() > 0x20000) {
		return fail("invalid IREG/DREG size");
	}
	if (ireg[0] != 0 || ireg[1] != 'I' || ireg[2] != 'R' || ireg[3] != 'F') {
		return fail("invalid IREG magic");
	}
	u8 iregDigest[20]{};
	if (!ComputeSHA1(ireg, 8, 20, iregDigest) || memcmp(iregDigest, ireg.data() + 8, sizeof(iregDigest)) != 0) {
		return fail("IREG SHA-1 mismatch");
	}

	struct IregEntry {
		bool valid = false;
		u16 parent = 0xFFFF;
		std::string name;
		Block block;
	};
	std::array<IregEntry, IREG_ENTRY_COUNT> entries{};
	const size_t dregSectorCount = dreg.size() / DREG_SECTOR_SIZE;
	for (size_t index = 0; index < entries.size(); ++index) {
		const u8 *entry = ireg.data() + IREG_HEADER_SIZE + index * IREG_ENTRY_SIZE;
		const std::string name = FixedString(entry + 0x0E, 0x1C);
		if (name.empty()) {
			continue;
		}
		if (!IsSafeName(name)) {
			return fail("invalid IREG category entry");
		}
		const u16 sectorCount = ReadLE16(entry + 0x0C);
		if (sectorCount == 0 || sectorCount > 7) {
			return fail("invalid IREG sector count");
		}
		IregEntry &parsed = entries[index];
		parsed.valid = true;
		parsed.parent = ReadLE16(entry + 4);
		parsed.name = name;
		parsed.block.keyCount = ReadLE16(entry + 0x0A);
		std::set<u16> uniqueSectors;
		for (u16 sectorIndex = 0; sectorIndex < sectorCount; ++sectorIndex) {
			const u16 sector = ReadLE16(entry + 0x2C + sectorIndex * 2);
			if (sector >= dregSectorCount || !uniqueSectors.insert(sector).second) {
				return fail("invalid IREG sector chain");
			}
			parsed.block.sectors.push_back(sector);
		}
	}

	std::array<std::string, IREG_ENTRY_COUNT> paths{};
	std::array<u8, IREG_ENTRY_COUNT> pathState{};
	std::function<bool(size_t)> resolvePath = [&](size_t index) {
		if (!entries[index].valid) {
			return false;
		}
		if (pathState[index] == 2) {
			return true;
		}
		if (pathState[index] == 1) {
			return false;
		}
		pathState[index] = 1;
		const IregEntry &entry = entries[index];
		if (entry.parent == 0xFFFF) {
			paths[index] = "/" + entry.name;
		} else {
			if (entry.parent >= entries.size() || !resolvePath(entry.parent)) {
				return false;
			}
			paths[index] = paths[entry.parent] == "/" ? "/" + entry.name : paths[entry.parent] + "/" + entry.name;
		}
		pathState[index] = 2;
		return true;
	};

	ireg_ = ireg;
	dreg_ = dreg;
	for (size_t index = 0; index < entries.size(); ++index) {
		if (!entries[index].valid) {
			continue;
		}
		if (!resolvePath(index) || categories_.find(paths[index]) != categories_.end()) {
			return fail("invalid IREG category hierarchy");
		}
		std::vector<u8> blockData;
		if (!ReadBlock(entries[index].block, &blockData) || blockData.size() < DREG_ENTRY_SIZE ||
			(blockData[0] & 0x0F) != 0x0F || ReadLE16(blockData.data() + 2) != entries[index].block.sectors.size() ||
			ReadLE16(blockData.data() + 4) != DREG_ENTRY_SIZE) {
			return fail("invalid DREG block header");
		}
		u8 expectedChecksum[4];
		memcpy(expectedChecksum, blockData.data() + 14, sizeof(expectedChecksum));
		std::vector<u8> checkedBlock = blockData;
		if (!UpdateBlockChecksum(&checkedBlock) || memcmp(expectedChecksum, checkedBlock.data() + 14, sizeof(expectedChecksum)) != 0) {
			return fail("DREG block checksum mismatch");
		}
		const u16 keyCount = ReadLE16(blockData.data() + 12);
		if ((size_t)(keyCount + 1) * DREG_ENTRY_SIZE > blockData.size()) {
			return fail("invalid DREG key count");
		}
		Block block = entries[index].block;
		block.keyCount = keyCount;
		blocks_[paths[index]] = block;
		PSPRegistryFileCategory category;
		for (u16 keyIndex = 1; keyIndex <= keyCount; ++keyIndex) {
			const u8 *key = blockData.data() + keyIndex * DREG_ENTRY_SIZE;
			const PSPRegistryValueType type = (PSPRegistryValueType)(key[0] & 0x0F);
			const size_t nameLength = type == PSPRegistryValueType::DIRECTORY ? 31 : 27;
			const std::string name = FixedString(key + 1, nameLength);
			if (!IsSafeName(name) || category.values.find(name) != category.values.end()) {
				return fail("invalid DREG key name");
			}
			PSPRegistryFileValue value;
			value.type = type;
			ValueLocation location;
			location.type = type;
			location.keyEntry = keyIndex;
			switch (type) {
			case PSPRegistryValueType::DIRECTORY:
				break;
			case PSPRegistryValueType::INTEGER:
				value.data.assign(key + 28, key + 32);
				break;
			case PSPRegistryValueType::STRING:
			case PSPRegistryValueType::BINARY: {
				const u16 length = ReadLE16(key + 28);
				const u16 dataEntry = key[31];
				const size_t dataOffset = (size_t)dataEntry * DREG_ENTRY_SIZE;
				const size_t capacity = std::max<size_t>(DREG_ENTRY_SIZE, (length + DREG_ENTRY_SIZE - 1) & ~(DREG_ENTRY_SIZE - 1));
				if (dataEntry <= keyCount || dataOffset > blockData.size() || capacity > blockData.size() - dataOffset || length > capacity) {
					return fail("invalid DREG indirect value");
				}
				value.data.assign(blockData.begin() + dataOffset, blockData.begin() + dataOffset + length);
				location.dataEntry = dataEntry;
				location.capacity = (u32)capacity;
				break;
			}
			default:
				return fail("unsupported DREG key type");
			}
			category.order.push_back(name);
			category.values.emplace(name, std::move(value));
			locations_[ValueIdentity(paths[index], name)] = location;
		}
		categories_.emplace(paths[index], std::move(category));
	}
	return true;
}

bool PSPRegistryFile::HasValue(const std::string &path, const std::string &name) const {
	return locations_.find(ValueIdentity(path, name)) != locations_.end();
}

bool PSPRegistryFile::SetValue(const std::string &path, const std::string &name, PSPRegistryValueType type,
	const std::vector<u8> &data, std::string *error) {
	auto fail = [&](const char *message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	auto blockIt = blocks_.find(path);
	auto locationIt = locations_.find(ValueIdentity(path, name));
	auto categoryIt = categories_.find(path);
	if (blockIt == blocks_.end() || locationIt == locations_.end() || categoryIt == categories_.end()) {
		return fail("key is not present in system.ireg/system.dreg");
	}
	ValueLocation &location = locationIt->second;
	if (location.type != type || type == PSPRegistryValueType::DIRECTORY) {
		return fail("registry value type mismatch");
	}
	std::vector<u8> blockData;
	if (!ReadBlock(blockIt->second, &blockData)) {
		return fail("could not read DREG block");
	}
	const size_t keyOffset = (size_t)location.keyEntry * DREG_ENTRY_SIZE;
	if (keyOffset > blockData.size() || DREG_ENTRY_SIZE > blockData.size() - keyOffset) {
		return fail("invalid DREG key offset");
	}
	if (type == PSPRegistryValueType::INTEGER) {
		if (data.size() != sizeof(u32)) {
			return fail("integer registry value must be four bytes");
		}
		memcpy(blockData.data() + keyOffset + 28, data.data(), data.size());
	} else {
		if (data.size() > location.capacity || data.size() > 0xFFFF) {
			return fail("registry value exceeds its real DREG allocation");
		}
		const size_t dataOffset = (size_t)location.dataEntry * DREG_ENTRY_SIZE;
		WriteLE16(blockData.data() + keyOffset + 28, (u16)data.size());
		std::fill(blockData.begin() + dataOffset, blockData.begin() + dataOffset + location.capacity, 0);
		if (!data.empty()) {
			memcpy(blockData.data() + dataOffset, data.data(), data.size());
		}
	}
	if (!UpdateBlockChecksum(&blockData) || !WriteBlock(blockIt->second, blockData)) {
		return fail("could not update DREG block checksum");
	}
	categoryIt->second.values[name].data = data;
	return true;
}

bool PSPRegistryFileTestRoundTrip(std::string *error) {
	auto fail = [&](const char *message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	std::vector<u8> ireg(IREG_HEADER_SIZE + IREG_ENTRY_SIZE * IREG_ENTRY_COUNT);
	std::vector<u8> dreg(DREG_SECTOR_SIZE * 3);
	ireg[1] = 'I';
	ireg[2] = 'R';
	ireg[3] = 'F';
	auto addIreg = [&](u16 index, u16 parent, const char *name, u16 sector, u16 keyCount) {
		u8 *entry = ireg.data() + IREG_HEADER_SIZE + index * IREG_ENTRY_SIZE;
		entry[0] = 0x68;
		entry[1] = 0x4D;
		entry[2] = 0x1C;
		entry[3] = 0x88;
		WriteLE16(entry + 4, parent);
		WriteLE16(entry + 6, index);
		WriteLE16(entry + 0x0A, keyCount);
		WriteLE16(entry + 0x0C, 1);
		strncpy((char *)entry + 0x0E, name, 0x1B);
		WriteLE16(entry + 0x2C, sector);
	};
	addIreg(1, 0xFFFF, "REGISTRY", 0, 1);
	addIreg(2, 0xFFFF, "CONFIG", 1, 1);
	addIreg(3, 2, "SYSTEM", 2, 2);
	u8 iregDigest[20]{};
	if (!ComputeSHA1(ireg, 8, 20, iregDigest)) {
		return fail("could not checksum synthetic IREG");
	}
	memcpy(ireg.data() + 8, iregDigest, sizeof(iregDigest));

	auto makeBlock = [&](int sector, int keyCount) {
		std::vector<u8> block(DREG_SECTOR_SIZE);
		block[0] = 0x0F;
		WriteLE16(block.data() + 2, 1);
		WriteLE16(block.data() + 4, DREG_ENTRY_SIZE);
		WriteLE16(block.data() + 10, 15);
		WriteLE16(block.data() + 12, (u16)keyCount);
		return block;
	};
	std::vector<u8> registryBlock = makeBlock(0, 1);
	registryBlock[DREG_ENTRY_SIZE] = (u8)PSPRegistryValueType::INTEGER;
	strcpy((char *)registryBlock.data() + DREG_ENTRY_SIZE + 1, "category_version");
	registryBlock[DREG_ENTRY_SIZE + 28] = 0x66;
	if (!UpdateDregChecksum(&registryBlock)) {
		return fail("could not checksum synthetic root DREG");
	}
	memcpy(dreg.data(), registryBlock.data(), registryBlock.size());

	std::vector<u8> configBlock = makeBlock(1, 1);
	configBlock[DREG_ENTRY_SIZE] = (u8)PSPRegistryValueType::DIRECTORY;
	strcpy((char *)configBlock.data() + DREG_ENTRY_SIZE + 1, "SYSTEM");
	if (!UpdateDregChecksum(&configBlock)) {
		return fail("could not checksum synthetic CONFIG DREG");
	}
	memcpy(dreg.data() + DREG_SECTOR_SIZE, configBlock.data(), configBlock.size());

	std::vector<u8> systemBlock = makeBlock(2, 2);
	u8 *volume = systemBlock.data() + DREG_ENTRY_SIZE;
	volume[0] = (u8)PSPRegistryValueType::INTEGER;
	strcpy((char *)volume + 1, "main_volume");
	volume[28] = 21;
	u8 *owner = systemBlock.data() + DREG_ENTRY_SIZE * 2;
	owner[0] = (u8)PSPRegistryValueType::STRING;
	strcpy((char *)owner + 1, "owner_name");
	WriteLE16(owner + 28, 6);
	owner[31] = 3;
	memcpy(systemBlock.data() + DREG_ENTRY_SIZE * 3, "Robyn", 6);
	if (!UpdateDregChecksum(&systemBlock)) {
		return fail("could not checksum synthetic SYSTEM DREG");
	}
	memcpy(dreg.data() + DREG_SECTOR_SIZE * 2, systemBlock.data(), systemBlock.size());

	PSPRegistryFile parsed;
	std::string parseError;
	if (!parsed.Load(ireg, dreg, &parseError)) {
		return fail(parseError.c_str());
	}
	const std::vector<u8> newVolume{17, 0, 0, 0};
	const std::vector<u8> newOwner{'T', 'e', 's', 't', 0};
	if (!parsed.SetValue("/CONFIG/SYSTEM", "main_volume", PSPRegistryValueType::INTEGER, newVolume, &parseError) ||
		!parsed.SetValue("/CONFIG/SYSTEM", "owner_name", PSPRegistryValueType::STRING, newOwner, &parseError)) {
		return fail(parseError.c_str());
	}
	if (parsed.IregData() != ireg) {
		return fail("existing-key write changed IREG");
	}
	PSPRegistryFile reloaded;
	if (!reloaded.Load(ireg, parsed.DregData(), &parseError)) {
		return fail(parseError.c_str());
	}
	auto category = reloaded.Categories().find("/CONFIG/SYSTEM");
	if (category == reloaded.Categories().end() || category->second.values.at("main_volume").data != newVolume ||
		category->second.values.at("owner_name").data != newOwner) {
		return fail("real registry values did not survive reload");
	}
	std::vector<u8> corrupt = parsed.DregData();
	corrupt[DREG_SECTOR_SIZE * 2 + DREG_ENTRY_SIZE + 28] ^= 1;
	PSPRegistryFile rejected;
	if (rejected.Load(ireg, corrupt, nullptr)) {
		return fail("corrupt DREG checksum was accepted");
	}
	if (error) {
		error->clear();
	}
	return true;
}
