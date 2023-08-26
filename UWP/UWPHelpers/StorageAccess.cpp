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

#include "StorageAsync.h"
#include "StorageAccess.h"
#include "UWPUtil.h"
#include "Common/File/Path.h"

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage::AccessCache;
using namespace Windows::ApplicationModel;

std::list<std::string> alist;
void AppendToAccessList(Platform::String^ path)
{
	Path p(FromPlatformString(path));
	alist.push_back(p.ToString());
}

// Get value from app local settings
Platform::String^ GetDataFromLocalSettings(Platform::String^ key) {
	ApplicationDataContainer^ localSettings{ ApplicationData::Current->LocalSettings };
	IPropertySet^ values{ localSettings->Values };
	if (key != nullptr) {
		Platform::Object^ tokenRetrive = values->Lookup(key);
		if (tokenRetrive != nullptr) {
			Platform::String^ ConvertedToken = (Platform::String^)tokenRetrive;
			return ConvertedToken;
		}
	}
	return nullptr;
}

std::string GetDataFromLocalSettings(std::string key) {
	return FromPlatformString(GetDataFromLocalSettings(ToPlatformString(key)));
}

// Add or replace value in app local settings
bool AddDataToLocalSettings(Platform::String^ key, Platform::String^ data, bool replace) {
	ApplicationDataContainer^ localSettings{ ApplicationData::Current->LocalSettings };
	IPropertySet^ values{ localSettings->Values };

	Platform::String^ testResult = GetDataFromLocalSettings(key);
	if (testResult == nullptr) {
		values->Insert(key, PropertyValue::CreateString(data));
		return true;
	}
	else if (replace) {
		values->Remove(key);
		values->Insert(key, PropertyValue::CreateString(data));
		return true;
	}

	return false;
}

bool AddDataToLocalSettings(std::string key, std::string data, bool replace) {
	return AddDataToLocalSettings(ToPlatformString(key), ToPlatformString(data),replace);
}

// Add folder to future list (to avoid request picker again)
void AddItemToFutureList(IStorageItem^ item) {
	try {
		if (item != nullptr) {
			Platform::String^ folderToken = AccessCache::StorageApplicationPermissions::FutureAccessList->Add(item);
			AppendToAccessList(item->Path);
		}
	}
	catch (Platform::COMException^ e) {
	}
}

// Get item by key
// This function can be used when you store token in LocalSettings as custom key
IStorageItem^ GetItemByKey(Platform::String^ key) {
	IStorageItem^ item;
	Platform::String^ itemToken = GetDataFromLocalSettings(key);
	if (itemToken != nullptr && AccessCache::StorageApplicationPermissions::FutureAccessList->ContainsItem(itemToken)) {
		ExecuteTask(item, AccessCache::StorageApplicationPermissions::FutureAccessList->GetItemAsync(itemToken));
	}

	return item;
}

std::list<std::string> GetFutureAccessList() {
	if (alist.empty()) {
		auto AccessList = AccessCache::StorageApplicationPermissions::FutureAccessList->Entries;
		for (auto it = 0; it != AccessList->Size; ++it){
			auto item = AccessList->GetAt(it);
			try {
				auto token = item.Token;
				if (token != nullptr && AccessCache::StorageApplicationPermissions::FutureAccessList->ContainsItem(token)) {
					IStorageItem^ storageItem;
					ExecuteTask(storageItem, AccessCache::StorageApplicationPermissions::FutureAccessList->GetItemAsync(token));
					if (storageItem != nullptr) {
						AppendToAccessList(storageItem->Path);
					}
					else {
						AccessCache::StorageApplicationPermissions::FutureAccessList->Remove(token);
					}
				}
			}
			catch (Platform::COMException^ e) {
			}
		}

		AppendToAccessList(ApplicationData::Current->LocalFolder->Path);
		AppendToAccessList(ApplicationData::Current->TemporaryFolder->Path);
	}
	return alist;
}
