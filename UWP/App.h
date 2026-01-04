#pragma once

#include <set>

#include "pch.h"
#include "Common/DeviceResources.h"
#include "PPSSPP_UWPMain.h"

namespace UWP {
	struct Touch {
		bool inUse = false;
		unsigned uid;
	};

	class TouchMapper {
	public:
		int TouchId(unsigned touch) {
			for (int touchIx = 0; touchIx < maxTouches; touchIx++)
				if (touches[touchIx].inUse && touches[touchIx].uid == touch)
					return touchIx;
			return -1;
		}

		int AddNewTouch(unsigned touch) {
			for (int touchIx = 0; touchIx < maxTouches; touchIx++) {
				if (!touches[touchIx].inUse) {
					touches[touchIx].inUse = true;
					touches[touchIx].uid = touch;
					return touchIx;
				}
			}
			return -1;
		}

		int RemoveTouch(unsigned touch) {
			for (int touchIx = 0; touchIx < maxTouches; touchIx++) {
				if (touches[touchIx].inUse && touches[touchIx].uid == touch) {
					touches[touchIx].inUse = false;
					return touchIx;
				}
			}
			return -1;
		}

	private:
		enum { maxTouches = 11 };
		Touch touches[maxTouches]{};
	};

	enum class HardwareButton {
		BACK,
	};

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

		bool HasBackButton();

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
		std::set<HardwareButton> m_hardwareButtons;
		std::unique_ptr<PPSSPP_UWPMain> m_main;
		bool m_windowClosed;
		bool m_windowVisible;

		bool m_isPhone = false;
		TouchMapper touchMap_;
	};
}

struct Direct3DApplicationSource : winrt::implements<Direct3DApplicationSource, winrt::Windows::ApplicationModel::Core::IFrameworkViewSource> {
	winrt::Windows::ApplicationModel::Core::IFrameworkView CreateView();
};
