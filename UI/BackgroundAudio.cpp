#include <string>
#include <mutex>

#include "base/logging.h"
#include "base/timeutil.h"
#include "file/chunk_file.h"
#include "file/vfs.h"
#include "ui/root.h"

#include "Common/CommonTypes.h"
#include "Core/HW/SimpleAudioDec.h"
#include "Core/HLE/__sceAudio.h"
#include "Common/FixedSizeQueue.h"
#include "GameInfoCache.h"
#include "Core/Config.h"
#include "UI/BackgroundAudio.h"

struct WavData {
	int num_channels = -1, sample_rate = -1, numFrames = -1, samplesPerSec = -1, avgBytesPerSec = -1, Nothing = -1;
	int raw_offset_loop_start_ = 0;
	int raw_offset_loop_end_ = 0;
	int loop_start_offset_ = 0;
	int loop_end_offset_ = 0;
	int codec = 0;
	int raw_bytes_per_frame_ = 0;
	uint8_t *raw_data_ = nullptr;
	int raw_data_size_ = 0;
	u8 at3_extradata[16];

	void Read(RIFFReader &riff);

	~WavData() {
		free(raw_data_);
		raw_data_ = nullptr;
	}
};

void WavData::Read(RIFFReader &file_) {
	// If we have no loop start info, we'll just loop the entire audio.
	raw_offset_loop_start_ = 0;
	raw_offset_loop_end_ = 0;

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
			avgBytesPerSec = file_.ReadInt();

			temp = file_.ReadInt();
			raw_bytes_per_frame_ = temp & 0xFFFF;
			Nothing = temp >> 16;

			// Not currently used, but part of the format.
			(void)avgBytesPerSec;
			(void)Nothing;

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
			// ILOG("got fmt data: %i", samplesPerSec);
		} else {
			ELOG("Error - no format chunk in wav");
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
					raw_offset_loop_start_ = (loops[i].startSample / samplesPerFrame) * raw_bytes_per_frame_;
					loop_start_offset_ = loops[i].startSample % samplesPerFrame;
					raw_offset_loop_end_ = (loops[i].endSample / samplesPerFrame) * raw_bytes_per_frame_;
					loop_end_offset_ = loops[i].endSample % samplesPerFrame;

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
			numFrames = numBytes / raw_bytes_per_frame_;  // numFrames

			raw_data_ = (uint8_t *)malloc(numBytes);
			raw_data_size_ = numBytes;
			if (num_channels == 1 || num_channels == 2) {
				file_.ReadData(raw_data_, numBytes);
			} else {
				ELOG("Error - bad blockalign or channels");
				free(raw_data_);
				raw_data_ = nullptr;
				return;
			}
			file_.Ascend();
		} else {
			ELOG("Error - no data chunk in wav");
			file_.Ascend();
			return;
		}
		file_.Ascend();
	} else {
		ELOG("Could not descend into RIFF file.");
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
			decoder_->SetExtraData(&wave_.at3_extradata[2], 14, wave_.raw_bytes_per_frame_);
		}
		ILOG("read ATRAC, frames: %d, rate %d", wave_.numFrames, wave_.sample_rate);
	}

	~AT3PlusReader() {
		delete[] buffer_;
		buffer_ = nullptr;
		delete decoder_;
		decoder_ = nullptr;
	}

	bool IsOK() { return wave_.raw_data_ != nullptr; }

	bool Read(int *buffer, int len) {
		if (!wave_.raw_data_)
			return false;

		while (bgQueue.size() < (size_t)(len * 2)) {
			int outBytes = 0;
			decoder_->Decode(wave_.raw_data_ + raw_offset_, wave_.raw_bytes_per_frame_, (uint8_t *)buffer_, &outBytes);
			if (!outBytes)
				return false;

			if (wave_.raw_offset_loop_end_ != 0 && raw_offset_ == wave_.raw_offset_loop_end_) {
				// Only take the remaining bytes, but convert to stereo s16.
				outBytes = std::min(outBytes, wave_.loop_end_offset_ * 4);
			}

			int start = skip_next_samples_;
			skip_next_samples_ = 0;

			for (int i = start; i < outBytes / 2; i++) {
				bgQueue.push(buffer_[i]);
			}

			if (wave_.raw_offset_loop_end_ != 0 && raw_offset_ == wave_.raw_offset_loop_end_) {
				// Time to loop.  Account for the addition below.
				raw_offset_ = wave_.raw_offset_loop_start_ - wave_.raw_bytes_per_frame_;
				// This time we're counting each stereo sample.
				skip_next_samples_ = wave_.loop_start_offset_ * 2;
			}

			// Handle loops when there's no loop info.
			raw_offset_ += wave_.raw_bytes_per_frame_;
			if (raw_offset_ >= wave_.raw_data_size_) {
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
}

BackgroundAudio::~BackgroundAudio() {
	delete[] buffer;
}

BackgroundAudio::Sample *BackgroundAudio::LoadSample(const std::string &path) {
	size_t bytes;
	uint8_t *data = VFSReadFile(path.c_str(), &bytes);
	if (!data) {
		return nullptr;
	}

	RIFFReader reader(data, (int)bytes);

	WavData wave;
	wave.Read(reader);

	if (wave.num_channels != 2 || wave.sample_rate != 44100 || wave.raw_bytes_per_frame_ != 4) {
		ELOG("Wave format not supported for mixer playback. Must be 16-bit raw stereo. '%s'", path.c_str());
		return nullptr;
	}

	int16_t *samples = new int16_t[2 * wave.numFrames];
	memcpy(samples, wave.raw_data_, wave.numFrames * wave.raw_bytes_per_frame_);

	return new BackgroundAudio::Sample(samples, wave.numFrames);
}

void BackgroundAudio::LoadSamples() {
	samples_.resize((size_t)MenuSFX::COUNT);
	samples_[(size_t)MenuSFX::BACK] = std::unique_ptr<Sample>(LoadSample("sfx_back.wav"));
	samples_[(size_t)MenuSFX::SELECT] = std::unique_ptr<Sample>(LoadSample("sfx_select.wav"));
	samples_[(size_t)MenuSFX::CONFIRM] = std::unique_ptr<Sample>(LoadSample("sfx_confirm.wav"));

	UI::SetSoundCallback([](UI::UISound sound) {
		MenuSFX sfx;
		switch (sound) {
		case UI::UISound::BACK: sfx = MenuSFX::BACK; break;
		case UI::UISound::CONFIRM: sfx = MenuSFX::CONFIRM; break;
		case UI::UISound::SELECT: sfx = MenuSFX::SELECT; break;
		default: return;
		}

		g_BackgroundAudio.PlaySFX(sfx);
	});
}

void BackgroundAudio::PlaySFX(MenuSFX sfx) {
	std::lock_guard<std::mutex> lock(g_bgMutex);
	plays_.push_back(PlayInstance{ sfx, 0, 64, false });
}

void BackgroundAudio::Clear(bool hard) {
	if (!hard) {
		fadingOut = true;
		volume = 1.0f;
		return;
	}
	if (at3Reader) {
		delete at3Reader;
		at3Reader = nullptr;
	}
	playbackOffset = 0;
}

void BackgroundAudio::SetGame(const std::string &path) {
	time_update();

	std::lock_guard<std::mutex> lock(g_bgMutex);
	if (path == bgGamePath) {
		// Do nothing
		return;
	}

	if (path.size() == 0) {
		Clear(false);
		fadingOut = true;
	} else {
		Clear(true);
		gameLastChanged = time_now_d();
		fadingOut = false;
	}
	volume = 1.0f;
	bgGamePath = path;
}

int BackgroundAudio::Play() {
	time_update();

	std::lock_guard<std::mutex> lock(g_bgMutex);

	// Immediately stop the sound if it is turned off while playing.
	if (!g_Config.bEnableSound) {
		Clear(true);
		__PushExternalAudio(0, 0);
		return 0;
	}

	double now = time_now_d();
	int sz = lastPlaybackTime <= 0.0 ? 44100 / 60 : (int)((now - lastPlaybackTime) * 44100);
	sz = std::min(BUFSIZE / 2, sz);
	if (at3Reader) {
		if (sz >= 16) {
			if (at3Reader->Read(buffer, sz)) {
				if (fadingOut) {
					for (int i = 0; i < sz*2; i += 2) {
						buffer[i] *= volume;
						buffer[i + 1] *= volume;
						volume += delta;
					}
				}
			}
		}
	} else {
		for (int i = 0; i < sz * 2; i += 2) {
			buffer[i] = 0;
			buffer[i + 1] = 0;
		}
	}

	// Mix in menu sound effects. Terribly slow mixer but meh.
	if (!plays_.empty()) {
		for (int i = 0; i < sz * 2; i += 2) {
			std::vector<PlayInstance>::iterator iter = plays_.begin();
			while (iter != plays_.end()) {
				PlayInstance inst = *iter;
				auto sample = samples_[(int)inst.sound].get();
				if (iter->offset >= sample->length_) {
					iter->done = true;
					iter = plays_.erase(iter);
				} else {
					if (!iter->done) {
						buffer[i] += sample->data_[inst.offset * 2] * inst.volume >> 8;
						buffer[i + 1] += sample->data_[inst.offset * 2 + 1] * inst.volume >> 8;
					}
					iter->offset++;
					iter++;
				}
			}
		}
	}

	__PushExternalAudio(buffer, sz);

	if (at3Reader && fadingOut && volume <= 0.0f) {
		Clear(true);
		fadingOut = false;
		gameLastChanged = 0;
	}

	lastPlaybackTime = now;

	return 0;
}

void BackgroundAudio::Update() {
	// If there's a game, and some time has passed since the selected game
	// last changed... (to prevent crazy amount of reads when skipping through a list)
	if (bgGamePath.size() && (time_now_d() - gameLastChanged > 0.5)) {
		std::lock_guard<std::mutex> lock(g_bgMutex);
		if (!at3Reader) {
			// Grab some audio from the current game and play it.
			if (!g_gameInfoCache)
				return;

			std::shared_ptr<GameInfo> gameInfo = g_gameInfoCache->GetInfo(NULL, bgGamePath, GAMEINFO_WANTSND);
			if (!gameInfo)
				return;

			if (gameInfo->pending) {
				// Should try again shortly..
				return;
			}

			if (gameInfo->sndFileData.size()) {
				const std::string &data = gameInfo->sndFileData;
				at3Reader = new AT3PlusReader(data);
				lastPlaybackTime = 0.0;
			}
		}
	}
}
