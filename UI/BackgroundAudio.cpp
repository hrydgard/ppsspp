#include <string>
#include "base/logging.h"
#include "base/timeutil.h"
#include "base/mutex.h"
#include "native/file/chunk_file.h"

#include "Common/CommonTypes.h"
#include "Core/HW/SimpleAudioDec.h"
#include "Common/FixedSizeQueue.h"
#include "GameInfoCache.h"

// Really simple looping in-memory AT3 player that also takes care of reading the file format.
// Turns out that AT3 files used for this are modified WAVE files so fairly easy to parse.
class AT3PlusReader {
public:
	AT3PlusReader(const std::string &data)
	: data_(data),
		raw_offset_(0),
		file_((const uint8_t *)&data[0],
		(int32_t)data.size()),
		raw_data_(0),
		raw_data_size_(0),
		buffer_(0),
		decoder_(0) {

		// Normally 8k but let's be safe.
		buffer_ = new short[32 * 1024];

		int codec = PSP_CODEC_AT3PLUS;
		u8 at3_extradata[16];

		int num_channels, sample_rate, numFrames, samplesPerSec, avgBytesPerSec, Nothing;
		if (file_.descend('RIFF')) {
			file_.readInt(); //get past 'WAVE'
			if (file_.descend('fmt ')) { //enter the format chunk
				int temp = file_.readInt();
				int format = temp & 0xFFFF;
				switch (format) {
				case 0xFFFE:
					codec = PSP_CODEC_AT3PLUS;
					break;
				case 0x270:
					codec = PSP_CODEC_AT3;
					break;
				default:
					ERROR_LOG(HLE, "Unexpected SND0.AT3 format %04x", format);
					return;
				}

				num_channels = temp >> 16;

				samplesPerSec = file_.readInt();
				avgBytesPerSec = file_.readInt();

				temp = file_.readInt();
				raw_bytes_per_frame_ = temp & 0xFFFF;
				Nothing = temp >> 16;

				if (codec == PSP_CODEC_AT3) {
					// The first two bytes are actually not useful part of the extradata.
					// We already read 16 bytes, so make sure there's enough left.
					if (file_.getCurrentChunkSize() >= 32) {
						file_.readData(at3_extradata, 16);
					} else {
						memset(at3_extradata, 0, sizeof(at3_extradata));
					}
				}
				file_.ascend();
				// ILOG("got fmt data: %i", samplesPerSec);
			} else {
				ELOG("Error - no format chunk in wav");
				file_.ascend();
				return;
			}

			if (file_.descend('data')) {			//enter the data chunk
				int numBytes = file_.getCurrentChunkSize();
				numFrames = numBytes / raw_bytes_per_frame_;  // numFrames

				raw_data_ = (uint8_t *)malloc(numBytes);
				raw_data_size_ = numBytes;
				if (/*raw_bytes_per_frame_ == 280 && */ num_channels == 2) {
					file_.readData(raw_data_, numBytes);
				} else {
					ELOG("Error - bad blockalign or channels");
					free(raw_data_);
					raw_data_ = 0;
					return;
				}
				file_.ascend();
			} else {
				ELOG("Error - no data chunk in wav");
				file_.ascend();
				return;
			}
			file_.ascend();
		} else {
			ELOG("Could not descend into RIFF file");
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

	bool Read(short *buffer, int len) {
		if (!raw_data_)
			return false;
		while (bgQueue.size() < len * 2) {
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
	const std::string &data_;
	ChunkFile file_;
	uint8_t *raw_data_;
	int raw_data_size_;
	int raw_offset_;
	int raw_bytes_per_frame_;
	FixedSizeQueue<s16, 128 * 1024> bgQueue;
	short *buffer_;
	SimpleAudio *decoder_;
};

static recursive_mutex bgMutex;
static std::string bgGamePath;
static int playbackOffset;
static AT3PlusReader *at3Reader;
static double gameLastChanged;

void SetBackgroundAudioGame(const std::string &path) {
	time_update();

	lock_guard lock(bgMutex);
	if (path == bgGamePath) {
		// Do nothing
		return;
	}

	gameLastChanged = time_now_d();
	if (at3Reader) {
		at3Reader->Shutdown();
		delete at3Reader;
		at3Reader = 0;
	}
	playbackOffset = 0;
	bgGamePath = path;
}

int MixBackgroundAudio(short *buffer, int size) {
	time_update();

	lock_guard lock(bgMutex);
	// If there's a game, and some time has passed since the selected game
	// last changed... (to prevent crazy amount of reads when skipping through a list)
	if (!at3Reader && bgGamePath.size() && (time_now_d() - gameLastChanged > 0.5)) {
		// Grab some audio from the current game and play it.
		GameInfo *gameInfo = g_gameInfoCache.GetInfo(bgGamePath, GAMEINFO_WANTSND);
		if (!gameInfo)
			return 0;

		if (gameInfo->sndFileData.size()) {
			const std::string &data = gameInfo->sndFileData;
			at3Reader = new AT3PlusReader(data);
		}
	}

	if (!at3Reader || !at3Reader->Read(buffer, size)) {
		memset(buffer, 0, size * 2 * sizeof(s16));
	}
	return 0;
}	