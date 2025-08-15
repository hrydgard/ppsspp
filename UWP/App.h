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
	ref class App sealed : public Windows::ApplicationModel::Core::IFrameworkView {
	public:
		App();

		// IFrameworkView Methods.
		virtual void Initialize(Windows::ApplicationModel::Core::CoreApplicationView^ applicationView);
		virtual void SetWindow(Windows::UI::Core::CoreWindow^ window);
		virtual void Load(Platform::String^ entryPoint);
		virtual void Run();
		virtual void Uninitialize();

		bool HasBackButton();

	protected:
		// Application lifecycle event handlers.
		void OnActivated(Windows::ApplicationModel::Core::CoreApplicationView^ applicationView, Windows::ApplicationModel::Activation::IActivatedEventArgs^ args);
		void OnSuspending(Platform::Object^ sender, Windows::ApplicationModel::SuspendingEventArgs^ args);
		void OnResuming(Platform::Object^ sender, Platform::Object^ args);

		// Window event handlers.
		void OnWindowSizeChanged(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::WindowSizeChangedEventArgs^ args);
		void OnVisibilityChanged(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::VisibilityChangedEventArgs^ args);
		void OnWindowClosed(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::CoreWindowEventArgs^ args);

		// DisplayInformation event handlers.
		void OnDpiChanged(Windows::Graphics::Display::DisplayInformation^ sender, Platform::Object^ args);
		void OnOrientationChanged(Windows::Graphics::Display::DisplayInformation^ sender, Platform::Object^ args);
		void OnDisplayContentsInvalidated(Windows::Graphics::Display::DisplayInformation^ sender, Platform::Object^ args);

		// Input
		void OnKeyDown(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args);
		void OnKeyUp(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args);
		void OnCharacterReceived(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::CharacterReceivedEventArgs^ args);

		void OnPointerMoved(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::PointerEventArgs^ args);
		void OnPointerEntered(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::PointerEventArgs^ args);
		void OnPointerExited(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::PointerEventArgs^ args);
		void OnPointerPressed(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::PointerEventArgs^ args);
		void OnPointerReleased(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::PointerEventArgs^ args);
		void OnPointerCaptureLost(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::PointerEventArgs^ args);
		void OnPointerWheelChanged(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::PointerEventArgs^ args);

		void App_BackRequested(Platform::Object^ sender, Windows::UI::Core::BackRequestedEventArgs^ e);
		void InitialPPSSPP();

	private:
		std::shared_ptr<DX::DeviceResources> m_deviceResources;
		std::set<HardwareButton> m_hardwareButtons;
		std::unique_ptr<PPSSPP_UWPMain> m_main;
		bool m_windowClosed;
		bool m_windowVisible;

		bool m_isPhone = false;
		TouchMapper touchMap_;
	};
}

ref class Direct3DApplicationSource sealed : Windows::ApplicationModel::Core::IFrameworkViewSource
{
public:
	virtual Windows::ApplicationModel::Core::IFrameworkView^ CreateView();
};
