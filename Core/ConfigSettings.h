#pragma once

#include <cstdint>
#include <unordered_map>
#include "Core/ConfigValues.h"

class Path;
class Section;  // ini file section
struct UrlEncoder;

enum class CfgFlag : u8 {
	DEFAULT = 0,
	DONT_SAVE = 1,  // normally don't like negative flags, but these are really not many.
	PER_GAME = 2,
	REPORT = 4,
};
ENUM_CLASS_BITOPS(CfgFlag);

struct ConfigSetting {
	enum Type {
		TYPE_TERMINATOR,
		TYPE_BOOL,
		TYPE_INT,
		TYPE_UINT32,
		TYPE_UINT64,
		TYPE_FLOAT,
		TYPE_STRING,
		TYPE_TOUCH_POS,
		TYPE_PATH,
		TYPE_CUSTOM_BUTTON
	};
	union DefaultValue {
		bool b;
		int i;
		uint32_t u;
		uint64_t lu;
		float f;
		const char *s;
		const char *p;  // not sure how much point..
		ConfigTouchPos touchPos;
		ConfigCustomButton customButton;
	};
	union SettingPtr {
		bool *b;
		int *i;
		uint32_t *u;
		uint64_t *lu;
		float *f;
		std::string *s;
		Path *p;
		ConfigTouchPos *touchPos;
		ConfigCustomButton *customButton;
	};

	typedef bool (*BoolDefaultCallback)();
	typedef int (*IntDefaultCallback)();
	typedef uint32_t(*Uint32DefaultCallback)();
	typedef uint64_t(*Uint64DefaultCallback)();
	typedef float (*FloatDefaultCallback)();
	typedef const char *(*StringDefaultCallback)();
	typedef ConfigTouchPos(*TouchPosDefaultCallback)();
	typedef const char *(*PathDefaultCallback)();
	typedef ConfigCustomButton(*CustomButtonDefaultCallback)();

	union DefaultCallback {
		BoolDefaultCallback b;
		IntDefaultCallback i;
		Uint32DefaultCallback u;
		Uint64DefaultCallback lu;
		FloatDefaultCallback f;
		StringDefaultCallback s;
		PathDefaultCallback p;
		TouchPosDefaultCallback touchPos;
		CustomButtonDefaultCallback customButton;
	};

	ConfigSetting(const char *ini, bool *v, bool def, CfgFlag flags = CfgFlag::DEFAULT)
		: iniKey_(ini), type_(TYPE_BOOL), flags_(flags) {
		ptr_.b = v;
		cb_.b = nullptr;
		default_.b = def;
		getPtrLUT()[v] = this;
	}

	ConfigSetting(const char *ini, int *v, int def, CfgFlag flags = CfgFlag::DEFAULT)
		: iniKey_(ini), type_(TYPE_INT), flags_(flags) {
		ptr_.i = v;
		cb_.i = nullptr;
		default_.i = def;
		getPtrLUT()[v] = this;
	}

	ConfigSetting(const char *ini, int *v, int def, std::string(*transTo)(int), int (*transFrom)(const std::string &), CfgFlag flags = CfgFlag::DEFAULT)
		: iniKey_(ini), type_(TYPE_INT), flags_(flags), translateTo_(transTo), translateFrom_(transFrom) {
		ptr_.i = v;
		cb_.i = nullptr;
		default_.i = def;
		getPtrLUT()[v] = this;
	}

	ConfigSetting(const char *ini, uint32_t *v, uint32_t def, CfgFlag flags = CfgFlag::DEFAULT)
		: iniKey_(ini), type_(TYPE_UINT32), flags_(flags) {
		ptr_.u = v;
		cb_.u = nullptr;
		default_.u = def;
		getPtrLUT()[v] = this;
	}

	ConfigSetting(const char *ini, uint64_t *v, uint64_t def, CfgFlag flags = CfgFlag::DEFAULT)
		: iniKey_(ini), type_(TYPE_UINT64), flags_(flags) {
		ptr_.lu = v;
		cb_.lu = nullptr;
		default_.lu = def;
		getPtrLUT()[v] = this;
	}

	ConfigSetting(const char *ini, float *v, float def, CfgFlag flags = CfgFlag::DEFAULT)
		: iniKey_(ini), type_(TYPE_FLOAT), flags_(flags) {
		ptr_.f = v;
		cb_.f = nullptr;
		default_.f = def;
		getPtrLUT()[v] = this;
	}

	ConfigSetting(const char *ini, std::string *v, const char *def, CfgFlag flags = CfgFlag::DEFAULT)
		: iniKey_(ini), type_(TYPE_STRING), flags_(flags) {
		ptr_.s = v;
		cb_.s = nullptr;
		default_.s = def;
		getPtrLUT()[v] = this;
	}

	ConfigSetting(const char *ini, Path *v, const char *def, CfgFlag flags = CfgFlag::DEFAULT)
		: iniKey_(ini), type_(TYPE_PATH), flags_(flags) {
		ptr_.p = v;
		cb_.p = nullptr;
		default_.p = def;
		getPtrLUT()[v] = this;
	}

