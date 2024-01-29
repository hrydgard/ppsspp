#include <string>
#include <mutex>

#include "Common/File/VFS/VFS.h"
#include "Common/UI/Root.h"

#include "Common/Data/Text/I18n.h"
#include "Common/CommonTypes.h"
#include "Common/Data/Format/RIFF.h"
#include "Common/Log.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/TimeUtil.h"
#include "Common/Data/Collections/FixedSizeQueue.h"
#include "Core/HW/SimpleAudioDec.h"
#include "Core/HLE/__sceAudio.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "UI/GameInfoCache.h"
#include "UI/BackgroundAudio.h"

struct WavData {
	int num_channels = -1;
	int sample_rate = -1;
	int numFrames = -1;
	int samplesPerSec = -1;
	int avgBytesPerSec = -1;
	int raw_offset_loop_start = 0;
	int raw_offset_loop_end = 0;
	int loop_start_offset = 0;
	int loop_end_offset = 0;
	int codec = 0;
	int raw_bytes_per_frame = 0;
	uint8_t *raw_data = nullptr;
	int raw_data_size = 0;
	u8 at3_extradata[16];

	void Read(RIFFReader &riff);

	~WavData() {
		free(raw_data);
		raw_data = nullptr;
	}

	bool IsSimpleWAV() const {
		bool isBad = raw_bytes_per_frame > sizeof(int16_t) * num_channels;
		return !isBad && num_channels > 0 && sample_rate >= 8000 && codec == 0;
	}
};

void WavData::Read(RIFFReader &file_) {
	// If we have no loop start info, we'll just loop the entire audio.
	raw_offset_loop_start = 0;
	raw_offset_loop_end = 0;

	if (file_.Descend('RIFF')) {
		file_.ReadInt(); //get past 'WAVE'
		if (file_.Descend('fmt ')) { //enter the format chunk
			int temp = file_.ReadInt();
			int format = temp & 0xFFFF;
			switch (format) {
			case 0xFFFE:
				codec = PSP_CODEC_AT3PLUS;
				break;
			case 0x270:
				codec = PSP_CODEC_AT3;
				break;
			case 1:
				// Raw wave data, no codec
				codec = 0;
				break;
			default:
				ERROR_LOG(SCEAUDIO, "Unexpected wave format %04x", format);
				return;
			}

			num_channels = temp >> 16;

			samplesPerSec = file_.ReadInt();
			/*avgBytesPerSec =*/ file_.ReadInt();

			temp = file_.ReadInt();
			raw_bytes_per_frame = temp & 0xFFFF;

			if (codec == PSP_CODEC_AT3) {
				// The first two bytes are actually not a useful part of the extradata.
				// We already read 16 bytes, so make sure there's enough left.
				if (file_.GetCurrentChunkSize() >= 32) {
					file_.ReadData(at3_extradata, 16);
				} else {
					memset(at3_extradata, 0, sizeof(at3_extradata));
				}
			}
			file_.Ascend();
			// INFO_LOG(AUDIO, "got fmt data: %i", samplesPerSec);
		} else {
			ERROR_LOG(AUDIO, "Error - no format chunk in wav");
			file_.Ascend();
			return;
		}

		if (file_.Descend('smpl')) {
			std::vector<u8> smplData;
			smplData.resize(file_.GetCurrentChunkSize());
			file_.ReadData(&smplData[0], (int)smplData.size());

			int numLoops = *(int *)&smplData[28];
			struct AtracLoopInfo {
				int cuePointID;
				int type;
				int startSample;
				int endSample;
				int fraction;
				int playCount;
			};

			if (numLoops > 0 && smplData.size() >= 36 + sizeof(AtracLoopInfo) * numLoops) {
				AtracLoopInfo *loops = (AtracLoopInfo *)&smplData[36];
				int samplesPerFrame = codec == PSP_CODEC_AT3PLUS ? 2048 : 1024;

				for (int i = 0; i < numLoops; ++i) {
					// Only seen forward loops, so let's ignore others.
					if (loops[i].type != 0)
						continue;

					// We ignore loop interpolation (fraction) and play count for now.
					raw_offset_loop_start = (loops[i].startSample / samplesPerFrame) * raw_bytes_per_frame;
					loop_start_offset = loops[i].startSample % samplesPerFrame;
					raw_offset_loop_end = (loops[i].endSample / samplesPerFrame) * raw_bytes_per_frame;
					loop_end_offset = loops[i].endSample % samplesPerFrame;

					if (loops[i].playCount == 0) {
						// This was an infinite loop, so ignore the rest.
						// In practice, there's usually only one and it's usually infinite.
						break;
					}
				}
			}

			file_.Ascend();
		}

		// enter the data chunk
		if (file_.Descend('data')) {
			int numBytes = file_.GetCurrentChunkSize();
			numFrames = numBytes / raw_bytes_per_frame;  // numFrames

			// It seems the atrac3 codec likes to read a little bit outside.
			const int padding = 32;  // 32 is the value FFMPEG uses.
			raw_data = (uint8_t *)malloc(numBytes + padding);
			raw_data_size = numBytes;

			if (num_channels == 1 || num_channels == 2) {
				file_.ReadData(raw_data, numBytes);
			} else {
				ERROR_LOG(AUDIO, "Error - bad blockalign or channels");
				free(raw_data);
				raw_data = nullptr;
				return;
			}
			file_.Ascend();
		} else {
			ERROR_LOG(AUDIO, "Error - no data chunk in wav");
			file_.Ascend();
			return;
		}
		file_.Ascend();
	} else {
		ERROR_LOG(AUDIO, "Could not descend into RIFF file.");
		return;
	}
	sample_rate = samplesPerSec;
}

