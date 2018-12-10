#include "pch.h"
#include "PPSSPP_UWPMain.h"

#include <cassert>
#include <mutex>

#include "base/basictypes.h"
#include "Common/FileUtil.h"
#include "Common/Log.h"
#include "Common/LogManager.h"
#include "Core/System.h"
#include "Core/Loaders.h"
#include "base/NativeApp.h"
#include "base/timeutil.h"
#include "input/input_state.h"
#include "file/vfs.h"
#include "file/zip_read.h"
#include "file/file_util.h"
#include "net/http_client.h"
#include "net/resolve.h"
#include "base/display.h"
#include "thread/threadutil.h"
#include "thin3d/thin3d_create.h"
#include "util/text/utf8.h"
#include "Common/DirectXHelper.h"
#include "NKCodeFromWindowsSystem.h"
#include "XAudioSoundStream.h"
#include "UWPHost.h"
#include "UWPUtil.h"
#include "StorageFileLoader.h"
#include "App.h"

using namespace UWP;
using namespace Windows::Foundation;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;
using namespace Windows::System::Threading;
using namespace Windows::ApplicationModel::DataTransfer;
using namespace Concurrency;

// UGLY!
PPSSPP_UWPMain *g_main;
extern WindowsAudioBackend *winAudioBackend;
// TODO: Use Microsoft::WRL::ComPtr<> for D3D11 objects?
// TODO: See https://github.com/Microsoft/Windows-universal-samples/tree/master/Samples/WindowsAudioSession for WASAPI with UWP
// TODO: Low latency input: https://github.com/Microsoft/Windows-universal-samples/tree/master/Samples/LowLatencyInput/cpp

// Loads and initializes application assets when the application is loaded.
PPSSPP_UWPMain::PPSSPP_UWPMain(App ^app, const std::shared_ptr<DX::DeviceResources>& deviceResources) :
	app_(app),
	m_deviceResources(deviceResources)
{
	g_main = this;

	net::Init();

	host = new UWPHost();
	// Register to be notified if the Device is lost or recreated
	m_deviceResources->RegisterDeviceNotify(this);

	// create_task(KnownFolders::GetFolderForUserAsync(nullptr, KnownFolderId::RemovableDevices)).then([this](StorageFolder ^));

	// TODO: Change the timer settings if you want something other than the default variable timestep mode.
	// e.g. for 60 FPS fixed timestep update logic, call:
	/*
	m_timer.SetFixedTimeStep(true);
	m_timer.SetTargetElapsedSeconds(1.0 / 60);
	*/

	ctx_.reset(new UWPGraphicsContext(deviceResources));

	const std::string &exePath = File::GetExeDirectory();
	VFSRegister("", new DirectoryAssetReader((exePath + "/Content/").c_str()));
	VFSRegister("", new DirectoryAssetReader(exePath.c_str()));

	wchar_t lcCountry[256];

	std::string langRegion;
	if (0 != GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SNAME, lcCountry, 256)) {
		langRegion = ConvertWStringToUTF8(lcCountry);
		for (size_t i = 0; i < langRegion.size(); i++) {
			if (langRegion[i] == '-')
				langRegion[i] = '_';
		}
	} else {
		langRegion = "en_US";
	}

	char configFilename[MAX_PATH] = { 0 };
	char controlsConfigFilename[MAX_PATH] = { 0 };

	std::wstring memstickFolderW = ApplicationData::Current->LocalFolder->Path->Data();

	g_Config.memStickDirectory = ReplaceAll(ConvertWStringToUTF8(memstickFolderW), "\\", "/");
	if (g_Config.memStickDirectory.back() != '/')
		g_Config.memStickDirectory += "/";

	// On Win32 it makes more sense to initialize the system directories here 
	// because the next place it was called was in the EmuThread, and it's too late by then.
	InitSysDirectories();

	LogManager::Init();

	// Load config up here, because those changes below would be overwritten
	// if it's not loaded here first.
	g_Config.AddSearchPath(GetSysDirectory(DIRECTORY_SYSTEM));
	g_Config.SetDefaultPath(GetSysDirectory(DIRECTORY_SYSTEM));
	g_Config.Load(configFilename, controlsConfigFilename);

	bool debugLogLevel = false;

	g_Config.iGPUBackend = (int)GPUBackend::DIRECT3D11;

	if (debugLogLevel) {
		LogManager::GetInstance()->SetAllLogLevels(LogTypes::LDEBUG);
	}

	const char *argv[2] = { "fake", nullptr };


	std::string cacheFolder = ConvertWStringToUTF8(ApplicationData::Current->LocalFolder->Path->Data());

	NativeInit(1, argv, "", "", cacheFolder.c_str());

	NativeInitGraphics(ctx_.get());
	NativeResized();

	int width = m_deviceResources->GetScreenViewport().Width;
	int height = m_deviceResources->GetScreenViewport().Height;

	ctx_->GetDrawContext()->HandleEvent(Draw::Event::GOT_BACKBUFFER, width, height, m_deviceResources->GetBackBufferRenderTargetView());
	InputDevice::BeginPolling();
}

