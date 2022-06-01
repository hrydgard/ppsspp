#pragma once

// I18N = I....18..dots.....N = INTERNATIONALIZATION

// Super simple I18N library.
// Just enough to be useful and usable.
// Spits out easy-to-edit utf-8 .INI files.

// As usual, everything is UTF-8. Nothing else allowed.

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Common/Common.h"
#include "Common/File/Path.h"

// Reasonably thread safe.

class I18NRepo;
class IniFile;
class Section;

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
	// NOTE: Name must be a global constant string - it is not copied.
	I18NCategory(const char *name) : name_(name) {}
	const char *T(const char *key, const char *def = 0);
	const char *T(const std::string &key) {
		return T(key.c_str(), nullptr);
	}

	const std::map<std::string, std::string> Missed() const {
		std::lock_guard<std::mutex> guard(missedKeyLock_);
		return missedKeyLog_;
	}

	const std::map<std::string, I18NEntry> &GetMap() { return map_; }
	void ClearMissed() { missedKeyLog_.clear(); }
	const char *GetName() const { return name_.c_str(); }

private:
	I18NCategory(I18NRepo *repo, const char *name) : name_(name) {}
	void SetMap(const std::map<std::string, std::string> &m);

	std::string name_;

	std::map<std::string, I18NEntry> map_;
	mutable std::mutex missedKeyLock_;
	std::map<std::string, std::string> missedKeyLog_;

	// Noone else can create these.
	friend class I18NRepo;

	DISALLOW_COPY_AND_ASSIGN(I18NCategory);
};

class I18NRepo {
public:
	I18NRepo() {}
	~I18NRepo();

	bool IniExists(const std::string &languageID) const;
	bool LoadIni(const std::string &languageID, const Path &overridePath = Path()); // NOT the filename!
	void SaveIni(const std::string &languageID);

	std::string LanguageID();

	std::shared_ptr<I18NCategory> GetCategory(const char *categoryName);
	bool HasCategory(const char *categoryName) const {
		std::lock_guard<std::mutex> guard(catsLock_);
		return cats_.find(categoryName) != cats_.end();
	}
	const char *T(const char *category, const char *key, const char *def = 0);

	std::map<std::string, std::vector<std::string>> GetMissingKeys() const;

private:
	std::string GetIniPath(const std::string &languageID) const;
	void Clear();
	I18NCategory *LoadSection(const Section *section, const char *name);
	void SaveSection(IniFile &ini, Section *section, std::shared_ptr<I18NCategory> cat);

	mutable std::mutex catsLock_;
	std::map<std::string, std::shared_ptr<I18NCategory>> cats_;
	std::string languageID_;

	DISALLOW_COPY_AND_ASSIGN(I18NRepo);
};

extern I18NRepo i18nrepo;

// These are simply talking to the one global instance of I18NRepo.

inline std::shared_ptr<I18NCategory> GetI18NCategory(const char *categoryName) {
	if (!categoryName)
		return nullptr;
	return i18nrepo.GetCategory(categoryName);
}

inline bool I18NCategoryLoaded(const char *categoryName) {
	return i18nrepo.HasCategory(categoryName);
}

inline const char *T(const char *category, const char *key, const char *def = 0) {
	return i18nrepo.T(category, key, def);
}

inline std::map<std::string, std::vector<std::string>> GetI18NMissingKeys() {
	return i18nrepo.GetMissingKeys();
}

