//
// MainPage.xaml.cpp
// Implementation of the MainPage class.
//

#include "pch.h"
#include "MainPage.xaml.h"
#include <cassert>

using namespace UWP;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;

static inline int Round( float f )
{
  return int( floorf( f ) );
}

struct Touch
{
  Touch()
    : inUse( false )
  {}

  bool inUse;
  int  x;
  int  y;
  unsigned uid;
};

const int maxTouches = 11;
Touch touches[ maxTouches ];

int TouchId( unsigned touch )
{
  for ( int touchIx = 0; touchIx < maxTouches; touchIx++ )
    if ( touches[ touchIx ].inUse && touches[ touchIx ].uid == touch )
      return touchIx;

  return -1;
}

int AddNewTouch( unsigned touch, int x, int y )
{
  for ( int touchIx = 0; touchIx < maxTouches; touchIx++ )
    if ( !touches[ touchIx ].inUse )
    {
      touches[ touchIx ].inUse = true;
      touches[ touchIx ].uid = touch;
      touches[ touchIx ].x = x;
      touches[ touchIx ].y = y;
      return touchIx;
    }

  return -1;
}

int RemoveTouch( unsigned touch )
{
  for ( int touchIx = 0; touchIx < maxTouches; touchIx++ )
    if ( touches[ touchIx ].inUse && touches[ touchIx ].uid == touch )
    {
      touches[ touchIx ].inUse = false;
      return touchIx;
    }

  return -1;
}

// The Blank Page item template is documented at http://go.microsoft.com/fwlink/?LinkId=402352&clcid=0x409

#include "file/vfs.h"
#include "file/zip_read.h"
#include "base/NativeApp.h"
#include "thread/threadutil.h"
#include "util/text/utf8.h"

#include "Core/Config.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "ext/disarm.h"
#include "base/display.h"
#include "input/input_state.h"

#include "Common/LogManager.h"

#include "UI/GameInfoCache.h"

#include "UWPWindowsHost.h"
#include "XAudioSoundStream.h"

#include "Windows/EmuThread.h"

extern InputState input_state;

static std::string langRegion;
static std::string osName;
static std::string gpuDriverVersion;

extern "C"
{
  HMODULE WINAPI GetModuleHandleA( LPCSTR lpModuleName )
  {
    assert( false );
    return nullptr;
  }

  HANDLE WINAPI GetStdHandle( DWORD nStdHandle )
  {
    assert( false );
    return nullptr;
  }

  struct CONSOLE_SCREEN_BUFFER_INFO;
  BOOL WINAPI GetConsoleScreenBufferInfo( HANDLE hConsoleOutput, CONSOLE_SCREEN_BUFFER_INFO* lpConsoleScreenBufferInfo )
  {
    assert( false );
    return FALSE;
  }

  BOOL WINAPI SetConsoleTextAttribute( HANDLE hConsoleOutput, WORD wAttributes )
  {
    assert( false );
    return FALSE;
  }

  BOOL WINAPI GetProcessAffinityMask( HANDLE hProcess, PDWORD_PTR lpProcessAffinityMask, PDWORD_PTR lpSystemAffinityMask )
  {
    assert( false );
    return FALSE;
  }

  BOOL WINAPI CryptAcquireContextA( HCRYPTPROV *phProv, LPCTSTR pszContainer, LPCTSTR pszProvider, DWORD dwProvType, DWORD dwFlags )
  {
    assert( false );
    return FALSE;
  }

  BOOL WINAPI CryptGenRandom( HCRYPTPROV hProv, DWORD dwLen, BYTE *pbBuffer )
  {
    assert( false );
    return FALSE;
  }
}

void System_AskForPermission( SystemPermission permission ) {}
PermissionStatus System_GetPermissionStatus( SystemPermission permission ) { return PERMISSION_STATUS_GRANTED; }

std::string System_GetProperty( SystemProperty prop ) {
  static bool hasCheckedGPUDriverVersion = false;
  switch ( prop ) {
  case SYSPROP_NAME:
    return osName;
  case SYSPROP_LANGREGION:
    return langRegion;
  case SYSPROP_CLIPBOARD_TEXT:
    return std::string();
  case SYSPROP_GPUDRIVER_VERSION:
    return std::string();
  default:
    return "";
  }
}

// UGLY!
extern WindowsAudioBackend *winAudioBackend;

int System_GetPropertyInt( SystemProperty prop ) {
  switch ( prop ) {
  case SYSPROP_AUDIO_SAMPLE_RATE:
    return winAudioBackend ? winAudioBackend->GetSampleRate() : -1;
  case SYSPROP_DISPLAY_REFRESH_RATE:
    return 60000;
  case SYSPROP_DEVICE_TYPE:
    return DEVICE_TYPE_DESKTOP;
  default:
    return -1;
  }
}

