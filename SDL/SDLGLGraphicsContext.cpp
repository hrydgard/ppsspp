#include <vector>

#include "SDLGLGraphicsContext.h"

#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/thin3d_create.h"

#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/System/Display.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"

#if defined(USING_EGL)
#include "EGL/egl.h"
#if !defined(USING_FBDEV) && !defined(__APPLE__)
#include <X11/Xlib.h>
#endif
#endif

class GLRenderManager;

#if defined(USING_EGL)

// TODO: Move these into the class.
static EGLDisplay               g_eglDisplay    = EGL_NO_DISPLAY;
static EGLContext               g_eglContext    = nullptr;
static EGLSurface               g_eglSurface    = nullptr;
static EGLNativeDisplayType     g_Display       = nullptr;
static bool                     g_XDisplayOpen  = false;
static EGLNativeWindowType      g_Window        = (EGLNativeWindowType)nullptr;
static bool useEGLSwap = false;

int CheckEGLErrors(const char *file, int line, std::string *errorMessage) {
	EGLenum error;
	const char *errortext = "unknown";
	error = eglGetError();
	switch (error)
	{
		case EGL_SUCCESS: case 0:           return 0;
		case EGL_NOT_INITIALIZED:           errortext = "EGL_NOT_INITIALIZED"; break;
		case EGL_BAD_ACCESS:                errortext = "EGL_BAD_ACCESS"; break;
		case EGL_BAD_ALLOC:                 errortext = "EGL_BAD_ALLOC"; break;
		case EGL_BAD_ATTRIBUTE:             errortext = "EGL_BAD_ATTRIBUTE"; break;
		case EGL_BAD_CONTEXT:               errortext = "EGL_BAD_CONTEXT"; break;
		case EGL_BAD_CONFIG:                errortext = "EGL_BAD_CONFIG"; break;
		case EGL_BAD_CURRENT_SURFACE:       errortext = "EGL_BAD_CURRENT_SURFACE"; break;
		case EGL_BAD_DISPLAY:               errortext = "EGL_BAD_DISPLAY"; break;
		case EGL_BAD_SURFACE:               errortext = "EGL_BAD_SURFACE"; break;
		case EGL_BAD_MATCH:                 errortext = "EGL_BAD_MATCH"; break;
		case EGL_BAD_PARAMETER:             errortext = "EGL_BAD_PARAMETER"; break;
		case EGL_BAD_NATIVE_PIXMAP:         errortext = "EGL_BAD_NATIVE_PIXMAP"; break;
		case EGL_BAD_NATIVE_WINDOW:         errortext = "EGL_BAD_NATIVE_WINDOW"; break;
		default:                            errortext = "unknown"; break;
	}
	if (errorMessage) {
		*errorMessage += StringFromFormat("EGL Error %s detected in file %s at line %d (0x%X)\n", errortext, file, line, error);
	}
	return 1;
}

static bool EGL_OpenInit(std::string *errorMessage) {
	if ((g_eglDisplay = eglGetDisplay(g_Display)) == EGL_NO_DISPLAY) {
		CheckEGLErrors(__FILE__, __LINE__, errorMessage);
		if (errorMessage) {
			*errorMessage += "Unable to create EGL display.\n";
		}
		return false;
	}
	if (eglInitialize(g_eglDisplay, NULL, NULL) != EGL_TRUE) {
		CheckEGLErrors(__FILE__, __LINE__, errorMessage);
		if (errorMessage) {
			*errorMessage += "Unable to initialize EGL display.\n";
		}
		eglTerminate(g_eglDisplay);
		g_eglDisplay = EGL_NO_DISPLAY;
		return false;
	}

	return true;
}

