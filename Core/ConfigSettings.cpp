#include <cstdint>
#include "Common/Data/Format/IniFile.h"
#include "Common/Net/URL.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"

#include "Core/ConfigSettings.h"
#include "Core/ConfigValues.h"
#include "Core/Config.h"

bool ConfigSetting::perGame(void *ptr) {
	return g_Config.bGameSpecific && g_Config.getPtrLUT().count(ptr) > 0 && g_Config.getPtrLUT()[ptr]->PerGame();
}

bool ConfigSetting::ReadFromIniSection(char *owner, const Section *section) const {
	switch (type_) {
	case Type::TYPE_BOOL:
		return section->Get(iniKey_, (bool *)(owner + offset_), cb_.b ? cb_.b() : default_.b);

	case Type::TYPE_INT:
		if (translateFrom_) {
			std::string value;
			if (section->Get(iniKey_, &value, nullptr)) {
				*((int *)(owner + offset_)) = translateFrom_(value);
				return true;
			}
		}
		return section->Get(iniKey_, (int *)(owner + offset_), cb_.i ? cb_.i() : default_.i);
	case Type::TYPE_UINT32:
		return section->Get(iniKey_, (uint32_t *)(owner + offset_), cb_.u ? cb_.u() : default_.u);
	case Type::TYPE_UINT64:
		return section->Get(iniKey_, (uint64_t *)(owner + offset_), cb_.lu ? cb_.lu() : default_.lu);
	case Type::TYPE_FLOAT:
		return section->Get(iniKey_, (float *)(owner + offset_), cb_.f ? cb_.f() : default_.f);
	case Type::TYPE_STRING:
		return section->Get(iniKey_, (std::string *)(owner + offset_), cb_.s ? cb_.s().c_str() : default_.s);
	case Type::TYPE_STRING_VECTOR:
	{
		// No support for callbacks for these yet. that's not an issue.
		std::vector<std::string> *ptr = (std::vector<std::string> *)(owner + offset_);
		bool success = section->Get(iniKey_, ptr, default_.v);
		if (success) {
			MakeUnique(*ptr);
		}
		return success;
	}
	case Type::TYPE_TOUCH_POS:
	{
		ConfigTouchPos defaultTouchPos = cb_.touchPos ? cb_.touchPos() : default_.touchPos;

		ConfigTouchPos *touchPos = ((ConfigTouchPos *)(owner + offset_));
		section->Get(iniKey_, &touchPos->x, defaultTouchPos.x);
		section->Get(ini2_, &touchPos->y, defaultTouchPos.y);
		section->Get(ini3_, &touchPos->scale, defaultTouchPos.scale);
		if (ini4_) {
			section->Get(ini4_, &touchPos->show, defaultTouchPos.show);
		} else {
			touchPos->show = defaultTouchPos.show;
		}
		return true;
	}
	case Type::TYPE_PATH:
	{
		std::string tmp;
		bool result = section->Get(iniKey_, &tmp, cb_.p ? cb_.p() : default_.p);
		if (result) {
			Path *path = (Path *)(owner + offset_);
			*path = Path(tmp);
		}
		return result;
	}
	case Type::TYPE_CUSTOM_BUTTON:
	{
		ConfigCustomButton defaultCustomButton = cb_.customButton ? cb_.customButton() : default_.customButton;

		ConfigCustomButton *customButton = ((ConfigCustomButton *)(owner + offset_));
		section->Get(iniKey_, &customButton->key, defaultCustomButton.key);
		section->Get(ini2_, &customButton->image, defaultCustomButton.image);
		section->Get(ini3_, &customButton->shape, defaultCustomButton.shape);
		section->Get(ini4_, &customButton->toggle, defaultCustomButton.toggle);
		section->Get(ini5_, &customButton->repeat, defaultCustomButton.repeat);
		return true;
	}
	default:
		_dbg_assert_msg_(false, "Get%.*s): Unexpected ini setting type: %d", STR_VIEW(iniKey_), (int)type_);
		return false;
	}
}

