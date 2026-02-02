#include "pch.h"
#include "PPSSPP_UWPMain.h"

#include <mutex>
#include <list>
#include <memory>
#include <thread>

#include "Common/File/FileUtil.h"
#include "Common/Net/HTTPClient.h"
#include "Common/Net/Resolve.h"
#include "Common/GPU/thin3d_create.h"

#include "Common/Common.h"
#include "Common/Audio/AudioBackend.h"
#include "Common/Input/InputState.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/DirectXHelper.h"
#include "Common/Log.h"
#include "Common/Log/LogManager.h"
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

// UWP Helpers includes
#include "UWPHelpers/StorageManager.h"
#include "UWPHelpers/StorageAsync.h"
#include "UWPHelpers/LaunchItem.h"
#include "UWPHelpers/InputHelpers.h"
#include "Windows/InputDevice.h"

using namespace UWP;
using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::System::Threading;
using namespace winrt::Windows::ApplicationModel::DataTransfer;
using namespace winrt::Windows::Devices::Enumeration;

// TODO: Use Microsoft::WRL::ComPtr<> for D3D11 objects?
// TODO: See https://github.com/Microsoft/Windows-universal-samples/tree/master/Samples/WindowsAudioSession for WASAPI with UWP
// TODO: Low latency input: https://github.com/Microsoft/Windows-universal-samples/tree/master/Samples/LowLatencyInput/cpp

// Loads and initializes application assets when the application is loaded.
PPSSPP_UWPMain::PPSSPP_UWPMain(App *app, const std::shared_ptr<DX::DeviceResources>& deviceResources) :
	app_(app),
	m_deviceResources(deviceResources)
{
	TimeInit();

	// Register to be notified if the Device is lost or recreated
	m_deviceResources->RegisterDeviceNotify(this);

	ctx_.reset(new UWPGraphicsContext(deviceResources));

#if _DEBUG
		g_logManager.SetAllLogLevels(LogLevel::LDEBUG);

		if (g_Config.bEnableLogging) {
			g_logManager.SetFileLogPath(Path(GetLogFile()));
		}
#endif

	// At this point we have main requirements initialized (Log, Config, NativeInit, Device)
	NativeInitGraphics(ctx_.get());
	NativeResized();

	int width = m_deviceResources->GetScreenViewport().Width;
	int height = m_deviceResources->GetScreenViewport().Height;

	ctx_->GetDrawContext()->HandleEvent(Draw::Event::GOT_BACKBUFFER, width, height, m_deviceResources->GetBackBufferRenderTargetView());

	// add first XInput device to respond
	g_InputManager.AddDevice(new XinputDevice());
	g_InputManager.BeginPolling();

	// Prepare input pane (for Xbox & touch devices)
	PrepareInputPane();
}

