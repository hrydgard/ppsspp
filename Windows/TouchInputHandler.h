#pragma once

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
public:
	TouchInputHandler();
	void handleTouchEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	void registerTouchWindow(HWND wnd);
	bool hasTouch();

private:
	int ToTouchID(int windowsID, bool allowAllocate = true);
	bool GetTouchPoint(HWND hWnd, const TOUCHINPUT &input, float &x, float &y);

	void disablePressAndHold(HWND hWnd);
	void touchUp(int id, float x, float y);
	void touchDown(int id, float x, float y);
	void touchMove(int id, float x, float y);

	int touchIds[10]{};
	getTouchInputProc touchInfo;
	closeTouchInputProc closeTouch;
	registerTouchProc registerTouch;
};

