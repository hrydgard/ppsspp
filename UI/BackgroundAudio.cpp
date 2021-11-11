#include <string>
#include <mutex>

#include "Common/File/VFS/VFS.h"
#include "Common/UI/Root.h"

#include "Common/CommonTypes.h"
#include "Common/Data/Format/RIFF.h"
#include "Common/Log.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/TimeUtil.h"
#include "Common/Data/Collections/FixedSizeQueue.h"
#include "Core/HW/SimpleAudioDec.h"
#include "Core/HLE/__sceAudio.h"
#include "GameInfoCache.h"
#include "Core/Config.h"
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

			raw_data = (uint8_t *)malloc(numBytes);
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

	delete [] data;

	if (wave.num_channels != 2 || wave.sample_rate != 44100 || wave.raw_bytes_per_frame != 4) {
		ERROR_LOG(AUDIO, "Wave format not supported for mixer playback. Must be 16-bit raw stereo. '%s'", path.c_str());
		return nullptr;
	}

	int16_t *samples = new int16_t[2 * wave.numFrames];
	memcpy(samples, wave.raw_data, wave.numFrames * wave.raw_bytes_per_frame);

	return new BackgroundAudio::Sample(samples, wave.numFrames);
}

void BackgroundAudio::LoadSamples() {
	samples_.resize((size_t)UI::UISound::COUNT);
	samples_[(size_t)UI::UISound::BACK] = std::unique_ptr<Sample>(LoadSample("sfx_back.wav"));
	samples_[(size_t)UI::UISound::SELECT] = std::unique_ptr<Sample>(LoadSample("sfx_select.wav"));
	samples_[(size_t)UI::UISound::CONFIRM] = std::unique_ptr<Sample>(LoadSample("sfx_confirm.wav"));
	samples_[(size_t)UI::UISound::TOGGLE_ON] = std::unique_ptr<Sample>(LoadSample("sfx_toggle_on.wav"));
	samples_[(size_t)UI::UISound::TOGGLE_OFF] = std::unique_ptr<Sample>(LoadSample("sfx_toggle_off.wav"));

	UI::SetSoundCallback([](UI::UISound sound) {
		g_BackgroundAudio.PlaySFX(sound);
	});
}

void BackgroundAudio::PlaySFX(UI::UISound sfx) {
	std::lock_guard<std::mutex> lock(mutex_);
	plays_.push_back(PlayInstance{ sfx, 0, 64, false });
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

int BackgroundAudio::Play() {
	std::lock_guard<std::mutex> lock(mutex_);

	// Immediately stop the sound if it is turned off while playing.
	if (!g_Config.bEnableSound) {
		Clear(true);
		__PushExternalAudio(0, 0);
		return 0;
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

	// Mix in menu sound effects. Terribly slow mixer but meh.
	if (!plays_.empty()) {
		for (int i = 0; i < sz * 2; i += 2) {
			std::vector<PlayInstance>::iterator iter = plays_.begin();
			while (iter != plays_.end()) {
				PlayInstance inst = *iter;
				auto sample = samples_[(int)inst.sound].get();
				if (!sample || iter->offset >= sample->length_) {
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

	if (at3Reader_ && fadingOut_ && volume_ <= 0.0f) {
		Clear(true);
		fadingOut_ = false;
		gameLastChanged_ = 0;
	}

	lastPlaybackTime_ = now;

	return 0;
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
		std::shared_ptr<GameInfo> gameInfo = g_gameInfoCache->GetInfo(nullptr, bgGamePath_, GAMEINFO_WANTSND);
		if (!gameInfo || gameInfo->pending) {
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
