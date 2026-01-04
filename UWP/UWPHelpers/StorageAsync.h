// Thanks to RetroArch/Libretro team for this idea 
// This is improved version of the original idea

#pragma once

#include "pch.h"
#include <ppl.h>
#include <ppltasks.h>
#include <type_traits>

#include "Common/Log.h"
#include "UWPUtil.h"

// Helper to detect if a type is a WinRT runtime class (nullable)
template<typename T>
struct is_winrt_class : std::bool_constant<std::is_base_of_v<winrt::Windows::Foundation::IUnknown, T>> {};

// Execute a WinRT async operation synchronously by spinning the message pump (for WinRT classes)
template<typename T>
T ExecuteAsyncWithPump(winrt::Windows::Foundation::IAsyncOperation<T> asyncOp)
{
	T result{ nullptr };
	bool done = false;

	asyncOp.Completed([&](auto&& sender, winrt::Windows::Foundation::AsyncStatus status) {
		if (status == winrt::Windows::Foundation::AsyncStatus::Completed) {
			try {
				result = sender.GetResults();
			}
			catch (const winrt::hresult_error& e) {
				ERROR_LOG(Log::FileSystem, "%s", winrt::to_string(e.message()).c_str());
			}
		}
		done = true;
	});

	winrt::Windows::UI::Core::CoreWindow corewindow = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread();
	while (!done)
	{
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

// Execute for value types with default value
template<typename T>
T ExecuteAsyncWithPumpValue(winrt::Windows::Foundation::IAsyncOperation<T> asyncOp, T def)
{
	T result = def;
	bool done = false;

	asyncOp.Completed([&](auto&& sender, winrt::Windows::Foundation::AsyncStatus status) {
		if (status == winrt::Windows::Foundation::AsyncStatus::Completed) {
			try {
				result = sender.GetResults();
			}
			catch (const winrt::hresult_error& e) {
				ERROR_LOG(Log::FileSystem, "%s", winrt::to_string(e.message()).c_str());
			}
		}
		done = true;
	});

	winrt::Windows::UI::Core::CoreWindow corewindow = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread();
	while (!done)
	{
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

bool ActionPass(winrt::Windows::Foundation::IAsyncAction action);

// ExecuteTask for WinRT runtime class types (StorageFile, StorageFolder, etc)
template<typename T, std::enable_if_t<is_winrt_class<T>::value, int> = 0>
void ExecuteTask(T& out, winrt::Windows::Foundation::IAsyncOperation<T> task)
{
	try {
		out = ExecuteAsyncWithPump<T>(task);
	}
	catch (...) {
		out = nullptr;
	}
}

// ExecuteTask for value types (bool, int, enums, etc)
template<typename T, std::enable_if_t<!is_winrt_class<T>::value, int> = 0>
void ExecuteTask(T& out, winrt::Windows::Foundation::IAsyncOperation<T> task)
{
	try {
		out = ExecuteAsyncWithPumpValue<T>(task, T{});
	}
	catch (...) {
		out = T{};
	}
}

// ExecuteTask for value types with default value
template<typename T>
void ExecuteTask(T& out, winrt::Windows::Foundation::IAsyncOperation<T> task, T def)
{
	try {
		out = ExecuteAsyncWithPumpValue<T>(task, def);
	}
	catch (...) {
		out = def;
	}
}

// Async action such as 'Delete' file
// @action: async action
// return false when action failed
bool ExecuteTask(winrt::Windows::Foundation::IAsyncAction action);
