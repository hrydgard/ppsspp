#pragma once

#include "pch.h"
#include "Common/DeviceResources.h"
#include "PPSSPP_UWPMain.h"

namespace UWP {
	// Main entry point for our app. Connects the app with the Windows shell and handles application lifecycle events.
	struct App : winrt::implements<App, winrt::Windows::ApplicationModel::Core::IFrameworkView> {
	public:
		App();

		// IFrameworkView Methods.
		void Initialize(const winrt::Windows::ApplicationModel::Core::CoreApplicationView& applicationView);
		void SetWindow(const winrt::Windows::UI::Core::CoreWindow& window);
		void Load(const winrt::hstring& entryPoint);
		void Run();
		void Uninitialize();

	private:
		// Application lifecycle event handlers.
		void OnActivated(const winrt::Windows::ApplicationModel::Core::CoreApplicationView& applicationView, const winrt::Windows::ApplicationModel::Activation::IActivatedEventArgs& args);
		void OnSuspending(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::ApplicationModel::SuspendingEventArgs& args);
		void OnResuming(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::Foundation::IInspectable& args);

		// Window event handlers.
		void OnWindowSizeChanged(const winrt::Windows::UI::Core::CoreWindow& sender, const winrt::Windows::UI::Core::WindowSizeChangedEventArgs& args);
		void OnVisibilityChanged(const winrt::Windows::UI::Core::CoreWindow& sender, const winrt::Windows::UI::Core::VisibilityChangedEventArgs& args);
		void OnWindowClosed(const winrt::Windows::UI::Core::CoreWindow& sender, const winrt::Windows::UI::Core::CoreWindowEventArgs& args);

		// DisplayInformation event handlers.
		void OnDpiChanged(const winrt::Windows::Graphics::Display::DisplayInformation& sender, const winrt::Windows::Foundation::IInspectable& args);
		void OnOrientationChanged(const winrt::Windows::Graphics::Display::DisplayInformation& sender, const winrt::Windows::Foundation::IInspectable& args);
		void OnDisplayContentsInvalidated(const winrt::Windows::Graphics::Display::DisplayInformation& sender, const winrt::Windows::Foundation::IInspectable& args);

		// Input
		void OnKeyDown(const winrt::Windows::UI::Core::CoreWindow& sender, const winrt::Windows::UI::Core::KeyEventArgs& args);
		void OnKeyUp(const winrt::Windows::UI::Core::CoreWindow& sender, const winrt::Windows::UI::Core::KeyEventArgs& args);
		void OnCharacterReceived(const winrt::Windows::UI::Core::CoreWindow& sender, const winrt::Windows::UI::Core::CharacterReceivedEventArgs& args);

		void OnPointerMoved(const winrt::Windows::UI::Core::CoreWindow& sender, const winrt::Windows::UI::Core::PointerEventArgs& args);
		void OnPointerEntered(const winrt::Windows::UI::Core::CoreWindow& sender, const winrt::Windows::UI::Core::PointerEventArgs& args);
		void OnPointerExited(const winrt::Windows::UI::Core::CoreWindow& sender, const winrt::Windows::UI::Core::PointerEventArgs& args);
		void OnPointerPressed(const winrt::Windows::UI::Core::CoreWindow& sender, const winrt::Windows::UI::Core::PointerEventArgs& args);
		void OnPointerReleased(const winrt::Windows::UI::Core::CoreWindow& sender, const winrt::Windows::UI::Core::PointerEventArgs& args);
		void OnPointerCaptureLost(const winrt::Windows::UI::Core::CoreWindow& sender, const winrt::Windows::UI::Core::PointerEventArgs& args);
		void OnPointerWheelChanged(const winrt::Windows::UI::Core::CoreWindow& sender, const winrt::Windows::UI::Core::PointerEventArgs& args);

		void App_BackRequested(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Core::BackRequestedEventArgs& e);
		void InitialPPSSPP();

		std::shared_ptr<DX::DeviceResources> m_deviceResources;
		std::unique_ptr<PPSSPP_UWPMain> m_main;
		bool m_windowClosed;
		bool m_windowVisible;

		TouchMapper touchMap_;
	};
}

struct Direct3DApplicationSource : winrt::implements<Direct3DApplicationSource, winrt::Windows::ApplicationModel::Core::IFrameworkViewSource> {
	winrt::Windows::ApplicationModel::Core::IFrameworkView CreateView();
};
