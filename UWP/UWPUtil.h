#pragma once

#include "pch.h"
#include "Common/Data/Encoding/Utf8.h"

inline winrt::hstring ToHString(std::string_view str) {
	return winrt::hstring(ConvertUTF8ToWString(str));
}

inline std::string FromHString(const winrt::hstring& str) {
	return ConvertWStringToUTF8(std::wstring(str));
}
