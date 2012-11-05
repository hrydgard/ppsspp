// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#if defined(ANDROID) || defined(BLACKBERRY)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#else
#include <GL/glew.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#include <windows.h>
#include <GL/gl.h>								// Header File For The OpenGL32 Library

#include "OpenGLBase.h"

static HDC			hDC=NULL;								// Private GDI Device Context
static HGLRC		hRC=NULL;								// Permanent Rendering Context
static HWND			hWnd=NULL;								// Holds Our Window Handle
static HINSTANCE	hInstance;								// Holds The Instance Of The Application

static int xres, yres;

typedef BOOL (APIENTRY *PFNWGLSWAPINTERVALFARPROC)( int );
PFNWGLSWAPINTERVALFARPROC wglSwapIntervalEXT = 0;

void setVSync(int interval=1)
{
	const char *extensions = (const char *)glGetString( GL_EXTENSIONS );

  if( strstr( extensions, "WGL_EXT_swap_control" ) == 0 )
    return; // Error: WGL_EXT_swap_control extension not supported on your computer.\n");
  else
  {
    wglSwapIntervalEXT = (PFNWGLSWAPINTERVALFARPROC)wglGetProcAddress( "wglSwapIntervalEXT" );

    if( wglSwapIntervalEXT )
      wglSwapIntervalEXT(interval);
  }
}
GLvoid ResizeGLScene()					// Resize And Initialize The GL Window
{
	RECT rc;
	GetWindowRect(hWnd,&rc);
	xres=rc.right-rc.left; //account for border :P
	yres=rc.bottom-rc.top;

	//swidth=width;									// Set Scissor Width To Window Width
	//sheight=height;								// Set Scissor Height To Window Height
	if (yres==0)									// Prevent A Divide By Zero By
	{
		yres=1;								// Making Height Equal One
	}
	glViewport(0,0,xres,yres);						// Reset The Current Viewport
	glMatrixMode(GL_PROJECTION);					// Select The Projection Matrix
	glLoadIdentity();								// Reset The Projection Matrix
	glOrtho(0.0f,xres,yres,0.0f,-1.0f,1.0f);		// Create Ortho 640x480 View (0,0 At Top Left)
	glMatrixMode(GL_MODELVIEW);						// Select The Modelview Matrix
	glLoadIdentity();								// Reset The Modelview Matrix
}


void GL_BeginFrame()
{

}


void GL_EndFrame()
{
	SwapBuffers(hDC);
}

bool GL_Init(HWND window)
{
	hWnd = window;
	GLuint		PixelFormat;									// Holds The Results After Searching For A Match

	hInstance			= GetModuleHandle(NULL);				// Grab An Instance For Our Window

	static	PIXELFORMATDESCRIPTOR pfd=							// pfd Tells Windows How We Want Things To Be
	{
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
			0, 0, 0, 0,												// Accumulation Bits Ignored
			16,														// 16Bit Z-Buffer (Depth Buffer)  
			0,														// No Stencil Buffer
			0,														// No Auxiliary Buffer
			PFD_MAIN_PLANE,											// Main Drawing Layer
			0,														// Reserved
			0, 0, 0													// Layer Masks Ignored
	};

	if (!(hDC = GetDC(hWnd)))										// Did We Get A Device Context?
	{
		MessageBox(NULL,"Can't Create A GL Device Context.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return false;											// Return FALSE
	}

	if (!(PixelFormat = ChoosePixelFormat(hDC,&pfd)))				// Did Windows Find A Matching Pixel Format?
	{
		MessageBox(NULL,"Can't Find A Suitable PixelFormat.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return false;
	}

	if(!SetPixelFormat(hDC,PixelFormat,&pfd))					// Are We Able To Set The Pixel Format?
	{
		MessageBox(NULL,"Can't Set The PixelFormat.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return false;
	}

	if (!(hRC = wglCreateContext(hDC)))							// Are We Able To Get A Rendering Context?
	{
		MessageBox(NULL,"Can't Create A GL Rendering Context.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return false;
	}	

	if(!wglMakeCurrent(hDC,hRC))								// Try To Activate The Rendering Context
	{
		MessageBox(NULL,"Can't Activate The GL Rendering Context.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return false;
	}
	setVSync(0);

	glewInit();

	ResizeGLScene();								// Set Up Our Perspective GL Screen

	return true;												// Success
}

void GL_Shutdown()
{ 
	if (hRC)									// Do We Have A Rendering Context?
	{
		if (!wglMakeCurrent(NULL,NULL))						// Are We Able To Release The DC And RC Contexts?
		{
			MessageBox(NULL,"Release Of DC And RC Failed.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		}

		if (!wglDeleteContext(hRC))						// Are We Able To Delete The RC?
		{
			MessageBox(NULL,"Release Rendering Context Failed.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		}
		hRC=NULL;								// Set RC To NULL
	}

	if (hDC && !ReleaseDC(hWnd,hDC))						// Are We Able To Release The DC
	{
		MessageBox(NULL,"Release Device Context Failed.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		hDC=NULL;								// Set DC To NULL
	}
}