// Really simple looping in-memory AT3 player that also takes care of reading the file format.
// Turns out that AT3 files used for this are modified WAVE files so fairly easy to parse.
class AT3PlusReader {
public:
	explicit AT3PlusReader(const std::string &data)
	: file_((const uint8_t *)&data[0], (int32_t)data.size()) {
		// Normally 8k but let's be safe.
		buffer_ = new short[32 * 1024];

		skip_next_samples_ = 0;

		wave_.Read(file_);

		decoder_ = new SimpleAudio(wave_.codec, wave_.sample_rate, wave_.num_channels);
		if (wave_.codec == PSP_CODEC_AT3) {
			decoder_->SetExtraData(&wave_.at3_extradata[2], 14, wave_.raw_bytes_per_frame);
		}
		INFO_LOG(AUDIO, "read ATRAC, frames: %d, rate %d", wave_.numFrames, wave_.sample_rate);
	}

	~AT3PlusReader() {
		delete[] buffer_;
		buffer_ = nullptr;
		delete decoder_;
		decoder_ = nullptr;
	}

	bool IsOK() { return wave_.raw_data != nullptr; }

	bool Read(int *buffer, int len) {
		if (!wave_.raw_data)
			return false;

		while (bgQueue.size() < (size_t)(len * 2)) {
			int outBytes = 0;
			decoder_->Decode(wave_.raw_data + raw_offset_, wave_.raw_bytes_per_frame, (uint8_t *)buffer_, &outBytes);
			if (!outBytes)
				return false;

			if (wave_.raw_offset_loop_end != 0 && raw_offset_ == wave_.raw_offset_loop_end) {
				// Only take the remaining bytes, but convert to stereo s16.
				outBytes = std::min(outBytes, wave_.loop_end_offset * 4);
			}

			int start = skip_next_samples_;
			skip_next_samples_ = 0;

			for (int i = start; i < outBytes / 2; i++) {
				bgQueue.push(buffer_[i]);
			}

			if (wave_.raw_offset_loop_end != 0 && raw_offset_ == wave_.raw_offset_loop_end) {
				// Time to loop.  Account for the addition below.
				raw_offset_ = wave_.raw_offset_loop_start - wave_.raw_bytes_per_frame;
				// This time we're counting each stereo sample.
				skip_next_samples_ = wave_.loop_start_offset * 2;
			}

			// Handle loops when there's no loop info.
			raw_offset_ += wave_.raw_bytes_per_frame;
			if (raw_offset_ >= wave_.raw_data_size) {
				raw_offset_ = 0;
			}
		}

		for (int i = 0; i < len * 2; i++) {
			buffer[i] = bgQueue.pop_front();
		}
		return true;
	}

private:
	RIFFReader file_;