void ConfigSetting::WriteToIniSection(const char *owner, Section *section) const {
	if (!SaveSetting()) {
		return;
	}

	switch (type_) {
	case Type::TYPE_BOOL:
		return section->Set(iniKey_, *(bool *)(owner + offset_));
	case Type::TYPE_INT:
		if (translateTo_) {
			int *ptr_i = (int *)(owner + offset_);
			std::string value = translateTo_(*ptr_i);
			return section->Set(iniKey_, value);
		}
		return section->Set(iniKey_, *(int *)(owner + offset_));
	case Type::TYPE_UINT32:
		return section->Set(iniKey_, *(uint32_t *)(owner + offset_));
	case Type::TYPE_UINT64:
		return section->Set(iniKey_, *(uint64_t *)(owner + offset_));
	case Type::TYPE_FLOAT:
		return section->Set(iniKey_, *(float *)(owner + offset_));
	case Type::TYPE_STRING:
		return section->Set(iniKey_, *(std::string *)(owner + offset_));
	case Type::TYPE_STRING_VECTOR:
		return section->Set(iniKey_, *(std::vector<std::string> *)(owner + offset_));
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

bool ConfigSetting::RestoreToDefault(const char *owner, bool log) const {
	switch (type_) {
	case Type::TYPE_BOOL:
	{
		bool *ptr_b = (bool *)(owner + offset_);
		const bool origValue = *ptr_b;
		*ptr_b = cb_.b ? cb_.b() : default_.b;
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
		*ptr_i = cb_.i ? cb_.i() : default_.i;
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
		*ptr_u = cb_.u ? cb_.u() : default_.u;
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
		*ptr_lu = cb_.lu ? cb_.lu() : default_.lu;
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
		*ptr_f = cb_.f ? cb_.f() : default_.f;
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
		*ptr_s = cb_.s ? cb_.s() : default_.s;
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
		*ptr_vec = *default_.v;
		break;
	}
	case Type::TYPE_TOUCH_POS:
	{
		ConfigTouchPos *ptr_touchPos = (ConfigTouchPos *)(owner + offset_);
		*ptr_touchPos = cb_.touchPos ? cb_.touchPos() : default_.touchPos;
		break;
	}
	case Type::TYPE_PATH:
	{
		Path *ptr_path = (Path *)(owner + offset_);
		*ptr_path = Path(cb_.p ? cb_.p() : default_.p);
		break;
	}
	case Type::TYPE_CUSTOM_BUTTON:
	{
		ConfigCustomButton *ptr_customButton = (ConfigCustomButton *)(owner + offset_);
		*ptr_customButton = cb_.customButton ? cb_.customButton() : default_.customButton;
		break;
	}
	default:
		_dbg_assert_msg_(false, "RestoreToDefault(%.*s): Unexpected ini setting type: %d", STR_VIEW(iniKey_), (int)type_);
		break;
	}
	return false;
}

void ConfigSetting::ReportSetting(const char *owner, UrlEncoder &data, const std::string &prefix) const {
	if (!Report())
		return;

	const std::string key = prefix + std::string(iniKey_);

	switch (type_) {
	case Type::TYPE_BOOL:   return data.Add(key, (const bool *)(owner + offset_));
	case Type::TYPE_INT:    return data.Add(key, (const int *)(owner + offset_));
	case Type::TYPE_UINT32: return data.Add(key, (const u32 *)(owner + offset_));
	case Type::TYPE_UINT64: return data.Add(key, (const u64 *)(owner + offset_));
	case Type::TYPE_FLOAT:  return data.Add(key, (const float *)(owner + offset_));
	case Type::TYPE_STRING: return data.Add(key, (const std::string *)(owner + offset_));
	case Type::TYPE_STRING_VECTOR: return data.Add(key, (const std::vector<std::string> *)(owner + offset_));
	case Type::TYPE_PATH:   return data.Add(key, ((const Path *)(owner + offset_))->ToString());
	case Type::TYPE_TOUCH_POS: return;   // Doesn't report.
	case Type::TYPE_CUSTOM_BUTTON: return; // Doesn't report.
	default:
		_dbg_assert_msg_(false, "Report(%s): Unexpected ini setting type: %d", key.c_str(), (int)type_);
		return;
	}
}
