#pragma once

#include <map>

class TouchInputHandler
{
public:
	TouchInputHandler();

	void handleTouchEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	~TouchInputHandler();
private:
	std::map<int, int> touchTranslate;
	void touchUp(int id, float x, float y);
	void touchDown(int id, float x, float y);
	void touchMove(int id, float x, float y);

};