PPSSPP_UWPMain::~PPSSPP_UWPMain() {
	InputDevice::StopPolling();
	ctx_->GetDrawContext()->HandleEvent(Draw::Event::LOST_BACKBUFFER, 0, 0, nullptr);
	NativeShutdownGraphics();
	NativeShutdown();

	// Deregister device notification
	m_deviceResources->RegisterDeviceNotify(nullptr);
	net::Shutdown();
}

// Updates application state when the window size changes (e.g. device orientation change)
void PPSSPP_UWPMain::CreateWindowSizeDependentResources() {
	ctx_->GetDrawContext()->HandleEvent(Draw::Event::LOST_BACKBUFFER, 0, 0, nullptr);

	NativeResized();

	int width = m_deviceResources->GetScreenViewport().Width;
	int height = m_deviceResources->GetScreenViewport().Height;
	ctx_->GetDrawContext()->HandleEvent(Draw::Event::GOT_BACKBUFFER, width, height, m_deviceResources->GetBackBufferRenderTargetView());
}

// Renders the current frame according to the current application state.
// Returns true if the frame was rendered and is ready to be displayed.
bool PPSSPP_UWPMain::Render() {
	ctx_->GetDrawContext()->HandleEvent(Draw::Event::PRESENTED, 0, 0, nullptr, nullptr);
	NativeUpdate();

	static bool hasSetThreadName = false;
	if (!hasSetThreadName) {
		setCurrentThreadName("UWPRenderThread");
		hasSetThreadName = true;
	}

	time_update();
	auto context = m_deviceResources->GetD3DDeviceContext();

	auto bounds = Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()->VisibleBounds;

	int boundTop = bounds.Top;
	int boundLeft = bounds.Left;
	int boundedWidth = bounds.Width;
	int boundedHeight = bounds.Height;

	switch (m_deviceResources->ComputeDisplayRotation()) {
	case DXGI_MODE_ROTATION_IDENTITY: g_display_rotation = DisplayRotation::ROTATE_0; break;
	case DXGI_MODE_ROTATION_ROTATE90: g_display_rotation = DisplayRotation::ROTATE_90; break;
	case DXGI_MODE_ROTATION_ROTATE180: g_display_rotation = DisplayRotation::ROTATE_180; break;
	case DXGI_MODE_ROTATION_ROTATE270: g_display_rotation = DisplayRotation::ROTATE_270; break;
	}
	// Not super elegant but hey.
	memcpy(&g_display_rot_matrix, &m_deviceResources->GetOrientationTransform3D(), sizeof(float) * 16);

	// Reset the viewport to target the whole screen.
	auto viewport = m_deviceResources->GetScreenViewport();

	pixel_xres = viewport.Width;
	pixel_yres = viewport.Height;

	if (g_display_rotation == DisplayRotation::ROTATE_90 || g_display_rotation == DisplayRotation::ROTATE_270) {
		// We need to swap our width/height.
		std::swap(pixel_xres, pixel_yres);
	}

	g_dpi = m_deviceResources->GetActualDpi();
	pixel_xres = (g_dpi / 96.0f) * boundedWidth;
	pixel_yres = (g_dpi / 96.0f) * boundedHeight;

	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_MOBILE) {
		// Boost DPI a bit to look better.
		g_dpi *= 96.0f / 136.0f;
	}
	g_dpi_scale_x = 96.0f / g_dpi;
	g_dpi_scale_y = 96.0f / g_dpi;

	pixel_in_dps_x = 1.0f / g_dpi_scale_x;
	pixel_in_dps_y = 1.0f / g_dpi_scale_y;

	dp_xres = pixel_xres * g_dpi_scale_x;
	dp_yres = pixel_yres * g_dpi_scale_y;

	context->RSSetViewports(1, &viewport);

	NativeRender(ctx_.get());
	return true;
}

// Notifies renderers that device resources need to be released.
void PPSSPP_UWPMain::OnDeviceLost() {
	ctx_->GetDrawContext()->HandleEvent(Draw::Event::LOST_DEVICE, 0, 0, nullptr);
}

