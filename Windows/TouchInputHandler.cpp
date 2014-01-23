#include "Windows/TouchInputHandler.h"

#include <algorithm>

#include "Common/CommonWindows.h"
#include "input/input_state.h"
#include "base/NativeApp.h"
#include "Windows/WndMainWindow.h"

extern InputState input_state;

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

void TouchInputHandler::handleTouchEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (hasTouch()){
		UINT inputCount = LOWORD(wParam);
		TOUCHINPUT *inputs = new TOUCHINPUT[inputCount];
		if (touchInfo((HTOUCHINPUT) lParam,
			inputCount,
			inputs,
			sizeof(TOUCHINPUT)))
		{
			for (int i = 0; i < inputCount; i++) {
				int id = 0;

				//here we map the windows touch id to the ppsspp internal touch id
				//currently we ignore the fact that the mouse uses  touch id 0, so that 
				//the mouse could possibly interfere with the mapping so for safety 
				//the maximum amount of touch points is MAX_POINTERS-1 
				std::map<int, int>::const_iterator it = touchTranslate.find(inputs[i].dwID);
				if (it != touchTranslate.end()) //check if we already mapped this touch id
				{
					id = it->second;
				}
				else
				{
					if (touchTranslate.size() + 1 >= MAX_POINTERS) //check if we're tracking too many points
					{
						touchUp(touchTranslate.begin()->second, 0, 0);
						touchTranslate.erase(touchTranslate.begin());
					}
					//finding first free internal touch id and map this windows id to an internal id
					bool *first_free = std::find(input_state.pointer_down, input_state.pointer_down + MAX_POINTERS, false);
					id = (first_free - input_state.pointer_down) / sizeof(bool);
					touchTranslate[inputs[i].dwID] = id;
				}

				POINT point;
				point.x = TOUCH_COORD_TO_PIXEL(inputs[i].x);
				point.y = TOUCH_COORD_TO_PIXEL(inputs[i].y);

				if (ScreenToClient(hWnd, &point)){
					if (inputs[i].dwFlags & TOUCHEVENTF_DOWN)
					{
						touchDown(id, point.x, point.y);
					}
					if (inputs[i].dwFlags & TOUCHEVENTF_MOVE)
					{
						touchMove(id, point.x, point.y);
					}
					if (inputs[i].dwFlags & TOUCHEVENTF_UP)
					{
						touchUp(id, point.x, point.y);
						touchTranslate.erase(touchTranslate.find(inputs[i].dwID));
					}
				}
			}
			closeTouch((HTOUCHINPUT) lParam);
		}
		else
		{
			// GetLastError() and error handling.
		}
		delete [] inputs;
	}
}

void TouchInputHandler::touchUp(int id, float x, float y){
	TouchInput touchevent;
	touchevent.id = id;
	touchevent.x = x;
	touchevent.y = y;
	touchevent.flags = TOUCH_UP;
	input_state.lock.lock();
	input_state.pointer_down[id] = false;
	input_state.pointer_x[id] = x;
	input_state.pointer_y[id] = y;
	input_state.lock.unlock();
	NativeTouch(touchevent);
}

void TouchInputHandler::touchDown(int id, float x, float y){
	TouchInput touchevent;
	touchevent.id = id;
	touchevent.x = x;
	touchevent.y = y;
	touchevent.flags = TOUCH_DOWN;
	input_state.lock.lock();
	input_state.pointer_down[id] = true;
	input_state.pointer_x[id] = x;
	input_state.pointer_y[id] = y;
	input_state.lock.unlock();
	NativeTouch(touchevent);
}

void TouchInputHandler::touchMove(int id, float x, float y){
	TouchInput touchevent;
	touchevent.id = id;
	touchevent.x = x;
	touchevent.y = y;
	touchevent.flags = TOUCH_MOVE;
	input_state.lock.lock();
	input_state.pointer_x[id] = x;
	input_state.pointer_y[id] = y;
	input_state.lock.unlock();
	NativeTouch(touchevent);
}

void TouchInputHandler::registerTouchWindow(HWND wnd)
{
	if (hasTouch())
		registerTouch(wnd, TWF_WANTPALM);
}

bool TouchInputHandler::hasTouch(){
	return (
		touchInfo		!= nullptr &&
		closeTouch		!= nullptr &&
		registerTouch	!= nullptr
		);
}