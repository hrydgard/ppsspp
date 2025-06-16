#include "Common/OSVersion.h"
#include "WindowsAudio.h"
#include "WASAPIContext.h"

WindowsAudioBackend *CreateAudioBackend(AudioBackendType type) {
	return new WASAPIAudioBackend();
}
