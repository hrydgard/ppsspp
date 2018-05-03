// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <cassert>
#include "Common/Log.h"
#include "Common/CommonWindows.h"
#include "gfx/gl_common.h"
#include "gfx/gl_debug_log.h"
#include "gfx_es2/gpu_features.h"
#include "thin3d/thin3d_create.h"
#include "thin3d/GLRenderManager.h"
#include "GL/gl.h"
#include "GL/wglew.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "util/text/utf8.h"
#include "i18n/i18n.h"
#include "UI/OnScreenDisplay.h"
#include "ext/glslang/glslang/Public/ShaderLang.h"

#include "Windows/W32Util/Misc.h"
#include "Windows/GPU/WindowsGLContext.h"

void WindowsGLContext::SwapBuffers() {
	// We no longer call RenderManager::Swap here, it's handled by the render thread, which
	// we're not on here.

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
}

void WindowsGLContext::Pause() {
	if (!hRC) {
		return;
	}
	if (Core_IsStepping()) {
		return;
	}

	pauseRequested = true;
	DWORD result = WaitForSingleObject(pauseEvent, INFINITE);
	if (result == WAIT_TIMEOUT) {
		ERROR_LOG(G3D, "Wait for pause timed out");
	}
	// OK, we now know the rendering thread is paused.
}

void WindowsGLContext::Resume() {
	if (!hRC) {
		return;
	}
	if (Core_IsStepping() && !resumeRequested) {
		return;
	}

	if (!resumeRequested) {
		ERROR_LOG(G3D, "Not waiting to get resumed");
	} else {
		SetEvent(resumeEvent);
	}
	resumeRequested = false;
}

void FormatDebugOutputARB(char outStr[], size_t outStrSize, GLenum source, GLenum type,
													GLuint id, GLenum severity, const char *msg) {

	char sourceStr[32];
	const char *sourceFmt;
	switch(source) {
	case GL_DEBUG_SOURCE_API_ARB:             sourceFmt = "API"; break;
	case GL_DEBUG_SOURCE_WINDOW_SYSTEM_ARB:   sourceFmt = "WINDOW_SYSTEM"; break;
	case GL_DEBUG_SOURCE_SHADER_COMPILER_ARB: sourceFmt = "SHADER_COMPILER"; break;
	case GL_DEBUG_SOURCE_THIRD_PARTY_ARB:     sourceFmt = "THIRD_PARTY"; break;
	case GL_DEBUG_SOURCE_APPLICATION_ARB:     sourceFmt = "APPLICATION"; break;
	case GL_DEBUG_SOURCE_OTHER_ARB:           sourceFmt = "OTHER"; break;
	default:                                  sourceFmt = "UNDEFINED(0x%04X)"; break;
	}
	snprintf(sourceStr, sizeof(sourceStr), sourceFmt, source);

	char typeStr[32];
	const char *typeFmt;
	switch(type) {
	case GL_DEBUG_TYPE_ERROR_ARB:               typeFmt = "ERROR"; break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB: typeFmt = "DEPRECATED_BEHAVIOR"; break;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:  typeFmt = "UNDEFINED_BEHAVIOR"; break;
	case GL_DEBUG_TYPE_PORTABILITY_ARB:         typeFmt = "PORTABILITY"; break;
	case GL_DEBUG_TYPE_PERFORMANCE_ARB:         typeFmt = "PERFORMANCE"; break;
	case GL_DEBUG_TYPE_OTHER_ARB:               typeFmt = "OTHER"; break;
	default:                                    typeFmt = "UNDEFINED(0x%04X)"; break;
	}
	snprintf(typeStr, sizeof(typeStr), typeFmt, type);

	char severityStr[32];
	const char *severityFmt;
	switch (severity) {
	case GL_DEBUG_SEVERITY_HIGH_ARB:   severityFmt = "HIGH"; break;
	case GL_DEBUG_SEVERITY_MEDIUM_ARB: severityFmt = "MEDIUM"; break;
	case GL_DEBUG_SEVERITY_LOW_ARB:    severityFmt = "LOW"; break;
	default:                           severityFmt = "UNDEFINED(%d)"; break;
	}

	snprintf(severityStr, sizeof(severityStr), severityFmt, severity);
	snprintf(outStr, outStrSize, "OpenGL: %s [source=%s type=%s severity=%s id=%d]\n", msg, sourceStr, typeStr, severityStr, id);
}

