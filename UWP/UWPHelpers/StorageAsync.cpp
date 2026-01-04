// Thanks to RetroArch/Libretro team for this idea 
// This is improved version of the original idea

#include "pch.h"
#include "StorageAsync.h"

bool ActionPass(winrt::Windows::Foundation::IAsyncAction action) {
	bool result = false;
	bool done = false;
	
	action.Completed([&](auto&& sender, winrt::Windows::Foundation::AsyncStatus status) {
		if (status == winrt::Windows::Foundation::AsyncStatus::Completed) {
			result = true;
		}
		else {
			ERROR_LOG(Log::FileSystem, "Async action failed with status %d", (int)status);
			result = false;
		}
		done = true;
	});

	winrt::Windows::UI::Core::CoreWindow corewindow = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread();
	while (!done) {
		try {
			if (corewindow) {
				corewindow.Dispatcher().ProcessEvents(winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);
			}
			else {
				corewindow = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread();
			}
		}
		catch (...) {
		}
	}

	return result;
}

// Async action such as 'Delete' file
// @action: async action
// return false when action failed
bool ExecuteTask(winrt::Windows::Foundation::IAsyncAction action) {
	try {
		return ActionPass(action);
	}
	catch (...) {
		return false;
	}
}
