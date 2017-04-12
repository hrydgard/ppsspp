#define NOMINMAX
#include <Windows.h>
#include "Windows/TouchInputHandler.h"

#include <algorithm>

#include "base/display.h"
#include "Common/CommonWindows.h"
#include "base/NativeApp.h"
#include "Windows/MainWindow.h"

TouchInputHandler::TouchInputHandler() : 
		touchInfo(nullptr),
		closeTouch(nullptr),
		registerTouch(nullptr)
{

	touchInfo = (getTouchInputProc) GetProcAddress(
		GetModuleHandle(TEXT("User32.dll")),
		"GetTouchInputInfo");

	closeTouch = (closeTouchInputProc) GetProcAddress(
		GetModuleHandle(TEXT("User32.dll")),
		"CloseTouchInputHandle");

	registerTouch = (registerTouchProc) GetProcAddress(
		GetModuleHandle(TEXT("User32.dll")),
		"RegisterTouchWindow");
}


TouchInputHandler::~TouchInputHandler()
{
}

int TouchInputHandler::ToTouchID(int windowsID, bool allowAllocate) {
	// Find the id for the touch.  Avoid 0 (mouse.)
	for (int localId = 1; localId < (int)ARRAY_SIZE(touchIds); ++localId) {
		if (touchIds[localId] == windowsID) {
			return localId;
		}
	}

	// Allocate a new one, perhaps?
	if (allowAllocate) {
		for (int localId = 1; localId < (int)ARRAY_SIZE(touchIds); ++localId) {
			if (touchIds[localId] == 0) {
				touchIds[localId] = windowsID;
				return localId;
			}
		}

		// None were free.
		// TODO: Better to just ignore this touch instead?
		touchUp(0, 0, 0);
		return 0;
	}

	return -1;
}

void TouchInputHandler::handleTouchEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (hasTouch()) {
		UINT inputCount = LOWORD(wParam);
		HTOUCHINPUT touchInputData = (HTOUCHINPUT) lParam;
		TOUCHINPUT *inputs = new TOUCHINPUT[inputCount];
		if (touchInfo(touchInputData, inputCount, inputs, sizeof(TOUCHINPUT))) {
			for (UINT i = 0; i < inputCount; i++) {
				POINT point;
				point.x = (float)(TOUCH_COORD_TO_PIXEL(inputs[i].x));
				point.y = (float)(TOUCH_COORD_TO_PIXEL(inputs[i].y));
				if (ScreenToClient(hWnd, &point)) {
					point.x *= g_dpi_scale;
					point.y *= g_dpi_scale;
					if (inputs[i].dwFlags & TOUCHEVENTF_DOWN) {
						touchDown(ToTouchID(inputs[i].dwID), point.x, point.y);
					}
					if (inputs[i].dwFlags & TOUCHEVENTF_MOVE) {
						touchMove(ToTouchID(inputs[i].dwID), point.x, point.y);
					}
					if (inputs[i].dwFlags & TOUCHEVENTF_UP) {
						int id = ToTouchID(inputs[i].dwID, false);
						if (id >= 0) {
							touchUp(id, point.x, point.y);
							touchIds[id] = 0;
						}
					}
				}
			}
			closeTouch((HTOUCHINPUT) lParam);
		} else {
			// GetLastError() and error handling.
		}
		delete [] inputs;
	}
}

// from http://msdn.microsoft.com/en-us/library/ms812373.aspx
// disable the press and hold gesture for the given window
void TouchInputHandler::disablePressAndHold(HWND hWnd)
{
	// The atom identifier and Tablet PC atom
	ATOM atomID = 0;
	LPCTSTR tabletAtom = _T("MicrosoftTabletPenServiceProperty");
	
	// Get the Tablet PC atom ID
	atomID = GlobalAddAtom(tabletAtom);
	
	// If getting the ID failed, return false
	if (atomID == 0)
	{
	 return;
	}
	
	// Try to disable press and hold gesture by 
	// setting the window property, return the result
	SetProp(hWnd, tabletAtom, (HANDLE)1);
}

void TouchInputHandler::touchUp(int id, float x, float y){
	TouchInput touchevent;
	touchevent.id = id;
	touchevent.x = x;
	touchevent.y = y;
	touchevent.flags = TOUCH_UP;
	NativeTouch(touchevent);
}

void TouchInputHandler::touchDown(int id, float x, float y){
	TouchInput touchevent;
	touchevent.id = id;
	touchevent.x = x;
	touchevent.y = y;
	touchevent.flags = TOUCH_DOWN;
	NativeTouch(touchevent);
}

void TouchInputHandler::touchMove(int id, float x, float y){
	TouchInput touchevent;
	touchevent.id = id;
	touchevent.x = x;
	touchevent.y = y;
	touchevent.flags = TOUCH_MOVE;
	NativeTouch(touchevent);
}

void TouchInputHandler::registerTouchWindow(HWND wnd)
{
	if (hasTouch())
	{
		registerTouch(wnd, TWF_WANTPALM);
		disablePressAndHold(wnd);
	}
}

bool TouchInputHandler::hasTouch(){
	return (
		touchInfo		!= nullptr &&
		closeTouch		!= nullptr &&
		registerTouch	!= nullptr
		);
}
