//
// App.xaml.cpp
// Implementation of the App class.
//

#include "pch.h"
#include "MainPage.xaml.h"
#include <ppltasks.h>

using namespace UWP;

using namespace Platform;
using namespace Platform::Collections;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Interop;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;

using namespace concurrency;
using namespace Windows::Storage;
using namespace Windows::Storage::Pickers;
using namespace Windows::Storage::Streams;

Window^ mainWindow;

void ImportGame( std::function< void() > completition )
{
  auto dsp = mainWindow->Dispatcher;
  dsp->RunAsync( CoreDispatcherPriority::Normal, ref new DispatchedHandler( [ completition ]()
  {
    auto picker = ref new FileOpenPicker();
    picker->CommitButtonText = "Import";
    picker->ViewMode = PickerViewMode::Thumbnail;
    picker->SuggestedStartLocation = PickerLocationId::Downloads;
    picker->FileTypeFilter->Append( ".elf" );
    picker->FileTypeFilter->Append( ".iso" );
    picker->FileTypeFilter->Append( ".cso" );
    picker->FileTypeFilter->Append( ".pbp" );

    create_task( picker->PickSingleFileAsync() ).then( [ completition ]( StorageFile^ file )
    {
      if ( file )
      {
        OutputDebugString( L"Importing " );
        OutputDebugString( file->Path->Data() );
        OutputDebugString( L" to " );
        OutputDebugString( ApplicationData::Current->LocalFolder->Path->Data() );
        OutputDebugString( L"\n" );

        // We need to copy files manualy as the copy function just works for less than one GB files.

        auto CopyOp = [ completition ]( IRandomAccessStream^ from, IRandomAccessStream^ to ) -> task<void>
        {
          struct local
          {
            static task<void> CopyChunk( std::function< void() > completition, IRandomAccessStream^ from, IRandomAccessStream^ to )
            {
              auto buffer = ref new Buffer( 2 * 1024 * 1024 );
              return create_task( from->ReadAsync( buffer, buffer->Capacity, InputStreamOptions::None )
              ).then( [ completition, from, to ]( IBuffer^ copyResult )
              {
                return to->WriteAsync( copyResult );
              }
              ).then( [ completition, from, to ]( size_t result )
              {
                if ( result == 0 )
                {
                  return create_task( []()
                  {
                    OutputDebugString( L"Filed to read.\n" );
                  } );
                }
                if ( to->Position < from->Size )
                {
                  OutputDebugString( L"Chunk copied.\n" );
                  return CopyChunk( completition, from, to );
                }
                else
                {
                  return create_task( [ completition ]()
                  {
                    OutputDebugString( L"Copy succeeded.\n" );
                    completition();
                  } );
                }
              } );
            };
          };

          return local::CopyChunk( completition, from, to );
        };

        create_task( file->OpenAsync( FileAccessMode::Read ) ).then( [ completition, file, CopyOp ]( IRandomAccessStream^ readStream )
        {
          if ( readStream )
          {
            OutputDebugString( L"File opened for reading.\n" );
            create_task( ApplicationData::Current->LocalFolder->CreateFileAsync( file->Name, CreationCollisionOption::ReplaceExisting ) ).then( [ completition, readStream, CopyOp ]( StorageFile^ destFile )
            {
              if ( destFile )
              {
                OutputDebugString( L"Destination file created.\n" );
                create_task( destFile->OpenAsync( FileAccessMode::ReadWrite ) ).then( [ completition, readStream, CopyOp ]( IRandomAccessStream^ writeStream )
                {
                  if ( writeStream )
                  {
                    OutputDebugString( L"File opened for writing.\n" );
                    CopyOp( readStream, writeStream );
                  }
                  else
                  {
                    OutputDebugString( L"Failed to open file for writing.\n" );
                  }
                } );
              }
              else
              {
                OutputDebugString( L"Destination file cannot be created.\n" );
              }
            } );
          }
          else
          {
            OutputDebugString( L"Failed to open file for reading.\n" );
          }
        } );
      }
    } );
  } ) );
}
namespace W32Util
{
  void ExitAndRestart() {
    *(int*)321 = 2;
  }
}

/// <summary>
/// Initializes the singleton application object.  This is the first line of authored code
/// executed, and as such is the logical equivalent of main() or WinMain().
/// </summary>
App::App()
{
  InitializeComponent();
	Suspending += ref new SuspendingEventHandler(this, &App::OnSuspending);
}

/// <summary>
/// Invoked when the application is launched normally by the end user.	Other entry points
/// will be used such as when the application is launched to open a specific file.
/// </summary>
/// <param name="e">Details about the launch request and process.</param>
void App::OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs^ e)
{
#ifdef _M_ARM
  Windows::UI::ViewManagement::StatusBar::GetForCurrentView()->HideAsync();
#endif

  mainWindow = Window::Current;
	auto rootFrame = dynamic_cast<Frame^>(Window::Current->Content);

	// Do not repeat app initialization when the Window already has content,
	// just ensure that the window is active
	if (rootFrame == nullptr)
	{
		// Create a Frame to act as the navigation context and associate it with
		// a SuspensionManager key
		rootFrame = ref new Frame();

		rootFrame->NavigationFailed += ref new Windows::UI::Xaml::Navigation::NavigationFailedEventHandler(this, &App::OnNavigationFailed);

		if (e->PreviousExecutionState == ApplicationExecutionState::Terminated)
		{
			// TODO: Restore the saved session state only when appropriate, scheduling the
			// final launch steps after the restore is complete

		}

		if (rootFrame->Content == nullptr)
		{
			// When the navigation stack isn't restored navigate to the first page,
			// configuring the new page by passing required information as a navigation
			// parameter
			rootFrame->Navigate(TypeName(MainPage::typeid), e->Arguments);
		}
		// Place the frame in the current Window
		Window::Current->Content = rootFrame;
		// Ensure the current window is active
		Window::Current->Activate();
	}
	else
	{
		if (rootFrame->Content == nullptr)
		{
			// When the navigation stack isn't restored navigate to the first page,
			// configuring the new page by passing required information as a navigation
			// parameter
			rootFrame->Navigate(TypeName(MainPage::typeid), e->Arguments);
		}
		// Ensure the current window is active
		Window::Current->Activate();
	}
}

/// <summary>
/// Invoked when application execution is being suspended.	Application state is saved
/// without knowing whether the application will be terminated or resumed with the contents
/// of memory still intact.
/// </summary>
/// <param name="sender">The source of the suspend request.</param>
/// <param name="e">Details about the suspend request.</param>
void App::OnSuspending(Object^ sender, SuspendingEventArgs^ e)
{
	(void) sender;	// Unused parameter
	(void) e;	// Unused parameter

	//TODO: Save application state and stop any background activity
}

/// <summary>
/// Invoked when Navigation to a certain page fails
/// </summary>
/// <param name="sender">The Frame which failed navigation</param>
/// <param name="e">Details about the navigation failure</param>
void App::OnNavigationFailed(Platform::Object ^sender, Windows::UI::Xaml::Navigation::NavigationFailedEventArgs ^e)
{
	throw ref new FailureException("Failed to load Page " + e->SourcePageType.Name);
}