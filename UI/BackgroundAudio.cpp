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
	: file_((const uint8_t *)&data[0], (int32_t)data.size()) {
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

			// If we have no loop info, we'll just loop the entire audio.
			raw_offset_loop_start_ = 0;
			raw_offset_loop_end_ = 0;
			skip_next_samples_ = 0;

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

			if (file_.Descend('data')) {			//enter the data chunk
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
		raw_data_ = nullptr;
		delete[] buffer_;
		buffer_ = nullptr;
		delete decoder_;
		decoder_ = nullptr;
	}

	bool IsOK() { return raw_data_ != nullptr; }

	bool Read(int *buffer, int len) {
		if (!raw_data_)
			return false;

		while (bgQueue.size() < (size_t)(len * 2)) {
			int outBytes = 0;
			decoder_->Decode(raw_data_ + raw_offset_, raw_bytes_per_frame_, (uint8_t *)buffer_, &outBytes);
			if (!outBytes)
				return false;

			if (raw_offset_loop_end_ != 0 && raw_offset_ == raw_offset_loop_end_) {
				// Only take the remaining bytes, but convert to stereo s16.
				outBytes = std::min(outBytes, loop_end_offset_ * 4);
			}

			int start = skip_next_samples_;
			skip_next_samples_ = 0;

			for (int i = start; i < outBytes / 2; i++) {
				bgQueue.push(buffer_[i]);
			}

			if (raw_offset_loop_end_ != 0 && raw_offset_ == raw_offset_loop_end_) {
				// Time to loop.  Account for the addition below.
				raw_offset_ = raw_offset_loop_start_ - raw_bytes_per_frame_;
				// This time we're counting each stereo sample.
				skip_next_samples_ = loop_start_offset_ * 2;
			}

			// Handle loops when there's no loop info.
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
	uint8_t *raw_data_ = nullptr;
	int raw_data_size_ = 0;
	int raw_offset_ = 0;
	int raw_bytes_per_frame_;
	int raw_offset_loop_start_ = 0;
	int raw_offset_loop_end_ = 0;
	int loop_start_offset_ = 0;
	int loop_end_offset_ = 0;
	int skip_next_samples_ = 0;
	FixedSizeQueue<s16, 128 * 1024> bgQueue;
	short *buffer_ = nullptr;
	SimpleAudio *decoder_ = nullptr;
};

static std::mutex g_bgMutex;
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

	std::lock_guard<std::mutex> lock(g_bgMutex);
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

	std::lock_guard<std::mutex> lock(g_bgMutex);

	// Immediately stop the sound if it is turned off while playing.
	if (!g_Config.bEnableSound) {
		ClearBackgroundAudio(true);
		__PushExternalAudio(0, 0);
		return 0;
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

// Stuff that should be on the UI thread only, like anything to do with
// g_gameInfoCache.
void UpdateBackgroundAudio() {
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