// Notifies renderers that device resources may now be recreated.
void PPSSPP_UWPMain::OnDeviceRestored() {
	CreateWindowSizeDependentResources();

	ctx_->GetDrawContext()->HandleEvent(Draw::Event::GOT_DEVICE, 0, 0, nullptr);
}

void PPSSPP_UWPMain::OnKeyDown(int scanCode, Windows::System::VirtualKey virtualKey, int repeatCount) {
	auto iter = virtualKeyCodeToNKCode.find(virtualKey);
	if (iter != virtualKeyCodeToNKCode.end()) {
		KeyInput key{};
		key.deviceId = DEVICE_ID_KEYBOARD;
		key.keyCode = iter->second;
		key.flags = KEY_DOWN | (repeatCount > 1 ? KEY_IS_REPEAT : 0);
		NativeKey(key);
	}
}

void PPSSPP_UWPMain::OnKeyUp(int scanCode, Windows::System::VirtualKey virtualKey) {
	auto iter = virtualKeyCodeToNKCode.find(virtualKey);
	if (iter != virtualKeyCodeToNKCode.end()) {
		KeyInput key{};
		key.deviceId = DEVICE_ID_KEYBOARD;
		key.keyCode = iter->second;
		key.flags = KEY_UP;
		NativeKey(key);
	}
}

void PPSSPP_UWPMain::OnMouseWheel(float delta) {
	int key = NKCODE_EXT_MOUSEWHEEL_UP;
	if (delta < 0) {
		key = NKCODE_EXT_MOUSEWHEEL_DOWN;
	} else if (delta == 0) {
		return;
	}

	KeyInput keyInput{};
	keyInput.keyCode = key;
	keyInput.deviceId = DEVICE_ID_MOUSE;
	keyInput.flags = KEY_DOWN | KEY_UP;
	NativeKey(keyInput);
}

bool PPSSPP_UWPMain::OnHardwareButton(HardwareButton button) {
	KeyInput keyInput{};
	keyInput.deviceId = DEVICE_ID_KEYBOARD;
	keyInput.flags = KEY_DOWN | KEY_UP;
	switch (button) {
	case HardwareButton::BACK:
		keyInput.keyCode = NKCODE_BACK;
		return NativeKey(keyInput);
	default:
		return false;
	}
}

void PPSSPP_UWPMain::OnTouchEvent(int touchEvent, int touchId, float x, float y, double timestamp) {
	// We get the coordinate in Windows' device independent pixels already. So let's undo that,
	// and then apply our own "dpi".
	float dpiFactor_x = m_deviceResources->GetActualDpi() / 96.0f;
	float dpiFactor_y = dpiFactor_x;
	dpiFactor_x /= pixel_in_dps_x;
	dpiFactor_y /= pixel_in_dps_y;

	TouchInput input{};
	input.id = touchId;
	input.x = x * dpiFactor_x;
	input.y = y * dpiFactor_y;
	input.flags = touchEvent;
	input.timestamp = timestamp;
	NativeTouch(input);

	KeyInput key{};
	key.deviceId = DEVICE_ID_MOUSE;
	if (touchEvent & TOUCH_DOWN) {
		key.keyCode = NKCODE_EXT_MOUSEBUTTON_1;
		key.flags = KEY_DOWN;
		NativeKey(key);
	}
	if (touchEvent & TOUCH_UP) {
		key.keyCode = NKCODE_EXT_MOUSEBUTTON_1;
		key.flags = KEY_UP;
		NativeKey(key);
	}
}

void PPSSPP_UWPMain::OnSuspend() {
	// TODO
}

void PPSSPP_UWPMain::LoadStorageFile(StorageFile ^file) {
	std::unique_ptr<FileLoaderFactory> factory(new StorageFileLoaderFactory(file, IdentifiedFileType::PSP_ISO));
	RegisterFileLoaderFactory("override://", std::move(factory));
	NativeMessageReceived("boot", "override://");
}

UWPGraphicsContext::UWPGraphicsContext(std::shared_ptr<DX::DeviceResources> resources) {
	std::vector<std::string> adapterNames;

	draw_ = Draw::T3DCreateD3D11Context(
		resources->GetD3DDevice(), resources->GetD3DDeviceContext(), resources->GetD3DDevice(), resources->GetD3DDeviceContext(), resources->GetDeviceFeatureLevel(), 0, adapterNames);
	bool success = draw_->CreatePresets();
	assert(success);
}

