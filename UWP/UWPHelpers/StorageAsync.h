// Thanks to RetroArch/Libretro team for this idea 
// This is improved version of the original idea

#pragma once

#include "pch.h"
#include <ppl.h>
#include <ppltasks.h>
#include <wrl.h>
#include <wrl/implements.h>

#include "Common/Log.h"
#include "UWPUtil.h"

using namespace Windows::UI::Core;

// Don't add 'using' 'Windows::Foundation'
// it might cause confilct with some types like 'Point'

#pragma region Async Handlers

template<typename T>
T TaskHandler(std::function<concurrency::task<T>()> wtask, T def)
{
	T result = def;
	bool done = false;
	wtask().then([&](concurrency::task<T> t) {
		try
	    {
		    result = t.get();
	    }
	    catch (Platform::Exception^ exception_)
	    {
			ERROR_LOG(Log::FileSystem, FromPlatformString(exception_->Message).c_str());
	    }
		done = true;
	});

	CoreWindow^ corewindow = CoreWindow::GetForCurrentThread();
	while (!done)
	{
		try {
			if (corewindow) {
				corewindow->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
			}
			else {
				corewindow = CoreWindow::GetForCurrentThread();
			}
		}
		catch (...) {

		}
	}

	return result;
};

template<typename T>
T TaskPass(Windows::Foundation::IAsyncOperation<T>^ task, T def)
{
	return TaskHandler<T>([&]() {
		return concurrency::create_task(task).then([](T res) {
			return res;
		});
	}, def);
}

bool ActionPass(Windows::Foundation::IAsyncAction^ action);

#pragma endregion

// Now it's more simple to execute async task
// @out: output variable
// @task: async task
template<typename T>
void ExecuteTask(T& out, Windows::Foundation::IAsyncOperation<T>^ task)
{
	try {
		out = TaskPass<T>(task, T());
	}
	catch (...) {
		out = T();
	}
};

// For specific return default value
// @out: output variable
// @task: async task
// @def: default value when fail
template<typename T>
void ExecuteTask(T& out, Windows::Foundation::IAsyncOperation<T>^ task, T def)
{
	try{
	    out = TaskPass<T>(task, def);
	}
	catch (...) {
		out = def;
	}
};


// Async action such as 'Delete' file
// @action: async action
// return false when action failed
bool ExecuteTask(Windows::Foundation::IAsyncAction^ action);
