// IniFile
// Taken from Dolphin but relicensed by me, Henrik Rydgard, under the MIT
// license as I wrote the whole thing originally and it has barely changed.

#pragma once

#include <istream>
#include <memory>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

#include "Common/File/Path.h"

class VFSInterface;

class ParsedIniLine {
public:
	explicit ParsedIniLine(std::string_view line);

	ParsedIniLine(std::string_view key, std::string_view value) {
		this->key = key;
		this->value = value;
	}
	ParsedIniLine(std::string_view key, std::string_view value, std::string_view comment) {
		this->key = key;
		this->value = value;
		this->comment = comment;
	}
	static ParsedIniLine CommentOnly(std::string_view comment) {
		return ParsedIniLine(std::string_view(), std::string_view(), comment);
	}

	void Reconstruct(std::string *output) const;

	// Having these as views allows a more efficient internal representation, like one joint string.
	std::string_view Key() const { return key; }
	std::string_view Value() const { return value; }
	std::string_view Comment() const { return comment; }

	void SetValue(std::string_view newValue) { value = newValue; }

private:
	std::string key;
	std::string value;
	std::string comment;
};

class Section {
	friend class IniFile;

public:
	Section() {}
	Section(std::string_view name) : name_(name) {}

	bool HasKey(std::string_view key) const;
	bool Delete(std::string_view key);

	void Clear();

	std::map<std::string, std::string> ToMap() const;

	ParsedIniLine *GetLine(std::string_view key);
	const ParsedIniLine *GetLine(std::string_view key) const;

	void Set(std::string_view key, std::string_view newValue);
	void Set(std::string_view key, std::string_view newValue, std::string_view defaultValue);

	void Set(std::string_view key, uint32_t newValue);
	void Set(std::string_view key, uint64_t newValue);
	void Set(std::string_view key, float newValue);
	void Set(std::string_view key, const float newValue, const float defaultValue);
	void Set(std::string_view key, double newValue);

	void Set(std::string_view key, int newValue, int defaultValue);
	void Set(std::string_view key, int newValue);

	void Set(std::string_view key, bool newValue, bool defaultValue);
	void Set(std::string_view key, const char *newValue) { Set(key, std::string_view{newValue}); }
	void Set(std::string_view key, bool newValue) {
		Set(key, std::string_view(newValue ? "True" : "False"));
	}
	void Set(std::string_view key, const std::vector<std::string>& newValues);

	void AddComment(std::string_view comment);

	bool Get(std::string_view key, std::string *value) const;
	bool Get(std::string_view key, int* value) const;
	bool Get(std::string_view key, uint32_t* value) const;
	bool Get(std::string_view key, uint64_t* value) const;
	bool Get(std::string_view key, bool* value) const;
	bool Get(std::string_view key, float* value) const;
	bool Get(std::string_view key, double* value) const;
	bool Get(std::string_view key, std::vector<std::string> *values) const;

	// Return a list of all keys in this section
	bool GetKeys(std::vector<std::string> *keys) const;

	bool operator < (const Section& other) const {
		return name_ < other.name_;
	}

	const std::string &name() const {
		return name_;
	}

	// For reading without copying. Note: You may have to ignore lines with empty keys.
	const std::vector<ParsedIniLine> &Lines() const {
		return lines_;
	}

protected:
	std::vector<ParsedIniLine> lines_;
	std::string name_;
	std::string comment;
};

class IniFile {
public:
	bool Load(const Path &path);
	bool Load(std::istream &istream);
	bool LoadFromVFS(VFSInterface &vfs, const std::string &filename);

	bool Save(const Path &path);

	bool DeleteSection(std::string_view sectionName);

	void SortSections();

	std::vector<std::unique_ptr<Section>> &Sections() { return sections; }

	bool HasSection(std::string_view section) { return GetSection(section) != nullptr; }
	const Section* GetSection(std::string_view section) const;
	Section* GetSection(std::string_view section);

	Section* GetOrCreateSection(std::string_view section);

private:
	std::vector<std::unique_ptr<Section>> sections;
};
