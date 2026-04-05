#include "ppsspp_config.h"

#include "pch.h"
#include "App.h"

#include <mutex>

#include "Common/Net/HTTPClient.h"
#include "Common/Net/Resolve.h"

#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Input/InputState.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/Log/LogManager.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "UWPHelpers/LaunchItem.h"
#include <UWPUtil.h>

using namespace UWP;

using namespace winrt;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::UI::Input;
using namespace winrt::Windows::System;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Graphics::Display;

// The main function is only used to initialize our IFrameworkView class.
int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
	winrt::init_apartment();
	CoreApplication::Run(winrt::make<Direct3DApplicationSource>());
	return 0;
}

IFrameworkView Direct3DApplicationSource::CreateView() {
	return winrt::make<App>();
}

App::App() :
	m_windowClosed(false),
	m_windowVisible(true)
{
}

void App::InitialPPSSPP() {
	// Initial net
	net::Init();

	// Get install location
	auto packageDirectory = Package::Current().InstalledPath();
	const Path& exePath = Path(FromHString(packageDirectory));
	g_VFS.Register("", new DirectoryReader(exePath / "Content"));
	g_VFS.Register("", new DirectoryReader(exePath));

	// Mount a filesystem
	g_Config.flash0Directory = exePath / "assets/flash0";

	// Prepare for initialization
	std::wstring internalDataFolderW = std::wstring(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path());
	g_Config.internalDataDirectory = Path(internalDataFolderW);
	g_Config.memStickDirectory = g_Config.internalDataDirectory;

	// On Win32 it makes more sense to initialize the system directories here
	// because the next place it was called was in the EmuThread, and it's too late by then.
	CreateSysDirectories();

	g_logManager.Init(&g_Config.bEnableLogging);

	// Set the config path to local state by default
	// it will be overrided by `NativeInit` if there is custom memStick
	g_Config.SetSearchPath(GetSysDirectory(DIRECTORY_SYSTEM));
	g_Config.Load();

	if (g_Config.bFirstRun) {
		// Clear `memStickDirectory` to show memory stick screen on first start
		g_Config.memStickDirectory.clear();
	}

	// Since we don't have any async operation in `NativeInit`
	// it's better to call it here
	const char* argv[2] = { "fake", nullptr };
	std::string cacheFolder = ConvertWStringToUTF8(
		std::wstring(winrt::Windows::Storage::ApplicationData::Current().TemporaryFolder().Path())
	);
	// We will not be able to use `argv`
	// since launch parameters usually handled by `OnActivated`
	// and `OnActivated` will be invoked later, even after `PPSSPP_UWPMain(..)`
	// so we are handling launch cases using `LaunchItem`
	NativeInit(1, argv, "", "", cacheFolder.c_str());

	// Override backend, `DIRECT3D11` is the only way for UWP apps
	g_Config.iGPUBackend = (int)GPUBackend::DIRECT3D11;

	// Calling `NativeInit` before will help us to deal with custom configs
	// such as custom adapter, so it's better to initial render device here
	m_deviceResources = std::make_shared<DX::DeviceResources>();
	m_deviceResources->CreateWindowSizeDependentResources();
}

// The first method called when the IFrameworkView is being created.
void App::Initialize(const CoreApplicationView& applicationView) {
	// Register event handlers for app lifecycle. This example includes Activated, so that we
	// can make the CoreWindow active and start rendering on the window.
	applicationView.Activated({ this, &App::OnActivated });
	CoreApplication::Suspending({ this, &App::OnSuspending });
	CoreApplication::Resuming({ this, &App::OnResuming });
}

