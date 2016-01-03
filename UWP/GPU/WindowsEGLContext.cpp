#include "pch.h"

#include "Common/Log.h"
#include "Core/System.h"

#include "Windows/GPU/WindowsGLContext.h"

#include "EGL/egl.h"
#include "EGL/eglext.h"
#include "GLES2/gl2.h"
#include "GLES2/gl2ext.h"
#include "angle_windowsstore.h"

static EGLDisplay display = EGL_NO_DISPLAY;
static EGLSurface surface = EGL_NO_SURFACE;
static EGLConfig  config  = nullptr;
static EGLContext context = EGL_NO_CONTEXT;
static volatile bool pauseRequested;
static volatile bool resumeRequested;
static HANDLE pauseEvent;
static HANDLE resumeEvent;

static int xres, yres;

void GL_SwapBuffers() {
	eglSwapBuffers(display, surface);

	// Used during fullscreen switching to prevent rendering.
	if (pauseRequested) {
		SetEvent(pauseEvent);
		resumeRequested = true;
		DWORD result = WaitForSingleObject(resumeEvent, INFINITE);
		if (result == WAIT_TIMEOUT) {
			ERROR_LOG(G3D, "Wait for resume timed out. Resuming rendering");
		}
		pauseRequested = false;
	}

	// According to some sources, doing this *after* swapbuffers can reduce frame latency
	// at a large performance cost. So let's not.
	// glFinish();
}

void GL_Pause() {
	if (context == EGL_NO_CONTEXT) {
		return;
	}

	pauseRequested = true;
	DWORD result = WaitForSingleObject(pauseEvent, INFINITE);
	if (result == WAIT_TIMEOUT) {
		ERROR_LOG(G3D, "Wait for pause timed out");
	}
	// OK, we now know the rendering thread is paused.
}

void GL_Resume() {
  if ( context == EGL_NO_CONTEXT ) {
    return;
	}

	if (!resumeRequested) {
		ERROR_LOG(G3D, "Not waiting to get resumed");
	} else {
		SetEvent(resumeEvent);
	}
	resumeRequested = false;
}

EGLSurface CreateEGLSurface(Windows::UI::Xaml::Controls::SwapChainPanel^ window, std::string *error_message)
{
  const EGLint surfaceAttributes[] =
  {
    EGL_ANGLE_SURFACE_RENDER_TO_BACK_BUFFER, EGL_TRUE,
    EGL_NONE
  };

  auto surfaceCreationProperties = ref new Windows::Foundation::Collections::PropertySet();
  surfaceCreationProperties->Insert( ref new Platform::String( EGLNativeWindowTypeProperty ), window );

  // You can configure the surface to render at a lower resolution and be scaled up to 
  // the full window size. The scaling is often free on mobile hardware.
  //
  // One way to configure the SwapChainPanel is to specify precisely which resolution it should render at.
  auto customRenderSurfaceSize = Windows::Foundation::Size( (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight );
  surfaceCreationProperties->Insert( ref new Platform::String( EGLRenderSurfaceSizeProperty ), Windows::Foundation::PropertyValue::CreateSize( customRenderSurfaceSize ) );
  //
  // Another way is to tell the SwapChainPanel to render at a certain scale factor compared to its size.
  // e.g. if the SwapChainPanel is 1920x1280 then setting a factor of 0.5f will make the app render at 960x640
  // float customResolutionScale = 0.5f;
  // surfaceCreationProperties->Insert(ref new String(EGLRenderResolutionScaleProperty), PropertyValue::CreateSingle(customResolutionScale));

  surface = eglCreateWindowSurface( display, config, reinterpret_cast<IInspectable*>( surfaceCreationProperties ), surfaceAttributes );
  if ( surface == EGL_NO_SURFACE )
    *error_message = "Failed to create EGL fullscreen surface";

  return surface;
}

bool GL_Init( Windows::UI::Xaml::Controls::SwapChainPanel^ window, std::string *error_message) 
{
  const EGLint configAttributes[] =
  {
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 0,
    EGL_DEPTH_SIZE, 0,
    EGL_STENCIL_SIZE, 0,
    EGL_NONE
  };

  const EGLint contextAttributes[] =
  {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };

  const EGLint defaultDisplayAttributes[] =
  {
    EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
    EGL_ANGLE_DISPLAY_ALLOW_RENDER_TO_BACK_BUFFER, EGL_TRUE,
    EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE, EGL_TRUE,
    EGL_NONE,
  };

  const EGLint fl9_3DisplayAttributes[] =
  {
    EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
    EGL_PLATFORM_ANGLE_MAX_VERSION_MAJOR_ANGLE, 9,
    EGL_PLATFORM_ANGLE_MAX_VERSION_MINOR_ANGLE, 3,
    EGL_ANGLE_DISPLAY_ALLOW_RENDER_TO_BACK_BUFFER, EGL_TRUE,
    EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE, EGL_TRUE,
    EGL_NONE,
  };

  PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>( eglGetProcAddress( "eglGetPlatformDisplayEXT" ) );
  if ( !eglGetPlatformDisplayEXT )
  {
    *error_message = "Failed to get function eglGetPlatformDisplayEXT";
    return false;
  }

  display = eglGetPlatformDisplayEXT( EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, defaultDisplayAttributes );
  if ( eglInitialize( display, NULL, NULL ) == EGL_FALSE )
  {
    display = eglGetPlatformDisplayEXT( EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, fl9_3DisplayAttributes );
    if ( display == EGL_NO_DISPLAY )
    {
      *error_message = "Failed to get EGL display";
      return false;
    }
    if ( eglInitialize( display, NULL, NULL ) == EGL_FALSE )
    {
      *error_message = "Failed to initialize EGL";
      return false;
    }
  }

  EGLint numConfigs = 0;
  if ( ( eglChooseConfig( display, configAttributes, &config, 1, &numConfigs ) == EGL_FALSE ) || ( numConfigs == 0 ) )
  {
    *error_message = "Failed to choose first EGLConfig";
    return false;
  }

  surface = CreateEGLSurface( window, error_message );
  if ( surface == EGL_NO_SURFACE )
    return false;

  context = eglCreateContext( display, config, EGL_NO_CONTEXT, contextAttributes );
  if ( context == EGL_NO_CONTEXT )
  {
    *error_message = "Failed to create EGL context";
    return false;
  }

  if ( eglMakeCurrent( display, surface, surface, context ) == EGL_FALSE )
  {
    *error_message = "Failed to make fullscreen EGLSurface current";
    return false;
  }

  return true;
}

bool GL_Resize( Windows::UI::Xaml::Controls::SwapChainPanel^ window ) {
  if ( surface == EGL_NO_SURFACE )
    return true;
  
  OutputDebugStringA( "GL_Reisze start...\n" );

  if ( eglMakeCurrent( display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT ) == EGL_FALSE )
  {
    OutputDebugStringA( "GL_Reisze failed to remove surface.\n" );
    return false;
  }

  eglDestroySurface( display, surface );

  std::string error;
  surface = CreateEGLSurface( window, &error );
  if ( surface == EGL_NO_SURFACE )
  {
    OutputDebugStringA( "GL_Reisze failed to create surface.\n" );
    return false;
  }

  if ( eglMakeCurrent( display, surface, surface, context ) == EGL_FALSE )
  {
    OutputDebugStringA( "GL_Reisze failed to make context current.\n" );
    return false;
  }

  OutputDebugStringA( "GL_Reisze succeeded.\n" );

  return true;
}

void GL_SwapInterval(int interval) {
  eglSwapInterval(display, interval);
}

void GL_Shutdown() {
}
