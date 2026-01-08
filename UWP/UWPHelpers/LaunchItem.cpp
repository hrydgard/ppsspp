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
#include <io.h>
#include <fcntl.h>
#include <regex>
#include <thread>

#include "LaunchItem.h"
#include "StorageAccess.h"

#include "Common/Log.h"
#include "Common/System/System.h"
#include "Common/File/Path.h"
#include "UWPUtil.h"

#include <ppl.h>
#include <ppltasks.h>

using namespace winrt;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::ApplicationModel::Activation;

#pragma region LaunchItemClass
class LaunchItem {
public:
	LaunchItem() {
	}

	~LaunchItem() {
	}

	void Activate(const IStorageFile& file) {
		storageFile = file;
		AddItemToFutureList(storageFile);
		launchPath = std::string();
		launchOnExit = std::string();
	}

	void Activate(const ProtocolActivatedEventArgs& args) {
		try {
			unsigned i;
			auto query = args.Uri().QueryParsed();

			for (i = 0; i < query.Size(); i++)
			{
				auto arg = query.GetAt(i);

				if (arg.Name() == L"cmd")
				{
					auto command = FromHString(arg.Value());
					DEBUG_LOG(Log::FileSystem, "Launch command %s", command.c_str());

					std::regex rgx("\"(.+[^\\\\/]+)\"");
					std::smatch match;

					if (std::regex_search(command, match, rgx)) {
						try
						{
							launchPath = match[1];
						}
						catch (...) {
							launchPath = match[0];
						}
						DEBUG_LOG(Log::FileSystem, "Launch target %s", launchPath.c_str());
					}
				}
				else if (arg.Name() == L"launchOnExit") {
					launchOnExit = FromHString(arg.Value());
					DEBUG_LOG(Log::FileSystem, "On exit URI %s", launchOnExit.c_str());
				}
			}
		}
		catch (...) {

		}
		storageFile = nullptr;
	}

	void Start() {
		if (IsValid()) {
			std::thread([this] {
				SetState(true);
				std::string path = GetFilePath();
				// Delay to be able to launch on startup too
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, path);
			}).detach();
		}
	}

	bool IsHandled() {
		return handled;
	}
	void SetState(bool fileHandled) {
		handled = fileHandled;
	}

	bool IsValid() {
		return storageFile != nullptr || !launchPath.empty();
	}

	std::string GetFilePath() {
		std::string path = launchPath;
		if (storageFile != nullptr) {
			path = FromHString(storageFile.Path());
		}
		return path;
	}

	void Close(bool callLaunchOnExit) {
		storageFile = nullptr;
		launchPath = std::string();
		handled = false;

		if (!launchOnExit.empty()) {
			if (callLaunchOnExit) {
				DEBUG_LOG(Log::FileSystem, "Calling back %s", launchOnExit.c_str());
				auto uri = winrt::Windows::Foundation::Uri(ToHString(launchOnExit));
				winrt::Windows::System::Launcher::LaunchUriAsync(uri);
			}
			else {
				DEBUG_LOG(Log::FileSystem, "Ignoring callback %s, due to callLaunchOnExit is false", launchOnExit.c_str());
			}
		}
		launchOnExit = std::string();
	}

private:
	IStorageFile storageFile = nullptr;
	std::string launchPath;
	std::string launchOnExit;
	bool handled = false;
};
#pragma endregion

LaunchItem launchItemHandler;
void DetectLaunchItem(const IActivatedEventArgs& activateArgs, bool onlyActivate) {
	if (activateArgs != nullptr) {
		if (!launchItemHandler.IsHandled()) {
			if (activateArgs.Kind() == ActivationKind::File) {
				auto fileArgs = activateArgs.try_as<FileActivatedEventArgs>();
				if (fileArgs) {
					auto file = fileArgs.Files().GetAt(0).try_as<StorageFile>();
					if (file) {
						launchItemHandler.Activate(file);
					}
				}
			}
			else if (activateArgs.Kind() == ActivationKind::Protocol)
			{
				auto protocolArgs = activateArgs.try_as<ProtocolActivatedEventArgs>();
				if (protocolArgs) {
					launchItemHandler.Activate(protocolArgs);
				}
			}
			if (!onlyActivate) {
				launchItemHandler.Start();
			}
		}
	}
}

std::string GetLaunchItemPath(const IActivatedEventArgs& activateArgs) {
	DetectLaunchItem(activateArgs, true); // Just activate
	if (launchItemHandler.IsValid()) {
		// Expected that 'GetLaunchItemPath' called to handle startup item
		// it should be marked as handled by default
		launchItemHandler.SetState(true);
	}
	return launchItemHandler.GetFilePath();
}

void CloseLaunchItem(bool launchOnExit) {
	if (launchItemHandler.IsValid() && launchItemHandler.IsHandled()) {
		launchItemHandler.Close(launchOnExit);
	}
}
