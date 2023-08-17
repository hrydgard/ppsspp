#include "pch.h"
#include "PPSSPP_UWPMain.h"

#include <mutex>
#include <list>
#include <memory>

#include "Common/File/FileUtil.h"
#include "Common/Net/HTTPClient.h"
#include "Common/Net/Resolve.h"
#include "Common/GPU/thin3d_create.h"

#include "Common/Common.h"
#include "Common/Input/InputState.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/DirectXHelper.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/LogManager.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Request.h"

#include "Core/System.h"
#include "Core/Loaders.h"
#include "Core/Config.h"

#include "Windows/InputDevice.h"
#include "Windows/XinputDevice.h"
#include "NKCodeFromWindowsSystem.h"
#include "XAudioSoundStream.h"
#include "UWPUtil.h"
#include "App.h"

// UWP Storage helper includes
#include "UWPHelpers/StorageManager.h"
#include "UWPHelpers/StorageAsync.h"
#include "UWPHelpers/LaunchItem.h"

using namespace UWP;
using namespace Windows::Foundation;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;
using namespace Windows::System::Threading;
using namespace Windows::ApplicationModel::DataTransfer;
using namespace Windows::Devices::Enumeration;
using namespace Concurrency;

// UGLY!
PPSSPP_UWPMain *g_main;
extern WindowsAudioBackend *winAudioBackend;
std::string langRegion;
std::list<std::unique_ptr<InputDevice>> g_input;

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

	const Path &exePath = File::GetExeDirectory();
	g_VFS.Register("", new DirectoryReader(exePath / "Content"));
	g_VFS.Register("", new DirectoryReader(exePath));

	wchar_t lcCountry[256];

	if (0 != GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SNAME, lcCountry, 256)) {
		langRegion = ConvertWStringToUTF8(lcCountry);
		for (size_t i = 0; i < langRegion.size(); i++) {
			if (langRegion[i] == '-')
				langRegion[i] = '_';
		}
	} else {
		langRegion = "en_US";
	}

	std::wstring internalDataFolderW = ApplicationData::Current->LocalFolder->Path->Data();
	g_Config.internalDataDirectory = Path(internalDataFolderW);
	g_Config.memStickDirectory = g_Config.internalDataDirectory;

	// On Win32 it makes more sense to initialize the system directories here
	// because the next place it was called was in the EmuThread, and it's too late by then.
	InitSysDirectories();

	LogManager::Init(&g_Config.bEnableLogging);

	// Load config up here, because those changes below would be overwritten
	// if it's not loaded here first.
	g_Config.SetSearchPath(GetSysDirectory(DIRECTORY_SYSTEM));
	g_Config.Load();

	if (g_Config.bFirstRun) {
		g_Config.memStickDirectory.clear();
	}

	bool debugLogLevel = false;
 
	g_Config.iGPUBackend = (int)GPUBackend::DIRECT3D11;

	if (debugLogLevel) {
		LogManager::GetInstance()->SetAllLogLevels(LogTypes::LDEBUG);
	}

	// Set log file location
	if (g_Config.bEnableLogging) {
		LogManager::GetInstance()->ChangeFileLog(GetLogFile().c_str());
	}

	const char *argv[2] = { "fake", nullptr };
	int argc = 1;
	
	std::string cacheFolder = ConvertWStringToUTF8(ApplicationData::Current->TemporaryFolder->Path->Data());

	// 'PPSSPP_UWPMain' is getting called before 'OnActivated'
	// this make detecting launch items on startup hard
	// I think 'Init' process must be moved out and invoked from 'OnActivated' one time only
	// currently launchItem will work fine but we cannot skip logo screen
	// we should pass file path to 'argv' using 'GetLaunchItemPath(args)' 
	// instead of depending on 'boot_filename' (LaunchItem.cpp)
	NativeInit(argc, argv, "", "", cacheFolder.c_str());

	NativeInitGraphics(ctx_.get());
	NativeResized();

	int width = m_deviceResources->GetScreenViewport().Width;
	int height = m_deviceResources->GetScreenViewport().Height;

	ctx_->GetDrawContext()->HandleEvent(Draw::Event::GOT_BACKBUFFER, width, height, m_deviceResources->GetBackBufferRenderTargetView());

	// add first XInput device to respond
	g_input.push_back(std::make_unique<XinputDevice>());

	InputDevice::BeginPolling();
}

