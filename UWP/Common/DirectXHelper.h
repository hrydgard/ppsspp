#pragma once

#include <ppltasks.h>	// For create_task

namespace DX
{
	inline void ThrowIfFailed(HRESULT hr)
	{
		if (FAILED(hr))
		{
			// Set a breakpoint on this line to catch Win32 API errors.
			winrt::throw_hresult(hr);
		}
	}

	// Function that reads from a binary file asynchronously.
	inline Concurrency::task<std::vector<uint8_t>> ReadDataAsync(const std::wstring& filename)
	{
		using namespace Concurrency;

		auto folder = winrt::Windows::ApplicationModel::Package::Current().InstalledLocation();

		return create_task([folder, filename]() -> std::vector<uint8_t> {
			auto file = folder.GetFileAsync(winrt::hstring(filename)).get();
			auto buffer = winrt::Windows::Storage::FileIO::ReadBufferAsync(file).get();

		
			std::vector<uint8_t> returnBuffer(buffer.Length());
			auto reader = winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer);
			reader.ReadBytes(returnBuffer);
			return returnBuffer;
		});
	}

	// Converts a length in device-independent pixels (DIPs) to a length in physical pixels.
	inline float ConvertDipsToPixels(float dips, float dpi)
	{
		static const float dipsPerInch = 96.0f;
		return floorf(dips * dpi / dipsPerInch + 0.5f); // Round to nearest integer.
	}

#if defined(_DEBUG)
	// Check for SDK Layer support.
	inline bool SdkLayersAvailable()
	{
		HRESULT hr = D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_NULL,       // There is no need to create a real hardware device.
			0,
			D3D11_CREATE_DEVICE_DEBUG,  // Check for the SDK layers.
			nullptr,                    // Any feature level will do.
			0,
			D3D11_SDK_VERSION,          // Always set this to D3D11_SDK_VERSION for Windows Store apps.
			nullptr,                    // No need to keep the D3D device reference.
			nullptr,                    // No need to know the feature level.
			nullptr                     // No need to keep the D3D device context reference.
			);

		return SUCCEEDED(hr);
	}
#endif
}
