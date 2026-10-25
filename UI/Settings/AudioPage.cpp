// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <string>
#include <vector>

#include "ppsspp_config.h"

#include "Common/Audio/AudioBackend.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/System/System.h"
#include "Common/TimeUtil.h"
#include "Common/UI/Root.h"
#include "Common/UI/View.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/HLE/sceUsbMic.h"
#include "UI/BackgroundAudio.h"
#include "UI/SettingsPage.h"
#include "UI/Settings/MainSettingsPages.h"

#if PPSSPP_PLATFORM(ANDROID)
#include "android/jni/AndroidAudio.h"

extern AndroidAudioState *g_audioState;
#endif

void BuildAudioSettings(SettingsPage *page) {
	using namespace UI;

#if PPSSPP_PLATFORM(IOS)
	page->Setting(&g_Config.bAudioRespectSilentMode).Enabled(&g_Config.bEnableSound);
#endif
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
	page->Setting(&g_Config.bAudioMixWithOthers).Enabled(&g_Config.bEnableSound);
#endif
	page->Header("Audio playback");

	page->Setting(&g_Config.iAudioPlaybackMode);
	page->Setting(&g_Config.bFillAudioGaps).EnabledFunc([]() {
		return g_Config.iAudioPlaybackMode == (int)AudioSyncMode::GRANULAR;
	});

	page->Header("Game volume");

	// This is here because it now only applies to in-game. Muting the menu sounds is separate.
	page->Setting(&g_Config.bEnableSound);

	page->Setting(&g_Config.iGameVolume).Enabled(&g_Config.bEnableSound);
	page->Setting(&g_Config.iReverbVolume).Enabled(&g_Config.bEnableSound);
	page->Setting(&g_Config.iAltSpeedVolume).Enabled(&g_Config.bEnableSound);
	page->Setting(&g_Config.iAchievementVolume).Enabled(&g_Config.bEnableSound).OnChange([](EventParams &e) {
		// Audio preview
		float achievementVolume = Volume100ToMultiplier(g_Config.iAchievementVolume);
		g_BackgroundAudio.SFX().Play(UI::UISound::ACHIEVEMENT_UNLOCKED, achievementVolume);
	});

	page->Header();

	page->Setting(&g_Config.bUISound);
	page->Setting(&g_Config.iUIVolume).LiveUpdate().Enabled(&g_Config.bUISound).OnChange([](EventParams &e) {
		static double lastTimePlayed = 0.0;
		double now = time_now_d();
		if (now - lastTimePlayed < 0.1) {
			return; // Don't play if we just played one, to avoid spamming when dragging.
		}
		lastTimePlayed = now;
		// Audio preview
		PlayUISound(UI::UISound::CONFIRM);
	});

	page->Setting(&g_Config.iGamePreviewVolume);

	bool sdlAudio = false;

#if defined(SDL)
	page->Header("Audio backend");
	std::vector<std::string> audioDeviceList;
	SplitString(System_GetProperty(SYSPROP_AUDIO_DEVICE_LIST), '\0', audioDeviceList);
	audioDeviceList.insert(audioDeviceList.begin(), std::string(T(I18NCat::AUDIO, "Auto")));
	page->Setting(&g_Config.sAudioDevice, std::move(audioDeviceList)).OnChange([](EventParams &e) {
		if (g_Config.sAudioDevice == T(I18NCat::AUDIO, "Auto")) {
			g_Config.sAudioDevice.clear();
		}
		System_Notify(SystemNotification::AUDIO_RESET_DEVICE);
	});
	sdlAudio = true;

	static const int bufferSizes[] = {128, 256, 512, 1024, 2048};
	page->IntSlider(&g_Config.iSDLAudioBufferSize, 0, 2048, "Buffer size").Default(256).RestrictTo(bufferSizes, ARRAY_SIZE(bufferSizes));
	page->Hint({ I18NCat::DIALOG, "This change will not take effect until PPSSPP is restarted." });
#endif

#if PPSSPP_PLATFORM(WINDOWS)
	extern AudioBackend *g_audioBackend;

	std::vector<std::string> audioDeviceNames;
	std::vector<std::string> audioDeviceIds;

	std::vector<AudioDeviceDesc> deviceDescs;
	g_audioBackend->EnumerateDevices(&deviceDescs);
	if (!deviceDescs.empty()) {
		page->Header("Audio backend");
		for (auto &desc : deviceDescs) {
			audioDeviceNames.push_back(desc.name);
			audioDeviceIds.push_back(desc.uniqueId);
		}

		audioDeviceNames.insert(audioDeviceNames.begin(), std::string(T(I18NCat::AUDIO, "Auto")));
		audioDeviceIds.insert(audioDeviceIds.begin(), "");

		page->Setting(&g_Config.sAudioDevice, std::move(audioDeviceNames), std::move(audioDeviceIds)).OnChange([](EventParams &) {
			bool reverted;
			if (g_audioBackend->InitOutputDevice(g_Config.sAudioDevice, LatencyMode::Aggressive, &reverted)) {
				if (reverted) {
					WARN_LOG(Log::Audio, "Unexpected: After a direct choice, audio device reverted to default. '%s'", g_Config.sAudioDevice.c_str());
				}
			} else {
				WARN_LOG(Log::Audio, "InitOutputDevice failed");
			}
		});
		page->Setting(&g_Config.bAutoSwitchAudioDevice).EnabledFunc([]()->bool {
			return g_Config.sAudioDevice.empty();
		});
	}

#else
	if (sdlAudio) {
		page->Setting(&g_Config.bAutoSwitchAudioDevice);
	} else {
		page->Header("Audio backend");
	}

#if PPSSPP_PLATFORM(ANDROID)
	page->Setting(&g_Config.bExtraAudioBuffering);

	auto restartAudio = [](UI::EventParams &) {
		AndroidAudio_Restart(g_audioState);
	};
	// AAudio isn't stable before Android 8.1, so there it's always OpenSL ES.
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 27) {
		page->Setting(&g_Config.iAndroidAudioApi).OnChange(restartAudio);
		page->Hint({ I18NCat::AUDIO, "AudioApiHint", "Try the other one if you have no sound or other audio problems." });

		page->Setting(&g_Config.bAndroidAudioMMap).OnChange(restartAudio).EnabledFunc([]() {
			return g_Config.iAndroidAudioApi == (int)AndroidAudioApi::AAUDIO;
		});
		page->Hint({ I18NCat::AUDIO, "AudioMMapHint", "Lowest audio delay. On some devices, it causes silence over Bluetooth or other connected audio devices. Turn it off if you get no sound." });
	}
	// Show Oboe debug info
	const std::string audioErrorStr = AndroidAudio_GetErrorString(g_audioState);
	if (!audioErrorStr.empty()) {
		page->Info("Audio Error", audioErrorStr);
	}
#endif
#endif

	std::vector<std::string> micList = Microphone::getDeviceList();
	if (!micList.empty()) {
		page->Header("Microphone");
		std::vector<std::string> micValues = micList;
		micList.emplace(micList.begin(), T(I18NCat::AUDIO, "Auto"));
		micValues.insert(micValues.begin(), "");
		page->Setting(&g_Config.sMicDevice, std::move(micList), std::move(micValues));
	}
}
