#include <cstdint>
#include "Common/Data/Format/IniFile.h"
#include "Common/Net/URL.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"

#include "Core/ConfigSettings.h"
#include "Core/ConfigValues.h"
#include "Core/Config.h"

std::unordered_map<void*, ConfigSetting*>& ConfigSetting::getPtrLUT() {
	static std::unordered_map<void*, ConfigSetting*> lut;
	return lut;
}

bool ConfigSetting::perGame(void *ptr) {
	return g_Config.bGameSpecific && getPtrLUT().count(ptr) > 0 && getPtrLUT()[ptr]->PerGame();
}

bool ConfigSetting::Get(const Section *section) const {
	switch (type_) {
	case TYPE_BOOL:
		return section->Get(iniKey_, ptr_.b, cb_.b ? cb_.b() : default_.b);

	case TYPE_INT:
		if (translateFrom_) {
			std::string value;
			if (section->Get(iniKey_, &value, nullptr)) {
				*ptr_.i = translateFrom_(value);
				return true;
			}
		}
		return section->Get(iniKey_, ptr_.i, cb_.i ? cb_.i() : default_.i);
	case TYPE_UINT32:
		return section->Get(iniKey_, ptr_.u, cb_.u ? cb_.u() : default_.u);
	case TYPE_UINT64:
		return section->Get(iniKey_, ptr_.lu, cb_.lu ? cb_.lu() : default_.lu);
	case TYPE_FLOAT:
		return section->Get(iniKey_, ptr_.f, cb_.f ? cb_.f() : default_.f);
	case TYPE_STRING:
		return section->Get(iniKey_, ptr_.s, cb_.s ? cb_.s().c_str() : default_.s);
	case TYPE_STRING_VECTOR:
	{
		// No support for callbacks for these yet. that's not an issue.
		bool success = section->Get(iniKey_, ptr_.v, default_.v);
		if (success) {
			MakeUnique(*ptr_.v);
		}
		return success;
	}
	case TYPE_TOUCH_POS:
	{
		ConfigTouchPos defaultTouchPos = cb_.touchPos ? cb_.touchPos() : default_.touchPos;
		section->Get(iniKey_, &ptr_.touchPos->x, defaultTouchPos.x);
		section->Get(ini2_, &ptr_.touchPos->y, defaultTouchPos.y);
		section->Get(ini3_, &ptr_.touchPos->scale, defaultTouchPos.scale);
		if (ini4_) {
			section->Get(ini4_, &ptr_.touchPos->show, defaultTouchPos.show);
		} else {
			ptr_.touchPos->show = defaultTouchPos.show;
		}
		return true;
	}
	case TYPE_PATH:
	{
		std::string tmp;
		bool result = section->Get(iniKey_, &tmp, cb_.p ? cb_.p() : default_.p);
		if (result) {
			*ptr_.p = Path(tmp);
		}
		return result;
	}
	case TYPE_CUSTOM_BUTTON:
	{
		ConfigCustomButton defaultCustomButton = cb_.customButton ? cb_.customButton() : default_.customButton;
		section->Get(iniKey_, &ptr_.customButton->key, defaultCustomButton.key);
		section->Get(ini2_, &ptr_.customButton->image, defaultCustomButton.image);
		section->Get(ini3_, &ptr_.customButton->shape, defaultCustomButton.shape);
		section->Get(ini4_, &ptr_.customButton->toggle, defaultCustomButton.toggle);
		section->Get(ini5_, &ptr_.customButton->repeat, defaultCustomButton.repeat);
		return true;
	}
	default:
		_dbg_assert_msg_(false, "Get(%s): Unexpected ini setting type: %d", iniKey_, (int)type_);
		return false;
	}
}

void ConfigSetting::Set(Section *section) const {
	if (!SaveSetting()) {
		return;
	}

	switch (type_) {
	case TYPE_BOOL:
		return section->Set(iniKey_, *ptr_.b);
	case TYPE_INT:
		if (translateTo_) {
			std::string value = translateTo_(*ptr_.i);
			return section->Set(iniKey_, value);
		}
		return section->Set(iniKey_, *ptr_.i);
	case TYPE_UINT32:
		return section->Set(iniKey_, *ptr_.u);
	case TYPE_UINT64:
		return section->Set(iniKey_, *ptr_.lu);
	case TYPE_FLOAT:
		return section->Set(iniKey_, *ptr_.f);
	case TYPE_STRING:
		return section->Set(iniKey_, *ptr_.s);
	case TYPE_STRING_VECTOR:
		return section->Set(iniKey_, *ptr_.v);
	case TYPE_PATH:
		return section->Set(iniKey_, ptr_.p->ToString());
	case TYPE_TOUCH_POS:
		section->Set(iniKey_, ptr_.touchPos->x);
		section->Set(ini2_, ptr_.touchPos->y);
		section->Set(ini3_, ptr_.touchPos->scale);
		if (ini4_) {
			section->Set(ini4_, ptr_.touchPos->show);
		}
		return;
	case TYPE_CUSTOM_BUTTON:
		section->Set(iniKey_, ptr_.customButton->key);
		section->Set(ini2_, ptr_.customButton->image);
		section->Set(ini3_, ptr_.customButton->shape);
		section->Set(ini4_, ptr_.customButton->toggle);
		section->Set(ini5_, ptr_.customButton->repeat);
		return;
	default:
		_dbg_assert_msg_(false, "Set(%s): Unexpected ini setting type: %d", iniKey_, (int)type_);
		return;
	}
}

