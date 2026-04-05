// Copyright (c) 2023- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "pch.h"
#include "StorageAsync.h"
#include "StorageAccess.h"
#include "UWPUtil.h"
#include "Common/File/Path.h"

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Storage::AccessCache;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::ApplicationModel;

std::list<std::string> alist;
void AppendToAccessList(winrt::hstring path)
{
	Path p(FromHString(path));
	alist.push_back(p.ToString());
}

// Get value from app local settings
winrt::hstring GetDataFromLocalSettings(winrt::hstring key) {
	ApplicationDataContainer localSettings = ApplicationData::Current().LocalSettings();
	IPropertySet values = localSettings.Values();
	if (!key.empty()) {
		auto tokenRetrive = values.TryLookup(key);
		if (tokenRetrive) {
			winrt::hstring ConvertedToken = winrt::unbox_value<winrt::hstring>(tokenRetrive);
			return ConvertedToken;
		}
	}
	return L"";
}

std::string GetDataFromLocalSettings(std::string key) {
	return FromHString(GetDataFromLocalSettings(ToHString(key)));
}

// Add or replace value in app local settings
bool AddDataToLocalSettings(winrt::hstring key, winrt::hstring data, bool replace) {
	ApplicationDataContainer localSettings = ApplicationData::Current().LocalSettings();
	IPropertySet values = localSettings.Values();

	winrt::hstring testResult = GetDataFromLocalSettings(key);
	if (testResult.empty()) {
		values.Insert(key, winrt::box_value(data));
		return true;
	}
	else if (replace) {
		values.Remove(key);
		values.Insert(key, winrt::box_value(data));
		return true;
	}

	return false;
}

bool AddDataToLocalSettings(std::string key, std::string data, bool replace) {
	return AddDataToLocalSettings(ToHString(key), ToHString(data), replace);
}

// Add folder to future list (to avoid request picker again)
void AddItemToFutureList(const winrt::Windows::Storage::IStorageItem& item) {
	try {
		if (item != nullptr) {
			winrt::hstring folderToken = StorageApplicationPermissions::FutureAccessList().Add(item);
			AppendToAccessList(item.Path());
		}
	}
	catch (const winrt::hresult_error&) {
	}
}

// Get item by key
// This function can be used when you store token in LocalSettings as custom key
winrt::Windows::Storage::IStorageItem GetItemByKey(winrt::hstring key) {
	winrt::Windows::Storage::IStorageItem item = nullptr;
	winrt::hstring itemToken = GetDataFromLocalSettings(key);
	if (!itemToken.empty() && StorageApplicationPermissions::FutureAccessList().ContainsItem(itemToken)) {
		ExecuteTask(item, StorageApplicationPermissions::FutureAccessList().GetItemAsync(itemToken));
	}

	return item;
}

std::list<std::string> GetFutureAccessList() {
	if (alist.empty()) {
		auto AccessList = StorageApplicationPermissions::FutureAccessList().Entries();
		for (uint32_t it = 0; it != AccessList.Size(); ++it){
			auto item = AccessList.GetAt(it);
			try {
				auto token = item.Token;
				if (!token.empty() && StorageApplicationPermissions::FutureAccessList().ContainsItem(token)) {
					winrt::Windows::Storage::IStorageItem storageItem = nullptr;
					ExecuteTask(storageItem, StorageApplicationPermissions::FutureAccessList().GetItemAsync(token));
					if (storageItem != nullptr) {
						AppendToAccessList(storageItem.Path());
					}
					else {
						StorageApplicationPermissions::FutureAccessList().Remove(token);
					}
				}
			}
			catch (const winrt::hresult_error&) {
			}
		}

		AppendToAccessList(ApplicationData::Current().LocalFolder().Path());
		AppendToAccessList(ApplicationData::Current().TemporaryFolder().Path());
	}
	return alist;
}