PPSSPP_UWPMain::~PPSSPP_UWPMain() {
	InputDevice::StopPolling();
	ctx_->GetDrawContext()->HandleEvent(Draw::Event::LOST_BACKBUFFER, 0, 0, nullptr);
	NativeShutdownGraphics();
	NativeShutdown();
	g_VFS.Clear();

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
	static bool hasSetThreadName = false;
	if (!hasSetThreadName) {
		SetCurrentThreadName("UWPRenderThread");
		hasSetThreadName = true;
	}

	auto context = m_deviceResources->GetD3DDeviceContext();

	switch (m_deviceResources->ComputeDisplayRotation()) {
	case DXGI_MODE_ROTATION_IDENTITY: g_display.rotation = DisplayRotation::ROTATE_0; break;
	case DXGI_MODE_ROTATION_ROTATE90: g_display.rotation = DisplayRotation::ROTATE_90; break;
	case DXGI_MODE_ROTATION_ROTATE180: g_display.rotation = DisplayRotation::ROTATE_180; break;
	case DXGI_MODE_ROTATION_ROTATE270: g_display.rotation = DisplayRotation::ROTATE_270; break;
	}
	// Not super elegant but hey.
	memcpy(&g_display.rot_matrix, &m_deviceResources->GetOrientationTransform3D(), sizeof(float) * 16);

	// Reset the viewport to target the whole screen.
	auto viewport = m_deviceResources->GetScreenViewport();

	g_display.pixel_xres = viewport.Width;
	g_display.pixel_yres = viewport.Height;

	if (g_display.rotation == DisplayRotation::ROTATE_90 || g_display.rotation == DisplayRotation::ROTATE_270) {
		// We need to swap our width/height.
		std::swap(g_display.pixel_xres, g_display.pixel_yres);
	}

	g_display.dpi = m_deviceResources->GetActualDpi();

	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_MOBILE) {
		// Boost DPI a bit to look better.
		g_display.dpi *= 96.0f / 136.0f;
	}
	g_display.dpi_scale_x = 96.0f / g_display.dpi;
	g_display.dpi_scale_y = 96.0f / g_display.dpi;

	g_display.pixel_in_dps_x = 1.0f / g_display.dpi_scale_x;
	g_display.pixel_in_dps_y = 1.0f / g_display.dpi_scale_y;

	g_display.dp_xres = g_display.pixel_xres * g_display.dpi_scale_x;
	g_display.dp_yres = g_display.pixel_yres * g_display.dpi_scale_y;

	context->RSSetViewports(1, &viewport);

	NativeFrame(ctx_.get());
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
	InputKeyCode key = NKCODE_EXT_MOUSEWHEEL_UP;
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
	dpiFactor_x /= g_display.pixel_in_dps_x;
	dpiFactor_y /= g_display.pixel_in_dps_y;

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


UWPGraphicsContext::UWPGraphicsContext(std::shared_ptr<DX::DeviceResources> resources) {
	std::vector<std::string> adapterNames;

	draw_ = Draw::T3DCreateD3D11Context(
		resources->GetD3DDevice(), resources->GetD3DDeviceContext(), resources->GetD3DDevice(), resources->GetD3DDeviceContext(), resources->GetSwapChain(), resources->GetDeviceFeatureLevel(), 0, adapterNames, g_Config.iInflightFrames);
	bool success = draw_->CreatePresets();
	_assert_(success);
}

void UWPGraphicsContext::Shutdown() {
	delete draw_;
}

std::string System_GetProperty(SystemProperty prop) {
	static bool hasCheckedGPUDriverVersion = false;
	switch (prop) {
	case SYSPROP_NAME:
		return "Windows 10 Universal";
	case SYSPROP_LANGREGION:
		return langRegion;
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
	case SYSPROP_BUILD_VERSION:
		return PPSSPP_GIT_VERSION;
	default:
		return "";
	}
}

std::vector<std::string> System_GetPropertyStringVec(SystemProperty prop) {
	std::vector<std::string> result;
	switch (prop) {
	case SYSPROP_TEMP_DIRS:
	{
		std::wstring tempPath(MAX_PATH, '\0');
		size_t sz = GetTempPath((DWORD)tempPath.size(), &tempPath[0]);
		if (sz >= tempPath.size()) {
			tempPath.resize(sz);
			sz = GetTempPath((DWORD)tempPath.size(), &tempPath[0]);
		}
		// Need to resize off the null terminator either way.
		tempPath.resize(sz);
		result.push_back(ConvertWStringToUTF8(tempPath));
		return result;
	}

	default:
		return result;
	}
}

