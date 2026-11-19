#pragma once

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "Common/File/Path.h"
#include "Common/UI/Root.h"

class BackgroundSoundReader;

struct Sample {
	// data must be new[]-ed.
	Sample(int16_t *data, int channels, int length, int rateInHz) : channels_(channels), data_(data), length_(length), rateInHz_(rateInHz) {}
	// A copy of pcm (interleaved when stereo).
	Sample(const std::vector<int16_t> &pcm, int channels, int rateInHz)
		: channels_(channels), data_(new int16_t[pcm.size()]), length_((int)(pcm.size() / channels)), rateInHz_(rateInHz) {
		std::copy(pcm.begin(), pcm.end(), data_);
	}
	~Sample() {
		delete[] data_;
	}
	int16_t *data_;
	int length_;  // stereo or mono samples.
	int rateInHz_;  // sampleRate
	int channels_;

	static Sample *Load(const std::string &path);
};

// Mixer for things played on top of everything.
class SoundEffectMixer {
public:
	void Init();
	void Mix(int16_t *buffer, int sz, int sampleRateHz);
	void Play(UI::UISound sfx, float volume);
	// Stops it if it's playing.
	void Stop(UI::UISound sfx);

	void UpdateSample(UI::UISound sound, Sample *sample);
	// Frees its sample, after which it's silent.
	void UnloadSample(UI::UISound sound);
	void LoadDefaultSample(UI::UISound sound);
	// Loads the samples again, in the background: after a firmware is installed or removed, since
	// its sounds replace the navigation ones.
	void ReloadSamples();

	std::vector<std::unique_ptr<Sample>> samples_;

	struct PlayInstance {
		UI::UISound sound;
		int64_t offset;  // 32.32 fixed point
		int volume; // 0..255
		bool done;
	};

	// This can be called on a thread.
	void LoadSamplesOnThread();
	// The load task's body: loads until no reload is asked for (see ReloadSamples).
	void RunSampleLoads();

	// Whether the system dialogs in games get the PSP firmware's sounds too (UISoundStyle::FIRMWARE,
	// with a firmware).
	bool FirmwareExtras() const { return firmwareExtras_; }
private:
	std::atomic<bool> firmwareExtras_{ false };
	// Guards the two below, which keep sample loads to one at a time.
	std::mutex loadMutex_;
	bool loadRequested_ = false;
	bool loadRunning_ = false;
	std::mutex mutex_;
	std::vector<PlayInstance> queue_;
	std::vector<PlayInstance> plays_;
};

class BackgroundAudio {
public:
	BackgroundAudio();
	~BackgroundAudio();

	void SetGame(const Path &path);
	void Update();
	bool Play();

	SoundEffectMixer &SFX() {
		return sfxMixer_;
	}

	// The sound of an ICON1.PMF, decoded on a task (see Update). Dropped if another game has been
	// selected since generation was handed out.
	void OnPMFAudioDecoded(int generation, std::vector<int16_t> &&pcm);

	// The time a game's ICON1.PMF video should be at. When its sound is playing, its position, so
	// the two stay in sync as on a PSP. -1 while that sound is still loading: show the icon until
	// then. Otherwise (no sound in the PMF, the game's SND0.AT3 playing instead, sound off) ownTime.
	double PreviewVideoTime(const Path &game, bool pmfHasAudio, double ownTime);

private:
	void Clear(bool hard);

	enum {
		// 0.5 ms buffer at 44.1 khz should be enough.
		BUFSIZE = 22050,
	};

	std::mutex mutex_;
	Path bgGamePath_;
	std::atomic<bool> sndLoadPending_;
	int playbackOffset_ = 0;
	// The game's SND0.AT3, or the sound muxed into its ICON1.PMF.
	BackgroundSoundReader *sndReader_ = nullptr;
	// Bumped by SetGame, so a decode finishing for a game no longer selected is dropped.
	int loadGeneration_ = 0;
	// A PMFAudioDecodeTask is running for the current game.
	bool pmfDecodePending_ = false;
	double gameLastChanged_ = 0.0;
	double lastPlaybackTime_ = 0.0;
	int *buffer_ = nullptr;
	bool fadingOut_ = true;
	float volumeFader_ = 0.0f;
	float delta_ = -0.0001f;
	SoundEffectMixer sfxMixer_;
};

extern BackgroundAudio g_BackgroundAudio;