PPSSPP_UWPMain::~PPSSPP_UWPMain() {
	g_InputManager.StopPolling();
	g_InputManager.Shutdown();

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

void PPSSPP_UWPMain::UpdateScreenState() {
	// This code was included into the render loop directly
	// based on my test I don't understand why it should be called each loop
	// is it better to call it on demand only, like when screen state changed?
	auto context = m_deviceResources->GetD3DDeviceContext();

	switch (m_deviceResources->ComputeDisplayRotation()) {
	case DXGI_MODE_ROTATION_IDENTITY: g_display.rotation = DisplayRotation::ROTATE_0; break;
	case DXGI_MODE_ROTATION_ROTATE90: g_display.rotation = DisplayRotation::ROTATE_90; break;
	case DXGI_MODE_ROTATION_ROTATE180: g_display.rotation = DisplayRotation::ROTATE_180; break;
	case DXGI_MODE_ROTATION_ROTATE270: g_display.rotation = DisplayRotation::ROTATE_270; break;
	}
	// Not super elegant but hey.
	auto orientMatrix = m_deviceResources->GetOrientationTransform3D();
	memcpy(&g_display.rot_matrix, &orientMatrix, sizeof(float) * 16);

	// Reset the viewport to target the whole screen.
	auto viewport = m_deviceResources->GetScreenViewport();

	g_display.pixel_xres = viewport.Width;
	g_display.pixel_yres = viewport.Height;

	if (g_display.rotation == DisplayRotation::ROTATE_90 || g_display.rotation == DisplayRotation::ROTATE_270) {
		// We need to swap our width/height.
		// TODO: This is most likely dead code, since we no longer support Windows Phone.
		std::swap(g_display.pixel_xres, g_display.pixel_yres);
	}

	// TODO: The below stuff is probably completely redundant since the UWP app elsewhere calls Native_UpdateScreenScale.

	float dpi = m_deviceResources->GetActualDpi();
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_MOBILE) {
		// Boost DPI a bit to look better.
		dpi *= 96.0f / 136.0f;
	}

	g_display.dpi_scale_real_x = 96.0f / dpi;
	g_display.dpi_scale_real_y = 96.0f / dpi;

	g_display.dpi_scale_x = g_display.dpi_scale_real_x;
	g_display.dpi_scale_y = g_display.dpi_scale_real_y;
	g_display.pixel_in_dps_x = 1.0f / g_display.dpi_scale_x;
	g_display.pixel_in_dps_y = 1.0f / g_display.dpi_scale_y;

	g_display.dp_xres = g_display.pixel_xres * g_display.dpi_scale_x;
	g_display.dp_yres = g_display.pixel_yres * g_display.dpi_scale_y;

	context->RSSetViewports(1, &viewport);
}