	WavData wave_;

	int raw_offset_ = 0;
	int skip_next_samples_ = 0;
	FixedSizeQueue<s16, 128 * 1024> bgQueue;
	short *buffer_ = nullptr;
	SimpleAudio *decoder_ = nullptr;
};

BackgroundAudio g_BackgroundAudio;

BackgroundAudio::BackgroundAudio() {
	buffer = new int[BUFSIZE]();
	sndLoadPending_.store(false);
}

BackgroundAudio::~BackgroundAudio() {
	delete at3Reader_;
	delete[] buffer;
}

void BackgroundAudio::Clear(bool hard) {
	if (!hard) {
		fadingOut_ = true;
		volume_ = 1.0f;
		return;
	}
	if (at3Reader_) {
		delete at3Reader_;
		at3Reader_ = nullptr;
	}
	playbackOffset_ = 0;
	sndLoadPending_ = false;
}

void BackgroundAudio::SetGame(const Path &path) {
	if (path == bgGamePath_) {
		// Do nothing
		return;
	}

	std::lock_guard<std::mutex> lock(mutex_);
	if (path.empty()) {
		Clear(false);
		sndLoadPending_ = false;
		fadingOut_ = true;
	} else {
		Clear(true);
		gameLastChanged_ = time_now_d();
		sndLoadPending_ = true;
		fadingOut_ = false;
	}
	volume_ = 1.0f;
	bgGamePath_ = path;
}

bool BackgroundAudio::Play() {
	std::lock_guard<std::mutex> lock(mutex_);

	// Immediately stop the sound if it is turned off while playing.
	if (!g_Config.bEnableSound) {
		Clear(true);
		System_AudioClear();
		return true;
	}

	double now = time_now_d();
	int sz = 44100 / 60;
	if (lastPlaybackTime_ > 0.0 && lastPlaybackTime_ <= now) {
		sz = (int)((now - lastPlaybackTime_) * 44100);
	}
	sz = std::min(BUFSIZE / 2, sz);
	if (at3Reader_) {
		if (at3Reader_->Read(buffer, sz)) {
			if (fadingOut_) {
				for (int i = 0; i < sz*2; i += 2) {
					buffer[i] *= volume_;
					buffer[i + 1] *= volume_;
					volume_ += delta_;
				}
			}
		}
	} else {
		for (int i = 0; i < sz * 2; i += 2) {
			buffer[i] = 0;
			buffer[i + 1] = 0;
		}
	}

	System_AudioPushSamples(buffer, sz);

	if (at3Reader_ && fadingOut_ && volume_ <= 0.0f) {
		Clear(true);
		fadingOut_ = false;
		gameLastChanged_ = 0;
	}

	lastPlaybackTime_ = now;

	return true;
}

void BackgroundAudio::Update() {
	// If there's a game, and some time has passed since the selected game
	// last changed... (to prevent crazy amount of reads when skipping through a list)
	if (sndLoadPending_ && (time_now_d() - gameLastChanged_ > 0.5)) {
		std::lock_guard<std::mutex> lock(mutex_);
		// Already loaded somehow?  Or no game info cache?
		if (at3Reader_ || !g_gameInfoCache)
			return;

		// Grab some audio from the current game and play it.
		std::shared_ptr<GameInfo> gameInfo = g_gameInfoCache->GetInfo(nullptr, bgGamePath_, GameInfoFlags::SND);
		if (!gameInfo->Ready(GameInfoFlags::SND)) {
			// Should try again shortly..
			return;
		}

		const std::string &data = gameInfo->sndFileData;
		if (!data.empty()) {
			at3Reader_ = new AT3PlusReader(data);
			lastPlaybackTime_ = 0.0;
		}
		sndLoadPending_ = false;
	}
}