int System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_AUDIO_SAMPLE_RATE:
		return winAudioBackend ? winAudioBackend->GetSampleRate() : -1;

	case SYSPROP_DEVICE_TYPE:
	{
		auto ver = Windows::System::Profile::AnalyticsInfo::VersionInfo;
		if (ver->DeviceFamily == "Windows.Mobile") {
			return DEVICE_TYPE_MOBILE;
		} else if (ver->DeviceFamily == "Windows.Xbox") {
			return DEVICE_TYPE_TV;
		} else {
			return DEVICE_TYPE_DESKTOP;
		}
	}
	case SYSPROP_DISPLAY_XRES:
	{
		CoreWindow^ corewindow = CoreWindow::GetForCurrentThread();
		return  (int)corewindow->Bounds.Width;
	}
	case SYSPROP_DISPLAY_YRES:
	{
		CoreWindow^ corewindow = CoreWindow::GetForCurrentThread();
		return (int)corewindow->Bounds.Height;
	}
	default:
		return -1;
	}
}

float System_GetPropertyFloat(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return 60.f;
	case SYSPROP_DISPLAY_SAFE_INSET_LEFT:
	case SYSPROP_DISPLAY_SAFE_INSET_RIGHT:
	case SYSPROP_DISPLAY_SAFE_INSET_TOP:
	case SYSPROP_DISPLAY_SAFE_INSET_BOTTOM:
		return 0.0f;
	default:
		return -1;
	}
}

void System_Toast(const char *str) {}

bool System_GetPropertyBool(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_HAS_OPEN_DIRECTORY:
	{
		auto ver = Windows::System::Profile::AnalyticsInfo::VersionInfo;
		return ver->DeviceFamily != "Windows.Xbox";
	}
	case SYSPROP_HAS_FILE_BROWSER:
		return true;
	case SYSPROP_HAS_FOLDER_BROWSER:
		return true;
	case SYSPROP_HAS_IMAGE_BROWSER:
		return true;  // we just use the file browser
	case SYSPROP_HAS_BACK_BUTTON:
		return true;
	case SYSPROP_APP_GOLD:
#ifdef GOLD
		return true;
#else
		return false;
#endif
	case SYSPROP_CAN_JIT:
		return true;
	case SYSPROP_HAS_KEYBOARD:
		return true;
	default:
		return false;
	}
}

void System_Notify(SystemNotification notification) {
	switch (notification) {
	case SystemNotification::POLL_CONTROLLERS:
	{
		for (const auto &device : g_input)
		{
			if (device->UpdateState() == InputDevice::UPDATESTATE_SKIP_PAD)
				break;
		}
		break;
	}
	default:
		break;
	}
}