static int8_t EGL_Open(SDL_Window *window, std::string *errorMessage) {
#if defined(USING_FBDEV)
	g_Display = (EGLNativeDisplayType)nullptr;
	g_Window = (EGLNativeWindowType)nullptr;
#elif defined(__APPLE__)
	g_Display = (EGLNativeDisplayType)XOpenDisplay(nullptr);
	g_XDisplayOpen = g_Display != nullptr;
	if (!g_XDisplayOpen) {
		if (errorMessage) {
			*errorMessage += "Unable to get display!\n";
		}
		return 1;
	}
	g_Window = (EGLNativeWindowType)nullptr;
#else
	// Get the SDL window native handle
	SDL_PropertiesID windowProps = SDL_GetWindowProperties(window);
	void *x11Display = SDL_GetPointerProperty(windowProps, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
	if (x11Display != nullptr) {
		g_Display = (EGLNativeDisplayType)x11Display;
		g_Window = (EGLNativeWindowType)(uintptr_t)SDL_GetNumberProperty(windowProps, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
	} else {
		void *waylandDisplay = SDL_GetPointerProperty(windowProps, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
		void *waylandEGLWindow = SDL_GetPointerProperty(windowProps, SDL_PROP_WINDOW_WAYLAND_EGL_WINDOW_POINTER, nullptr);
		if (waylandDisplay != nullptr && waylandEGLWindow != nullptr) {
			g_Display = (EGLNativeDisplayType)waylandDisplay;
			g_Window = (EGLNativeWindowType)waylandEGLWindow;
		} else {
			if (errorMessage) {
				*errorMessage += "Unable to retrieve native window properties, falling back to X11.\n";
			}
			g_Display = (EGLNativeDisplayType)XOpenDisplay(nullptr);
			g_XDisplayOpen = g_Display != nullptr;
			if (!g_XDisplayOpen) {
				if (errorMessage) {
					*errorMessage += "Unable to get display!\n";
				}
				return 1;
			}
			g_Window = (EGLNativeWindowType)nullptr;
		}
	}

	if (!EGL_OpenInit(errorMessage)) {
		g_Display = (EGLNativeDisplayType)XOpenDisplay(nullptr);
		g_XDisplayOpen = g_Display != nullptr;
		if (!g_XDisplayOpen) {
			if (errorMessage) {
				*errorMessage += "Unable to get display!\n";
			}
			return 1;
		}
		g_Window = (EGLNativeWindowType)nullptr;
	}

#endif
	if (g_eglDisplay == EGL_NO_DISPLAY)
		EGL_OpenInit(errorMessage);
	return g_eglDisplay == EGL_NO_DISPLAY ? 1 : 0;
}

#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR (1 << 6)
#endif

EGLConfig EGL_FindConfig(int *contextVersion, std::string *errorMessage) {
	std::vector<EGLConfig> configs;
	EGLint numConfigs = 0;

	EGLBoolean result = eglGetConfigs(g_eglDisplay, nullptr, 0, &numConfigs);
	if (result != EGL_TRUE || numConfigs == 0) {
		CheckEGLErrors(__FILE__, __LINE__, errorMessage);
		if (errorMessage) {
			*errorMessage += "eglGetConfigs failed to return any configs.\n";
		}
		return nullptr;
	}

	configs.resize(numConfigs);
	result = eglGetConfigs(g_eglDisplay, &configs[0], numConfigs, &numConfigs);
	if (result != EGL_TRUE || numConfigs == 0) {
		CheckEGLErrors(__FILE__, __LINE__, errorMessage);
		if (errorMessage) {
			*errorMessage += "eglGetConfigs failed to return any configs.\n";
		}
		return nullptr;
	}

	// Mali (ARM) seems to have compositing issues with alpha backbuffers.
	// EGL_TRANSPARENT_TYPE doesn't help.
	const char *vendorName = eglQueryString(g_eglDisplay, EGL_VENDOR);
	const bool avoidAlphaGLES = vendorName && !strcmp(vendorName, "ARM");

	EGLConfig best = nullptr;
	int bestScore = 0;
	int bestContextVersion = 0;
	for (const EGLConfig &config : configs) {
		auto readConfig = [&](EGLint attr) -> EGLint {
			EGLint val = 0;
			eglGetConfigAttrib(g_eglDisplay, config, attr, &val);
			return val;
		};

		// We don't want HDR modes with more than 8 bits per component.
		// But let's assume some color is better than no color at all.
		auto readConfigMax = [&](EGLint attr, EGLint m, EGLint def = 1) -> EGLint {
			EGLint val = readConfig(attr);
			return val > m ? def : val;
		};

		int colorScore = readConfigMax(EGL_RED_SIZE, 8) + readConfigMax(EGL_BLUE_SIZE, 8) + readConfigMax(EGL_GREEN_SIZE, 8);
		int alphaScore = readConfigMax(EGL_ALPHA_SIZE, 8);
		int depthScore = readConfig(EGL_DEPTH_SIZE);
		int levelScore = readConfig(EGL_LEVEL) == 0 ? 100 : 0;
		int samplesScore = readConfig(EGL_SAMPLES) == 0 ? 100 : 0;
		int sampleBufferScore = readConfig(EGL_SAMPLE_BUFFERS) == 0 ? 100 : 0;
		int stencilScore = readConfig(EGL_STENCIL_SIZE);
		int transparentScore = readConfig(EGL_TRANSPARENT_TYPE) == EGL_NONE ? 50 : 0;

		EGLint caveat = readConfig(EGL_CONFIG_CAVEAT);
		// Let's assume that non-conformant configs aren't so awful.
		int caveatScore = caveat == EGL_NONE ? 100 : (caveat == EGL_NON_CONFORMANT_CONFIG ? 95 : 0);

#ifndef USING_FBDEV
		EGLint surfaceType = readConfig(EGL_SURFACE_TYPE);
		// Only try a non-Window config in the worst case when there are only non-Window configs.
		int surfaceScore = (surfaceType & EGL_WINDOW_BIT) ? 1000 : 0;
#endif

		EGLint renderable = readConfig(EGL_RENDERABLE_TYPE);
		bool renderableGLES3 = (renderable & EGL_OPENGL_ES3_BIT_KHR) != 0;
		bool renderableGLES2 = (renderable & EGL_OPENGL_ES2_BIT) != 0;
		bool renderableGL = (renderable & EGL_OPENGL_BIT) != 0;
#ifdef USING_GLES2
		int renderableScoreGLES = renderableGLES3 ? 100 : (renderableGLES2 ? 80 : 0);
		int renderableScoreGL = 0;
#else
		int renderableScoreGLES = 0;
		int renderableScoreGL = renderableGL ? 100 : (renderableGLES3 ? 80 : 0);
#endif

		if (avoidAlphaGLES && renderableScoreGLES > 0) {
			alphaScore = 8 - alphaScore;
		}

		int score = 0;
		// Here's a good place to play with the weights to pick a better config.
		score += colorScore * 10 + alphaScore * 2;
		score += depthScore * 5 + stencilScore;
		score += levelScore + samplesScore + sampleBufferScore + transparentScore;
		score += caveatScore + renderableScoreGLES + renderableScoreGL;

#ifndef USING_FBDEV
		score += surfaceScore;
#endif

		if (score > bestScore) {
			bestScore = score;
			best = config;
			bestContextVersion = renderableGLES3 ? 3 : (renderableGLES2 ? 2 : 0);
		}
	}

	*contextVersion = bestContextVersion;
	return best;
}

int8_t EGL_Init(SDL_Window *window, std::string *errorMessage) {
	int contextVersion = 0;
	EGLConfig eglConfig = EGL_FindConfig(&contextVersion, errorMessage);
	if (!eglConfig) {
		if (errorMessage) {
			*errorMessage += "Unable to find a usable EGL config.\n";
		}
		return 1;
	}

	EGLint contextAttributes[] = {
		EGL_CONTEXT_CLIENT_VERSION, contextVersion,
		EGL_NONE,
	};
	if (contextVersion == 0) {
		contextAttributes[0] = EGL_NONE;
	}

	g_eglContext = eglCreateContext(g_eglDisplay, eglConfig, nullptr, contextAttributes);
	if (g_eglContext == EGL_NO_CONTEXT) {
		CheckEGLErrors(__FILE__, __LINE__, errorMessage);
		if (errorMessage) {
			*errorMessage += "Unable to create GLES context!\n";
		}
		return 1;
	}

	g_eglSurface = eglCreateWindowSurface(g_eglDisplay, eglConfig, g_Window, nullptr);
	if (g_eglSurface == EGL_NO_SURFACE) {
		CheckEGLErrors(__FILE__, __LINE__, errorMessage);
		if (errorMessage) {
			*errorMessage += "Unable to create EGL surface!\n";
		}
		return 1;
	}

	if (eglMakeCurrent(g_eglDisplay, g_eglSurface, g_eglSurface, g_eglContext) != EGL_TRUE) {
		CheckEGLErrors(__FILE__, __LINE__, errorMessage);
		if (errorMessage) {
			*errorMessage += "Unable to make GLES context current.\n";
		}
		return 1;
	}

	return 0;
}

void EGL_Close() {
	if (g_eglDisplay != EGL_NO_DISPLAY) {
		eglMakeCurrent(g_eglDisplay, NULL, NULL, EGL_NO_CONTEXT);
		if (g_eglContext != NULL) {
			eglDestroyContext(g_eglDisplay, g_eglContext);
		}
		if (g_eglSurface != NULL) {
			eglDestroySurface(g_eglDisplay, g_eglSurface);
		}
		eglTerminate(g_eglDisplay);
		g_eglDisplay = EGL_NO_DISPLAY;
	}
	if (g_Display != nullptr) {
#if !defined(USING_FBDEV)
		if (g_XDisplayOpen)
			XCloseDisplay((Display *)g_Display);
#endif
		g_XDisplayOpen = false;
		g_Display = nullptr;
	}
	g_eglSurface = NULL;
	g_eglContext = NULL;
}

#endif // USING_EGL

SDL_Window *CreateSDLGLWindowAndContext(int x, int y, int w, int h, int mode, int forceGLVersion, SDL_GLContext *glContextOut, std::string *errorMessage) {
	// We start hidden because we have to try several windows.
	// On Mac, full screen animates so each attempt is slow.
	mode |= SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN;
	struct GLVersionPair {
		int major;
		int minor;
	};
	static const GLVersionPair attemptVersions[] = {
#ifdef USING_GLES2
		{3, 2}, {3, 1}, {3, 0}, {2, 0},
#else
		{4, 6}, {4, 5}, {4, 4}, {4, 3}, {4, 2}, {4, 1}, {4, 0},
		{3, 3}, {3, 2}, {3, 1}, {3, 0},
#endif
	};

	SDL_Window *window = nullptr;

	SDL_GLContext glContext{};
	for (size_t i = 0; i < ARRAY_SIZE(attemptVersions); ++i) {
		const auto &ver = attemptVersions[i];
		// If we force a specific OpenGL version, skip the ones
		// that do not match, which may be all of them - e.g.
		// requesting nonsensical "--graphics=opengl0" reliably
		// skips straight to fallback code below.
		if (forceGLVersion >= 0 && 10 * ver.major + ver.minor != forceGLVersion) {
			continue;
		}
		// Make sure to request a somewhat modern GL context at least - the
		// latest supported by MacOS X (really, really sad...)
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, ver.major);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, ver.minor);
#ifdef USING_GLES2
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		SetGLCoreContext(false);
#else
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SetGLCoreContext(true);
#endif
		window = SDL_CreateWindow("PPSSPP", w, h, (SDL_WindowFlags)mode);
		if (!window) {
			// Definitely don't shutdown here: we'll keep trying more GL versions.
			if (errorMessage) {
				*errorMessage += StringFromFormat("SDL_CreateWindow failed for GL %d.%d: %s\n", ver.major, ver.minor, SDL_GetError());
			}
			// Skip the DestroyWindow.
			continue;
		}

		glContext = SDL_GL_CreateContext(window);
		if (glContext) {
			// Victory, got one. Window should now be valid.
			break;
		}

		// Let's keep trying.  To be safe, destroy the window - docs say needed to change profile.
		// in practice, it doesn't seem to matter, but maybe it differs by platform.
		SDL_DestroyWindow(window);
		window = nullptr;
	}

	if (!glContext) {
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 0);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		SetGLCoreContext(false);

		_dbg_assert_(window == nullptr);

		window = SDL_CreateWindow("PPSSPP", w, h, (SDL_WindowFlags)mode);
		if (window == nullptr) {
			if (errorMessage) {
				*errorMessage += StringFromFormat("SDL_CreateWindow failed: %s\n", SDL_GetError());
			}
			return nullptr;
		}

		glContext = SDL_GL_CreateContext(window);
		if (!glContext) {
			// OK, now we really have tried everything. We give up.
			if (errorMessage) {
				*errorMessage += StringFromFormat("SDL_GL_CreateContext failed: %s\n", SDL_GetError());
			}
			SDL_DestroyWindow(window);
			return nullptr;
		}
	}

	// For some reason we have to set the position here, can't wait until after the window is shown (??).
	if (x != SDL_WINDOWPOS_UNDEFINED && y != SDL_WINDOWPOS_UNDEFINED) {
		SDL_SetWindowPosition(window, x, y);
	}

#ifdef USING_EGL
	// EGL is optional here - if it fails, we just keep using the regular SDL/GLX swap set up above.
	std::string eglError;
	if (EGL_Open(window, &eglError) != 0) {
		WARN_LOG(Log::G3D, "EGL_Open() failed: %s", eglError.c_str());
	} else if (EGL_Init(window, &eglError) != 0) {
		WARN_LOG(Log::G3D, "EGL_Init() failed: %s", eglError.c_str());
	} else {
		useEGLSwap = true;
	}
#endif

#ifndef USING_GLES2
	// Some core profile drivers elide certain extensions from GL_EXTENSIONS/etc.
	// glewExperimental allows us to force GLEW to search for the pointers anyway.
	if (gl_extensions.IsCoreContext) {
		glewExperimental = true;
	}
	GLenum glew_err = glewInit();
	// glx is not required, igore.
	if (glew_err != GLEW_OK && glew_err != GLEW_ERROR_NO_GLX_DISPLAY) {
		if (errorMessage) {
			*errorMessage += StringFromFormat("Failed to initialize glew: %s\n", (const char *)glewGetErrorString(glew_err));
		}
		SDL_GL_DestroyContext(glContext);
		SDL_DestroyWindow(window);
		return nullptr;
	}
	// Unfortunately, glew will generate an invalid enum error, ignore.
	if (gl_extensions.IsCoreContext)
		glGetError();

	if (GLEW_VERSION_2_0) {
		INFO_LOG(Log::G3D, "OpenGL 2.0 or higher.");
	} else {
		if (errorMessage) {
			*errorMessage += "Sorry, this program requires OpenGL 2.0.\n";
		}
		SDL_GL_DestroyContext(glContext);
		SDL_DestroyWindow(window);
		return nullptr;
	}
#endif
	*glContextOut = glContext;
	return window;
}