	ConfigSetting(const char *iniX, const char *iniY, const char *iniScale, const char *iniShow, ConfigTouchPos *v, ConfigTouchPos def, CfgFlag flags = CfgFlag::DEFAULT)
		: iniKey_(iniX), ini2_(iniY), ini3_(iniScale), ini4_(iniShow), type_(TYPE_TOUCH_POS), flags_(flags) {
		ptr_.touchPos = v;
		cb_.touchPos = nullptr;
		default_.touchPos = def;
		getPtrLUT()[v] = this;
	}

	ConfigSetting(const char *iniKey, const char *iniImage, const char *iniShape, const char *iniToggle, const char *iniRepeat, ConfigCustomButton *v, ConfigCustomButton def, CfgFlag flags = CfgFlag::DEFAULT)
		: iniKey_(iniKey), ini2_(iniImage), ini3_(iniShape), ini4_(iniToggle), ini5_(iniRepeat), type_(TYPE_CUSTOM_BUTTON), flags_(flags) {
		ptr_.customButton = v;
		cb_.customButton = nullptr;
		default_.customButton = def;
		getPtrLUT()[v] = this;
	}

	ConfigSetting(const char *ini, bool *v, BoolDefaultCallback def, CfgFlag flags = CfgFlag::DEFAULT)
		: iniKey_(ini), type_(TYPE_BOOL), flags_(flags) {
		ptr_.b = v;
		cb_.b = def;
		getPtrLUT()[v] = this;
	}

	ConfigSetting(const char *ini, int *v, IntDefaultCallback def, CfgFlag flags = CfgFlag::DEFAULT)
		: iniKey_(ini), type_(TYPE_INT), flags_(flags) {
		ptr_.i = v;
		cb_.i = def;
		getPtrLUT()[v] = this;
	}

	ConfigSetting(const char *ini, int *v, IntDefaultCallback def, std::string(*transTo)(int), int(*transFrom)(const std::string &), CfgFlag flags = CfgFlag::DEFAULT)
		: iniKey_(ini), type_(TYPE_INT), flags_(flags), translateTo_(transTo), translateFrom_(transFrom) {
		ptr_.i = v;
		cb_.i = def;
		getPtrLUT()[v] = this;
	}

	ConfigSetting(const char *ini, uint32_t *v, Uint32DefaultCallback def, CfgFlag flags = CfgFlag::DEFAULT)
		: iniKey_(ini), type_(TYPE_UINT32), flags_(flags) {
		ptr_.u = v;
		cb_.u = def;
		getPtrLUT()[v] = this;
	}

	ConfigSetting(const char *ini, float *v, FloatDefaultCallback def, CfgFlag flags = CfgFlag::DEFAULT)
		: iniKey_(ini), type_(TYPE_FLOAT), flags_(flags) {
		ptr_.f = v;
		cb_.f = def;
		getPtrLUT()[v] = this;
	}

	ConfigSetting(const char *ini, std::string *v, StringDefaultCallback def, CfgFlag flags = CfgFlag::DEFAULT)
		: iniKey_(ini), type_(TYPE_STRING), flags_(flags) {
		ptr_.s = v;
		cb_.s = def;
		getPtrLUT()[v] = this;
	}

	ConfigSetting(const char *iniX, const char *iniY, const char *iniScale, const char *iniShow, ConfigTouchPos *v, TouchPosDefaultCallback def, CfgFlag flags = CfgFlag::DEFAULT)
		: iniKey_(iniX), ini2_(iniY), ini3_(iniScale), ini4_(iniShow), type_(TYPE_TOUCH_POS), flags_(flags) {
		ptr_.touchPos = v;
		cb_.touchPos = def;
		getPtrLUT()[v] = this;
	}

	// Should actually be called ReadFromIni or something.
	bool Get(const Section *section) const;

	// Yes, this can be const because what's modified is not the ConfigSetting struct, but the value which is stored elsewhere.
	// Should actually be called WriteToIni or something.
	void Set(Section *section) const;

	void RestoreToDefault() const;

	void ReportSetting(UrlEncoder &data, const std::string &prefix) const;

	// Easy flag accessors.
	bool PerGame() const { return flags_ & CfgFlag::PER_GAME; }
	bool SaveSetting() const { return !(flags_ & CfgFlag::DONT_SAVE); }
	bool Report() const { return flags_ & CfgFlag::REPORT; }

	const char *iniKey_ = nullptr;
	const char *ini2_ = nullptr;
	const char *ini3_ = nullptr;
	const char *ini4_ = nullptr;
	const char *ini5_ = nullptr;

	const Type type_;

	// Returns false if per-game settings are not currently used
	static bool perGame(void *ptr);

private:
	CfgFlag flags_;
	SettingPtr ptr_;
	DefaultValue default_{};
	DefaultCallback cb_;

	// We only support transform for ints.
	std::string (*translateTo_)(int) = nullptr;
	int (*translateFrom_)(const std::string &) = nullptr;

	static std::unordered_map<void*, ConfigSetting*>& getPtrLUT();
};

struct ConfigSectionSettings {
	const char *section;
	const ConfigSetting *settings;
	size_t settingsCount;
};