void UWPGraphicsContext::Shutdown() {
	delete draw_;
}

void UWPGraphicsContext::SwapInterval(int interval) {
	
}

std::string System_GetProperty(SystemProperty prop) {
	static bool hasCheckedGPUDriverVersion = false;
	switch (prop) {
	case SYSPROP_NAME:
		return "Windows 10 Universal";
	case SYSPROP_LANGREGION:
		return "en_US";  // TODO UWP
	case SYSPROP_CLIPBOARD_TEXT:
		/* TODO: Need to either change this API or do this on a thread in an ugly fashion.
		DataPackageView ^view = Clipboard::GetContent();
		if (view) {
			string text = await view->GetTextAsync();
		}
		*/
		return "";
	case SYSPROP_GPUDRIVER_VERSION:
		return "";
	default:
		return "";
	}
}

int System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_AUDIO_SAMPLE_RATE:
		return winAudioBackend ? winAudioBackend->GetSampleRate() : -1;
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return 60000;
	case SYSPROP_DEVICE_TYPE:
		// TODO: Detect touch screen instead.
#ifdef _M_ARM
		return DEVICE_TYPE_MOBILE;
#else
		return DEVICE_TYPE_DESKTOP;
#endif
	default:
		return -1;
	}
}

bool VulkanMayBeAvailable() {
	return false;
}

bool System_GetPropertyBool(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_HAS_FILE_BROWSER:
		return true;
	case SYSPROP_HAS_IMAGE_BROWSER:
		return false;
	case SYSPROP_HAS_BACK_BUTTON:
		return true;
	case SYSPROP_APP_GOLD:
#ifdef GOLD
		return true;
#else
		return false;
#endif
	default:
		return false;
	}
}

void System_SendMessage(const char *command, const char *parameter) {
	using namespace concurrency;

	if (!strcmp(command, "finish")) {
		// Not really supposed to support this under UWP.
	} else if (!strcmp(command, "browse_file")) {
		auto picker = ref new Windows::Storage::Pickers::FileOpenPicker();
		picker->ViewMode = Pickers::PickerViewMode::List;

		// These are single files that can be loaded directly using StorageFileLoader.
		picker->FileTypeFilter->Append(".cso");
		picker->FileTypeFilter->Append(".iso");

		// Can't load these this way currently, they require mounting the underlying folder.
		// picker->FileTypeFilter->Append(".bin");
		// picker->FileTypeFilter->Append(".elf");
		picker->SuggestedStartLocation = Pickers::PickerLocationId::DocumentsLibrary;

		create_task(picker->PickSingleFileAsync()).then([](StorageFile ^file){
			if (file) {
				g_main->LoadStorageFile(file);
			}
		});
	}
}

void LaunchBrowser(const char *url) {
	auto uri = ref new Windows::Foundation::Uri(ToPlatformString(url));

	create_task(Windows::System::Launcher::LaunchUriAsync(uri)).then([](bool b) {});
}

void Vibrate(int length_ms) {
#if _M_ARM
	if (length_ms == -1 || length_ms == -3)
		length_ms = 50;
	else if (length_ms == -2)
		length_ms = 25;
	else
		return;

	auto timeSpan = Windows::Foundation::TimeSpan();
	timeSpan.Duration = length_ms * 10000;
	// TODO: Can't use this?
	// Windows::Phone::Devices::Notification::VibrationDevice::GetDefault()->Vibrate(timeSpan);
#endif
}

void System_AskForPermission(SystemPermission permission) {
	// Do nothing
}

PermissionStatus System_GetPermissionStatus(SystemPermission permission) {
	return PERMISSION_STATUS_GRANTED;
}

bool System_InputBoxGetString(const char *title, const char *defaultValue, char *outValue, size_t outLength) {
	return false;
}

bool System_InputBoxGetWString(const wchar_t *title, const std::wstring &defaultvalue, std::wstring &outvalue) {
	return false;
}

// Emulation of TlsAlloc for Windows 10. Used by glslang. Doesn't actually seem to work, other than fixing the linking errors?

extern "C" {
DWORD WINAPI __imp_TlsAlloc() {
	return FlsAlloc(nullptr);
}
BOOL WINAPI __imp_TlsFree(DWORD index) {
	return FlsFree(index);
}
BOOL WINAPI __imp_TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue) {
	return FlsSetValue(dwTlsIndex, lpTlsValue);
}
LPVOID WINAPI __imp_TlsGetValue(DWORD dwTlsIndex) {
	return FlsGetValue(dwTlsIndex);
}
}
