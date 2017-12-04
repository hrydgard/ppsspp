#include <string>
#include <mutex>

#include "base/logging.h"
#include "base/timeutil.h"
#include "file/chunk_file.h"

#include "Common/CommonTypes.h"
#include "Core/HW/SimpleAudioDec.h"
#include "Core/HLE/__sceAudio.h"
#include "Common/FixedSizeQueue.h"
#include "GameInfoCache.h"
#include "Core/Config.h"

// Really simple looping in-memory AT3 player that also takes care of reading the file format.
// Turns out that AT3 files used for this are modified WAVE files so fairly easy to parse.
class AT3PlusReader {
public:
	AT3PlusReader(const std::string &data)
	: file_((const uint8_t *)&data[0], (int32_t)data.size()),
		raw_data_(0),
		raw_data_size_(0),
		raw_offset_(0),
		buffer_(0),
		decoder_(0) {

		// Normally 8k but let's be safe.
		buffer_ = new short[32 * 1024];

		int codec = PSP_CODEC_AT3PLUS;
		u8 at3_extradata[16];

		int num_channels, sample_rate, numFrames, samplesPerSec, avgBytesPerSec, Nothing;
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
				default:
					ERROR_LOG(SCEAUDIO, "Unexpected SND0.AT3 format %04x", format);
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

			if (file_.Descend('data')) {			//enter the data chunk
				int numBytes = file_.GetCurrentChunkSize();
				numFrames = numBytes / raw_bytes_per_frame_;  // numFrames

				raw_data_ = (uint8_t *)malloc(numBytes);
				raw_data_size_ = numBytes;
				if (/*raw_bytes_per_frame_ == 280 && */ num_channels == 1 || num_channels == 2) {
					file_.ReadData(raw_data_, numBytes);
				} else {
					ELOG("Error - bad blockalign or channels");
					free(raw_data_);
					raw_data_ = 0;
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
			ELOG("Could not descend into RIFF file. Data size=%d", (int32_t)data.size());
			return;
		}
		sample_rate = samplesPerSec;
		decoder_ = new SimpleAudio(codec, sample_rate, num_channels);
		if (codec == PSP_CODEC_AT3) {
			decoder_->SetExtraData(&at3_extradata[2], 14, raw_bytes_per_frame_);
		}
		ILOG("read ATRAC, frames: %i, rate %i", numFrames, sample_rate);
	}

	~AT3PlusReader() {
	}

	void Shutdown() {
		free(raw_data_);
		raw_data_ = 0;
		delete[] buffer_;
		buffer_ = 0;
		delete decoder_;
		decoder_ = 0;
	}

	bool IsOK() { return raw_data_ != 0; }

	bool Read(int *buffer, int len) {
		if (!raw_data_)
			return false;

		while (bgQueue.size() < (size_t)(len * 2)) {
			int outBytes;
			decoder_->Decode(raw_data_ + raw_offset_, raw_bytes_per_frame_, (uint8_t *)buffer_, &outBytes);
			if (!outBytes)
				return false;

			for (int i = 0; i < outBytes / 2; i++) {
				bgQueue.push(buffer_[i]);
			}

			// loop!
			raw_offset_ += raw_bytes_per_frame_;
			if (raw_offset_ >= raw_data_size_) {
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
	uint8_t *raw_data_;
	int raw_data_size_;
	int raw_offset_;
	int raw_bytes_per_frame_;
	FixedSizeQueue<s16, 128 * 1024> bgQueue;
	short *buffer_;
	SimpleAudio *decoder_;
};

static std::mutex bgMutex;
static std::string bgGamePath;
static int playbackOffset;
static AT3PlusReader *at3Reader;
static double gameLastChanged;
static double lastPlaybackTime;
static int buffer[44100];
static bool fadingOut = true;
static float volume;
static float delta = -0.0001f;

static void ClearBackgroundAudio(bool hard) {
	if (!hard) {
		fadingOut = true;
		volume = 1.0f;
		return;
	}
	if (at3Reader) {
		at3Reader->Shutdown();
		delete at3Reader;
		at3Reader = nullptr;
	}
	playbackOffset = 0;
}

void SetBackgroundAudioGame(const std::string &path) {
	time_update();

	std::lock_guard<std::mutex> lock(bgMutex);
	if (path == bgGamePath) {
		// Do nothing
		return;
	}

	if (path.size() == 0) {
		ClearBackgroundAudio(false);
		fadingOut = true;
	} else {
		ClearBackgroundAudio(true);
		gameLastChanged = time_now_d();
		fadingOut = false;
	}
	volume = 1.0f;
	bgGamePath = path;
}

int PlayBackgroundAudio() {
	time_update();

	std::lock_guard<std::mutex> lock(bgMutex);

	// Immediately stop the sound if it is turned off while playing.
	if (!g_Config.bEnableSound) {
		ClearBackgroundAudio(true);
		__PushExternalAudio(0, 0);
		return 0;
	}

	// If there's a game, and some time has passed since the selected game
	// last changed... (to prevent crazy amount of reads when skipping through a list)
	if (!at3Reader && bgGamePath.size() && (time_now_d() - gameLastChanged > 0.5)) {
		// Grab some audio from the current game and play it.
		if (!g_gameInfoCache)
			return 0;  // race condition?

		std::shared_ptr<GameInfo> gameInfo = g_gameInfoCache->GetInfo(NULL, bgGamePath, GAMEINFO_WANTSND);
		if (!gameInfo)
			return 0;

		if (gameInfo->pending) {
			// Should try again shortly..
			return 0;
		}

		if (gameInfo->sndFileData.size()) {
			const std::string &data = gameInfo->sndFileData;
			at3Reader = new AT3PlusReader(data);
			lastPlaybackTime = 0.0;
		}
	}

	double now = time_now();
	if (at3Reader) {
		int sz = lastPlaybackTime <= 0.0 ? 44100 / 60 : (int)((now - lastPlaybackTime) * 44100);
		sz = std::min((int)ARRAY_SIZE(buffer) / 2, sz);
		if (sz >= 16) {
			if (at3Reader->Read(buffer, sz)) {
				if (!fadingOut) {
					__PushExternalAudio(buffer, sz);
				} else {
					for (int i = 0; i < sz*2; i += 2) {
						buffer[i] *= volume;
						buffer[i + 1] *= volume;
						volume += delta;
					}
					__PushExternalAudio(buffer, sz);
					if (volume <= 0.0f) {
						ClearBackgroundAudio(true);
						fadingOut = false;
						gameLastChanged = 0;
					}
				}
			}
			lastPlaybackTime = now;
		}
	} else {
		__PushExternalAudio(0, 0);
		lastPlaybackTime = now;
	}

	return 0;
}