bool ConfigSetting::RestoreToDefault(bool log) const {
	switch (type_) {
	case TYPE_BOOL:
	{
		const bool origValue = *ptr_.b;
		*ptr_.b = cb_.b ? cb_.b() : default_.b;
		if (*ptr_.b != origValue) {
			if (log) {
				INFO_LOG(Log::System, "Restored %s from %s to default %s", iniKey_,
					origValue ? "true" : "false",
					*ptr_.b ? "true" : "false");
			}
			return true;
		}
		break;
	}
	case TYPE_INT:
	{
		const int origValue = *ptr_.i;
		*ptr_.i = cb_.i ? cb_.i() : default_.i;
		if (*ptr_.i != origValue) {
			if (log) {
				INFO_LOG(Log::System, "Restored %s from %d to default %d", iniKey_,
					origValue, *ptr_.i);
			}
			return true;
		}
		break;
	}
	case TYPE_UINT32:
	{
		const u32 origValue = *ptr_.u;
		*ptr_.u = cb_.u ? cb_.u() : default_.u;
		if (*ptr_.u != origValue) {
			if (log) {
				INFO_LOG(Log::System, "Restored %s from %u to default %u", iniKey_,
					origValue, *ptr_.u);
			}
			return true;
		}
		break;
	}
	case TYPE_UINT64:
	{
		const u64 origValue = *ptr_.lu;
		*ptr_.lu = cb_.lu ? cb_.lu() : default_.lu;
		if (*ptr_.lu != origValue) {
			if (log) {
				INFO_LOG(Log::System, "Restored %s from %llu to default %llu", iniKey_,
					(unsigned long long)origValue, (unsigned long long)(*ptr_.lu));
			}
			return true;
		}
		break;
	}
	case TYPE_FLOAT:
	{
		const float origValue = *ptr_.f;
		*ptr_.f = cb_.f ? cb_.f() : default_.f;
		if (*ptr_.f != origValue) {
			if (log) {
				INFO_LOG(Log::System, "Restored %s from %f to default %f", iniKey_,
					origValue, *ptr_.f);
			}
			return true;
		}
		break;
	}
	case TYPE_STRING:
	{
		const std::string origValue = *ptr_.s;
		*ptr_.s = cb_.s ? cb_.s() : default_.s;
		if (*ptr_.s != origValue) {
			if (log) {
				INFO_LOG(Log::System, "Restored %s from \"%s\" to default \"%s\"", iniKey_,
					origValue.c_str(), ptr_.s->c_str());
			}
			return true;
		}
		break;
	}
	case TYPE_STRING_VECTOR:
	{
		*ptr_.v = *default_.v;
		break;
	}
	case TYPE_TOUCH_POS:
	{
		*ptr_.touchPos = cb_.touchPos ? cb_.touchPos() : default_.touchPos;
		break;
	}
	case TYPE_PATH:
	{
		*ptr_.p = Path(cb_.p ? cb_.p() : default_.p);
		break;
	}
	case TYPE_CUSTOM_BUTTON:
	{
		*ptr_.customButton = cb_.customButton ? cb_.customButton() : default_.customButton;
		break;
	}
	default:
		_dbg_assert_msg_(false, "RestoreToDefault(%s): Unexpected ini setting type: %d", iniKey_, (int)type_);
		break;
	}
	return false;
}

void ConfigSetting::ReportSetting(UrlEncoder &data, const std::string &prefix) const {
	if (!Report())
		return;

	switch (type_) {
	case TYPE_BOOL:   return data.Add(prefix + iniKey_, *ptr_.b);
	case TYPE_INT:    return data.Add(prefix + iniKey_, *ptr_.i);
	case TYPE_UINT32: return data.Add(prefix + iniKey_, *ptr_.u);
	case TYPE_UINT64: return data.Add(prefix + iniKey_, *ptr_.lu);
	case TYPE_FLOAT:  return data.Add(prefix + iniKey_, *ptr_.f);
	case TYPE_STRING: return data.Add(prefix + iniKey_, *ptr_.s);
	case TYPE_STRING_VECTOR: return data.Add(prefix + iniKey_, *ptr_.v);
	case TYPE_PATH:   return data.Add(prefix + iniKey_, ptr_.p->ToString());
	case TYPE_TOUCH_POS: return;   // Doesn't report.
	case TYPE_CUSTOM_BUTTON: return; // Doesn't report.
	default:
		_dbg_assert_msg_(false, "Report(%s): Unexpected ini setting type: %d", iniKey_, (int)type_);
		return;
	}
}
