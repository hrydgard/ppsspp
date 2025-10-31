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
	enum class Type {
		TYPE_TERMINATOR,
		TYPE_BOOL,
		TYPE_INT,
		TYPE_UINT32,
		TYPE_UINT64,
		TYPE_FLOAT,
		TYPE_STRING,
		TYPE_STRING_VECTOR,
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
		const std::vector<std::string> *v;
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
		std::vector<std::string> *v;
		Path *p;
		ConfigTouchPos *touchPos;
		ConfigCustomButton *customButton;
	};

	typedef bool (*BoolDefaultCallback)();
	typedef int (*IntDefaultCallback)();
	typedef uint32_t (*Uint32DefaultCallback)();
	typedef uint64_t (*Uint64DefaultCallback)();
	typedef float (*FloatDefaultCallback)();
	typedef std::string (*StringDefaultCallback)();
	typedef ConfigTouchPos (*TouchPosDefaultCallback)();
	typedef const char *(*PathDefaultCallback)();
	typedef ConfigCustomButton (*CustomButtonDefaultCallback)();

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

	ConfigSetting(std::string_view ini, const char *owner, bool *v, bool def, CfgFlag flags) noexcept
		: iniKey_(ini), type_(Type::TYPE_BOOL), flags_(flags), offset_((const char *)v - owner) {
		cb_.b = nullptr;
		default_.b = def;
	}

	ConfigSetting(std::string_view ini, const char *owner, int *v, int def, CfgFlag flags) noexcept
		: iniKey_(ini), type_(Type::TYPE_INT), flags_(flags), offset_((const char *)v - owner) {
		cb_.i = nullptr;
		default_.i = def;
	}

	ConfigSetting(std::string_view ini, const char *owner, int *v, int def, std::string (*transTo)(int), int (*transFrom)(const std::string &), CfgFlag flags) noexcept
		: iniKey_(ini), type_(Type::TYPE_INT), flags_(flags), translateTo_(transTo), translateFrom_(transFrom), offset_((const char *)v - owner) {
		cb_.i = nullptr;
		default_.i = def;
	}

	ConfigSetting(std::string_view ini, const char *owner, uint32_t *v, uint32_t def, CfgFlag flags) noexcept
		: iniKey_(ini), type_(Type::TYPE_UINT32), flags_(flags), offset_((const char *)v - owner) {
		cb_.u = nullptr;
		default_.u = def;
	}

	ConfigSetting(std::string_view ini, const char *owner, uint64_t *v, uint64_t def, CfgFlag flags) noexcept
		: iniKey_(ini), type_(Type::TYPE_UINT64), flags_(flags), offset_((const char *)v - owner) {
		cb_.lu = nullptr;
		default_.lu = def;
	}

	ConfigSetting(std::string_view ini, const char *owner, float *v, float def, CfgFlag flags) noexcept
		: iniKey_(ini), type_(Type::TYPE_FLOAT), flags_(flags), offset_((const char *)v - owner) {
		cb_.f = nullptr;
		default_.f = def;
	}

	ConfigSetting(std::string_view ini, const char *owner, std::string *v, const char *def, CfgFlag flags) noexcept
		: iniKey_(ini), type_(Type::TYPE_STRING), flags_(flags), offset_((const char *)v - owner) {
		cb_.s = nullptr;
		default_.s = def;
	}

	ConfigSetting(std::string_view ini, const char *owner, std::vector<std::string> *v, const std::vector<std::string> *def, CfgFlag flags) noexcept
		: iniKey_(ini), type_(Type::TYPE_STRING_VECTOR), flags_(flags), offset_((const char *)v - owner) {
		default_.v = def;
	}

	ConfigSetting(std::string_view ini, const char *owner, Path *v, const char *def, CfgFlag flags) noexcept
		: iniKey_(ini), type_(Type::TYPE_PATH), flags_(flags), offset_((const char *)v - owner) {
		cb_.p = nullptr;
		default_.p = def;
	}

	ConfigSetting(const char *iniX, const char *iniY, const char *iniScale, const char *iniShow, const char *owner, ConfigTouchPos *v, ConfigTouchPos def, CfgFlag flags) noexcept
		: iniKey_(iniX), ini2_(iniY), ini3_(iniScale), ini4_(iniShow), type_(Type::TYPE_TOUCH_POS), flags_(flags), offset_((const char *)v - owner) {
		cb_.touchPos = nullptr;
		default_.touchPos = def;
	}

	ConfigSetting(const char *iniKey, const char *iniImage, const char *iniShape, const char *iniToggle, const char *iniRepeat, const char *owner, ConfigCustomButton *v, ConfigCustomButton def, CfgFlag flags) noexcept
		: iniKey_(iniKey), ini2_(iniImage), ini3_(iniShape), ini4_(iniToggle), ini5_(iniRepeat), type_(Type::TYPE_CUSTOM_BUTTON), flags_(flags), offset_((const char *)v - owner) {
		cb_.customButton = nullptr;
		default_.customButton = def;
	}

	ConfigSetting(std::string_view ini, const char *owner, bool *v, BoolDefaultCallback def, CfgFlag flags) noexcept
		: iniKey_(ini), type_(Type::TYPE_BOOL), flags_(flags), offset_((const char *)v - owner) {
		cb_.b = def;
	}

	ConfigSetting(std::string_view ini, const char *owner, int *v, IntDefaultCallback def, CfgFlag flags) noexcept
		: iniKey_(ini), type_(Type::TYPE_INT), flags_(flags), offset_((const char *)v - owner) {
		cb_.i = def;
	}

	ConfigSetting(std::string_view ini, const char *owner, int *v, IntDefaultCallback def, std::string(*transTo)(int), int(*transFrom)(const std::string &), CfgFlag flags) noexcept
		: iniKey_(ini), type_(Type::TYPE_INT), flags_(flags), offset_((const char *)v - owner), translateTo_(transTo), translateFrom_(transFrom) {
		cb_.i = def;
	}

	ConfigSetting(std::string_view ini, const char *owner, uint32_t *v, Uint32DefaultCallback def, CfgFlag flags) noexcept
		: iniKey_(ini), type_(Type::TYPE_UINT32), flags_(flags), offset_((const char *)v - owner) {
		cb_.u = def;
	}

	ConfigSetting(std::string_view ini, const char *owner, float *v, FloatDefaultCallback def, CfgFlag flags) noexcept
		: iniKey_(ini), type_(Type::TYPE_FLOAT), flags_(flags), offset_((const char *)v - owner) {
		cb_.f = def;
	}

	ConfigSetting(std::string_view ini, const char *owner, std::string *v, StringDefaultCallback def, CfgFlag flags) noexcept
		: iniKey_(ini), type_(Type::TYPE_STRING), flags_(flags), offset_((const char *)v - owner) {
		cb_.s = def;
	}

	ConfigSetting(std::string_view iniX, const char *iniY, const char *iniScale, const char *iniShow, const char *owner, ConfigTouchPos *v, TouchPosDefaultCallback def, CfgFlag flags) noexcept
		: iniKey_(iniX), ini2_(iniY), ini3_(iniScale), ini4_(iniShow), type_(Type::TYPE_TOUCH_POS), flags_(flags), offset_((const char *)v - owner) {
		cb_.touchPos = def;
	}

	bool ReadFromIniSection(char *owner, const Section *section) const;

	// Yes, this can be const because what's modified is not the ConfigSetting struct, but the value which is stored elsewhere.
	// Should actually be called WriteToIni or something.
	void WriteToIniSection(const char *owner, Section *section) const;

	// If log is true, logs if the setting changed.
	bool RestoreToDefault(const char *owner, bool log) const;

	void ReportSetting(const char *owner, UrlEncoder &data, const std::string &prefix) const;

	// Easy flag accessors.
	bool PerGame() const { return flags_ & CfgFlag::PER_GAME; }
	bool SaveSetting() const { return !(flags_ & CfgFlag::DONT_SAVE); }
	bool Report() const { return flags_ & CfgFlag::REPORT; }

	std::string_view iniKey_;
	const char *ini2_ = nullptr;
	const char *ini3_ = nullptr;
	const char *ini4_ = nullptr;
	const char *ini5_ = nullptr;

	const Type type_;

	// Returns false if per-game settings are not currently used
	static bool perGame(void *ptr);

	const void *GetVoidPtr(const char *owner) const {
		// undefined behavior but in reality will work.
		return (const void *)(owner + offset_);
	}

private:
	CfgFlag flags_;
	DefaultValue default_{};
	DefaultCallback cb_{};
	u32 offset_;

	// We only support transform for ints.
	std::string (*translateTo_)(int) = nullptr;
	int (*translateFrom_)(const std::string &) = nullptr;
};

struct ConfigSectionSettings {
	char *owner;
	const char *section;
	const ConfigSetting *settings;
	size_t settingsCount;
};
