#include <cstring>

#include "Common/Data/Text/I18n.h"
#include "Common/Data/Format/IniFile.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Log.h"

#include "Common/StringUtils.h"

static const char * const g_categoryNames[(size_t)I18NCat::CATEGORY_COUNT] = {
	"Audio",
	"Controls",
	"CwCheats",
	"DesktopUI",
	"Developer",
	"Dialog",
	"Error",
	"Game",
	"Graphics",
	"InstallZip",
	"KeyMapping",
	"MainMenu",
	"MainSettings",
	"MappableControls",
	"Networking",
	"Pause",
	"PostShaders",
	"PSPCredits",
	"MemStick",
	"RemoteISO",
	"Reporting",
	"Savedata",
	"Screen",
	"Search",
	"Store",
	"SysInfo",
	"System",
	"TextureShaders",
	"Themes",
	"UI Elements",
	"Upgrade",
	"VR",
	"Achievements",
	"PSPSettings",
};

I18NRepo g_i18nrepo;

std::string I18NRepo::LanguageID() {
	return languageID_;
}

I18NRepo::I18NRepo() {
	Clear();
}

void I18NRepo::Clear() {
	std::lock_guard<std::mutex> guard(catsLock_);
	for (auto &iter : cats_) {
		// Initialize with empty categories, so that early lookups don't crash.
		iter = std::make_shared<I18NCategory>();
	}
}

I18NCategory::I18NCategory(const Section &section) {
	std::map<std::string, std::string> sectionMap = section.ToMap();
	SetMap(sectionMap);
}

void I18NCategory::Clear() {
	map_.clear();
	missedKeyLog_.clear();
}

std::string_view I18NCategory::T(std::string_view key, std::string_view def) {
	auto iter = map_.find(key);
	if (iter != map_.end()) {
		return iter->second.text.c_str();
	} else {
		std::lock_guard<std::mutex> guard(missedKeyLock_);
		std::string missedKey(key);
		if (!def.empty())
			missedKeyLog_[missedKey] = def;
		else
			missedKeyLog_[missedKey] = std::string(key);
		return !def.empty() ? def : key;
	}
}

const char *I18NCategory::T_cstr(const char *key, const char *def) {
	auto iter = map_.find(key);
	if (iter != map_.end()) {
		return iter->second.text.c_str();
	} else {
		std::lock_guard<std::mutex> guard(missedKeyLock_);
		std::string missedKey(key);
		if (def)
			missedKeyLog_[missedKey] = def;
		else
			missedKeyLog_[missedKey] = std::string(key);
		return def ? def : key;
	}
}

void I18NCategory::SetMap(const std::map<std::string, std::string> &m) {
	for (auto iter = m.begin(); iter != m.end(); ++iter) {
		if (map_.find(iter->first) == map_.end()) {
			std::string text = ReplaceAll(iter->second, "\\n", "\n");
			_dbg_assert_(iter->first.find('\n') == std::string::npos);
			map_[iter->first] = I18NEntry(text);
		}
	}
}

std::shared_ptr<I18NCategory> I18NRepo::GetCategory(I18NCat category) {
	std::lock_guard<std::mutex> guard(catsLock_);
	if (category != I18NCat::NONE)
		return cats_[(size_t)category];
	else
		return nullptr;
}

Path I18NRepo::GetIniPath(const std::string &languageID) const {
	return Path("lang") / (languageID + ".ini");
}

bool I18NRepo::IniExists(const std::string &languageID) const {
	File::FileInfo info;
	if (!g_VFS.GetFileInfo(GetIniPath(languageID).ToString().c_str(), &info))
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
		iniPath = GetIniPath(languageID);
	}

	if (!ini.LoadFromVFS(g_VFS, iniPath.ToString()))
		return false;

	Clear();

	const std::vector<std::unique_ptr<Section>> &sections = ini.Sections();

	std::lock_guard<std::mutex> guard(catsLock_);
	for (auto &section : sections) {
		for (size_t i = 0; i < (size_t)I18NCat::CATEGORY_COUNT; i++) {
			if (!strcmp(section->name().c_str(), g_categoryNames[i])) {
				cats_[i].reset(new I18NCategory(*section.get()));
			}
		}
	}

	languageID_ = languageID;
	return true;
}

void I18NRepo::LogMissingKeys() const {
	std::lock_guard<std::mutex> guard(catsLock_);
	for (size_t i = 0; i < (size_t)I18NCat::CATEGORY_COUNT; i++) {
		auto &cat = cats_[i];
		for (auto &key : cat->Missed()) {
			INFO_LOG(SYSTEM, "Missing translation [%s]: %s (%s)", g_categoryNames[i], key.first.c_str(), key.second.c_str());
		}
	}
}

std::shared_ptr<I18NCategory> GetI18NCategory(I18NCat category) {
	if (category == I18NCat::NONE) {
		return std::shared_ptr<I18NCategory>();
	}
	std::shared_ptr<I18NCategory> cat = g_i18nrepo.GetCategory(category);
	_dbg_assert_(cat);
	return cat;
}
