#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

#include <string>

// For desktop operating systems only. Stubbed out on Android.
// Simplified as this will only be used in utilities / temp code.
// An false returned means cancel;
bool OpenFileDialog(const char *title, const char *extension, std::string *filename)
{
	OPENFILENAME of;
	memset(&of, 0, sizeof(of));
	char buffer[512] = {0};
	of.lStructSize = sizeof(OPENFILENAME);
	of.hInstance = 0;
	of.hwndOwner = GetActiveWindow();

	// These weird strings with zeroes in them can't be dealt with using normal string
	// functions, so here we go - evil hackery.
	char filter[256] = "XXX files\0*.XXX\0\0";
	memcpy(filter, extension, 3);
	memcpy(filter + 12, extension, 3);
	of.lpstrFilter = filter;

	of.lpstrDefExt = extension;
	of.lpstrFile = buffer;
	of.nMaxFile = 511;

	of.Flags = OFN_FILEMUSTEXIST;
	if (!GetOpenFileName(&of)) return false;
	*filename = of.lpstrFile;
	return true;
}

bool SaveFileDialog(const char *title, const char *extension, std::string *filename)
{
	OPENFILENAME of;
	memset(&of, 0, sizeof(of));
	char buffer[512] = {0};
	of.lStructSize = sizeof(OPENFILENAME);
	of.hInstance = 0;
	of.hwndOwner = GetActiveWindow();

	// These weird strings with zeroes in them can't be dealt with using normal string
	// functions, so here we go - evil hackery.
	char filter[256] = "XXX files\0*.XXX\0\0";
	memcpy(filter, extension, 3);
	memcpy(filter + 12, extension, 3);
	of.lpstrFilter = filter;

	of.lpstrDefExt = extension;
	of.lpstrFile = buffer;
	of.nMaxFile = 511;

	of.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
	if (!GetSaveFileName(&of))
		return false;
	*filename = of.lpstrFile;
	return true;
}

#else

#include <string>
#include "base/basictypes.h"
#include "base/logging.h"

bool OpenFileDialog(const char *title, const char *extension, std::string *filename)
{
	ELOG("Asked for OpenFileDialog, not present on this platform.");
	return false;
}

bool SaveFileDialog(const char *title, const char *extension, std::string *filename)
{
	ELOG("Asked for SaveFileDialog, not present on this platform.");
	return false;
}

#endif
