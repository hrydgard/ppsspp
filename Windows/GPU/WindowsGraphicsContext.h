#pragma once

#include "Common/GraphicsContext.h"
#include "Common/CommonWindows.h"

class WindowsGraphicsContext : public GraphicsContext {
public:
	virtual bool Init(HINSTANCE hInst, HWND window, std::string *error_message) = 0;
};