bool System_MakeRequest(SystemRequestType type, int requestId, const std::string &param1, const std::string &param2, int param3) {
	switch (type) {

	case SystemRequestType::EXIT_APP:
	{
		bool state = false;
		ExecuteTask(state, Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()->TryConsolidateAsync());
		if (!state) {
			// Notify the user?
		}
		return true;
	}
	case SystemRequestType::RESTART_APP:
	{
		Windows::ApplicationModel::Core::AppRestartFailureReason error;
		ExecuteTask(error, Windows::ApplicationModel::Core::CoreApplication::RequestRestartAsync(nullptr));
		if (error != Windows::ApplicationModel::Core::AppRestartFailureReason::RestartPending) {
			// Shutdown
			System_MakeRequest(SystemRequestType::EXIT_APP, requestId, param1, param2, param3);
		}
		return true;
	}
	case SystemRequestType::BROWSE_FOR_IMAGE:
	{
		std::vector<std::string> supportedExtensions = { ".jpg", ".png" };

		//Call file picker
		ChooseFile(supportedExtensions).then([requestId](std::string filePath) {
			if (filePath.size() > 1) {
				g_requestManager.PostSystemSuccess(requestId, filePath.c_str());
			}
			else {
				g_requestManager.PostSystemFailure(requestId);
			}
			});
		return true;
	}
	case SystemRequestType::BROWSE_FOR_FILE:
	{
		std::vector<std::string> supportedExtensions = {};
		switch ((BrowseFileType)param3) {
		case BrowseFileType::BOOTABLE:
			supportedExtensions = { ".cso", ".bin", ".iso", ".elf", ".pbp", ".zip" };
			break;
		case BrowseFileType::INI:
			supportedExtensions = { ".ini" };
			break;
		case BrowseFileType::DB:
			supportedExtensions = { ".db" };
			break;
		case BrowseFileType::SOUND_EFFECT:
			supportedExtensions = { ".wav" };
			break;
		case BrowseFileType::ANY:
			// 'ChooseFile' will added '*' by default when there are no extensions assigned
			break;
		default:
			ERROR_LOG(FILESYS, "Unexpected BrowseFileType: %d", param3);
			return false;
		}

		//Call file picker
		ChooseFile(supportedExtensions).then([requestId](std::string filePath) {
			if (filePath.size() > 1) {
				g_requestManager.PostSystemSuccess(requestId, filePath.c_str());
			}
			else {
				g_requestManager.PostSystemFailure(requestId);
			}
		});

		return true;
	}
	case SystemRequestType::BROWSE_FOR_FOLDER:
	{
		ChooseFolder().then([requestId](std::string folderPath) {
			if (folderPath.size() > 1) {
				g_requestManager.PostSystemSuccess(requestId, folderPath.c_str());
			}
			else {
				g_requestManager.PostSystemFailure(requestId);
			}
			});
		return true;
	}
	case SystemRequestType::NOTIFY_UI_STATE:
	{
		if (!param1.empty() && !strcmp(param1.c_str(), "menu")) {
			CloseLaunchItem();
		}
		return true;
	}
	case SystemRequestType::COPY_TO_CLIPBOARD:
	{
		auto dataPackage = ref new DataPackage();
		dataPackage->RequestedOperation = DataPackageOperation::Copy;
		dataPackage->SetText(ToPlatformString(param1));
		Clipboard::SetContent(dataPackage);
		return true;
	}
	case SystemRequestType::TOGGLE_FULLSCREEN_STATE:
	{
		auto view = Windows::UI::ViewManagement::ApplicationView::GetForCurrentView();
		bool flag = !view->IsFullScreenMode;
		if (param1 == "0") {
			flag = false;
		} else if (param1 == "1"){
			flag = true;
		}
		if (flag) {
			view->TryEnterFullScreenMode();
		} else {
			view->ExitFullScreenMode();
		}
		return true;
	}
	default:
		return false;
	}
}

void System_ShowFileInFolder(const char *path) {
	OpenFolder(std::string(path));
}

void System_LaunchUrl(LaunchUrlType urlType, const char *url) {
	auto uri = ref new Windows::Foundation::Uri(ToPlatformString(url));

	create_task(Windows::System::Launcher::LaunchUriAsync(uri)).then([](bool b) {});
}

void System_Vibrate(int length_ms) {
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

std::string GetCPUBrandString() {
	Platform::String^ cpu_id = nullptr;
	Platform::String^ cpu_name = nullptr;

	// GUID_DEVICE_PROCESSOR: {97FADB10-4E33-40AE-359C-8BEF029DBDD0}
	Platform::String^ if_filter = L"System.Devices.InterfaceClassGuid:=\"{97FADB10-4E33-40AE-359C-8BEF029DBDD0}\"";

	// Enumerate all CPU DeviceInterfaces, and get DeviceInstanceID of the first one.
	auto if_task = create_task(
		DeviceInformation::FindAllAsync(if_filter)).then([&](DeviceInformationCollection ^ collection) {
			if (collection->Size > 0) {
				auto cpu = collection->GetAt(0);
				auto id = cpu->Properties->Lookup(L"System.Devices.DeviceInstanceID");
				cpu_id = dynamic_cast<Platform::String^>(id);
			}
	});

	try {
		if_task.wait();
	}
	catch (const std::exception & e) {
		const char* what = e.what();
		INFO_LOG(SYSTEM, "%s", what);
	}

	if (cpu_id != nullptr) {
		// Get the Device with the same ID as the DeviceInterface
		// Then get the name (description) of that Device
		// We have to do this because the DeviceInterface we get doesn't have a proper description.
		Platform::String^ dev_filter = L"System.Devices.DeviceInstanceID:=\"" + cpu_id + L"\"";

		auto dev_task = create_task(
			DeviceInformation::FindAllAsync(dev_filter, {}, DeviceInformationKind::Device)).then(
				[&](DeviceInformationCollection ^ collection) {
					if (collection->Size > 0) {
						cpu_name = collection->GetAt(0)->Name;
					}
		});

		try {
			dev_task.wait();
		}
		catch (const std::exception & e) {
			const char* what = e.what();
			INFO_LOG(SYSTEM, "%s", what);
		}
	}

	if (cpu_name != nullptr) {
		return FromPlatformString(cpu_name);
	} else {
		return "Unknown";
	}
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
