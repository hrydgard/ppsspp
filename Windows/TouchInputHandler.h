#pragma once

#include <map>

typedef BOOL(WINAPI *getTouchInputProc)(
	HTOUCHINPUT hTouchInput,
	UINT cInputs,
	PTOUCHINPUT pInputs,
	int cbSize
	);

typedef BOOL(WINAPI *closeTouchInputProc)(
	HTOUCHINPUT hTouchInput
	);

typedef BOOL(WINAPI *registerTouchProc)(
	HWND hWnd,
	ULONG ulFlags
	);

class TouchInputHandler
{
private:
	std::map<int, int> touchTranslate;
	getTouchInputProc touchInfo;
	closeTouchInputProc closeTouch;
	registerTouchProc registerTouch;

public:
	TouchInputHandler();
	~TouchInputHandler();
	void handleTouchEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	void registerTouchWindow(HWND wnd);
	bool hasTouch();
private:
	void touchUp(int id, float x, float y);
	void touchDown(int id, float x, float y);
	void touchMove(int id, float x, float y);
};

