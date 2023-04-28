// UWP STORAGE MANAGER
// Copyright (c) 2023 Bashar Astifan.
// Email: bashar@astifan.online
// Telegram: @basharastifan

// Functions:
// ChooseFolder()
// ChooseFile(std::vector<std::string> exts)

#include "pch.h"

#include "StorageLog.h"
#include "StorageExtensions.h"
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
			filePicker->FileTypeFilter->Append(convert(ext));
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
		return convert(filePath);
	});
}

concurrency::task<std::string> ChooseFolder() {
	return PickSingleFolder().then([](Platform::String^ folderPath) {
		return convert(folderPath);
	});

}
