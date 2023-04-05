#include "Common/Data/Text/I18n.h"
#include "Common/Data/Format/IniFile.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Log.h"

#include "Common/StringUtils.h"

I18NRepo i18nrepo;

I18NRepo::~I18NRepo() {
	Clear();
}

std::string I18NRepo::LanguageID() {
	return languageID_;
}

I18NRepo::I18NRepo() {
	cats_[(size_t)I18NCat::AUDIO].SetName("Audio");
	cats_[(size_t)I18NCat::CONTROLS].SetName("Controls");
	cats_[(size_t)I18NCat::SYSTEM].SetName("System");
	cats_[(size_t)I18NCat::CWCHEATS].SetName("CwCheats");
	cats_[(size_t)I18NCat::DESKTOPUI].SetName("DesktopUI");
	cats_[(size_t)I18NCat::DEVELOPER].SetName("Developer");
	cats_[(size_t)I18NCat::DIALOG].SetName("Dialog");
	cats_[(size_t)I18NCat::ERRORS].SetName("Error");
	cats_[(size_t)I18NCat::GAME].SetName("Game");
	cats_[(size_t)I18NCat::GRAPHICS].SetName("Graphics");
	cats_[(size_t)I18NCat::INSTALLZIP].SetName("InstallZip");
	cats_[(size_t)I18NCat::KEYMAPPING].SetName("KeyMapping");
	cats_[(size_t)I18NCat::MAINMENU].SetName("MainMenu");
	cats_[(size_t)I18NCat::MAINSETTINGS].SetName("MainSettings");
	cats_[(size_t)I18NCat::MAPPABLECONTROLS].SetName("MappableControls");
	cats_[(size_t)I18NCat::NETWORKING].SetName("Networking");
	cats_[(size_t)I18NCat::PAUSE].SetName("Pause");
	cats_[(size_t)I18NCat::POSTSHADERS].SetName("PostShaders");
	cats_[(size_t)I18NCat::PSPCREDITS].SetName("PSPCredits");
	cats_[(size_t)I18NCat::MEMSTICK].SetName("MemStick");
	cats_[(size_t)I18NCat::REMOTEISO].SetName("RemoteISO");
	cats_[(size_t)I18NCat::REPORTING].SetName("Reporting");
	cats_[(size_t)I18NCat::SAVEDATA].SetName("Savedata");
	cats_[(size_t)I18NCat::SCREEN].SetName("Screen");
	cats_[(size_t)I18NCat::SEARCH].SetName("Search");
	cats_[(size_t)I18NCat::STORE].SetName("Store");
	cats_[(size_t)I18NCat::SYSINFO].SetName("SysInfo");
	cats_[(size_t)I18NCat::SYSTEM].SetName("System");
	cats_[(size_t)I18NCat::TEXTURESHADERS].SetName("TextureShaders");
	cats_[(size_t)I18NCat::THEMES].SetName("Themes");
	cats_[(size_t)I18NCat::UI_ELEMENTS].SetName("UI Elements");
	cats_[(size_t)I18NCat::UPGRADE].SetName("Upgrade");
	cats_[(size_t)I18NCat::VR].SetName("VR");
}

void I18NRepo::Clear() {
	std::lock_guard<std::mutex> guard(catsLock_);
	for (auto &iter : cats_) {
		iter.Clear();
	}
}

void I18NCategory::Clear() {
	map_.clear();
	missedKeyLog_.clear();
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
		return iter->second.text.c_str();
	} else {
		std::lock_guard<std::mutex> guard(missedKeyLock_);
		if (def)
			missedKeyLog_[key] = def;
		else
			missedKeyLog_[key] = modifiedKey;
		return def ? def : key;
	}
}

void I18NCategory::SetMap(const std::map<std::string, std::string> &m) {
	for (auto iter = m.begin(); iter != m.end(); ++iter) {
		if (map_.find(iter->first) == map_.end()) {
			std::string text = ReplaceAll(iter->second, "\\n", "\n");
			map_[iter->first] = I18NEntry(text);
		}
	}
}

I18NCategory *I18NRepo::GetCategory(I18NCat category) {
	std::lock_guard<std::mutex> guard(catsLock_);
	if (category != I18NCat::NONE)
		return &cats_[(size_t)category];
	else
		return nullptr;
}

I18NCategory *I18NRepo::GetCategoryByName(const char *name) {
	for (auto &iter : cats_) {
		if (!strcmp(iter.GetName(), name)) {
			return &iter;
		}
	}
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

	const std::vector<Section> &sections = ini.Sections();

	std::lock_guard<std::mutex> guard(catsLock_);
	for (auto &iter : sections) {
		I18NCategory *cat = GetCategoryByName(iter.name().c_str());
		if (cat) {
			cat->LoadSection(iter);
		}
	}

	languageID_ = languageID;
	return true;
}

void I18NRepo::LogMissingKeys() const {
	std::lock_guard<std::mutex> guard(catsLock_);
	for (auto &cat : cats_) {
		for (auto &key : cat.Missed()) {
			INFO_LOG(SYSTEM, "Missing translation [%s]: %s (%s)", cat.GetName(), key.first.c_str(), key.second.c_str());
		}
	}
}

void I18NCategory::LoadSection(const Section &section) {
	std::map<std::string, std::string> sectionMap = section.ToMap();
	SetMap(sectionMap);
}
