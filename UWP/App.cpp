#include "ppsspp_config.h"

#include "pch.h"
#include "App.h"

#include <mutex>

#include "Common/Input/InputState.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/Core.h"

#include <ppltasks.h>

using namespace UWP;

using namespace concurrency;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::UI::Core;
using namespace Windows::UI::Input;
using namespace Windows::System;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Display;

// The main function is only used to initialize our IFrameworkView class.
[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^) {
	auto direct3DApplicationSource = ref new Direct3DApplicationSource();
	CoreApplication::Run(direct3DApplicationSource);
	return 0;
}

IFrameworkView^ Direct3DApplicationSource::CreateView() {
	return ref new App();
}

App::App() :
	m_windowClosed(false),
	m_windowVisible(true)
{
}

// The first method called when the IFrameworkView is being created.
void App::Initialize(CoreApplicationView^ applicationView) {
	// Register event handlers for app lifecycle. This example includes Activated, so that we
	// can make the CoreWindow active and start rendering on the window.
	applicationView->Activated +=
		ref new TypedEventHandler<CoreApplicationView^, IActivatedEventArgs^>(this, &App::OnActivated);

	CoreApplication::Suspending +=
		ref new EventHandler<SuspendingEventArgs^>(this, &App::OnSuspending);

	CoreApplication::Resuming +=
		ref new EventHandler<Platform::Object^>(this, &App::OnResuming);

	// At this point we have access to the device. 
	// We can create the device-dependent resources.
	m_deviceResources = std::make_shared<DX::DeviceResources>();
}

// Called when the CoreWindow object is created (or re-created).
void App::SetWindow(CoreWindow^ window) {
	window->SizeChanged += 
		ref new TypedEventHandler<CoreWindow^, WindowSizeChangedEventArgs^>(this, &App::OnWindowSizeChanged);

	window->VisibilityChanged +=
		ref new TypedEventHandler<CoreWindow^, VisibilityChangedEventArgs^>(this, &App::OnVisibilityChanged);

	window->Closed += 
		ref new TypedEventHandler<CoreWindow^, CoreWindowEventArgs^>(this, &App::OnWindowClosed);

	DisplayInformation^ currentDisplayInformation = DisplayInformation::GetForCurrentView();

	currentDisplayInformation->DpiChanged +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &App::OnDpiChanged);

	currentDisplayInformation->OrientationChanged +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &App::OnOrientationChanged);

	DisplayInformation::DisplayContentsInvalidated +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &App::OnDisplayContentsInvalidated);

	window->KeyDown += ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &App::OnKeyDown);
	window->KeyUp += ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &App::OnKeyUp);

	window->PointerMoved += ref new TypedEventHandler<CoreWindow^, PointerEventArgs^>(this, &App::OnPointerMoved);
	window->PointerEntered += ref new TypedEventHandler<CoreWindow^, PointerEventArgs^>(this, &App::OnPointerEntered);
	window->PointerExited += ref new TypedEventHandler<CoreWindow^, PointerEventArgs^>(this, &App::OnPointerExited);
	window->PointerPressed += ref new TypedEventHandler<CoreWindow^, PointerEventArgs^>(this, &App::OnPointerPressed);
	window->PointerReleased += ref new TypedEventHandler<CoreWindow^, PointerEventArgs^>(this, &App::OnPointerReleased);
	window->PointerCaptureLost += ref new TypedEventHandler<CoreWindow^, PointerEventArgs^>(this, &App::OnPointerCaptureLost);
	window->PointerWheelChanged += ref new TypedEventHandler<CoreWindow^, PointerEventArgs^>(this, &App::OnPointerWheelChanged);

	if (Windows::Foundation::Metadata::ApiInformation::IsTypePresent("Windows.Phone.UI.Input.HardwareButtons")) {
		m_hardwareButtons.insert(HardwareButton::BACK);
	}

	if (Windows::System::Profile::AnalyticsInfo::VersionInfo->DeviceFamily == "Windows.Mobile") {
		m_isPhone = true;
	}

	Windows::UI::Core::SystemNavigationManager::GetForCurrentView()->
		BackRequested += ref new Windows::Foundation::EventHandler<
		Windows::UI::Core::BackRequestedEventArgs^>(
			this, &App::App_BackRequested);

	m_deviceResources->SetWindow(window);
}

