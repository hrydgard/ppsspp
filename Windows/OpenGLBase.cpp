// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include <windows.h>
#include "../native/gfx_es2/gl_state.h"
#include "../native/gfx/gl_common.h"
#include "GL/gl.h"
#include "GL/wglew.h"

#include "OpenGLBase.h"

static HDC hDC;     // Private GDI Device Context
static HGLRC hRC;   // Permanent Rendering Context
static HWND hWnd;   // Holds Our Window Handle
	
static int xres, yres;

// TODO: Make config?
static bool enableGLDebug = false;

void GL_SetVSyncInterval(int interval=1)
{
  if( wglSwapIntervalEXT )
    wglSwapIntervalEXT(interval);
}

void GL_Resized() {
	if (!hWnd)
		return;
	RECT rc;
	GetWindowRect(hWnd,&rc);
	xres = rc.right - rc.left; //account for border :P
	yres = rc.bottom - rc.top;

	if (yres == 0)
		yres = 1;
	glstate.viewport.set(0, 0, xres, yres);
	glstate.viewport.restore();
}

void GL_SwapBuffers() {
	SwapBuffers(hDC);
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
	ERROR_LOG(HLE, "GL: %s", finalMessage);
}

bool GL_Init(HWND window, std::string *error_message) {
	*error_message = "ok";
	hWnd = window;
	GLuint PixelFormat;

	static const PIXELFORMATDESCRIPTOR pfd = {
		sizeof(PIXELFORMATDESCRIPTOR),							// Size Of This Pixel Format Descriptor
			1,														// Version Number
			PFD_DRAW_TO_WINDOW |									// Format Must Support Window
			PFD_SUPPORT_OPENGL |									// Format Must Support OpenGL
			PFD_DOUBLEBUFFER,										// Must Support Double Buffering
			PFD_TYPE_RGBA,											// Request An RGBA Format
			32,														// Select Our Color Depth
			0, 0, 0, 0, 0, 0,										// Color Bits Ignored
			0,														// No Alpha Buffer
			0,														// Shift Bit Ignored
			0,														// No Accumulation Buffer
			0, 0, 0, 0,										// Accumulation Bits Ignored
			16,														// 16Bit Z-Buffer (Depth Buffer)  
			0,														// No Stencil Buffer
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

	// Did Windows Find A Matching Pixel Format?
	if (!(PixelFormat = ChoosePixelFormat(hDC,&pfd)))
	{
		*error_message = "Can't Find A Suitable PixelFormat.";
		return false;
	}

	// Are We Able To Set The Pixel Format?
	if (!SetPixelFormat(hDC,PixelFormat,&pfd)) {
		*error_message = "Can't Set The PixelFormat.";
		return false;
	}

	// Are We Able To Get A Rendering Context?
	if (!(hRC = wglCreateContext(hDC)))	{
		*error_message = "Can't Create A GL Rendering Context.";
		return false;
	}	

	// Try To Activate The Rendering Context
	if (!wglMakeCurrent(hDC,hRC)) {
		*error_message = "Can't activate the GL Rendering Context.";
		return false;
	}

	if (GLEW_OK != glewInit()) {
		*error_message = "Failed to initialize GLEW.";
		return false;
	}

	// Alright, now for the modernity.
	static const int attribs[] = {
		WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
		WGL_CONTEXT_MINOR_VERSION_ARB, 1,
		WGL_CONTEXT_FLAGS_ARB, enableGLDebug ? WGL_CONTEXT_DEBUG_BIT_ARB : 0,
		0
	};

	HGLRC	m_hrc;
	if(wglewIsSupported("WGL_ARB_create_context") == 1) {
		m_hrc = wglCreateContextAttribsARB(hDC, 0, attribs);
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

	if (!m_hrc) {
		*error_message = "No m_hrc";
		return false;
	}

	hRC = m_hrc;

	//Checking GL version
	const char *GLVersionString = (const char *)glGetString(GL_VERSION);

	//Or better yet, use the GL3 way to get the version number
	int OpenGLVersion[2];
	glGetIntegerv(GL_MAJOR_VERSION, &OpenGLVersion[0]);
	glGetIntegerv(GL_MINOR_VERSION, &OpenGLVersion[1]);

	glstate.Initialize();
	GL_SetVSyncInterval(0);
	if (enableGLDebug && glewIsSupported("GL_ARB_debug_output")) {
		glDebugMessageCallbackARB((GLDEBUGPROCARB)&DebugCallbackARB, 0); // print debug output to stderr
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
	}

	GL_Resized();								// Set Up Our Perspective GL Screen
	return true;												// Success
}

void GL_Shutdown() { 
	if (hRC) {
		// Are We Able To Release The DC And RC Contexts?
		if (!wglMakeCurrent(NULL,NULL)) {
			MessageBox(NULL,"Release Of DC And RC Failed.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		}

		// Are We Able To Delete The RC?
		if (!wglDeleteContext(hRC)) {
			MessageBox(NULL,"Release Rendering Context Failed.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		}
		hRC = NULL;
	}

	if (hDC && !ReleaseDC(hWnd,hDC)) {
		MessageBox(NULL,"Release Device Context Failed.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		hDC = NULL;
	}
	hWnd = NULL;
}
