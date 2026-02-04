#include <cstdint>
#include "Common/Data/Format/IniFile.h"
#include "Common/Net/URL.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"

#include "Core/ConfigSettings.h"
#include "Core/ConfigValues.h"
#include "Core/Config.h"

bool ConfigSetting::PerGame(void *ptr) {
	return g_Config.IsGameSpecific() && g_Config.getPtrLUT().count(ptr) > 0 && g_Config.getPtrLUT()[ptr].second->PerGame();
}

bool ConfigSetting::ReadFromIniSection(ConfigBlock *configBlock, const Section *section, bool applyDefaultIfMissing) const {
	char *owner = (char *)configBlock;
	_dbg_assert_msg_(offset_ >= 0 && offset_ < configBlock->Size(), "offset: %d size: %d", (int)offset_, (int)configBlock->Size());

	switch (type_) {
	case Type::TYPE_BOOL:
	{
		bool *target = (bool *)(owner + offset_);
		if (!section || !section->Get(iniKey_, target)) {
			if (applyDefaultIfMissing) {
				*target = defaultCallback_.b ? defaultCallback_.b() : default_.b;
			}
			return false;
		}
		return true;
	}
	case Type::TYPE_INT:
	{
		int *target = (int *)(owner + offset_);
		if (translateFrom_ && section) {
			std::string value;
			if (section->Get(iniKey_, &value)) {
				*((int *)(owner + offset_)) = translateFrom_(value);
				return true;
			}
		}
		if (!section || !section->Get(iniKey_, target)) {
			if (applyDefaultIfMissing) {
				*target = defaultCallback_.i ? defaultCallback_.i() : default_.i;
			}
			return false;
		}
		return true;
	}
	case Type::TYPE_UINT32:
	{
		uint32_t *target = (uint32_t *)(owner + offset_);
		if (!section || !section->Get(iniKey_, target)) {
			if (applyDefaultIfMissing) {
				*target = defaultCallback_.u ? defaultCallback_.u() : default_.u;
			}
			return false;
		}
		return true;
	}
	case Type::TYPE_UINT64:
	{
		uint64_t *target = (uint64_t *)(owner + offset_);
		if (!section || !section->Get(iniKey_, target)) {
			if (applyDefaultIfMissing) {
				*target = defaultCallback_.lu ? defaultCallback_.lu() : default_.lu;
			}
			return false;
		}
		return true;
	}
	case Type::TYPE_FLOAT:
	{
		float *target = (float *)(owner + offset_);
		if (!section || !section->Get(iniKey_, target)) {
			if (applyDefaultIfMissing) {
				*target = defaultCallback_.f ? defaultCallback_.f() : default_.f;
			}
			return false;
		}
		return true;
	}
	case Type::TYPE_STRING:
	{
		std::string *target = (std::string *)(owner + offset_);
		if (!section || !section->Get(iniKey_, target)) {
			if (applyDefaultIfMissing) {
				if (defaultCallback_.s) {
					*target = defaultCallback_.s();
				} else if (default_.s) {
					*target = default_.s;
				} else {
					target->clear();
				}
			}
			return false;
		}
		return true;
	}
	case Type::TYPE_STRING_VECTOR:
	{
		// No support for callbacks for these yet. that's not an issue.
		std::vector<std::string> *ptr = (std::vector<std::string> *)(owner + offset_);
		if (!section || !section->Get(iniKey_, ptr)) {
			if (applyDefaultIfMissing && default_.v) {
				CopyStrings(ptr, *default_.v);
			}
			return false;
		}
		MakeUnique(*ptr);
		return true;
	}
	case Type::TYPE_TOUCH_POS:
	{
		ConfigTouchPos defaultTouchPos = defaultCallback_.touchPos ? defaultCallback_.touchPos() : default_.touchPos;

		ConfigTouchPos *touchPos = ((ConfigTouchPos *)(owner + offset_));
		if (!section || (!section->Get(iniKey_, &touchPos->x) && applyDefaultIfMissing)) {
			touchPos->x = defaultTouchPos.x;
		}
		if (!section || (!section->Get(ini2_, &touchPos->y) && applyDefaultIfMissing)) {
			touchPos->y = defaultTouchPos.y;
		}
		if (!section || (!section->Get(ini3_, &touchPos->scale) && applyDefaultIfMissing)) {
			touchPos->scale = defaultTouchPos.scale;
		}
		if (ini4_ && section && section->Get(ini4_, &touchPos->show)) {
			// do nothing, succeeded.
		} else if (applyDefaultIfMissing) {
			touchPos->show = defaultTouchPos.show;
		}
		return true;
	}
	case Type::TYPE_PATH:
	{
		Path *target = (Path *)(owner + offset_);
		std::string tmp;
		if (!section || !section->Get(iniKey_, &tmp)) {
			if (applyDefaultIfMissing) {
				if (defaultCallback_.p) {
					*target = defaultCallback_.p();
				} else {
					*target = Path(default_.p);
				}
			}
			return false;
		}
		*target = Path(tmp);
		return true;
	}
	case Type::TYPE_CUSTOM_BUTTON:
	{
		ConfigCustomButton defaultCustomButton = defaultCallback_.customButton ? defaultCallback_.customButton() : default_.customButton;

		ConfigCustomButton *customButton = ((ConfigCustomButton *)(owner + offset_));
		if (!section || (!section->Get(iniKey_, &customButton->key) && applyDefaultIfMissing)) {
			customButton->key = defaultCustomButton.key;
		}
		if (!section || (!section->Get(ini2_, &customButton->image) && applyDefaultIfMissing)) {
			customButton->image = defaultCustomButton.image;
		}
		if (!section || (!section->Get(ini3_, &customButton->shape) && applyDefaultIfMissing)) {
			customButton->shape = defaultCustomButton.shape;
		}
		if (!section || (!section->Get(ini4_, &customButton->toggle) && applyDefaultIfMissing)) {
			customButton->toggle = defaultCustomButton.toggle;
		}
		if (!section || (!section->Get(ini5_, &customButton->repeat) && applyDefaultIfMissing)) {
			customButton->repeat = defaultCustomButton.repeat;
		}
		return true;
	}
	default:
		_dbg_assert_msg_(false, "Get%.*s): Unexpected ini setting type: %d", STR_VIEW(iniKey_), (int)type_);
		return false;
	}
}