void System_SendMessage( const char *command, const char *parameter ) {
}

void LaunchBrowser( const char *url ) {
  std::string urls( url );
  std::wstring urlw( urls.begin(), urls.end() );
  auto uri = ref new Windows::Foundation::Uri( ref new Platform::String( urlw.data() ) );
  Windows::System::Launcher::LaunchUriAsync( uri );
}

bool IsVistaOrHigher() {
  return true;
}

void Vibrate( int length_ms ) {
#if _M_ARM
  if ( length_ms == -1 || length_ms == -3 )
    length_ms = 50;
  else if ( length_ms == -2 )
    length_ms = 25;
  else
    return;

  auto timeSpan = Windows::Foundation::TimeSpan();
  timeSpan.Duration = length_ms * 10000;
  Windows::Phone::Devices::Notification::VibrationDevice::GetDefault()->Vibrate( timeSpan );
#endif
}

bool System_InputBoxGetString( const char *title, const char *defaultValue, char *outValue, size_t outLength ) {
  return false;
}

std::vector<std::wstring> GetWideCmdLine() {
  std::vector<std::wstring> wideArgs;
  return wideArgs;
}

namespace MainWindow
{
HWND GetHWND() {
  return nullptr;
}
}

MainPage::MainPage()
{
  InitializeComponent();

#ifndef _DEBUG
  bool showLog = false;
#else
  bool showLog = false;
#endif

  const std::string &exePath = File::GetExeDirectory();
  VFSRegister( "", new DirectoryAssetReader( ( exePath + "/Content/" ).c_str() ) );
  VFSRegister( "", new DirectoryAssetReader( exePath.c_str() ) );

  wchar_t lcCountry[ 256 ];

  if ( 0 != GetLocaleInfoEx( LOCALE_NAME_USER_DEFAULT, LOCALE_SNAME, lcCountry, 256 ) ) {
    langRegion = ConvertWStringToUTF8( lcCountry );
    for ( size_t i = 0; i < langRegion.size(); i++ ) {
      if ( langRegion[ i ] == '-' )
        langRegion[ i ] = '_';
    }
  }
  else {
    langRegion = "en_US";
  }

#if _M_ARM
  osName = "Microsoft Windows 10 ARM";
#else
  osName = "Microsoft Windows 10 x86";
#endif

  char configFilename[ MAX_PATH ] = { 0 };
  char controlsConfigFilename[ MAX_PATH ] = { 0 };

  // On Win32 it makes more sense to initialize the system directories here 
  // because the next place it was called was in the EmuThread, and it's too late by then.
  InitSysDirectories();

  // Load config up here, because those changes below would be overwritten
  // if it's not loaded here first.
  g_Config.AddSearchPath( "" );
  g_Config.AddSearchPath( GetSysDirectory( DIRECTORY_SYSTEM ) );
  g_Config.SetDefaultPath( GetSysDirectory( DIRECTORY_SYSTEM ) );
  g_Config.Load( configFilename, controlsConfigFilename );

  bool debugLogLevel = false;

  g_Config.iGPUBackend = GPU_BACKEND_OPENGL;
  g_Config.bSoftwareRendering = false;

#ifdef _DEBUG
  g_Config.bEnableLogging = true;
#endif

  LogManager::Init();

  if ( debugLogLevel )
    LogManager::GetInstance()->SetAllLogLevels( LogTypes::LDEBUG );

  dp_xres = 480;
  dp_yres = 272;

  pixel_xres = 480;
  pixel_yres = 272;

  std::string initError;
  host = new UWPWindowsHost(DrawSurface);

#if _M_ARM
  Windows::Phone::UI::Input::HardwareButtons::BackPressed += ref new EventHandler< Windows::Phone::UI::Input::BackPressedEventArgs^ >( this, &MainPage::OnBackPressed );
#endif

  KeyDown += ref new Windows::UI::Xaml::Input::KeyEventHandler( this, &UWP::MainPage::OnKeyDown );
  KeyUp += ref new Windows::UI::Xaml::Input::KeyEventHandler( this, &UWP::MainPage::OnKeyUp );

  DrawSurface->SizeChanged += ref new Windows::UI::Xaml::SizeChangedEventHandler( this, &MainPage::OnSwapChainPanelSizeChanged );
  DrawSurface->CompositionScaleChanged += ref new TypedEventHandler<SwapChainPanel^, Object^>( this, &MainPage::OnCompositionScaleChanged );

  auto workItemHandler = ref new Windows::System::Threading::WorkItemHandler( [ this ]( IAsyncAction ^ )
  {
    m_coreInput = DrawSurface->CreateCoreIndependentInputSource(
      Windows::UI::Core::CoreInputDeviceTypes::Mouse |
      Windows::UI::Core::CoreInputDeviceTypes::Touch |
      Windows::UI::Core::CoreInputDeviceTypes::Pen
      );

    m_coreInput->PointerPressed += ref new TypedEventHandler<Object^, Windows::UI::Core::PointerEventArgs^>( this, &MainPage::OnPointerPressed );
    m_coreInput->PointerMoved += ref new TypedEventHandler<Object^, Windows::UI::Core::PointerEventArgs^>( this, &MainPage::OnPointerMoved );
    m_coreInput->PointerReleased += ref new TypedEventHandler<Object^, Windows::UI::Core::PointerEventArgs^>( this, &MainPage::OnPointerReleased );

    m_coreInput->Dispatcher->ProcessEvents( Windows::UI::Core::CoreProcessEventsOption::ProcessUntilQuit );
  } );

  m_inputLoopWorker = Windows::System::Threading::ThreadPool::RunAsync( workItemHandler, Windows::System::Threading::WorkItemPriority::High, Windows::System::Threading::WorkItemOptions::TimeSliced );
}