void DebugCallbackARB(GLenum source, GLenum type, GLuint id, GLenum severity,
											GLsizei length, const GLchar *message, GLvoid *userParam) {
	// Ignore buffer mapping messages from NVIDIA
	if (source == GL_DEBUG_SOURCE_API_ARB && type == GL_DEBUG_TYPE_OTHER_ARB && id == 131185)
		return;

	(void)length;
	FILE *outFile = (FILE *)userParam;
	char finalMessage[1024];
	FormatDebugOutputARB(finalMessage, sizeof(finalMessage), source, type, id, severity, message);
	OutputDebugStringA(finalMessage);

	// Truncate the \n before passing to our log functions.
	size_t len = strlen(finalMessage);
	if (len) {
		finalMessage[len - 1] = '\0';
	}

	switch (type) {
	case GL_DEBUG_TYPE_ERROR_ARB:
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB:
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:
		ERROR_LOG(G3D, "GL: %s", finalMessage);
		break;

	case GL_DEBUG_TYPE_PORTABILITY_ARB:
	case GL_DEBUG_TYPE_PERFORMANCE_ARB:
		NOTICE_LOG(G3D, "GL: %s", finalMessage);
		break;

	case GL_DEBUG_TYPE_OTHER_ARB:
	default:
		// These are just performance warnings.
		VERBOSE_LOG(G3D, "GL: %s", finalMessage);
		break;
	}
}

bool WindowsGLContext::Init(HINSTANCE hInst, HWND window, std::string *error_message) {
	glslang::InitializeProcess();

	hInst_ = hInst;
	hWnd_ = window;
	*error_message = "ok";
	return true;
}