// Renders the current frame according to the current application state.
// Returns true if the frame was rendered and is ready to be displayed.
bool PPSSPP_UWPMain::Render() {
	static bool hasSetThreadName = false;
	if (!hasSetThreadName) {
		SetCurrentThreadName("EmuThread");
		hasSetThreadName = true;
	}

	UpdateScreenState();

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

void PPSSPP_UWPMain::OnKeyDown(int scanCode, winrt::Windows::System::VirtualKey virtualKey, int repeatCount) {
	// TODO: Look like (Ctrl, Alt, Shift) don't trigger this event
	bool isDPad = (int)virtualKey >= 195 && (int)virtualKey <= 218; // DPad buttons range
	DPadInputState(isDPad);

	auto iter = virtualKeyCodeToNKCode.find(virtualKey);
	if (iter != virtualKeyCodeToNKCode.end()) {
		KeyInput key{};
		key.deviceId = DEVICE_ID_KEYBOARD;
		key.keyCode = iter->second;
		key.flags = KeyInputFlags::DOWN;
		if (repeatCount > 1)
			key.flags |= KeyInputFlags::IS_REPEAT;
		NativeKey(key);
	}
}

void PPSSPP_UWPMain::OnKeyUp(int scanCode, winrt::Windows::System::VirtualKey virtualKey) {
	auto iter = virtualKeyCodeToNKCode.find(virtualKey);
	if (iter != virtualKeyCodeToNKCode.end()) {
		KeyInput key{};
		key.deviceId = DEVICE_ID_KEYBOARD;
		key.keyCode = iter->second;
		key.flags = KeyInputFlags::UP;
		NativeKey(key);
	}
}

void PPSSPP_UWPMain::OnCharacterReceived(int scanCode, unsigned int keyCode) {
	// This event triggered only in chars case, (Arrows, Delete..etc don't call it)
	// TODO: Add ` && !IsCtrlOnHold()` once it's ready and implemented
	if (isTextEditActive()) {
		KeyInput key{};
		key.deviceId = DEVICE_ID_KEYBOARD;
		key.keyCode = (InputKeyCode)keyCode;
		// After many tests turns out for char just add `KeyInputFlags::CHAR` for the flags
		// any other flag like `KeyInputFlags::DOWN` will cause conflict and trigger something else
		key.flags = KeyInputFlags::CHAR;
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
	keyInput.flags = KeyInputFlags::DOWN;
	NativeKey(keyInput);

	// KeyInputFlags::UP is now sent automatically afterwards for mouse wheel events, see NativeKey.
}

bool PPSSPP_UWPMain::OnHardwareButton(HardwareButton button) {
	KeyInput keyInput{};
	keyInput.deviceId = DEVICE_ID_KEYBOARD;
	keyInput.flags = KeyInputFlags::DOWN | KeyInputFlags::UP;
	switch (button) {
	case HardwareButton::BACK:
		keyInput.keyCode = NKCODE_BACK;
		return NativeKey(keyInput);
	default:
		return false;
	}
}

void PPSSPP_UWPMain::OnTouchEvent(TouchInputFlags flags, int touchId, float x, float y, double timestamp) {
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
	input.flags = flags;
	input.timestamp = timestamp;
	NativeTouch(input);

	KeyInput key{};
	key.deviceId = DEVICE_ID_MOUSE;
	if (flags & TouchInputFlags::DOWN) {
		key.keyCode = NKCODE_EXT_MOUSEBUTTON_1;
		key.flags = KeyInputFlags::DOWN;
		NativeKey(key);
	}
	if (flags & TouchInputFlags::UP) {
		key.keyCode = NKCODE_EXT_MOUSEBUTTON_1;
		key.flags = KeyInputFlags::UP;
		NativeKey(key);
	}
}

void PPSSPP_UWPMain::OnSuspend() {
	// TODO
}


UWPGraphicsContext::UWPGraphicsContext(std::shared_ptr<DX::DeviceResources> resources) {
	std::vector<std::string> adapterNames = resources->GetAdapters();

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
		return GetSystemName();
	case SYSPROP_SYSTEMBUILD:
		return GetWindowsBuild();
	case SYSPROP_LANGREGION:
		return GetLangRegion();
	case SYSPROP_CLIPBOARD_TEXT:
		/* TODO: Need to either change this API or do this on a thread in an ugly fashion.
		auto view = winrt::Windows::ApplicationModel::DataTransfer::Clipboard::GetContent();
		if (view) {
			winrt::hstring text = co_await view.GetTextAsync();
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

extern AudioBackend *g_audioBackend;

int64_t System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_AUDIO_SAMPLE_RATE:
		return g_audioBackend ? g_audioBackend->SampleRate() : -1;

	case SYSPROP_DEVICE_TYPE:
	{
		if (IsMobile()) {
			return DEVICE_TYPE_MOBILE;
		} else if (IsXBox()) {
			return DEVICE_TYPE_TV;
		} else {
			return DEVICE_TYPE_DESKTOP;
		}
	}
	case SYSPROP_DISPLAY_XRES:
	{
		winrt::Windows::UI::Core::CoreWindow corewindow = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread();
		if (corewindow) {
			return  (int)corewindow.Bounds().Width;
		}
	}
	case SYSPROP_DISPLAY_YRES:
	{
		winrt::Windows::UI::Core::CoreWindow corewindow = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread();
		if (corewindow) {
			return (int)corewindow.Bounds().Height;
		}
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

void System_Toast(std::string_view str) {}

bool System_GetPropertyBool(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_HAS_TEXT_CLIPBOARD:
	case SYSPROP_HAS_OPEN_DIRECTORY:
	{
		return !IsXBox();
	}
	case SYSPROP_HAS_FILE_BROWSER:
		return true;
	case SYSPROP_HAS_FOLDER_BROWSER:
		return true;
	case SYSPROP_HAS_IMAGE_BROWSER:
		return true;  // we just use the file browser
	case SYSPROP_HAS_BACK_BUTTON:
		return true;
	case SYSPROP_HAS_ACCELEROMETER:
		return IsMobile();
	case SYSPROP_APP_GOLD:
#ifdef GOLD
		return true;
#else
		return false;
#endif
	case SYSPROP_CAN_JIT:
		return true;
	case SYSPROP_HAS_KEYBOARD:
	{
		// Do actual check
		// touch devices has input pane, we need to depend on it
		// I don't know any possible way to display input dialog in non-xaml apps
		return isKeyboardAvailable() || isTouchAvailable();
	}
	case SYSPROP_DEBUGGER_PRESENT:
		return IsDebuggerPresent();
	case SYSPROP_OK_BUTTON_LEFT:
		return true;
	default:
		return false;
	}
}

void System_Notify(SystemNotification notification) {}

bool System_MakeRequest(SystemRequestType type, int requestId, const std::string &param1, const std::string &param2, int64_t param3, int64_t param4) {
	switch (type) {

	case SystemRequestType::EXIT_APP:
	{
		bool state = false;
		ExecuteTask(state, winrt::Windows::UI::ViewManagement::ApplicationView::GetForCurrentView().TryConsolidateAsync());
		if (!state) {
			// Notify the user?
		}
		return true;
	}
	case SystemRequestType::RESTART_APP:
	{
		winrt::Windows::ApplicationModel::Core::AppRestartFailureReason error;
		ExecuteTask(error, winrt::Windows::ApplicationModel::Core::CoreApplication::RequestRestartAsync(L""));
		if (error != winrt::Windows::ApplicationModel::Core::AppRestartFailureReason::RestartPending) {
			// Shutdown
			System_MakeRequest(SystemRequestType::EXIT_APP, requestId, param1, param2, param3, param4);
		}
		return true;
	}
	case SystemRequestType::BROWSE_FOR_IMAGE:
	{
		std::vector<std::string> supportedExtensions = { ".jpg", ".png" };

		//Call file picker
		std::string filePath = ChooseFile(supportedExtensions);
		if (filePath.size() > 1) {
			g_requestManager.PostSystemSuccess(requestId, filePath.c_str());
		}
		else {
			g_requestManager.PostSystemFailure(requestId);
		}
		return true;
	}
	case SystemRequestType::BROWSE_FOR_FILE:
	{
		std::vector<std::string> supportedExtensions = {};
		switch ((BrowseFileType)param3) {
		case BrowseFileType::BOOTABLE:
			supportedExtensions = { ".cso", ".iso", ".chd", ".elf", ".pbp", ".zip", ".prx", ".bin" };  // should .bin even be here?
			break;
		case BrowseFileType::INI:
			supportedExtensions = { ".ini" };
			break;
		case BrowseFileType::ZIP:
			supportedExtensions = { ".zip" };
			break;
		case BrowseFileType::SYMBOL_MAP:
			supportedExtensions = { ".ppmap" };
			break;
		case BrowseFileType::SYMBOL_MAP_NOCASH:
			supportedExtensions = { ".sym" };
			break;
		case BrowseFileType::DB:
			supportedExtensions = { ".db" };
			break;
		case BrowseFileType::SOUND_EFFECT:
			supportedExtensions = { ".wav", ".mp3" };
			break;
		case BrowseFileType::ATRAC3:
			supportedExtensions = { ".at3" };
			break;
		case BrowseFileType::ANY:
			// 'ChooseFile' will added '*' by default when there are no extensions assigned
			break;
		default:
			ERROR_LOG(Log::FileSystem, "Unexpected BrowseFileType: %d", param3);
			return false;
		}

		//Call file picker
		std::string filePath = ChooseFile(supportedExtensions);
		if (filePath.size() > 1) {
			g_requestManager.PostSystemSuccess(requestId, filePath.c_str());
		}
		else {
			g_requestManager.PostSystemFailure(requestId);
		}

		return true;
	}
	case SystemRequestType::BROWSE_FOR_FOLDER:
	{
		std::string folderPath = ChooseFolder();
		if (folderPath.size() > 1) {
			g_requestManager.PostSystemSuccess(requestId, folderPath.c_str());
		}
		else {
			g_requestManager.PostSystemFailure(requestId);
		}
		return true;
	}
	case SystemRequestType::NOTIFY_UI_EVENT:
	{
		switch ((UIEventNotification)param3) {
		case UIEventNotification::MENU_RETURN:
			CloseLaunchItem();
			break;
		case UIEventNotification::POPUP_CLOSED:
			DeactivateTextEditInput();
			break;
		case UIEventNotification::TEXT_GOTFOCUS:
			ActivateTextEditInput(true);
			break;
		case UIEventNotification::TEXT_LOSTFOCUS:
			DeactivateTextEditInput(true);
			break;
		default:
			break;
		}
		return true;
	}
	case SystemRequestType::COPY_TO_CLIPBOARD:
	{
		winrt::Windows::ApplicationModel::DataTransfer::DataPackage dataPackage;
		dataPackage.RequestedOperation(winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
		dataPackage.SetText(ToHString(param1));
		winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(dataPackage);
		return true;
	}
	case SystemRequestType::APPLY_FULLSCREEN_STATE:
	{
		auto view = winrt::Windows::UI::ViewManagement::ApplicationView::GetForCurrentView();
		bool flag = !view.IsFullScreenMode();
		if (param1 == "0") {
			flag = false;
		} else if (param1 == "1"){
			flag = true;
		}
		if (flag && g_Config.bFullScreen) {
			view.TryEnterFullScreenMode();
		} else if (!g_Config.bFullScreen){
			view.ExitFullScreenMode();
		}
		return true;
	}
	case SystemRequestType::SHOW_FILE_IN_FOLDER:
		OpenFolder(param1);
		return true;
	default:
		return false;
	}
}

void System_LaunchUrl(LaunchUrlType urlType, std::string_view url) {
	auto uri = winrt::Windows::Foundation::Uri(ToHString(url));
	winrt::Windows::System::Launcher::LaunchUriAsync(uri);
}

void System_Vibrate(int length_ms) {
#if _M_ARM
	if (length_ms == -1 || length_ms == -3)
		length_ms = 50;
	else if (length_ms == -2)
		length_ms = 25;
	else
		return;

	winrt::Windows::Foundation::TimeSpan timeSpan;
	timeSpan.count = length_ms * 10000;
	// TODO: Can't use this?
	// winrt::Windows::Phone::Devices::Notification::VibrationDevice::GetDefault().Vibrate(timeSpan);
#endif
}

void System_AskForPermission(SystemPermission permission) {
}

PermissionStatus System_GetPermissionStatus(SystemPermission permission) {
	return PERMISSION_STATUS_GRANTED;
}

std::string GetCPUBrandString() {
	winrt::hstring cpu_id;
	winrt::hstring cpu_name;

	// GUID_DEVICE_PROCESSOR: {97FADB10-4E33-40AE-359C-8BEF029DBDD0}
	winrt::hstring if_filter = L"System.Devices.InterfaceClassGuid:=\"{97FADB10-4E33-40AE-359C-8BEF029DBDD0}\"";

	// Enumerate all CPU DeviceInterfaces, and get DeviceInstanceID of the first one.
	try {
		auto collection = winrt::Windows::Devices::Enumeration::DeviceInformation::FindAllAsync(if_filter).get();
		if (collection.Size() > 0) {
			auto cpu = collection.GetAt(0);
			auto id = cpu.Properties().Lookup(L"System.Devices.DeviceInstanceID");
			cpu_id = winrt::unbox_value<winrt::hstring>(id);
		}
	}
	catch (const winrt::hresult_error& e) {
		INFO_LOG(Log::System, "%s", winrt::to_string(e.message()).c_str());
	}

	if (!cpu_id.empty()) {
		// Get the Device with the same ID as the DeviceInterface
		// Then get the name (description) of that Device
		// We have to do this because the DeviceInterface we get doesn't have a proper description.
		winrt::hstring dev_filter = L"System.Devices.DeviceInstanceID:=\"" + cpu_id + L"\"";

		try {
			auto collection = winrt::Windows::Devices::Enumeration::DeviceInformation::FindAllAsync(dev_filter, {}, 
				winrt::Windows::Devices::Enumeration::DeviceInformationKind::Device).get();
			if (collection.Size() > 0) {
				cpu_name = collection.GetAt(0).Name();
			}
		}
		catch (const winrt::hresult_error& e) {
			INFO_LOG(Log::System, "%s", winrt::to_string(e.message()).c_str());
		}
	}

	if (!cpu_name.empty()) {
		return FromHString(cpu_name);
	} else {
		return "Unknown";
	}
}
