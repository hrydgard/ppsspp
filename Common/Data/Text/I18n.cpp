#include "Common/Data/Text/I18n.h"
#include "Common/Data/Format/IniFile.h"
#include "Common/File/VFS/VFS.h"

#include "Common/StringUtils.h"

I18NRepo i18nrepo;

I18NRepo::~I18NRepo() {
	Clear();
}

std::string I18NRepo::LanguageID() {
	return languageID_;
}

void I18NRepo::Clear() {
	std::lock_guard<std::mutex> guard(catsLock_);
	for (auto iter = cats_.begin(); iter != cats_.end(); ++iter) {
		iter->second.reset();
	}
	cats_.clear();
}

const char *I18NCategory::T(const char *key, const char *def) {
	if (!key) {
		return "ERROR";
	}
	// Replace the \n's with \\n's so that key values with newlines will be found correctly.
	std::string modifiedKey = key;
	modifiedKey = ReplaceAll(modifiedKey, "\n", "\\n");

	auto iter = map_.find(modifiedKey);
	if (iter != map_.end()) {
//		INFO_LOG(SYSTEM, "translation key found in %s: %s", name_.c_str(), key);
		return iter->second.text.c_str();
	} else {
		std::lock_guard<std::mutex> guard(missedKeyLock_);
		if (def)
			missedKeyLog_[key] = def;
		else
			missedKeyLog_[key] = modifiedKey.c_str();
//		INFO_LOG(SYSTEM, "Missed translation key in %s: %s", name_.c_str(), key);
		return def ? def : key;
	}
}

void I18NCategory::SetMap(const std::map<std::string, std::string> &m) {
	for (auto iter = m.begin(); iter != m.end(); ++iter) {
		if (map_.find(iter->first) == map_.end()) {
			std::string text = ReplaceAll(iter->second, "\\n", "\n");
			map_[iter->first] = I18NEntry(text);
//			INFO_LOG(SYSTEM, "Language entry: %s -> %s", iter->first.c_str(), text.c_str());
		}
	}
}

std::shared_ptr<I18NCategory> I18NRepo::GetCategory(const char *category) {
	std::lock_guard<std::mutex> guard(catsLock_);
	auto iter = cats_.find(category);
	if (iter != cats_.end()) {
		return iter->second;
	} else {
		I18NCategory *c = new I18NCategory(this, category);
		cats_[category].reset(c);
		return cats_[category];
	}
}

std::string I18NRepo::GetIniPath(const std::string &languageID) const {
	return "lang/" + languageID + ".ini";
}

bool I18NRepo::IniExists(const std::string &languageID) const {
	File::FileInfo info;
	if (!VFSGetFileInfo(GetIniPath(languageID).c_str(), &info))
		return false;
	if (!info.exists)
		return false;
	return true;
}

bool I18NRepo::LoadIni(const std::string &languageID, const Path &overridePath) {
	IniFile ini;
	Path iniPath;

//	INFO_LOG(SYSTEM, "Loading lang ini %s", iniPath.c_str());
	if (!overridePath.empty()) {
		iniPath = overridePath / (languageID + ".ini");
	} else {
		iniPath = Path(GetIniPath(languageID));
	}

	if (!ini.LoadFromVFS(iniPath.ToString()))
		return false;

	Clear();

	const std::vector<Section> &sections = ini.Sections();

	std::lock_guard<std::mutex> guard(catsLock_);
	for (auto iter = sections.begin(); iter != sections.end(); ++iter) {
		if (iter->name() != "") {
			cats_[iter->name()].reset(LoadSection(&(*iter), iter->name().c_str()));
		}
	}

	languageID_ = languageID;
	return true;
}

std::map<std::string, std::vector<std::string>> I18NRepo::GetMissingKeys() const {
	std::map<std::string, std::vector<std::string>> ret;
	std::lock_guard<std::mutex> guard(catsLock_);
	for (auto &cat : cats_) {
		for (auto &key : cat.second->Missed()) {
			ret[cat.first].push_back(key.first);
		}
	}
	return ret;
}

I18NCategory *I18NRepo::LoadSection(const Section *section, const char *name) {
	I18NCategory *cat = new I18NCategory(this, name);
	std::map<std::string, std::string> sectionMap = section->ToMap();
	cat->SetMap(sectionMap);
	return cat;
}

// This is a very light touched save variant - it won't overwrite 
// anything, only create new entries.
void I18NRepo::SaveIni(const std::string &languageID) {
	IniFile ini;
	ini.Load(GetIniPath(languageID));
	std::lock_guard<std::mutex> guard(catsLock_);
	for (auto iter = cats_.begin(); iter != cats_.end(); ++iter) {
		std::string categoryName = iter->first;
		Section *section = ini.GetOrCreateSection(categoryName.c_str());
		SaveSection(ini, section, iter->second);
	}
	ini.Save(GetIniPath(languageID));
}

void I18NRepo::SaveSection(IniFile &ini, Section *section, std::shared_ptr<I18NCategory> cat) {
	const std::map<std::string, std::string> &missed = cat->Missed();

	for (auto iter = missed.begin(); iter != missed.end(); ++iter) {
		if (!section->Exists(iter->first.c_str())) {
			std::string text = ReplaceAll(iter->second, "\n", "\\n");
			section->Set(iter->first, text);
		}
	}

	const std::map<std::string, I18NEntry> &entries = cat->GetMap();
	for (auto iter = entries.begin(); iter != entries.end(); ++iter) {
		std::string text = ReplaceAll(iter->second.text, "\n", "\\n");
		section->Set(iter->first, text);
	}

	cat->ClearMissed();
}