inline int16_t ConvertU8ToI16(uint8_t value) {
	int ivalue = value - 128;
	return ivalue * 255;
}

Sample *Sample::Load(const std::string &path) {
	size_t bytes = 0;
	uint8_t *data = g_VFS.ReadFile(path.c_str(), &bytes);
	if (!data || bytes > 100000000) {
		WARN_LOG(AUDIO, "Failed to load sample '%s'", path.c_str());
		return nullptr;
	}

	RIFFReader reader(data, (int)bytes);

	WavData wave;
	wave.Read(reader);

	delete[] data;

	if (!wave.IsSimpleWAV()) {
		ERROR_LOG(AUDIO, "Wave format not supported for mixer playback. Must be 8-bit or 16-bit raw mono or stereo. '%s'", path.c_str());
		return nullptr;
	}

	int16_t *samples = new int16_t[wave.num_channels * wave.numFrames];
	if (wave.raw_bytes_per_frame == wave.num_channels * 2) {
		// 16-bit
		memcpy(samples, wave.raw_data, wave.numFrames * wave.raw_bytes_per_frame);
	} else if (wave.raw_bytes_per_frame == wave.num_channels) {
		// 8-bit. Convert.
		for (int i = 0; i < wave.num_channels * wave.numFrames; i++) {
			samples[i] = ConvertU8ToI16(wave.raw_data[i]);
		}
	}

	// Protect against bad metadata.
	int actualFrames = std::min(wave.numFrames, wave.raw_data_size / wave.raw_bytes_per_frame);

	return new Sample(samples, wave.num_channels, actualFrames, wave.sample_rate);
}

static inline int16_t Clamp16(int32_t sample) {
	if (sample < -32767) return -32767;
	if (sample > 32767) return 32767;
	return sample;
}

void SoundEffectMixer::Mix(int16_t *buffer, int sz, int sampleRateHz) {
	{
		std::lock_guard<std::mutex> guard(mutex_);
		if (!queue_.empty()) {
			for (const auto &entry : queue_) {
				plays_.push_back(entry);
			}
			queue_.clear();
		}
		if (plays_.empty()) {
			return;
		}
	}

	for (std::vector<PlayInstance>::iterator iter = plays_.begin(); iter != plays_.end(); ) {
		auto sample = samples_[(int)iter->sound].get();
		if (!sample) {
			// Remove playback instance if sample invalid.
			iter = plays_.erase(iter);
			continue;
		}

		int64_t rateOfSample = sample->rateInHz_;
		int64_t stride = (rateOfSample << 32) / sampleRateHz;

		for (int i = 0; i < sz * 2; i += 2) {
			if ((iter->offset >> 32) >= sample->length_ - 2) {
				iter->done = true;
				break;
			}

			int wholeOffset = iter->offset >> 32;
			int frac = (iter->offset >> 20) & 0xFFF;  // Use a 12 bit fraction to get away with 32-bit multiplies

			if (sample->channels_ == 2) {
				int interpolatedLeft = (sample->data_[wholeOffset * 2] * (0x1000 - frac) + sample->data_[(wholeOffset + 1) * 2] * frac) >> 12;
				int interpolatedRight = (sample->data_[wholeOffset * 2 + 1] * (0x1000 - frac) + sample->data_[(wholeOffset + 1) * 2 + 1] * frac) >> 12;

				// Clamping add on top per sample. Not great, we should be mixing at higher bitrate instead. Oh well.
				int left = Clamp16(buffer[i] + (interpolatedLeft * iter->volume >> 8));
				int right = Clamp16(buffer[i + 1] + (interpolatedRight * iter->volume >> 8));

				buffer[i] = left;
				buffer[i + 1] = right;
			} else if (sample->channels_ == 1) {
				int interpolated = (sample->data_[wholeOffset] * (0x1000 - frac) + sample->data_[wholeOffset + 1] * frac) >> 12;

				// Clamping add on top per sample. Not great, we should be mixing at higher bitrate instead. Oh well.
				int value = Clamp16(buffer[i] + (interpolated * iter->volume >> 8));

				buffer[i] = value;
				buffer[i + 1] = value;
			}

			iter->offset += stride;
		}

		if (iter->done) {
			iter = plays_.erase(iter);
		} else {
			iter++;
		}
	}
}

