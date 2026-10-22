#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

struct ConfigBlock;

// Where a setting is within its ConfigBlock, along with its type. Made by the SETTING macro in Config.cpp.
template <class T>
struct ConfigSettingOffset {
	uint32_t offset;
};

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
		constexpr DefaultValue() : i(0) {}
		constexpr DefaultValue(bool v) : b(v) {}
		constexpr DefaultValue(int v) : i(v) {}
		constexpr DefaultValue(uint32_t v) : u(v) {}
		constexpr DefaultValue(uint64_t v) : lu(v) {}
		constexpr DefaultValue(float v) : f(v) {}
		constexpr DefaultValue(const char *v) : s(v) {}  // Strings and paths.
		constexpr DefaultValue(const std::vector<std::string_view> *v) : v(v) {}
		constexpr DefaultValue(ConfigTouchPos v) : touchPos(v) {}
		constexpr DefaultValue(ConfigCustomButton v) : customButton(v) {}

		bool b;
		int i;
		uint32_t u;
		uint64_t lu;
		float f;
		const char *s;
		const std::vector<std::string_view> *v;
		ConfigTouchPos touchPos;
		ConfigCustomButton customButton;
	};

	typedef bool (*BoolDefaultCallback)();
	typedef int (*IntDefaultCallback)();
	typedef uint32_t (*Uint32DefaultCallback)();
	typedef uint64_t (*Uint64DefaultCallback)();
	typedef float (*FloatDefaultCallback)();
	typedef std::string (*StringDefaultCallback)();
	typedef ConfigTouchPos (*TouchPosDefaultCallback)();
	typedef Path (*PathDefaultCallback)();
	typedef ConfigCustomButton (*CustomButtonDefaultCallback)();

	union DefaultCallback {
		constexpr DefaultCallback() : b(nullptr) {}
		constexpr DefaultCallback(BoolDefaultCallback cb) : b(cb) {}
		constexpr DefaultCallback(IntDefaultCallback cb) : i(cb) {}
		constexpr DefaultCallback(Uint32DefaultCallback cb) : u(cb) {}
		constexpr DefaultCallback(Uint64DefaultCallback cb) : lu(cb) {}
		constexpr DefaultCallback(FloatDefaultCallback cb) : f(cb) {}
		constexpr DefaultCallback(StringDefaultCallback cb) : s(cb) {}
		constexpr DefaultCallback(PathDefaultCallback cb) : p(cb) {}
		constexpr DefaultCallback(TouchPosDefaultCallback cb) : touchPos(cb) {}
		constexpr DefaultCallback(CustomButtonDefaultCallback cb) : customButton(cb) {}

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

	// The offset says where the setting is within its ConfigBlock, so it can be applied to any block of that type.
	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<bool> v, bool def, CfgFlag flags) noexcept
		: type_(Type::TYPE_BOOL), iniKey_(ini), flags_(flags), default_(def), offset_(v.offset) {}
	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<bool> v, CfgFlag flags) noexcept
		: type_(Type::TYPE_BOOL), iniKey_(ini), flags_(flags), default_(false), offset_(v.offset) {}  // The default is unused with this constructor.
	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<int> v, int def, CfgFlag flags) noexcept
		: type_(Type::TYPE_INT), iniKey_(ini), flags_(flags), default_(def), offset_(v.offset) {}
	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<int> v, CfgFlag flags) noexcept
		: type_(Type::TYPE_INT), iniKey_(ini), flags_(flags), default_(0), offset_(v.offset) {}  // The default is unused with this constructor.
	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<int> v, int def, std::string (*transTo)(int), int (*transFrom)(const std::string &), CfgFlag flags) noexcept
		: type_(Type::TYPE_INT), iniKey_(ini), flags_(flags), default_(def), offset_(v.offset), translateTo_(transTo), translateFrom_(transFrom) {}
	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<uint32_t> v, uint32_t def, CfgFlag flags) noexcept
		: type_(Type::TYPE_UINT32), iniKey_(ini), flags_(flags), default_(def), offset_(v.offset) {}
	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<uint64_t> v, uint64_t def, CfgFlag flags) noexcept
		: type_(Type::TYPE_UINT64), iniKey_(ini), flags_(flags), default_(def), offset_(v.offset) {}
	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<float> v, float def, CfgFlag flags) noexcept
		: type_(Type::TYPE_FLOAT), iniKey_(ini), flags_(flags), default_(def), offset_(v.offset) {}
	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<float> v, CfgFlag flags) noexcept
		: type_(Type::TYPE_FLOAT), iniKey_(ini), flags_(flags), default_(0.0f), offset_(v.offset) {}
	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<std::string> v, const char *def, CfgFlag flags) noexcept
		: type_(Type::TYPE_STRING), iniKey_(ini), flags_(flags), default_(def), offset_(v.offset) {}
	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<std::vector<std::string>> v, const std::vector<std::string_view> *def, CfgFlag flags) noexcept
		: type_(Type::TYPE_STRING_VECTOR), iniKey_(ini), flags_(flags), default_(def), offset_(v.offset) {}
	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<Path> v, const char *def, CfgFlag flags) noexcept
		: type_(Type::TYPE_PATH), iniKey_(ini), flags_(flags), default_(def), offset_(v.offset) {}
	constexpr ConfigSetting(const char *iniX, const char *iniY, const char *iniScale, const char *iniShow, ConfigSettingOffset<ConfigTouchPos> v, ConfigTouchPos def, CfgFlag flags) noexcept
		: ini2_(iniY), ini3_(iniScale), ini4_(iniShow), type_(Type::TYPE_TOUCH_POS), iniKey_(iniX), flags_(flags), default_(def), offset_(v.offset) {}
	constexpr ConfigSetting(const char *iniKey, const char *iniImage, const char *iniShape, const char *iniToggle, const char *iniRepeat, ConfigSettingOffset<ConfigCustomButton> v, ConfigCustomButton def, CfgFlag flags) noexcept
		: ini2_(iniImage), ini3_(iniShape), ini4_(iniToggle), ini5_(iniRepeat), type_(Type::TYPE_CUSTOM_BUTTON), iniKey_(iniKey), flags_(flags), default_(def), offset_(v.offset) {}

	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<bool> v, BoolDefaultCallback def, CfgFlag flags) noexcept
		: type_(Type::TYPE_BOOL), iniKey_(ini), flags_(flags), defaultCallback_(def), offset_(v.offset) {}
	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<int> v, IntDefaultCallback def, CfgFlag flags) noexcept
		: type_(Type::TYPE_INT), iniKey_(ini), flags_(flags), defaultCallback_(def), offset_(v.offset) {}
	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<int> v, IntDefaultCallback def, std::string(*transTo)(int), int(*transFrom)(const std::string &), CfgFlag flags) noexcept
		: type_(Type::TYPE_INT), iniKey_(ini), flags_(flags), defaultCallback_(def), offset_(v.offset), translateTo_(transTo), translateFrom_(transFrom) {}
	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<uint32_t> v, Uint32DefaultCallback def, CfgFlag flags) noexcept
		: type_(Type::TYPE_UINT32), iniKey_(ini), flags_(flags), defaultCallback_(def), offset_(v.offset) {}
	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<float> v, FloatDefaultCallback def, CfgFlag flags) noexcept
		: type_(Type::TYPE_FLOAT), iniKey_(ini), flags_(flags), defaultCallback_(def), offset_(v.offset) {}
	constexpr ConfigSetting(std::string_view ini, ConfigSettingOffset<std::string> v, StringDefaultCallback def, CfgFlag flags) noexcept
		: type_(Type::TYPE_STRING), iniKey_(ini), flags_(flags), defaultCallback_(def), offset_(v.offset) {}
	constexpr ConfigSetting(std::string_view iniX, const char *iniY, const char *iniScale, const char *iniShow, ConfigSettingOffset<ConfigTouchPos> v, TouchPosDefaultCallback def, CfgFlag flags) noexcept
		: ini2_(iniY), ini3_(iniScale), ini4_(iniShow), type_(Type::TYPE_TOUCH_POS), iniKey_(iniX), flags_(flags), defaultCallback_(def), offset_(v.offset) {}

	bool ReadFromIniSection(ConfigBlock *configBlock, const Section *section, bool applyDefaultIfMissing) const;

	// Yes, this can be const because what's modified is not the ConfigSetting struct, but the value which is stored elsewhere.
	// Should actually be called WriteToIni or something.
	void WriteToIniSection(const ConfigBlock *configBlock, Section *section) const;

	// If log is true, logs if the setting changed.
	bool RestoreToDefault(ConfigBlock *configBlock, bool log) const;

	void ReportSetting(const ConfigBlock *configBlock, UrlEncoder &data, const std::string &prefix) const;

	// Easy flag accessors.
	CfgFlag Flags() const { return flags_; }
	bool PerGame() const { return flags_ & CfgFlag::PER_GAME; }
	bool SaveSetting() const { return !(flags_ & CfgFlag::DONT_SAVE); }
	bool Report() const { return flags_ & CfgFlag::REPORT; }

	int GetDefaultInt() const {
		_dbg_assert_(type_ == Type::TYPE_INT);
		return defaultCallback_.i ? defaultCallback_.i() : default_.i;
	}

	std::string_view IniKey() const {
		return iniKey_;
	}

	const char *ini2_ = nullptr;
	const char *ini3_ = nullptr;
	const char *ini4_ = nullptr;
	const char *ini5_ = nullptr;

	const Type type_;

	// Returns false if per-game settings are not currently used
	static bool PerGame(void *ptr);

	const void *GetVoidPtr(ConfigBlock *configBlock) const {
		char *configBlockBase = (char *)configBlock;
		return (const void *)(configBlockBase + offset_);
	}

private:
	std::string_view iniKey_;
	CfgFlag flags_;
	DefaultValue default_{};
	DefaultCallback defaultCallback_{};
	u32 offset_;

	// We only support transform for ints.
	std::string (*translateTo_)(int) = nullptr;
	int (*translateFrom_)(const std::string &) = nullptr;
};
