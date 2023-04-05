#pragma once

#include <cstdint>

#include "Core/ConfigValues.h"

class Path;
class Section;  // ini file section
struct UrlEncoder;

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

	ConfigSetting(const char *ini, bool *v, bool def, bool save, bool perGame)
		: iniKey_(ini), type_(TYPE_BOOL), report_(false), save_(save), perGame_(perGame) {
		ptr_.b = v;
		cb_.b = nullptr;
		default_.b = def;
	}

	ConfigSetting(const char *ini, int *v, int def, bool save, bool perGame)
		: iniKey_(ini), type_(TYPE_INT), report_(false), save_(save), perGame_(perGame) {
		ptr_.i = v;
		cb_.i = nullptr;
		default_.i = def;
	}

	ConfigSetting(const char *ini, int *v, int def, std::string(*transTo)(int), int (*transFrom)(const std::string &), bool save, bool perGame)
		: iniKey_(ini), type_(TYPE_INT), report_(false), save_(save), perGame_(perGame), translateTo_(transTo), translateFrom_(transFrom) {
		ptr_.i = v;
		cb_.i = nullptr;
		default_.i = def;
	}

	ConfigSetting(const char *ini, uint32_t *v, uint32_t def, bool save, bool perGame)
		: iniKey_(ini), type_(TYPE_UINT32), report_(false), save_(save), perGame_(perGame) {
		ptr_.u = v;
		cb_.u = nullptr;
		default_.u = def;
	}

	ConfigSetting(const char *ini, uint64_t *v, uint64_t def, bool save, bool perGame)
		: iniKey_(ini), type_(TYPE_UINT64), report_(false), save_(save), perGame_(perGame) {
		ptr_.lu = v;
		cb_.lu = nullptr;
		default_.lu = def;
	}

	ConfigSetting(const char *ini, float *v, float def, bool save, bool perGame)
		: iniKey_(ini), type_(TYPE_FLOAT), report_(false), save_(save), perGame_(perGame) {
		ptr_.f = v;
		cb_.f = nullptr;
		default_.f = def;
	}

	ConfigSetting(const char *ini, std::string *v, const char *def, bool save, bool perGame)
		: iniKey_(ini), type_(TYPE_STRING), report_(false), save_(save), perGame_(perGame) {
		ptr_.s = v;
		cb_.s = nullptr;
		default_.s = def;
	}

	ConfigSetting(const char *ini, Path *p, const char *def, bool save, bool perGame)
		: iniKey_(ini), type_(TYPE_PATH), report_(false), save_(save), perGame_(perGame) {
		ptr_.p = p;
		cb_.p = nullptr;
		default_.p = def;
	}

	ConfigSetting(const char *iniX, const char *iniY, const char *iniScale, const char *iniShow, ConfigTouchPos *v, ConfigTouchPos def, bool save, bool perGame)
		: iniKey_(iniX), ini2_(iniY), ini3_(iniScale), ini4_(iniShow), type_(TYPE_TOUCH_POS), report_(false), save_(save), perGame_(perGame) {
		ptr_.touchPos = v;
		cb_.touchPos = nullptr;
		default_.touchPos = def;
	}

	ConfigSetting(const char *iniKey, const char *iniImage, const char *iniShape, const char *iniToggle, const char *iniRepeat, ConfigCustomButton *v, ConfigCustomButton def, bool save, bool perGame)
		: iniKey_(iniKey), ini2_(iniImage), ini3_(iniShape), ini4_(iniToggle), ini5_(iniRepeat), type_(TYPE_CUSTOM_BUTTON), report_(false), save_(save), perGame_(perGame) {
		ptr_.customButton = v;
		cb_.customButton = nullptr;
		default_.customButton = def;
	}

	ConfigSetting(const char *ini, bool *v, BoolDefaultCallback def, bool save, bool perGame)
		: iniKey_(ini), type_(TYPE_BOOL), report_(false), save_(save), perGame_(perGame) {
		ptr_.b = v;
		cb_.b = def;
	}

	ConfigSetting(const char *ini, int *v, IntDefaultCallback def, bool save, bool perGame)
		: iniKey_(ini), type_(TYPE_INT), report_(false), save_(save), perGame_(perGame) {
		ptr_.i = v;
		cb_.i = def;
	}

	ConfigSetting(const char *ini, int *v, IntDefaultCallback def, std::string(*transTo)(int), int(*transFrom)(const std::string &), bool save, bool perGame)
		: iniKey_(ini), type_(TYPE_INT), report_(false), save_(save), perGame_(perGame), translateTo_(transTo), translateFrom_(transFrom) {
		ptr_.i = v;
		cb_.i = def;
	}

	ConfigSetting(const char *ini, uint32_t *v, Uint32DefaultCallback def, bool save, bool perGame)
		: iniKey_(ini), type_(TYPE_UINT32), report_(false), save_(save), perGame_(perGame) {
		ptr_.u = v;
		cb_.u = def;
	}

	ConfigSetting(const char *ini, float *v, FloatDefaultCallback def, bool save, bool perGame)
		: iniKey_(ini), type_(TYPE_FLOAT), report_(false), save_(save), perGame_(perGame) {
		ptr_.f = v;
		cb_.f = def;
	}

	ConfigSetting(const char *ini, std::string *v, StringDefaultCallback def, bool save, bool perGame)
		: iniKey_(ini), type_(TYPE_STRING), report_(false), save_(save), perGame_(perGame) {
		ptr_.s = v;
		cb_.s = def;
	}

	ConfigSetting(const char *iniX, const char *iniY, const char *iniScale, const char *iniShow, ConfigTouchPos *v, TouchPosDefaultCallback def, bool save, bool perGame)
		: iniKey_(iniX), ini2_(iniY), ini3_(iniScale), ini4_(iniShow), type_(TYPE_TOUCH_POS), report_(false), save_(save), perGame_(perGame) {
		ptr_.touchPos = v;
		cb_.touchPos = def;
	}

	// Should actually be called ReadFromIni or something.
	bool Get(const Section *section) const;

	// Yes, this can be const because what's modified is not the ConfigSetting struct, but the value which is stored elsewhere.
	// Should actually be called WriteToIni or something.
	void Set(Section *section) const;

	void RestoreToDefault() const;

	void Report(UrlEncoder &data, const std::string &prefix) const;

	const char *iniKey_ = nullptr;
	const char *ini2_ = nullptr;
	const char *ini3_ = nullptr;
	const char *ini4_ = nullptr;
	const char *ini5_ = nullptr;

	const Type type_;

	bool report_;
	const bool save_;
	const bool perGame_;
	SettingPtr ptr_;
	DefaultValue default_{};
	DefaultCallback cb_;

	// We only support transform for ints.
	std::string(*translateTo_)(int) = nullptr;
	int(*translateFrom_)(const std::string &) = nullptr;
};

struct ReportedConfigSetting : public ConfigSetting {
	template <typename T1, typename T2>
	ReportedConfigSetting(const char *ini, T1 *v, T2 def, bool save, bool perGame)
		: ConfigSetting(ini, v, def, save, perGame) {
		report_ = true;
	}

	template <typename T1, typename T2>
	ReportedConfigSetting(const char *ini, T1 *v, T2 def, std::string(*transTo)(int), int(*transFrom)(const std::string &), bool save, bool perGame)
		: ConfigSetting(ini, v, def, transTo, transFrom, save, perGame) {
		report_ = true;
	}
};

struct ConfigSectionSettings {
	const char *section;
	const ConfigSetting *settings;
	size_t settingsCount;
};
