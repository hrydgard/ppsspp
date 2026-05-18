#pragma once

#include "Common/Render/Text/draw_text_uwp.h"

#if defined(_WIN32) && !defined(USING_QT_UI)
using TextDrawerWin32 = TextDrawerUWP;

#endif