static bool IsWindowSmall( int width ) {
  return width < 480 + 80;
}

static bool UpdateScreenScale( int width, int height, int dpi, float compoScale, bool smallWindow ) {
  g_dpi = dpi;
  g_dpi_scale = 144.0f / dpi;
  pixel_in_dps = 1.0f / g_dpi_scale;

  int new_dp_xres = width * g_dpi_scale;
  int new_dp_yres = height * g_dpi_scale;

  bool dp_changed = new_dp_xres != dp_xres || new_dp_yres != dp_yres;
  bool px_changed = pixel_xres != width || pixel_yres != height;

  if ( dp_changed || px_changed ) {
    dp_xres = new_dp_xres;
    dp_yres = new_dp_yres;
    pixel_xres = width;
    pixel_yres = height;

    NativeResized();
    return true;
  }
  return false;
}

void MainPage::OnResolution()
{
  //   int width  = e->NewSize.Width;
  //   int height = e->NewSize.Height;
  ResetEvent( ( (UWPWindowsHost*)host )->resizeLock );

  compositionScaleX = DrawSurface->CompositionScaleX;
  compositionScaleY = DrawSurface->CompositionScaleY;

  int width = std::max( 1.0, DrawSurface->ActualWidth  * DrawSurface->CompositionScaleX );
  int height = std::max( 1.0, DrawSurface->ActualHeight * DrawSurface->CompositionScaleY );

  {
    char msg[ 256 ];
    sprintf_s( msg, "whs: %f, %f, %f, %f\n", DrawSurface->ActualWidth, DrawSurface->ActualHeight, DrawSurface->CompositionScaleX, DrawSurface->CompositionScaleY );
    OutputDebugStringA( msg );
  }

  if ( width >= 4 && height >= 4 ) {
    PSP_CoreParameter().pixelWidth = width;
    PSP_CoreParameter().pixelHeight = height;
  }

  int dpi = Windows::Graphics::Display::DisplayInformation::GetForCurrentView()->LogicalDpi;
  int rdpix = Windows::Graphics::Display::DisplayInformation::GetForCurrentView()->RawDpiX;
  int rdpiy = Windows::Graphics::Display::DisplayInformation::GetForCurrentView()->RawDpiY;

  {
    char msg[ 256 ];
    sprintf_s( msg, "size: %d, %d, %d, %d, %d\n", width, height, dpi, rdpix, rdpiy );
    OutputDebugStringA( msg );
  }

  if ( UpdateScreenScale( width, height, dpi, compositionScaleX, IsWindowSmall( width ) ) )
    NativeMessageReceived( "gpu resized", "" );

  if ( !emuRunning )
  {
    EmuThread_Start();
#ifndef _M_ARM
    InputDevice::BeginPolling();
#endif
    emuRunning = true;
  }

  WaitForSingleObject( ( (UWPWindowsHost*)host )->resizeLock, 1000 );
}

void MainPage::OnSwapChainPanelSizeChanged( Object ^ sender, Windows::UI::Xaml::SizeChangedEventArgs ^ e )
{
  OnResolution();
}

void UWP::MainPage::OnCompositionScaleChanged( Windows::UI::Xaml::Controls::SwapChainPanel^ sender, Object^ args )
{
  OnResolution();
}

