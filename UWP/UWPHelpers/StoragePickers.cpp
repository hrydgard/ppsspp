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

using namespace Platform;
using namespace Windows::Storage;
using namespace Windows::Foundation;

extern void AddItemToFutureList(IStorageItem^ folder);

// Call folder picker (the selected folder will be added to future list)
concurrency::task<Platform::String^> PickSingleFolder()
{
	auto folderPicker = ref new Windows::Storage::Pickers::FolderPicker();
	folderPicker->SuggestedStartLocation = Windows::Storage::Pickers::PickerLocationId::Desktop;
	folderPicker->FileTypeFilter->Append("*");

	return concurrency::create_task(folderPicker->PickSingleFolderAsync()).then([](StorageFolder^ folder) {
		auto path = ref new Platform::String();
		if (folder != nullptr)
		{
			AddItemToFutureList(folder);
			path = folder->Path;
		}
		return path;
	});
}

// Call file picker (the selected file will be added to future list)
concurrency::task<Platform::String^> PickSingleFile(std::vector<std::string> exts)
{
	auto filePicker = ref new Windows::Storage::Pickers::FileOpenPicker();
	filePicker->SuggestedStartLocation = Windows::Storage::Pickers::PickerLocationId::Desktop;
	filePicker->ViewMode = Pickers::PickerViewMode::List;

	if (exts.size() > 0) {
		for each (auto ext in exts) {
			filePicker->FileTypeFilter->Append(ToPlatformString(ext));
		}
	}
	else
	{
		filePicker->FileTypeFilter->Append("*");
	}
	return concurrency::create_task(filePicker->PickSingleFileAsync()).then([](StorageFile^ file) {
		auto path = ref new Platform::String();
		if (file != nullptr)
		{
			AddItemToFutureList(file);
			path = file->Path;
		}
		return path;
	});
}


concurrency::task<std::string> ChooseFile(std::vector<std::string> exts) {
	return PickSingleFile(exts).then([](Platform::String^ filePath) {
		return FromPlatformString(filePath);
	});
}

concurrency::task<std::string> ChooseFolder() {
	return PickSingleFolder().then([](Platform::String^ folderPath) {
		return FromPlatformString(folderPath);
	});

}
