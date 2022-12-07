// IniFile
// Taken from Dolphin but relicensed by me, Henrik Rydgard, under the MIT
// license as I wrote the whole thing originally and it has barely changed.

#pragma once

#include <istream>
#include <map>
#include <string>
#include <vector>

#include "Common/File/Path.h"

class Section {
	friend class IniFile;

public:
	Section() {}
	Section(const std::string& name) : name_(name) {}

	bool Exists(const char *key) const;
	bool Delete(const char *key);

	void Clear();

	std::map<std::string, std::string> ToMap() const;

	std::string *GetLine(const char* key, std::string* valueOut, std::string* commentOut);
	const std::string *GetLine(const char* key, std::string* valueOut, std::string* commentOut) const;

	void Set(const char* key, const char* newValue);
	void Set(const char* key, const std::string& newValue, const std::string& defaultValue);

	void Set(const std::string &key, const std::string &value) {
		Set(key.c_str(), value.c_str());
	}
	bool Get(const char* key, std::string* value, const char* defaultValue) const;

	void Set(const char* key, uint32_t newValue);
	void Set(const char* key, uint64_t newValue);
	void Set(const char* key, float newValue);
	void Set(const char* key, const float newValue, const float defaultValue);
	void Set(const char* key, double newValue);

	void Set(const char* key, int newValue, int defaultValue);
	void Set(const char* key, int newValue);

	void Set(const char* key, bool newValue, bool defaultValue);
	void Set(const char* key, bool newValue) {
		Set(key, newValue ? "True" : "False");
	}
	void Set(const char* key, const std::vector<std::string>& newValues);

	// Declare without a body to make it fail to compile. This is to prevent accidentally
	// setting a pointer as a bool. The failure is in the linker unfortunately, but that's better
	// than accidentally succeeding in a bad way.
	template<class T>
	void Set(const char *key, T *ptr);

	void AddComment(const std::string &comment);

	bool Get(const char* key, int* value, int defaultValue = 0) const;
	bool Get(const char* key, uint32_t* value, uint32_t defaultValue = 0) const;
	bool Get(const char* key, uint64_t* value, uint64_t defaultValue = 0) const;
	bool Get(const char* key, bool* value, bool defaultValue = false) const;
	bool Get(const char* key, float* value, float defaultValue = false) const;
	bool Get(const char* key, double* value, double defaultValue = false) const;
	bool Get(const char* key, std::vector<std::string>& values) const;

	bool operator < (const Section& other) const {
		return name_ < other.name_;
	}

	const std::string &name() const {
		return name_;
	}

protected:
	std::vector<std::string> lines;
	std::string name_;
	std::string comment;
};

class IniFile {
public:
	bool Load(const Path &path);
	bool Load(const std::string &filename) { return Load(Path(filename)); }
	bool Load(std::istream &istream);
	bool LoadFromVFS(const std::string &filename);

	bool Save(const Path &path);
	bool Save(const std::string &filename) { return Save(Path(filename)); }

	// Returns true if key exists in section
	bool Exists(const char* sectionName, const char* key) const;

	// TODO: Get rid of these, in favor of the Section ones.
	void Set(const char* sectionName, const char* key, const char* newValue) {
		GetOrCreateSection(sectionName)->Set(key, newValue);
	}
	void Set(const char* sectionName, const char* key, const std::string& newValue) {
		GetOrCreateSection(sectionName)->Set(key, newValue.c_str());
	}
	void Set(const char* sectionName, const char* key, int newValue) {
		GetOrCreateSection(sectionName)->Set(key, newValue);
	}
	void Set(const char* sectionName, const char* key, uint32_t newValue) {
		GetOrCreateSection(sectionName)->Set(key, newValue);
	}
	void Set(const char* sectionName, const char* key, uint64_t newValue) {
		GetOrCreateSection(sectionName)->Set(key, newValue);
	}
	void Set(const char* sectionName, const char* key, bool newValue) {
		GetOrCreateSection(sectionName)->Set(key, newValue);
	}
	void Set(const char* sectionName, const char* key, const std::vector<std::string>& newValues) {
		GetOrCreateSection(sectionName)->Set(key, newValues);
	}

	// TODO: Get rid of these, in favor of the Section ones.
	bool Get(const char* sectionName, const char* key, std::string* value, const char* defaultValue = "");
	bool Get(const char* sectionName, const char* key, int* value, int defaultValue = 0);
	bool Get(const char* sectionName, const char* key, uint32_t* value, uint32_t defaultValue = 0);
	bool Get(const char* sectionName, const char* key, uint64_t* value, uint64_t defaultValue = 0);
	bool Get(const char* sectionName, const char* key, bool* value, bool defaultValue = false);
	bool Get(const char* sectionName, const char* key, std::vector<std::string>& values);

	template<typename T> bool GetIfExists(const char* sectionName, const char* key, T value)
	{
		if (Exists(sectionName, key))
			return Get(sectionName, key, value);
		return false;
	}

	bool GetKeys(const char* sectionName, std::vector<std::string>& keys) const;

	void SetLines(const char* sectionName, const std::vector<std::string> &lines);
	bool GetLines(const char* sectionName, std::vector<std::string>& lines, const bool remove_comments = true) const;

	bool DeleteKey(const char* sectionName, const char* key);
	bool DeleteSection(const char* sectionName);

	void SortSections();
	std::vector<Section> &Sections() { return sections; }

	bool HasSection(const char *section) { return GetSection(section) != 0; }

	Section* GetOrCreateSection(const char* section);

private:
	std::vector<Section> sections;

	const Section* GetSection(const char* section) const;
	Section* GetSection(const char* section);
};