bool App::HasBackButton() {
	if (m_hardwareButtons.count(HardwareButton::BACK) != 0)
		return true;
	else
		return false;
}

void App::App_BackRequested(Platform::Object^ sender, Windows::UI::Core::BackRequestedEventArgs^ e) {
	if (m_isPhone) {
		e->Handled = m_main->OnHardwareButton(HardwareButton::BACK);
	} else {
		e->Handled = true;
	}
}

void App::OnKeyDown(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args) {
	m_main->OnKeyDown(args->KeyStatus.ScanCode, args->VirtualKey, args->KeyStatus.RepeatCount);
}

void App::OnKeyUp(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args) {
	m_main->OnKeyUp(args->KeyStatus.ScanCode, args->VirtualKey);
}

void App::OnPointerMoved(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::PointerEventArgs^ args) {
	int pointerId = touchMap_.TouchId(args->CurrentPoint->PointerId);
	if (pointerId < 0)
		return;
	float X = args->CurrentPoint->Position.X;
	float Y = args->CurrentPoint->Position.Y;
	int64_t timestamp = args->CurrentPoint->Timestamp;
	m_main->OnTouchEvent(TOUCH_MOVE, pointerId, X, Y, (double)timestamp);
}

void App::OnPointerEntered(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::PointerEventArgs^ args) {
}

void App::OnPointerExited(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::PointerEventArgs^ args) {
}

void App::OnPointerPressed(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::PointerEventArgs^ args) {
	int pointerId = touchMap_.TouchId(args->CurrentPoint->PointerId);
	if (pointerId < 0)
		pointerId = touchMap_.AddNewTouch(args->CurrentPoint->PointerId);

	float X = args->CurrentPoint->Position.X;
	float Y = args->CurrentPoint->Position.Y;
	int64_t timestamp = args->CurrentPoint->Timestamp;
	m_main->OnTouchEvent(TOUCH_DOWN|TOUCH_MOVE, pointerId, X, Y, (double)timestamp);
	if (!m_isPhone) {
		sender->SetPointerCapture();
	}
}

void App::OnPointerReleased(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::PointerEventArgs^ args) {
	int pointerId = touchMap_.RemoveTouch(args->CurrentPoint->PointerId);
	if (pointerId < 0)
		return;
	float X = args->CurrentPoint->Position.X;
	float Y = args->CurrentPoint->Position.Y;
	int64_t timestamp = args->CurrentPoint->Timestamp;
	m_main->OnTouchEvent(TOUCH_UP|TOUCH_MOVE, pointerId, X, Y, (double)timestamp);
	if (!m_isPhone) {
		sender->ReleasePointerCapture();
	}
}

void App::OnPointerCaptureLost(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::PointerEventArgs^ args) {
}

void App::OnPointerWheelChanged(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::PointerEventArgs^ args) {
	int pointerId = 0;  // irrelevant
	float delta = (float)args->CurrentPoint->GetCurrentPoint(args->CurrentPoint->PointerId)->Properties->MouseWheelDelta;
	m_main->OnMouseWheel(delta);
}

// Initializes scene resources, or loads a previously saved app state.
void App::Load(Platform::String^ entryPoint) {
	if (m_main == nullptr) {
		m_main = std::unique_ptr<PPSSPP_UWPMain>(new PPSSPP_UWPMain(this, m_deviceResources));
	}
}

// This method is called after the window becomes active.
void App::Run() {
	while (!m_windowClosed) {
		if (m_windowVisible) {
			CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
			if (m_main->Render()) {
				m_deviceResources->Present();
			}
		} else {
			CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
		}
	}
}