void UWP::MainPage::OnPointerPressed( Platform::Object ^ sender, Windows::UI::Core::PointerEventArgs ^ e )
{
  int x = Round( e->CurrentPoint->Position.X * compositionScaleX * g_dpi_scale );
  int y = Round( e->CurrentPoint->Position.Y * compositionScaleX * g_dpi_scale );

  int pointerId = TouchId( e->CurrentPoint->PointerId );
  if ( pointerId < 0 )
    pointerId = AddNewTouch( e->CurrentPoint->PointerId, x, y );
  else
    return;

  {
    lock_guard guard( input_state.lock );
    input_state.mouse_valid = true;
    input_state.pointer_down[ pointerId ] = true;

    input_state.pointer_x[ pointerId ] = x;
    input_state.pointer_y[ pointerId ] = y;
  }

  TouchInput touch;
  touch.id    = pointerId;
  touch.flags = TOUCH_DOWN;
  touch.x     = x;
  touch.y     = y;
  NativeTouch( touch );
}

void UWP::MainPage::OnPointerMoved( Platform::Object ^ sender, Windows::UI::Core::PointerEventArgs ^ e )
{
  int x = Round( e->CurrentPoint->Position.X * compositionScaleX * g_dpi_scale );
  int y = Round( e->CurrentPoint->Position.Y * compositionScaleX * g_dpi_scale );

  int pointerId = TouchId( e->CurrentPoint->PointerId );
  if ( pointerId < 0 )
    return;

  touches[ pointerId ].x = x;
  touches[ pointerId ].y = y;

  {
    lock_guard guard( input_state.lock );
    input_state.pointer_x[ pointerId ] = x;
    input_state.pointer_y[ pointerId ] = y;
  }

  if ( e->CurrentPoint->PointerDevice->PointerDeviceType != Windows::Devices::Input::PointerDeviceType::Mouse || e->CurrentPoint->Properties->IsLeftButtonPressed ) 
  {
    TouchInput touch;
    touch.id    = pointerId;
    touch.flags = TOUCH_MOVE;
    touch.x     = x;
    touch.y     = y;
    NativeTouch( touch );
  }
}

void UWP::MainPage::OnPointerReleased( Platform::Object ^ sender, Windows::UI::Core::PointerEventArgs ^ e )
{
  int x = Round( e->CurrentPoint->Position.X * compositionScaleX * g_dpi_scale );
  int y = Round( e->CurrentPoint->Position.Y * compositionScaleX * g_dpi_scale );

  int pointerId = RemoveTouch( e->CurrentPoint->PointerId );
  if ( pointerId < 0 )
    return;

  {
    lock_guard guard( input_state.lock );
    input_state.pointer_down[ pointerId ] = false;
    input_state.pointer_x[ pointerId ] = x;
    input_state.pointer_y[ pointerId ] = y;
  }

  TouchInput touch;
  touch.id    = pointerId;
  touch.flags = TOUCH_UP;
  touch.x     = x;
  touch.y     = y;
  NativeTouch( touch );
}

int ToKeyCode( Windows::System::VirtualKey k )
{
  switch ( k )
  {
  case Windows::System::VirtualKey::Up:
    return NKCODE_DPAD_UP;
  case Windows::System::VirtualKey::Down:
    return NKCODE_DPAD_DOWN;
  case Windows::System::VirtualKey::Left:
    return NKCODE_DPAD_LEFT;
  case Windows::System::VirtualKey::Right:
    return NKCODE_DPAD_RIGHT;
  }

  return 0;
}

void UWP::MainPage::OnKeyDown( Platform::Object ^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs ^ e )
{
  KeyInput key;

  key.flags   = KEY_DOWN;
  key.keyCode = ToKeyCode( e->OriginalKey );

  if ( key.keyCode ) 
    NativeKey( key );
}

void UWP::MainPage::OnKeyUp( Platform::Object ^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs ^ e )
{
  KeyInput key;

  key.flags   = KEY_UP;
  key.keyCode = ToKeyCode( e->OriginalKey );

  if ( key.keyCode ) 
    NativeKey( key );
}

#if _M_ARM
void UWP::MainPage::OnBackPressed( Object^ sender, Windows::Phone::UI::Input::BackPressedEventArgs^ e )
{
  if ( GetUIState() == UISTATE_INGAME )
  {
    NativeMessageReceived( "pause", "" );
  }
  else
  {
    KeyInput key;

    key.flags = KEY_DOWN;
    key.keyCode = NKCODE_ESCAPE;
    NativeKey( key );

    key.flags = KEY_UP;
    NativeKey( key );
  }

  e->Handled = true;
}
#endif
