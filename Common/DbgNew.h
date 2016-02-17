#pragma once

// Utility file for using the MS CRT's memory tracking.
// crtdbg.h overloads malloc with malloc_dbg etc, but does not catch new. So here we go.

#include <crtdbg.h>

#ifdef _DEBUG
#define USE_CRT_DBG
#ifndef DBG_NEW
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#define new DBG_NEW
#endif


#endif  // _DEBUG