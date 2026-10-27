// Copyright (c) 2026- PPSSPP Project.

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
#include "Core/HW/MpegDemux.h"
#include "Core/HW/SimpleAudioDec.h"
#include "Core/Util/VideoPlayer.h"
#include "UI/BackgroundAudio.h"
#include "UI/GameBootAnimation.h"
#include "UI/MiscViews.h"

// If neither the audio nor the video say when it's over.
static constexpr double MAX_DURATION_SECONDS = 10.0;

static Path GameBootPath() {
	return g_Config.nandRootDirectory / "flash0/vsh/resource/gameboot.pmf";
}

static uint32_t ReadBE32(const uint8_t *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// The movie's ATRAC3+ audio, decoded, as interleaved stereo. Same as MediaEngine does for games'
// movies: our demuxer for the PSMF stream, then the ATRAC3+ decoder a frame at a time.
static std::vector<int16_t> DecodeAudio(const std::string &file) {
	std::vector<int16_t> pcm;
	const uint8_t *data = (const uint8_t *)file.data();
	if (file.size() < 0x10 || memcmp(data, "PSMF", 4) != 0) {
		return pcm;
	}
	// Where the stream starts, after the PSMF header.
	const uint32_t offset = ReadBE32(data + 8);
	if (offset >= file.size()) {
		return pcm;
	}

	MpegDemux demux((int)file.size() + 2048, (int)offset);
	demux.addStreamData(data, (int)file.size());
	std::unique_ptr<AudioDecoder> decoder(CreateAudioDecoder(PSP_CODEC_AT3PLUS));
	if (!decoder) {
		return pcm;
	}

	int16_t buffer[2048 * 2];
	for (int frames = 0; frames < 10000; frames++) {
		demux.demux(0);
		uint8_t *frame = nullptr;
		int headerCode1 = 0, headerCode2 = 0;
		const int frameSize = demux.getNextAudioFrame(&frame, &headerCode1, &headerCode2);
		if (frameSize <= 0 || !frame) {
			break;
		}
		if (headerCode1 == 0x24) {
			decoder->SetChannels(1);  // Mono. Still decodes to stereo.
		}
		int consumed = 0;
		int samples = 0;
		if (!decoder->Decode(frame, frameSize, &consumed, 2, buffer, &samples) || samples <= 0) {
			continue;
		}
		samples = std::min(samples, 2048);
		pcm.insert(pcm.end(), buffer, buffer + samples * 2);
	}
	return pcm;
}

GameBootAnimation::GameBootAnimation() {}

GameBootAnimation::~GameBootAnimation() {
	Stop();
}

bool GameBootAnimation::Available() {
	return pmf_player_available() && !g_Config.nandRootDirectory.empty() && File::Exists(GameBootPath());
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
	std::vector<int16_t> pcm = DecodeAudio(file_);
	if (pcm.size() >= 4) {
		const int frames = (int)(pcm.size() / 2);
		duration_ = std::min(MAX_DURATION_SECONDS, frames / 44100.0);
		int16_t *data = new int16_t[pcm.size()];
		memcpy(data, pcm.data(), pcm.size() * sizeof(int16_t));
		SoundEffectMixer &sfx = g_BackgroundAudio.SFX();
		sfx.UpdateSample(UI::UISound::GAME_BOOT, new Sample(data, 2, frames, 44100));
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
		g_BackgroundAudio.SFX().Stop(UI::UISound::GAME_BOOT);
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
	Draw::Texture *frame = video_->Update(dc.GetDrawContext(), time_now_d() - startTime_);
	if (frame) {
		dc.GetDrawContext()->BindTexture(0, frame);
		dc.Draw()->DrawTexRect(screen, 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF);
		dc.Flush();
		dc.RebindTexture();
	}
}

void GameBootAnimation::DeviceLost() {
	if (video_) {
		video_->DeviceLost();
	}
}
