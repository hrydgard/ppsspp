#include "Common/Data/Format/IniFile.h"
#include "Common/Net/URL.h"
#include "Common/Log.h"

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
		return section->Get(iniKey_, ptr_.s, cb_.s ? cb_.s() : default_.s);
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

void ConfigSetting::RestoreToDefault() const {
	switch (type_) {
	case TYPE_BOOL:   *ptr_.b = cb_.b ? cb_.b() : default_.b; break;
	case TYPE_INT:    *ptr_.i = cb_.i ? cb_.i() : default_.i; break;
	case TYPE_UINT32: *ptr_.u = cb_.u ? cb_.u() : default_.u; break;
	case TYPE_UINT64: *ptr_.lu = cb_.lu ? cb_.lu() : default_.lu; break;
	case TYPE_FLOAT:  *ptr_.f = cb_.f ? cb_.f() : default_.f; break;
	case TYPE_STRING: *ptr_.s = cb_.s ? cb_.s() : default_.s; break;
	case TYPE_TOUCH_POS: *ptr_.touchPos = cb_.touchPos ? cb_.touchPos() : default_.touchPos; break;
	case TYPE_PATH: *ptr_.p = Path(cb_.p ? cb_.p() : default_.p); break;
	case TYPE_CUSTOM_BUTTON: *ptr_.customButton = cb_.customButton ? cb_.customButton() : default_.customButton; break;
	default:
		_dbg_assert_msg_(false, "RestoreToDefault(%s): Unexpected ini setting type: %d", iniKey_, (int)type_);
	}
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
	case TYPE_PATH:   return data.Add(prefix + iniKey_, ptr_.p->ToString());
	case TYPE_TOUCH_POS: return;   // Doesn't report.
	case TYPE_CUSTOM_BUTTON: return; // Doesn't report.
	default:
		_dbg_assert_msg_(false, "Report(%s): Unexpected ini setting type: %d", iniKey_, (int)type_);
		return;
	}
}