void ConfigSetting::WriteToIniSection(const ConfigBlock *configBlock, Section *section) const {
	if (!SaveSetting()) {
		return;
	}
	_dbg_assert_(section);
	_dbg_assert_(offset_ >= 0 && offset_ < configBlock->Size());

	const char *owner = (const char *)configBlock;
	switch (type_) {
	case Type::TYPE_BOOL:
		return section->Set(iniKey_, *(const bool *)(owner + offset_));
	case Type::TYPE_INT:
		if (translateTo_) {
			int *ptr_i = (int *)(owner + offset_);
			std::string value = translateTo_(*ptr_i);
			return section->Set(iniKey_, value);
		}
		return section->Set(iniKey_, *(const int *)(owner + offset_));
	case Type::TYPE_UINT32:
		return section->Set(iniKey_, *(const uint32_t *)(owner + offset_));
	case Type::TYPE_UINT64:
		return section->Set(iniKey_, *(const uint64_t *)(owner + offset_));
	case Type::TYPE_FLOAT:
		return section->Set(iniKey_, *(const float *)(owner + offset_));
	case Type::TYPE_STRING:
		return section->Set(iniKey_, *(const std::string *)(owner + offset_));
	case Type::TYPE_STRING_VECTOR:
		return section->Set(iniKey_, *(const std::vector<std::string> *)(owner + offset_));
	case Type::TYPE_PATH:
	{
		Path *path = (Path *)(owner + offset_);
		return section->Set(iniKey_, path->ToString());
	}
	case Type::TYPE_TOUCH_POS:
	{
		const ConfigTouchPos *touchPos = (const ConfigTouchPos *)(owner + offset_);
		section->Set(iniKey_, touchPos->x);
		section->Set(ini2_, touchPos->y);
		section->Set(ini3_, touchPos->scale);
		if (ini4_) {
			section->Set(ini4_, touchPos->show);
		}
		return;
	}
	case Type::TYPE_CUSTOM_BUTTON:
	{
		const ConfigCustomButton *customButton = (const ConfigCustomButton *)(owner + offset_);
		section->Set(iniKey_, customButton->key);
		section->Set(ini2_, customButton->image);
		section->Set(ini3_, customButton->shape);
		section->Set(ini4_, customButton->toggle);
		section->Set(ini5_, customButton->repeat);
		return;
	}
	default:
		_dbg_assert_msg_(false, "Set%.*s): Unexpected ini setting type: %d", STR_VIEW(iniKey_), (int)type_);
		return;
	}
}

