#pragma once

#include "Common/Data/Encoding/Utf8.h"

inline Platform::String ^ToPlatformString(std::string str) {
	return ref new Platform::String(ConvertUTF8ToWString(str).c_str());
}

inline std::string FromPlatformString(Platform::String ^str) {
	std::wstring wstr(str->Data());
	return ConvertWStringToUTF8(wstr);
}