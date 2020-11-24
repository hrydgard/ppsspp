#pragma once

#include "Common/CommonWindows.h"
#include "Core/Config.h"

#include "Windows/WindowsAudio.h"

// Factory
WindowsAudioBackend *CreateAudioBackend(AudioBackendType type);
