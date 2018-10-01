#include "Common/OSVersion.h"
#include "WindowsAudio.h"
#include "DSoundStream.h"
#include "WASAPIStream.h"

WindowsAudioBackend *CreateAudioBackend(AudioBackendType type) {
	if (IsVistaOrHigher()) {
		switch (type) {
		case AUDIO_BACKEND_WASAPI:
		case AUDIO_BACKEND_AUTO:
			return new WASAPIAudioBackend();
		case AUDIO_BACKEND_DSOUND:
		default:
			return new DSoundAudioBackend();
		}
	} else {
		return new DSoundAudioBackend();
	}
}