bool WindowsGLContext::InitFromRenderThread(std::string *error_message) {
	*error_message = "ok";
	GLuint PixelFormat;

	// TODO: Change to use WGL_ARB_pixel_format instead
	static const PIXELFORMATDESCRIPTOR pfd = {
		sizeof(PIXELFORMATDESCRIPTOR),							// Size Of This Pixel Format Descriptor
			1,														// Version Number
			PFD_DRAW_TO_WINDOW |									// Format Must Support Window
			PFD_SUPPORT_OPENGL |									// Format Must Support OpenGL
			PFD_DOUBLEBUFFER,										// Must Support Double Buffering
			PFD_TYPE_RGBA,											// Request An RGBA Format
			24,														// Select Our Color Depth
			0, 0, 0, 0, 0, 0,										// Color Bits Ignored
			8,														// No Alpha Buffer
			0,														// Shift Bit Ignored
			0,														// No Accumulation Buffer
			0, 0, 0, 0,										// Accumulation Bits Ignored
			16,														// At least a 16Bit Z-Buffer (Depth Buffer)  
			8,														// 8-bit Stencil Buffer
			0,														// No Auxiliary Buffer
			PFD_MAIN_PLANE,								// Main Drawing Layer
			0,														// Reserved
			0, 0, 0												// Layer Masks Ignored
	};

	hDC = GetDC(hWnd_);

	if (!hDC) {
		*error_message = "Failed to get a device context.";
		return false;											// Return FALSE
	}

	if (!(PixelFormat = ChoosePixelFormat(hDC, &pfd))) {
		*error_message = "Can't find a suitable PixelFormat.";
		return false;
	}

	if (!SetPixelFormat(hDC, PixelFormat, &pfd)) {
		*error_message = "Can't set the PixelFormat.";
		return false;
	}

	if (!(hRC = wglCreateContext(hDC))) {
		*error_message = "Can't create a GL rendering context.";
		return false;
	}

	if (!wglMakeCurrent(hDC, hRC)) {
		*error_message = "Can't activate the GL rendering context.";
		return false;
	}

	// Check for really old OpenGL drivers and refuse to run really early in some cases.

	// TODO: Also either tell the user to give up or point the user to the right websites. Here's some collected
	// information about a system that will not work:

	// GL_VERSION                        GL_VENDOR        GL_RENDERER
	// "1.4.0 - Build 8.14.10.2364"      "intel"          intel Pineview Platform
	I18NCategory *err = GetI18NCategory("Error");

	std::string glVersion = (const char *)glGetString(GL_VERSION);
	std::string glRenderer = (const char *)glGetString(GL_RENDERER);
	const std::string openGL_1 = "1.";

	if (glRenderer == "GDI Generic" || glVersion.substr(0, openGL_1.size()) == openGL_1) {
		//The error may come from 16-bit colour mode
		//Check Colour depth 
		HDC dc = GetDC(NULL);
		u32 colour_depth = GetDeviceCaps(dc, BITSPIXEL);
		ReleaseDC(NULL, dc);
		if (colour_depth != 32) {
			MessageBox(0, L"Please switch your display to 32-bit colour mode", L"OpenGL Error", MB_OK);
			ExitProcess(1);
		}
		const char *defaultError = "Insufficient OpenGL driver support detected!\n\n"
			"Your GPU reports that it does not support OpenGL 2.0. Would you like to try using DirectX instead?\n\n"
			"DirectX is currently compatible with less games, but on your GPU it may be the only choice.\n\n"
			"Visit the forums at http://forums.ppsspp.org for more information.\n\n";

		std::wstring versionDetected = ConvertUTF8ToWString(glVersion + "\n\n");
		std::wstring error = ConvertUTF8ToWString(err->T("InsufficientOpenGLDriver", defaultError));
		std::wstring title = ConvertUTF8ToWString(err->T("OpenGLDriverError", "OpenGL driver error"));
		std::wstring combined = versionDetected + error;

		bool yes = IDYES == MessageBox(hWnd_, combined.c_str(), title.c_str(), MB_ICONERROR | MB_YESNO);

		if (yes) {
			// Change the config to D3D and restart.
			const char *d3d9Or11 = "Direct3D 9? (Or no for Direct3D 11)";
			std::wstring whichD3D9 = ConvertUTF8ToWString(err->T("D3D9or11", d3d9Or11));
			bool d3d9 = IDYES == MessageBox(hWnd_, whichD3D9.c_str(), title.c_str(), MB_YESNO);
			g_Config.iGPUBackend = d3d9 ? (int)GPUBackend::DIRECT3D9 : (int)GPUBackend::DIRECT3D11;
			g_Config.Save();

			W32Util::ExitAndRestart();
		}

		// Avoid further error messages. Let's just bail, it's safe, and we can't continue.
		ExitProcess(1);
	}

	// Some core profile drivers elide certain extensions from GL_EXTENSIONS/etc.
	// glewExperimental allows us to force GLEW to search for the pointers anyway.
	glewExperimental = true;
	if (GLEW_OK != glewInit()) {
		*error_message = "Failed to initialize GLEW.";
		return false;
	}
	// Unfortunately, glew will generate an invalid enum error, ignore.
	glGetError();

	int contextFlags = g_Config.bGfxDebugOutput ? WGL_CONTEXT_DEBUG_BIT_ARB : 0;

	HGLRC m_hrc = nullptr;
	// Alright, now for the modernity. First try a 4.4, then 4.3, context, if that fails try 3.3.
	// I can't seem to find a way that lets you simply request the newest version available.
	if (wglewIsSupported("WGL_ARB_create_context") == 1) {
		for (int tryCore = 1; tryCore >= 0 && m_hrc == nullptr; --tryCore) {
			SetGLCoreContext(tryCore == 1);

			for (int minor = 6; minor >= 0 && m_hrc == nullptr; --minor) {
				const int attribs4x[] = {
					WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
					WGL_CONTEXT_MINOR_VERSION_ARB, minor,
					WGL_CONTEXT_FLAGS_ARB, contextFlags,
					WGL_CONTEXT_PROFILE_MASK_ARB, gl_extensions.IsCoreContext ? WGL_CONTEXT_CORE_PROFILE_BIT_ARB : WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
					0
				};
				m_hrc = wglCreateContextAttribsARB(hDC, 0, attribs4x);
			}
			const int attribs33[] = {
				WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
				WGL_CONTEXT_MINOR_VERSION_ARB, 3,
				WGL_CONTEXT_FLAGS_ARB, contextFlags,
				WGL_CONTEXT_PROFILE_MASK_ARB, gl_extensions.IsCoreContext ? WGL_CONTEXT_CORE_PROFILE_BIT_ARB : WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
				0
			};
			if (!m_hrc)
				m_hrc = wglCreateContextAttribsARB(hDC, 0, attribs33);
		}

		if (!m_hrc) {
			// Fall back
			m_hrc = hRC;
		} else {
			// Switch to the new ARB context.
			wglMakeCurrent(nullptr, nullptr);
			wglDeleteContext(hRC);
			wglMakeCurrent(hDC, m_hrc);
		}
	} else {
		// We can't make a GL 3.x context. Use an old style context (GL 2.1 and before)
		m_hrc = hRC;
	}

	if (GLEW_OK != glewInit()) {
		*error_message = "Failed to re-initialize GLEW.";
		return false;
	}
	// Unfortunately, glew will generate an invalid enum error, ignore.
	if (gl_extensions.IsCoreContext)
		glGetError();

	if (!m_hrc) {
		*error_message = "No m_hrc";
		return false;
	}

	hRC = m_hrc;

	if (g_Config.bGfxDebugOutput) {
		if (wglewIsSupported("GL_KHR_debug") == 1) {
			glGetError();
			glDebugMessageCallback((GLDEBUGPROC)&DebugCallbackARB, nullptr);
			if (glGetError()) {
				ERROR_LOG(G3D, "Failed to register a debug log callback");
			}
			glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
			if (glGetError()) {
				ERROR_LOG(G3D, "Failed to enable synchronous debug output");
			}
		} else if (glewIsSupported("GL_ARB_debug_output")) {
			glGetError();
			glDebugMessageCallbackARB((GLDEBUGPROCARB)&DebugCallbackARB, 0); // print debug output to stderr
			if (glGetError()) {
				ERROR_LOG(G3D, "Failed to register a debug log callback");
			}
			glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
			if (glGetError()) {
				ERROR_LOG(G3D, "Failed to enable synchronous debug output");
			}

			// For extra verbosity uncomment this (MEDIUM and HIGH are on by default):
			// glDebugMessageControlARB(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_LOW_ARB, 0, nullptr, GL_TRUE);
		}

		glEnable(GL_DEBUG_OUTPUT);
	}

	pauseRequested = false;
	resumeRequested = false;

	// These are auto-reset events.
	pauseEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	resumeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	CheckGLExtensions();
	draw_ = Draw::T3DCreateGLContext();
	renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	SetGPUBackend(GPUBackend::OPENGL);
	bool success = draw_->CreatePresets();  // if we get this far, there will always be a GLSL compiler capable of compiling these.
	assert(success);
	renderManager_->SetSwapFunction([&]() {::SwapBuffers(hDC); });
	if (wglSwapIntervalEXT) {
		// glew loads wglSwapIntervalEXT if available
		renderManager_->SetSwapIntervalFunction([&](int interval) {
			wglSwapIntervalEXT(interval);
		});
	}
	CHECK_GL_ERROR_IF_DEBUG();
	return true;												// Success
}