bool ConfigSetting::RestoreToDefault(ConfigBlock *configBlock, bool log) const {
	// If the block supports resetting itself, don't allow per-setting resets. Shake them out with this assert.
	_dbg_assert_(!configBlock->CanResetToDefault());
	_dbg_assert_(offset_ >= 0 && offset_ < configBlock->Size());

	const char *owner = (const char *)configBlock;
	switch (type_) {
	case Type::TYPE_BOOL:
	{
		bool *ptr_b = (bool *)(owner + offset_);
		const bool origValue = *ptr_b;
		*ptr_b = defaultCallback_.b ? defaultCallback_.b() : default_.b;
		if (*ptr_b != origValue) {
			if (log) {
				INFO_LOG(Log::System, "Restored %.*s from %s to default %s", STR_VIEW(iniKey_),
					origValue ? "true" : "false",
					*ptr_b ? "true" : "false");
			}
			return true;
		}
		break;
	}
	case Type::TYPE_INT:
	{
		int *ptr_i = (int *)(owner + offset_);
		const int origValue = *ptr_i;
		*ptr_i = defaultCallback_.i ? defaultCallback_.i() : default_.i;
		if (*ptr_i != origValue) {
			if (log) {
				INFO_LOG(Log::System, "Restored %.*s from %d to default %d", STR_VIEW(iniKey_),
					origValue, *ptr_i);
			}
			return true;
		}
		break;
	}
	case Type::TYPE_UINT32:
	{
		uint32_t *ptr_u = (uint32_t *)(owner + offset_);
		const uint32_t origValue = *ptr_u;
		*ptr_u = defaultCallback_.u ? defaultCallback_.u() : default_.u;
		if (*ptr_u != origValue) {
			if (log) {
				INFO_LOG(Log::System, "Restored %.*s from %u to default %u", STR_VIEW(iniKey_),
					origValue, *ptr_u);
			}
			return true;
		}
		break;
	}
	case Type::TYPE_UINT64:
	{
		uint64_t *ptr_lu = (uint64_t *)(owner + offset_);
		const uint64_t origValue = *ptr_lu;
		*ptr_lu = defaultCallback_.lu ? defaultCallback_.lu() : default_.lu;
		if (*ptr_lu != origValue) {
			if (log) {
				INFO_LOG(Log::System, "Restored %.*s from %llu to default %llu", STR_VIEW(iniKey_),
					(unsigned long long)origValue, (unsigned long long)(*ptr_lu));
			}
			return true;
		}
		break;
	}
	case Type::TYPE_FLOAT:
	{
		float *ptr_f = (float *)(owner + offset_);
		const float origValue = *ptr_f;
		*ptr_f = defaultCallback_.f ? defaultCallback_.f() : default_.f;
		if (*ptr_f != origValue) {
			if (log) {
				INFO_LOG(Log::System, "Restored %.*s from %f to default %f", STR_VIEW(iniKey_),
					origValue, *ptr_f);
			}
			return true;
		}
		break;
	}
	case Type::TYPE_STRING:
	{
		std::string *ptr_s = (std::string *)(owner + offset_);
		const std::string origValue = *ptr_s;
		if (defaultCallback_.s) {
			*ptr_s = defaultCallback_.s();
		} else if (default_.s != nullptr) {
			*ptr_s = default_.s;
		} else {
			*ptr_s = "";
		}
		if (*ptr_s != origValue) {
			if (log) {
				INFO_LOG(Log::System, "Restored %.*s from \"%s\" to default \"%s\"", STR_VIEW(iniKey_),
					origValue.c_str(), ptr_s->c_str());
			}
			return true;
		}
		break;
	}
	case Type::TYPE_STRING_VECTOR:
	{
		std::vector<std::string> *ptr_vec = (std::vector<std::string> *)(owner + offset_);
		CopyStrings(ptr_vec, *default_.v);
		break;
	}
	case Type::TYPE_TOUCH_POS:
	{
		ConfigTouchPos *ptr_touchPos = (ConfigTouchPos *)(owner + offset_);
		*ptr_touchPos = defaultCallback_.touchPos ? defaultCallback_.touchPos() : default_.touchPos;
		break;
	}
	case Type::TYPE_PATH:
	{
		Path *ptr_path = (Path *)(owner + offset_);
		if (defaultCallback_.p) {
			*ptr_path = defaultCallback_.p();
			break;
		} else if (default_.p) {
			*ptr_path = Path(default_.p);
		} else {
			ptr_path->clear();
		}
		break;
	}
	case Type::TYPE_CUSTOM_BUTTON:
	{
		ConfigCustomButton *ptr_customButton = (ConfigCustomButton *)(owner + offset_);
		*ptr_customButton = defaultCallback_.customButton ? defaultCallback_.customButton() : default_.customButton;
		break;
	}
	default:
		_dbg_assert_msg_(false, "RestoreToDefault(%.*s): Unexpected ini setting type: %d", STR_VIEW(iniKey_), (int)type_);
		break;
	}
	return false;
}

