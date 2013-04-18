// IniFile
// Taken from Dolphin but relicensed by me, Henrik Rydgard, under the MIT
// license as I wrote the whole thing originally and it has barely changed.

#pragma once

#include <string>
#include <vector>
#include <map>

#include "base/stringutil.h"

class IniFile
{
public:
	class Section
	{
		friend class IniFile;

	public:
		Section() {}
		Section(const std::string& name) : name_(name) {}

		bool Exists(const char *key) const;
		bool Delete(const char *key);

		std::map<std::string, std::string> ToMap() const;

		std::string* GetLine(const char* key, std::string* valueOut, std::string* commentOut);
		void Set(const char* key, const char* newValue);
		void Set(const char* key, const std::string& newValue, const std::string& defaultValue);

		void Set(const std::string &key, const std::string &value) {
			Set(key.c_str(), value.c_str());
		}
		bool Get(const char* key, std::string* value, const char* defaultValue);

		void Set(const char* key, uint32_t newValue) {
			Set(key, StringFromFormat("0x%08x", newValue).c_str());
		}
		void Set(const char* key, float newValue) {
			Set(key, StringFromFormat("%f", newValue).c_str());
		}
		void Set(const char* key, const float newValue, const float defaultValue);
		void Set(const char* key, double newValue) {
			Set(key, StringFromFormat("%f", newValue).c_str());
		}
		
		void Set(const char* key, int newValue, int defaultValue);
		void Set(const char* key, int newValue) {
			Set(key, StringFromInt(newValue).c_str());
		}
		
		void Set(const char* key, bool newValue, bool defaultValue);
		void Set(const char* key, bool newValue) {
			Set(key, StringFromBool(newValue).c_str());
		}
		void Set(const char* key, const std::vector<std::string>& newValues);

		template<typename U, typename V>
		void Set(const char* key, const std::map<U,V>& newValues)
		{
			std::vector<std::string> temp;
			for(typename std::map<U,V>::const_iterator it = newValues.begin(); it != newValues.end(); it++)
			{
				temp.push_back(ValueToString<U>(it->first)+"_"+ValueToString<V>(it->second));
			}
			Set(key,temp);
		}

		bool Get(const char* key, int* value, int defaultValue = 0);
		bool Get(const char* key, uint32_t* value, uint32_t defaultValue = 0);
		bool Get(const char* key, bool* value, bool defaultValue = false);
		bool Get(const char* key, float* value, float defaultValue = false);
		bool Get(const char* key, double* value, double defaultValue = false);
		bool Get(const char* key, std::vector<std::string>& values);
		template<typename U, typename V>
		bool Get(const char* key, std::map<U,V>& values)
		{
			std::vector<std::string> temp;
			if(!Get(key,temp))
			{
				return false;
			}
			values.clear();
			for(size_t i = 0; i < temp.size(); i++)
			{
				std::vector<std::string> key_val;
				SplitString(temp[i],'_',key_val);
				if(key_val.size() < 2)
					continue;
				U mapKey;
				V mapValue;
				if(!TryParse<U>(key_val[0],&mapKey))
					continue;
				if(!TryParse<V>(key_val[1],&mapValue))
					continue;
				values[mapKey] = mapValue;
			}
			return true;
		}

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

	bool Load(const char* filename);
	bool Load(const std::string &filename) { return Load(filename.c_str()); }
	bool Load(std::istream &istream);
	bool LoadFromVFS(const std::string &filename);

	bool Save(const char* filename);
	bool Save(const std::string &filename) { return Save(filename.c_str()); }

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
	const std::vector<Section> &Sections() { return sections; }

	Section* GetOrCreateSection(const char* section);

private:
	std::vector<Section> sections;

	const Section* GetSection(const char* section) const;
	Section* GetSection(const char* section);
	std::string* GetLine(const char* section, const char* key);
	void CreateSection(const char* section);
};
