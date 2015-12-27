//
// MainPage.xaml.h
// Declaration of the MainPage class.
//

#pragma once

#include "MainPage.g.h"

namespace UWP
{
	/// <summary>
	/// An empty page that can be used on its own or navigated to within a Frame.
	/// </summary>
	public ref class MainPage sealed
	{
	public:
		MainPage();

  protected:
    void OnCompositionScaleChanged( Windows::UI::Xaml::Controls::SwapChainPanel^ sender, Object^ args );
    void OnSwapChainPanelSizeChanged( Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e );

    void OnPointerPressed( Platform::Object^ sender, Windows::UI::Core::PointerEventArgs^ e );
    void OnPointerMoved( Platform::Object^ sender, Windows::UI::Core::PointerEventArgs^ e );
    void OnPointerReleased( Platform::Object^ sender, Windows::UI::Core::PointerEventArgs^ e );

    void OnKeyDown( Platform::Object^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e );
    void OnKeyUp( Platform::Object^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e );

#if _M_ARM
    void OnBackPressed( Object^ sender, Windows::Phone::UI::Input::BackPressedEventArgs^ e );
#endif

  private:
    void OnResolution();

    Windows::Foundation::IAsyncAction^ m_inputLoopWorker;
    Windows::UI::Core::CoreIndependentInputSource^ m_coreInput;

    bool emuRunning = false;

    float compositionScaleX = 1;
    float compositionScaleY = 1;
  };
}
