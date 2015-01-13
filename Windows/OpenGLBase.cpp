#include "Common/CommonWindows.h"
#include "native/gfx_es2/gl_state.h"
#include "native/gfx/gl_common.h"
#include "GL/gl.h"
#include "GL/wglew.h"
#include "Core/Config.h"
#include "util/text/utf8.h"
#include "i18n/i18n.h"

#include "Windows/W32Util/Misc.h"
#include "Windows/OpenGLBase.h"

static HDC hDC;     // Private GDI Device Context
static HGLRC hRC;   // Permanent Rendering Context
static HWND hWnd;   // Holds Our Window Handle

static int xres, yres;

// TODO: Make config?
static bool enableGLDebug = false;

void GL_SwapBuffers() {
	SwapBuffers(hDC);

	// According to some sources, doing this *after* swapbuffers can reduce frame latency
	// at a large performance cost.
	// glFinish();
}

void FormatDebugOutputARB(char outStr[], size_t outStrSize, GLenum source, GLenum type,
													GLuint id, GLenum severity, const char *msg) {
	char sourceStr[32];
	const char *sourceFmt = "UNDEFINED(0x%04X)";
	switch(source) {
	case GL_DEBUG_SOURCE_API_ARB:             sourceFmt = "API"; break;
	case GL_DEBUG_SOURCE_WINDOW_SYSTEM_ARB:   sourceFmt = "WINDOW_SYSTEM"; break;
	case GL_DEBUG_SOURCE_SHADER_COMPILER_ARB: sourceFmt = "SHADER_COMPILER"; break;
	case GL_DEBUG_SOURCE_THIRD_PARTY_ARB:     sourceFmt = "THIRD_PARTY"; break;
	case GL_DEBUG_SOURCE_APPLICATION_ARB:     sourceFmt = "APPLICATION"; break;
	case GL_DEBUG_SOURCE_OTHER_ARB:           sourceFmt = "OTHER"; break;
	}
	_snprintf(sourceStr, 32, sourceFmt, source);

	char typeStr[32];
	const char *typeFmt = "UNDEFINED(0x%04X)";
	switch(type) {
	case GL_DEBUG_TYPE_ERROR_ARB:               typeFmt = "ERROR"; break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB: typeFmt = "DEPRECATED_BEHAVIOR"; break;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:  typeFmt = "UNDEFINED_BEHAVIOR"; break;
	case GL_DEBUG_TYPE_PORTABILITY_ARB:         typeFmt = "PORTABILITY"; break;
	case GL_DEBUG_TYPE_PERFORMANCE_ARB:         typeFmt = "PERFORMANCE"; break;
	case GL_DEBUG_TYPE_OTHER_ARB:               typeFmt = "OTHER"; break;
	}
	_snprintf(typeStr, 32, typeFmt, type);

	char severityStr[32];
	const char *severityFmt = "UNDEFINED";
	switch(severity)
	{
	case GL_DEBUG_SEVERITY_HIGH_ARB:   severityFmt = "HIGH";   break;
	case GL_DEBUG_SEVERITY_MEDIUM_ARB: severityFmt = "MEDIUM"; break;
	case GL_DEBUG_SEVERITY_LOW_ARB:    severityFmt = "LOW"; break;
	}

	_snprintf(severityStr, 32, severityFmt, severity);

	_snprintf(outStr, outStrSize, "OpenGL: %s [source=%s type=%s severity=%s id=%d]", msg, sourceStr, typeStr, severityStr, id);
}

void DebugCallbackARB(GLenum source, GLenum type, GLuint id, GLenum severity,
											GLsizei length, const GLchar *message, GLvoid *userParam) {
	(void)length;
	FILE *outFile = (FILE*)userParam;
	char finalMessage[256];
	FormatDebugOutputARB(finalMessage, 256, source, type, id, severity, message);
	OutputDebugStringA(finalMessage);
	ERROR_LOG(G3D, "GL: %s", finalMessage);
}

