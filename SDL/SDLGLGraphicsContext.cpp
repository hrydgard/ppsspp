#include "SDLGLGraphicsContext.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "base/NativeApp.h"
#include "base/display.h"
#include "gfx_es2/gpu_features.h"

class GLRenderManager;

// Returns 0 on success.
int SDLGLGraphicsContext::Init(SDL_Window *&window, int x, int y, int mode, std::string *error_message) {
	struct GLVersionPair {
		int major;
		int minor;
	};
	GLVersionPair attemptVersions[] = {
#ifdef USING_GLES2
		{3, 2}, {3, 1}, {3, 0}, {2, 0},
#else
		{4, 6}, {4, 5}, {4, 4}, {4, 3}, {4, 2}, {4, 1}, {4, 0},
		{3, 3}, {3, 2}, {3, 1}, {3, 0},
#endif
	};

#ifdef USING_GLES2
	mode |= SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN;
#else
	mode |= SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
#endif
	SDL_GLContext glContext = nullptr;
	for (size_t i = 0; i < ARRAY_SIZE(attemptVersions); ++i) {
		const auto &ver = attemptVersions[i];
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

		window = SDL_CreateWindow("PPSSPP", x,y, pixel_xres, pixel_yres, mode);
		if (!window) {
			NativeShutdown();
			fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
			continue;
		}

		glContext = SDL_GL_CreateContext(window);
		if (glContext != nullptr) {
			// Victory, got one.
			break;
		}

		// Let's keep trying.  To be safe, destroy the window - docs say needed to change profile.
		// in practice, it doesn't seem to matter, but maybe it differs by platform.
		SDL_DestroyWindow(window);
	}

	if (glContext == nullptr) {
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 0);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		SetGLCoreContext(false);

		window = SDL_CreateWindow("PPSSPP", x,y, pixel_xres, pixel_yres, mode);
		if (window == nullptr) {
			NativeShutdown();
			fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
			SDL_Quit();
			return 2;
		}

		glContext = SDL_GL_CreateContext(window);
		if (glContext == nullptr) {
			NativeShutdown();
			fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
			SDL_Quit();
			return 2;
		}
	}

#ifdef USING_EGL
	EGL_Init();
#endif

#ifndef USING_GLES2
	// Some core profile drivers elide certain extensions from GL_EXTENSIONS/etc.
	// glewExperimental allows us to force GLEW to search for the pointers anyway.
	if (gl_extensions.IsCoreContext) {
		glewExperimental = true;
	}
	if (GLEW_OK != glewInit()) {
		printf("Failed to initialize glew!\n");
		return 1;
	}
	// Unfortunately, glew will generate an invalid enum error, ignore.
	if (gl_extensions.IsCoreContext)
		glGetError();

	if (GLEW_VERSION_2_0) {
		printf("OpenGL 2.0 or higher.\n");
	} else {
		printf("Sorry, this program requires OpenGL 2.0.\n");
		return 1;
	}
#endif

	// Finally we can do the regular initialization.
	CheckGLExtensions();
	draw_ = Draw::T3DCreateGLContext();
	renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	SetGPUBackend(GPUBackend::OPENGL);
	bool success = draw_->CreatePresets();
	assert(success);
	renderManager_->SetSwapFunction([&]() {
#ifdef USING_EGL
		eglSwapBuffers(g_eglDisplay, g_eglSurface);
#else
		SDL_GL_SwapWindow(window_);
#endif
	});
	window_ = window;
	return 0;
}

void SDLGLGraphicsContext::Shutdown() {
}

void SDLGLGraphicsContext::ShutdownFromRenderThread() {
	delete draw_;
	draw_ = nullptr;

#ifdef USING_EGL
	EGL_Close();
#else
	SDL_GL_DeleteContext(glContext);
#endif
}