bool SDLGLGraphicsContext::InitSurface(WindowSystem winsys, void *data1, void *data2, std::string *error_message) {
	SDL_Window *window = (SDL_Window *)data1;
	SDL_GLContext glContext = (SDL_GLContext)data2;
	if (!window || !glContext) {
		*error_message = "SDLGLGraphicsContext::InitSurface: no window or GL context (window/context creation must have failed)";
		return false;
	}
	glContext_ = glContext;

	// Finally we can do the regular initialization.
	CheckGLExtensions();
	draw_ = Draw::T3DCreateGLContext(true);
	renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	renderManager_->SetInflightFrames(g_Config.iInflightFrames);
	SetGPUBackend(GPUBackend::OPENGL);
	bool success = draw_->CreatePresets();
	_assert_(success);
	renderManager_->SetSwapFunction([&]() {
#ifdef USING_EGL
		if (useEGLSwap)
			eglSwapBuffers(g_eglDisplay, g_eglSurface);
		else
			SDL_GL_SwapWindow(window_);
#else
		SDL_GL_SwapWindow(window_);
#endif
	});
	renderManager_->SetSwapIntervalFunction([&](int interval) {
		INFO_LOG(Log::G3D, "SDL SwapInterval: %d", interval);
		SDL_GL_SetSwapInterval(interval);
	});

	window_ = window;
	return true;
}

void SDLGLGraphicsContext::ShutdownSurface() {
	delete draw_;
	draw_ = nullptr;
	renderManager_ = nullptr;
}

bool SDLGLGraphicsContext::InitAPI(void *ctx, std::string *deviceName, std::string *errorMessage) {
	return true;
}

void SDLGLGraphicsContext::ShutdownAPI() {
#ifdef USING_EGL
	EGL_Close();
#endif
	SDL_GL_DestroyContext(glContext_);
	glContext_ = nullptr;
	window_ = nullptr;
}
