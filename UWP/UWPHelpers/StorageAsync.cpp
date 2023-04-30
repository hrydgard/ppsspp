// UWP STORAGE MANAGER
// Copyright (c) 2023 Bashar Astifan.
// Email: bashar@astifan.online
// Telegram: @basharastifan
// GitHub: https://github.com/basharast/UWP2Win32

// Thanks to RetroArch/Libretro team for this idea 
// This is improved version of the original idea

#include "StorageAsync.h"

bool ActionPass(Windows::Foundation::IAsyncAction^ action)
{
	try {
		return TaskHandler<bool>([&]() {
			return concurrency::create_task(action).then([]() {
				return true;
				});
			}, false);
	}
	catch (...) {
		return false;
	}
}

// Async action such as 'Delete' file
// @action: async action
// return false when action failed
bool ExecuteTask(Windows::Foundation::IAsyncAction^ action)
{
	return ActionPass(action);
};
