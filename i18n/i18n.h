#pragma once

// I18N = I....18..dots.....N = INTERNATIONALIZATION

// Super simple I18N library.
// Just enough to be useful and usable.
// Spits out easy-to-edit utf-8 .INI files.

// As usual, everything is UTF-8. Nothing else allowed.

#include <map>
#include <string>
#include <vector>

#include "base/stringutil.h"
#include "file/ini_file.h"

// Reasonably thread safe.

class I18NRepo;

struct I18NEntry {
	I18NEntry(const std::string &t) : text(t), readFlag(false) {}
	I18NEntry() : readFlag(false) {}
	std::string text;
	bool readFlag;
};

struct I18NCandidate {
	I18NCandidate() : key(0), defVal(0) {}
	I18NCandidate(const char *k, const char *d) : key(k), defVal(d) {}
	const char *key;
	const char *defVal;
};

class I18NCategory {
public:
	const char *T(const char *key, const char *def = 0);

	const std::map<std::string, std::string> &Missed() const {
		return missedKeyLog_;
	}

	void SetMap(const std::map<std::string, std::string> &m);
	const std::map<std::string, I18NEntry> &GetMap() { return map_; }
	void ClearMissed() { missedKeyLog_.clear(); }

private:
	I18NCategory(I18NRepo *repo) {}

	std::map<std::string, I18NEntry> map_;
	std::map<std::string, std::string> missedKeyLog_;

	// Noone else can create these.
	friend class I18NRepo;

	DISALLOW_COPY_AND_ASSIGN(I18NCategory);
};

class I18NRepo {
public:
	I18NRepo() {}
	~I18NRepo();

	bool LoadIni(const std::string &languageID);  // NOT the filename!
	void SaveIni(const std::string &languageID);

	I18NCategory *GetCategory(const char *categoryName);
	const char *T(const char *category, const char *key, const char *def = 0);

private:
	std::string GetIniPath(const std::string &languageID) const;
	void Clear();
	I18NCategory *LoadSection(const IniFile::Section *section);
	void SaveSection(IniFile &ini, IniFile::Section *section, I18NCategory *cat);

	std::map<std::string, I18NCategory *> cats_;

	DISALLOW_COPY_AND_ASSIGN(I18NRepo);
};

extern I18NRepo i18nrepo;

// These are simply talking to the one global instance of I18NRepo.

inline I18NCategory *GetI18NCategory(const char *categoryName) {
	return i18nrepo.GetCategory(categoryName);
}

inline const char *T(const char *category, const char *key, const char *def = 0) {
	return i18nrepo.T(category, key, def);
}