// Called when the CoreWindow object is created (or re-created).
void App::SetWindow(const CoreWindow& window) {
	window.SizeChanged({ this, &App::OnWindowSizeChanged });
	window.VisibilityChanged({ this, &App::OnVisibilityChanged });
	window.Closed({ this, &App::OnWindowClosed });

	DisplayInformation currentDisplayInformation = DisplayInformation::GetForCurrentView();

	currentDisplayInformation.DpiChanged({ this, &App::OnDpiChanged });
	currentDisplayInformation.OrientationChanged({ this, &App::OnOrientationChanged });
	DisplayInformation::DisplayContentsInvalidated({ this, &App::OnDisplayContentsInvalidated });

	window.KeyDown({ this, &App::OnKeyDown });
	window.KeyUp({ this, &App::OnKeyUp });
	window.CharacterReceived({ this, &App::OnCharacterReceived });

	window.PointerMoved({ this, &App::OnPointerMoved });
	window.PointerEntered({ this, &App::OnPointerEntered });
	window.PointerExited({ this, &App::OnPointerExited });
	window.PointerPressed({ this, &App::OnPointerPressed });
	window.PointerReleased({ this, &App::OnPointerReleased });
	window.PointerCaptureLost({ this, &App::OnPointerCaptureLost });
	window.PointerWheelChanged({ this, &App::OnPointerWheelChanged });

	if (winrt::Windows::Foundation::Metadata::ApiInformation::IsTypePresent(L"Windows.Phone.UI.Input.HardwareButtons")) {
		m_hardwareButtons.insert(HardwareButton::BACK);
	}

	if (winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo().DeviceFamily() == L"Windows.Mobile") {
		m_isPhone = true;
	}

	winrt::Windows::UI::Core::SystemNavigationManager::GetForCurrentView().BackRequested(
		{ this, &App::App_BackRequested }
	);

	InitialPPSSPP();
}

bool App::HasBackButton() {
	if (m_hardwareButtons.count(HardwareButton::BACK) != 0)
		return true;
	else
		return false;
}

void App::App_BackRequested(const IInspectable& sender, const BackRequestedEventArgs& e) {
	if (m_isPhone) {
		e.Handled(m_main->OnHardwareButton(HardwareButton::BACK));
	} else {
		e.Handled(true);
	}
}

void App::OnKeyDown(const CoreWindow& sender, const KeyEventArgs& args) {
	m_main->OnKeyDown(args.KeyStatus().ScanCode, args.VirtualKey(), args.KeyStatus().RepeatCount);
}

void App::OnKeyUp(const CoreWindow& sender, const KeyEventArgs& args) {
	m_main->OnKeyUp(args.KeyStatus().ScanCode, args.VirtualKey());
}

void App::OnCharacterReceived(const CoreWindow& sender, const CharacterReceivedEventArgs& args) {
	m_main->OnCharacterReceived(args.KeyStatus().ScanCode, args.KeyCode());
}

void App::OnPointerMoved(const CoreWindow& sender, const PointerEventArgs& args) {
	int pointerId = touchMap_.TouchId(args.CurrentPoint().PointerId());
	if (pointerId < 0)
		return;
	float X = args.CurrentPoint().Position().X;
	float Y = args.CurrentPoint().Position().Y;
	int64_t timestamp = args.CurrentPoint().Timestamp();
	m_main->OnTouchEvent(TouchInputFlags::MOVE, pointerId, X, Y, (double)timestamp);
}

void App::OnPointerEntered(const CoreWindow& sender, const PointerEventArgs& args) {
}

void App::OnPointerExited(const CoreWindow& sender, const PointerEventArgs& args) {
}

void App::OnPointerPressed(const CoreWindow& sender, const PointerEventArgs& args) {
	int pointerId = touchMap_.TouchId(args.CurrentPoint().PointerId());
	if (pointerId < 0)
		pointerId = touchMap_.AddNewTouch(args.CurrentPoint().PointerId());

	float X = args.CurrentPoint().Position().X;
	float Y = args.CurrentPoint().Position().Y;
	int64_t timestamp = args.CurrentPoint().Timestamp();
	m_main->OnTouchEvent(TouchInputFlags::DOWN | TouchInputFlags::MOVE, pointerId, X, Y, (double)timestamp);
	if (!m_isPhone) {
		sender.SetPointerCapture();
	}
}

void App::OnPointerReleased(const CoreWindow& sender, const PointerEventArgs& args) {
	int pointerId = touchMap_.RemoveTouch(args.CurrentPoint().PointerId());
	if (pointerId < 0)
		return;
	float X = args.CurrentPoint().Position().X;
	float Y = args.CurrentPoint().Position().Y;
	int64_t timestamp = args.CurrentPoint().Timestamp();
	m_main->OnTouchEvent(TouchInputFlags::UP | TouchInputFlags::MOVE, pointerId, X, Y, (double)timestamp);
	if (!m_isPhone) {
		sender.ReleasePointerCapture();
	}
}

void App::OnPointerCaptureLost(const CoreWindow& sender, const PointerEventArgs& args) {
}

void App::OnPointerWheelChanged(const CoreWindow& sender, const PointerEventArgs& args) {
	int pointerId = 0;  // irrelevant
	float delta = (float)args.CurrentPoint().Properties().MouseWheelDelta();
	m_main->OnMouseWheel(delta);
}