void WindowsGLContext::SwapInterval(int interval) {
	// Delegate to the render manager to make sure it's done on the right thread.
	renderManager_->SwapInterval(interval);
}

void WindowsGLContext::Shutdown() {
	glslang::FinalizeProcess();
}

void WindowsGLContext::ShutdownFromRenderThread() {
	delete draw_;
	draw_ = nullptr;
	CloseHandle(pauseEvent);
	CloseHandle(resumeEvent);
	if (hRC) {
		// Are we able to release the DC and RC contexts?
		if (!wglMakeCurrent(NULL,NULL)) {
			MessageBox(NULL, L"Release of DC and RC failed.", L"SHUTDOWN ERROR", MB_OK | MB_ICONINFORMATION);
		}

		// Are we able to delete the RC?
		if (!wglDeleteContext(hRC)) {
			MessageBox(NULL, L"Release rendering context failed.", L"SHUTDOWN ERROR", MB_OK | MB_ICONINFORMATION);
		}
		hRC = NULL;
	}

	if (hDC && !ReleaseDC(hWnd_, hDC)) {
		DWORD err = GetLastError();
		if (err != ERROR_DC_NOT_FOUND) {
			MessageBox(NULL, L"Release device context failed.", L"SHUTDOWN ERROR", MB_OK | MB_ICONINFORMATION);
		}
		hDC = NULL;
	}
	hWnd_ = NULL;
}

void WindowsGLContext::Resize() {
}

void WindowsGLContext::ThreadStart() {
	renderManager_->ThreadStart();
}

bool WindowsGLContext::ThreadFrame() {
	return renderManager_->ThreadFrame();
}

void WindowsGLContext::ThreadEnd() {
	renderManager_->ThreadEnd();
}

void WindowsGLContext::StopThread() {
	renderManager_->WaitUntilQueueIdle();
	renderManager_->StopThread();
}
