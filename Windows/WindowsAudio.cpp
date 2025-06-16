#include "Common/OSVersion.h"
#include "WindowsAudio.h"
#include "WASAPIContext.h"

AudioBackend *System_CreateAudioBackend() {
	return new WASAPIContext();
}