// Initializes scene resources, or loads a previously saved app state.
void App::Load(const hstring& entryPoint) {
	if (m_main == nullptr) {
		m_main = std::unique_ptr<PPSSPP_UWPMain>(new PPSSPP_UWPMain(this, m_deviceResources));
	}
}

// This method is called after the window becomes active.
void App::Run() {
	while (!m_windowClosed) {
		if (m_windowVisible) {
			CoreWindow::GetForCurrentThread().Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
			m_main->Render();
			// TODO: Adopt some practices from m_deviceResources->Present();
		} else {
			CoreWindow::GetForCurrentThread().Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
		}
	}
}

// Required for IFrameworkView.
// Terminate events do not cause Uninitialize to be called. It will be called if your IFrameworkView
// class is torn down while the app is in the foreground.
void App::Uninitialize() {
}

// Application lifecycle event handlers.
void App::OnActivated(const CoreApplicationView& applicationView, const IActivatedEventArgs& args) {
	// Run() won't start until the CoreWindow is activated.
	CoreWindow::GetForCurrentThread().Activate();
	// On mobile, we force-enter fullscreen mode.
	if (m_isPhone)
		g_Config.bFullScreen = true;

	if (g_Config.bFullScreen)
		winrt::Windows::UI::ViewManagement::ApplicationView::GetForCurrentView().TryEnterFullScreenMode();

	//Detect if app started or activated by launch item (file, uri)
	DetectLaunchItem(args);
}

void App::OnSuspending(const IInspectable& sender, const SuspendingEventArgs& args) {
	// Save app state asynchronously after requesting a deferral. Holding a deferral
	// indicates that the application is busy performing suspending operations. Be
	// aware that a deferral may not be held indefinitely. After about five seconds,
	// the app will be forced to exit.
	SuspendingDeferral deferral = args.SuspendingOperation().GetDeferral();
	auto app = this;

	std::thread([app, deferral]() {
		g_Config.Save("App::OnSuspending");
		app->m_deviceResources->Trim();
		deferral.Complete();
	}).detach();
}

void App::OnResuming(const IInspectable& sender, const IInspectable& args) {
	// Restore any data or state that was unloaded on suspend. By default, data
	// and state are persisted when resuming from suspend. Note that this event
	// does not occur if the app was previously terminated.

	// Insert your code here.
}

// Window event handlers.
void App::OnWindowSizeChanged(const CoreWindow& sender, const WindowSizeChangedEventArgs& args) {
	auto view = winrt::Windows::UI::ViewManagement::ApplicationView::GetForCurrentView();
	g_Config.bFullScreen = view.IsFullScreenMode();

	float width = sender.Bounds().Width;
	float height = sender.Bounds().Height;
	float scale = m_deviceResources->GetDpi() / 96.0f;

	m_deviceResources->SetLogicalSize(winrt::Windows::Foundation::Size(width, height));
	if (m_main) {
		m_main->CreateWindowSizeDependentResources();
	}

	PSP_CoreParameter().pixelWidth = (int)(width * scale);
	PSP_CoreParameter().pixelHeight = (int)(height * scale);
	if (Native_UpdateScreenScale((int)width, (int)height, UIScaleFactorToMultiplier(g_Config.iUIScaleFactor))) {
		System_PostUIMessage(UIMessage::GPU_DISPLAY_RESIZED);
	}
}

void App::OnVisibilityChanged(const CoreWindow& sender, const VisibilityChangedEventArgs& args) {
	m_windowVisible = args.Visible();
}

void App::OnWindowClosed(const CoreWindow& sender, const CoreWindowEventArgs& args) {
	m_windowClosed = true;
}

// DisplayInformation event handlers.
void App::OnDpiChanged(const DisplayInformation& sender, const IInspectable& args) {
	// Note: The value for LogicalDpi retrieved here may not match the effective DPI of the app
	// if it is being scaled for high resolution devices. Once the DPI is set on DeviceResources,
	// you should always retrieve it using the GetDpi method.
	// See DeviceResources.cpp for more details.
	m_deviceResources->SetDpi(sender.LogicalDpi());
	m_main->CreateWindowSizeDependentResources();
}

void App::OnOrientationChanged(const DisplayInformation& sender, const IInspectable& args) {
	m_deviceResources->SetCurrentOrientation(sender.CurrentOrientation());
	m_main->CreateWindowSizeDependentResources();
}

void App::OnDisplayContentsInvalidated(const DisplayInformation& sender, const IInspectable& args) {
	m_deviceResources->ValidateDevice();
}
