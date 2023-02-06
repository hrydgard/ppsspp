#include "ppsspp_config.h"

#include <string>

#if PPSSPP_PLATFORM(WINDOWS) && PPSSPP_ARCH(AMD64) && !PPSSPP_PLATFORM(UWP)

#include "ext/nvda/x64/nvdaController.h"
#include "Common/Data/Encoding/Utf8.h"

void TTS_Say(const char *text) {
	std::wstring wstr = ConvertUTF8ToWString(text);
	nvdaController_speakText(wstr.c_str());
}

void TTS_Braille(const char *text) {
	std::wstring wstr = ConvertUTF8ToWString(text);
	nvdaController_speakText(wstr.c_str());
}

bool TTS_Active() {
	return nvdaController_testIfRunning() == 0;
}

#else

void TTS_Active() { return false; }
void TTS_Say(const char *text) {}
void TTS_Braille(const char *text) {}

#endif
