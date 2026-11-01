#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Common/Data/Text/I18n.h"
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

// What a setting means, for the UI: its name, and which values make sense. Set with the chained
// setters on ConfigSetting. All texts are translation keys in the label's category.
struct ConfigSettingInfo {
	I18NCat cat = I18NCat::NONE;
	const char *label = nullptr;
	const char *labelDefault = nullptr;  // English text, if the key isn't it.

	// Int settings, with the values choiceMin and up.
	const char *const *choices = nullptr;
	int choiceCount = 0;
	int choiceMin = 0;

	// Int and float settings.
	float minValue = 0.0f;
	float maxValue = 0.0f;
	float step = 0.0f;
	const char *units = nullptr;

	// Int settings: texts shown instead of 0 (like "Unlimited") and negative values (like "Disabled").
	const char *zeroLabel = nullptr;
	const char *negativeLabel = nullptr;

	// Called when the UI changes the value, for what has to happen because of the new value.
	void (*onChanged)() = nullptr;

	constexpr bool HasRange() const { return maxValue > minValue; }
	constexpr bool HasChoices() const { return choiceCount > 0; }
};

// Called when a setter is used wrongly. It isn't constexpr, so in a constexpr table that's a compile error.
void ConfigSettingTableError();

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

	// Chained setters for the info. Each returns a modified copy, so they can be used in constexpr tables.
	constexpr ConfigSetting Label(I18NCat cat, const char *label, const char *labelDefault = nullptr) const {
		ConfigSetting s = *this;
		s.info_.cat = cat;
		s.info_.label = label;
		s.info_.labelDefault = labelDefault;
		return s;
	}
	// Int values from minValue and up.
	template <size_t N>
	constexpr ConfigSetting Choices(const char *const (&names)[N], int minValue = 0) const {
		Check(type_ == Type::TYPE_INT && !info_.HasChoices() && !info_.HasRange());
		ConfigSetting s = *this;
		s.info_.choices = names;
		s.info_.choiceCount = (int)N;
		s.info_.choiceMin = minValue;
		return s;
	}
	constexpr ConfigSetting Range(float minValue, float maxValue, float step = 0.0f) const {
		Check((type_ == Type::TYPE_INT || type_ == Type::TYPE_FLOAT) && !info_.HasChoices() && maxValue > minValue);
		ConfigSetting s = *this;
		s.info_.minValue = minValue;
		s.info_.maxValue = maxValue;
		s.info_.step = step;
		return s;
	}
	constexpr ConfigSetting Units(const char *units) const {
		Check(type_ == Type::TYPE_INT || type_ == Type::TYPE_FLOAT);
		ConfigSetting s = *this;
		s.info_.units = units;
		return s;
	}
	constexpr ConfigSetting OnChanged(void (*func)()) const {
		ConfigSetting s = *this;
		s.info_.onChanged = func;
		return s;
	}
	constexpr ConfigSetting ZeroLabel(const char *label) const {
		Check(type_ == Type::TYPE_INT);
		ConfigSetting s = *this;
		s.info_.zeroLabel = label;
		return s;
	}
	constexpr ConfigSetting NegativeLabel(const char *label) const {
		Check(type_ == Type::TYPE_INT);
		ConfigSetting s = *this;
		s.info_.negativeLabel = label;
		return s;
	}

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
	float GetDefaultFloat() const {
		_dbg_assert_(type_ == Type::TYPE_FLOAT);
		return defaultCallback_.f ? defaultCallback_.f() : default_.f;
	}

	constexpr const ConfigSettingInfo &Info() const {
		return info_;
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

	const void *GetVoidPtr(const ConfigBlock *configBlock) const {
		const char *configBlockBase = (const char *)configBlock;
		return (const void *)(configBlockBase + offset_);
	}

private:
	static constexpr void Check(bool ok) {
		if (!ok) {
			ConfigSettingTableError();
		}
	}

	std::string_view iniKey_;
	CfgFlag flags_;
	DefaultValue default_{};
	DefaultCallback defaultCallback_{};
	u32 offset_;

	// We only support transform for ints.
	std::string (*translateTo_)(int) = nullptr;
	int (*translateFrom_)(const std::string &) = nullptr;

	ConfigSettingInfo info_{};
};

// Where a setting lives: the block instance it's in, and the name of that block's ini section.
struct ConfigSettingRef {
	ConfigBlock *block;
	const ConfigSetting *setting;
	std::string_view section;
};