// Might be used to copy individual settings from defaulted blocks. Didn't end up using this for now.
void ConfigSetting::CopyFromBlock(const ConfigBlock *other) {
	_dbg_assert_(offset_ >= 0 && offset_ < other->Size());

	const char *otherOwner = (const char *)other;
	const char *thisOwner = (const char *)this;
	switch (type_) {
	case Type::TYPE_BOOL:   *(bool *)(thisOwner + offset_) = *(const bool *)(otherOwner + offset_); break;
	case Type::TYPE_INT:    *(int *)(thisOwner + offset_) = *(const int *)(otherOwner + offset_); break;
	case Type::TYPE_UINT32: *(uint32_t *)(thisOwner + offset_) = *(const uint32_t *)(otherOwner + offset_); break;
	case Type::TYPE_UINT64: *(uint64_t *)(thisOwner + offset_) = *(const uint64_t *)(otherOwner + offset_); break;
	case Type::TYPE_FLOAT: *(float *)(thisOwner + offset_) = *(const float *)(otherOwner + offset_); break;
	case Type::TYPE_STRING: *(std::string *)(thisOwner + offset_) = *(const std::string *)(otherOwner + offset_); break;
	case Type::TYPE_STRING_VECTOR: *(std::vector<std::string> *)(thisOwner + offset_) = *(const std::vector<std::string> *)(otherOwner + offset_); break;
	case Type::TYPE_PATH:   *(Path *)(thisOwner + offset_) = *(const Path *)(otherOwner + offset_); break;
	case Type::TYPE_TOUCH_POS: *(ConfigTouchPos *)(thisOwner + offset_) = *(const ConfigTouchPos *)(otherOwner + offset_); break;
	case Type::TYPE_CUSTOM_BUTTON: *(ConfigCustomButton *)(thisOwner + offset_) = *(const ConfigCustomButton *)(otherOwner + offset_); break;
	default:
		_dbg_assert_msg_(false, "CopyFromBlock(%.*s): Unexpected setting type: %d", STR_VIEW(iniKey_), (int)type_);
		return;
	}
}

void ConfigSetting::ReportSetting(const ConfigBlock *configBlock, UrlEncoder &data, const std::string &prefix) const {
	if (!Report())
		return;

	_dbg_assert_(offset_ >= 0 && offset_ < configBlock->Size());
	const char *owner = (const char *)configBlock;
	const std::string key = join(prefix, iniKey_);

	switch (type_) {
	case Type::TYPE_BOOL:   return data.Add(key, *(const bool *)(owner + offset_));
	case Type::TYPE_INT:    return data.Add(key, *(const int *)(owner + offset_));
	case Type::TYPE_UINT32: return data.Add(key, *(const uint32_t *)(owner + offset_));
	case Type::TYPE_UINT64: return data.Add(key, *(const uint64_t *)(owner + offset_));
	case Type::TYPE_FLOAT:  return data.Add(key, *(const float *)(owner + offset_));
	case Type::TYPE_STRING: return data.Add(key, *(const std::string *)(owner + offset_));
	case Type::TYPE_STRING_VECTOR: return data.Add(key, *(const std::vector<std::string> *)(owner + offset_));
	case Type::TYPE_PATH:   return data.Add(key, ((const Path *)(owner + offset_))->ToString());
	case Type::TYPE_TOUCH_POS: return;   // Doesn't report.
	case Type::TYPE_CUSTOM_BUTTON: return; // Doesn't report.
	default:
		_dbg_assert_msg_(false, "Report(%s): Unexpected ini setting type: %d", key.c_str(), (int)type_);
		return;
	}
}
