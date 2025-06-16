#include "Common/OSVersion.h"
#include "WindowsAudio.h"
#include "WASAPIStream.h"

WindowsAudioBackend *CreateAudioBackend(AudioBackendType type) {
	return new WASAPIAudioBackend();
}