// Required for IFrameworkView.
// Terminate events do not cause Uninitialize to be called. It will be called if your IFrameworkView
// class is torn down while the app is in the foreground.
void App::Uninitialize() {
}

// Application lifecycle event handlers.

void App::OnActivated(CoreApplicationView^ applicationView, IActivatedEventArgs^ args) {
	// Run() won't start until the CoreWindow is activated.
	CoreWindow::GetForCurrentThread()->Activate();
	// On mobile, we force-enter fullscreen mode.
	if (m_isPhone)
		g_Config.iForceFullScreen = 1;

	if (g_Config.UseFullScreen())
		Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()->TryEnterFullScreenMode();
}

void App::OnSuspending(Platform::Object^ sender, SuspendingEventArgs^ args) {
	// Save app state asynchronously after requesting a deferral. Holding a deferral
	// indicates that the application is busy performing suspending operations. Be
	// aware that a deferral may not be held indefinitely. After about five seconds,
	// the app will be forced to exit.
	SuspendingDeferral^ deferral = args->SuspendingOperation->GetDeferral();
	auto app = this;

	create_task([app, deferral]() {
		app->m_deviceResources->Trim();
		deferral->Complete();
	});
}

void App::OnResuming(Platform::Object^ sender, Platform::Object^ args) {
	// Restore any data or state that was unloaded on suspend. By default, data
	// and state are persisted when resuming from suspend. Note that this event
	// does not occur if the app was previously terminated.

	// Insert your code here.
}

// Window event handlers.

void App::OnWindowSizeChanged(CoreWindow^ sender, WindowSizeChangedEventArgs^ args) {
	auto view = Windows::UI::ViewManagement::ApplicationView::GetForCurrentView();
	g_Config.bFullScreen = view->IsFullScreenMode;
	g_Config.iForceFullScreen = -1;

	float width = sender->Bounds.Width;
	float height = sender->Bounds.Height;
	float scale = m_deviceResources->GetDpi() / 96.0f;

	m_deviceResources->SetLogicalSize(Size(width, height));
	if (m_main) {
		m_main->CreateWindowSizeDependentResources();
	}

	PSP_CoreParameter().pixelWidth = (int)(width * scale);
	PSP_CoreParameter().pixelHeight = (int)(height * scale);
	if (UpdateScreenScale((int)width, (int)height)) {
		NativeMessageReceived("gpu_resized", "");
	}
}

void App::OnVisibilityChanged(CoreWindow^ sender, VisibilityChangedEventArgs^ args) {

	if (args->Visible == false) {
		// MainScreen::OnExit and even App::OnWindowClosed
		// doesn't seem to be called when closing the window
		// Try to save the config here
		g_Config.Save("App::OnVisibilityChanged");
	}
	m_windowVisible = args->Visible;
}

void App::OnWindowClosed(CoreWindow^ sender, CoreWindowEventArgs^ args) {
	m_windowClosed = true;
}

// DisplayInformation event handlers.

void App::OnDpiChanged(DisplayInformation^ sender, Object^ args) {
	// Note: The value for LogicalDpi retrieved here may not match the effective DPI of the app
	// if it is being scaled for high resolution devices. Once the DPI is set on DeviceResources,
	// you should always retrieve it using the GetDpi method.
	// See DeviceResources.cpp for more details.
	m_deviceResources->SetDpi(sender->LogicalDpi);
	m_main->CreateWindowSizeDependentResources();
}

void App::OnOrientationChanged(DisplayInformation^ sender, Object^ args) {
	m_deviceResources->SetCurrentOrientation(sender->CurrentOrientation);
	m_main->CreateWindowSizeDependentResources();
}

void App::OnDisplayContentsInvalidated(DisplayInformation^ sender, Object^ args) {
	m_deviceResources->ValidateDevice();
}