void SoundEffectMixer::Play(UI::UISound sfx, float volume) {
	std::lock_guard<std::mutex> guard(mutex_);
	queue_.push_back(PlayInstance{ sfx, 0, (int)(255.0f * volume), false });
}

void SoundEffectMixer::UpdateSample(UI::UISound sound, Sample *sample) {
	if (sample) {
		std::lock_guard<std::mutex> guard(mutex_);
		samples_[(size_t)sound] = std::unique_ptr<Sample>(sample);
	} else {
		LoadDefaultSample(sound);
	}
}

void SoundEffectMixer::LoadDefaultSample(UI::UISound sound) {
	const char *filename = nullptr;
	switch (sound) {
	case UI::UISound::BACK: filename = "sfx_back.wav"; break;
	case UI::UISound::SELECT: filename = "sfx_select.wav"; break;
	case UI::UISound::CONFIRM: filename = "sfx_confirm.wav"; break;
	case UI::UISound::TOGGLE_ON: filename = "sfx_toggle_on.wav"; break;
	case UI::UISound::TOGGLE_OFF: filename = "sfx_toggle_off.wav"; break;
	case UI::UISound::ACHIEVEMENT_UNLOCKED: filename = "sfx_achievement_unlocked.wav"; break;
	case UI::UISound::LEADERBOARD_SUBMITTED: filename = "sfx_leaderbord_submitted.wav"; break;
	default:
		return;
	}
	Sample *sample = Sample::Load(filename);
	if (!sample) {
		ERROR_LOG(SYSTEM, "Failed to load the default sample for UI sound %d", (int)sound);
	}
	std::lock_guard<std::mutex> guard(mutex_);
	samples_[(size_t)sound] = std::unique_ptr<Sample>(sample);
}

void SoundEffectMixer::LoadSamples() {
	samples_.resize((size_t)UI::UISound::COUNT);
	LoadDefaultSample(UI::UISound::BACK);
	LoadDefaultSample(UI::UISound::SELECT);
	LoadDefaultSample(UI::UISound::CONFIRM);
	LoadDefaultSample(UI::UISound::TOGGLE_ON);
	LoadDefaultSample(UI::UISound::TOGGLE_OFF);

	if (!g_Config.sAchievementsUnlockAudioFile.empty()) {
		UpdateSample(UI::UISound::ACHIEVEMENT_UNLOCKED, Sample::Load(g_Config.sAchievementsUnlockAudioFile));
	} else {
		LoadDefaultSample(UI::UISound::ACHIEVEMENT_UNLOCKED);
	}
	if (!g_Config.sAchievementsLeaderboardSubmitAudioFile.empty()) {
		UpdateSample(UI::UISound::LEADERBOARD_SUBMITTED, Sample::Load(g_Config.sAchievementsLeaderboardSubmitAudioFile));
	} else {
		LoadDefaultSample(UI::UISound::LEADERBOARD_SUBMITTED);
	}

	UI::SetSoundCallback([](UI::UISound sound, float volume) {
		g_BackgroundAudio.SFX().Play(sound, volume);
	});
}
