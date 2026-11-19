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

#include <algorithm>
#include <cstring>
#include <vector>

#include "Common/File/FileUtil.h"
#include "Common/GPU/thin3d.h"
#include "Common/Log.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/TimeUtil.h"
#include "Common/UI/Context.h"
#include "Common/UI/Root.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Util/PSARUnpack.h"
#include "Core/Util/VideoPlayer.h"
#include "UI/BackgroundAudio.h"
#include "UI/GameBootAnimation.h"
#include "UI/MiscViews.h"

// If neither the audio nor the video say when it's over.
static constexpr double MAX_DURATION_SECONDS = 10.0;

static const char *const GAMEBOOT_FILE = "vsh/resource/gameboot.pmf";

static Path GameBootPath() {
	return InstalledFirmwareFile(GAMEBOOT_FILE);
}

GameBootAnimation::GameBootAnimation() {}

GameBootAnimation::~GameBootAnimation() {
	Stop();
}

bool GameBootAnimation::Available() {
	return pmf_player_available() && InstalledFirmwareHasFile(GAMEBOOT_FILE);
}

bool GameBootAnimation::Start() {
	Stop();
	if (!Available() || !File::ReadBinaryFileToString(GameBootPath(), &file_)) {
		return false;
	}
	video_ = std::make_unique<PMFVideoTexture>(file_);
	if (!video_->IsValid()) {
		WARN_LOG(Log::System, "Couldn't play %s", GameBootPath().c_str());
		video_.reset();
		return false;
	}

	duration_ = MAX_DURATION_SECONDS;
	std::vector<int16_t> pcm = pmf_decode_audio((const uint8_t *)file_.data(), file_.size());
	if (pcm.size() >= 4) {
		const int frames = (int)(pcm.size() / 2);
		duration_ = std::min(MAX_DURATION_SECONDS, frames / 44100.0);
		SoundEffectMixer &sfx = g_BackgroundAudio.SFX();
		sfx.UpdateSample(UI::UISound::GAME_BOOT, new Sample(pcm, 2, 44100));
		// The XMB plays it at the system volume; ours for that is the game volume.
		if (g_Config.bEnableSound) {
			sfx.Play(UI::UISound::GAME_BOOT, Volume100ToMultiplier(g_Config.iGameVolume));
		}
	} else {
		WARN_LOG(Log::System, "No audio in %s", GameBootPath().c_str());
	}

	INFO_LOG(Log::System, "Playing the PSP's game start intro (%.1f s)", duration_);
	startTime_ = time_now_d();
	playing_ = true;
	return true;
}

void GameBootAnimation::Update() {
	if (!playing_) {
		return;
	}
	const double elapsed = time_now_d() - startTime_;
	if (elapsed >= duration_ || (video_ && video_->ReachedEnd())) {
		Stop();
	}
}

void GameBootAnimation::Stop() {
	if (playing_) {
		// A few seconds of 44.1 kHz stereo, not worth keeping until the next boot.
		g_BackgroundAudio.SFX().Stop(UI::UISound::GAME_BOOT);
		g_BackgroundAudio.SFX().UnloadSample(UI::UISound::GAME_BOOT);
	}
	playing_ = false;
	video_.reset();
	file_.clear();
}

void GameBootAnimation::Draw(UIContext &dc, const Bounds &screen) {
	if (!playing_ || !video_) {
		return;
	}
	dc.Flush();
	dc.RebindTexture();
	dc.FillRect(UI::Drawable(0xFF000000), dc.GetBounds());
	dc.Flush();
	video_->Update(dc.GetDrawContext(), time_now_d() - startTime_);
	video_->DrawFrame(dc, screen);
}

void GameBootAnimation::DeviceLost() {
	if (video_) {
		video_->DeviceLost();
	}
}