bool GL_Init(HWND window, std::string *error_message) {
	*error_message = "ok";
	hWnd = window;
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

	hDC = GetDC(hWnd);

	if (!hDC) {
		*error_message = "Failed to get a device context.";
		return false;											// Return FALSE
	}

	if (!(PixelFormat = ChoosePixelFormat(hDC, &pfd)))	{
		*error_message = "Can't find a suitable PixelFormat.";
		return false;
	}

	if (!SetPixelFormat(hDC, PixelFormat, &pfd)) {
		*error_message = "Can't set the PixelFormat.";
		return false;
	}

	if (!(hRC = wglCreateContext(hDC)))	{
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
		const char *defaultError = "Insufficient OpenGL driver support detected!\n\n"
			"Your GPU reports that it does not support OpenGL 2.0. Would you like to try using DirectX 9 instead?\n\n"
			"DirectX is currently compatible with less games, but on your GPU it may be the only choice.\n\n"
			"Visit the forums at http://forums.ppsspp.org for more information.\n\n";

		std::wstring versionDetected = ConvertUTF8ToWString(glVersion + "\n\n");
		std::wstring error = ConvertUTF8ToWString(err->T("InsufficientOpenGLDriver", defaultError));
		std::wstring title = ConvertUTF8ToWString(err->T("OpenGLDriverError", "OpenGL driver error"));
		std::wstring combined = versionDetected + error;

		bool yes = IDYES == MessageBox(hWnd, combined.c_str(), title.c_str(), MB_ICONERROR | MB_YESNO);

		if (yes) {
			// Change the config to D3D and restart.
			g_Config.iGPUBackend = GPU_BACKEND_DIRECT3D9;
			g_Config.Save();

			W32Util::ExitAndRestart();
		}

		// Avoid further error messages. Let's just bail, it's safe, and we can't continue.
		ExitProcess(1);
	}

	if (GLEW_OK != glewInit()) {
		*error_message = "Failed to initialize GLEW.";
		return false;
	}

	CheckGLExtensions();

	int contextFlags = enableGLDebug ? WGL_CONTEXT_DEBUG_BIT_ARB : 0;

	// Alright, now for the modernity. First try a 4.4, then 4.3, context, if that fails try 3.3.
	// I can't seem to find a way that lets you simply request the newest version available.
	const int attribs44[] = {
		WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
		WGL_CONTEXT_MINOR_VERSION_ARB, 4,
		WGL_CONTEXT_FLAGS_ARB, contextFlags,
		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
		0
	};
	const int attribs43[] = {
		WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
		WGL_CONTEXT_MINOR_VERSION_ARB, 3,
		WGL_CONTEXT_FLAGS_ARB, contextFlags,
		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
		0
	};
	const int attribs33[] = {
		WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
		WGL_CONTEXT_MINOR_VERSION_ARB, 3,
		WGL_CONTEXT_FLAGS_ARB, contextFlags,
		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
		0
	};

	HGLRC	m_hrc;
	if(wglewIsSupported("WGL_ARB_create_context") == 1) {
		m_hrc = wglCreateContextAttribsARB(hDC, 0, attribs44);
		if (!m_hrc)
			m_hrc = wglCreateContextAttribsARB(hDC, 0, attribs43);
		if (!m_hrc)
			m_hrc = wglCreateContextAttribsARB(hDC, 0, attribs33);
		if (!m_hrc) {
			// Fall back
			m_hrc = hRC;
		} else {
			// Switch to the new ARB context.
			wglMakeCurrent(NULL, NULL);
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


	if (!m_hrc) {
		*error_message = "No m_hrc";
		return false;
	}

	hRC = m_hrc;

	glstate.Initialize();
	if (wglSwapIntervalEXT)
		wglSwapIntervalEXT(0);
	if (enableGLDebug && glewIsSupported("GL_ARB_debug_output")) {
		glDebugMessageCallbackARB((GLDEBUGPROCARB)&DebugCallbackARB, 0); // print debug output to stderr
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
	}
	return true;												// Success
}

void GL_Shutdown() { 
	if (hRC) {
		// Are we able to release the DC and RC contexts?
		if (!wglMakeCurrent(NULL,NULL)) {
			MessageBox(NULL,L"Release of DC and RC failed.", L"SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		}

		// Are we able to delete the RC?
		if (!wglDeleteContext(hRC)) {
			MessageBox(NULL,L"Release rendering context failed.", L"SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		}
		hRC = NULL;
	}

	if (hDC && !ReleaseDC(hWnd,hDC)) {
		DWORD err = GetLastError();
		if (err != ERROR_DC_NOT_FOUND) {
			MessageBox(NULL,L"Release device context failed.", L"SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		}
		hDC = NULL;
	}
	hWnd = NULL;
}
