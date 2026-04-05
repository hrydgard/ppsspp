#pragma once

#include <mutex>
#include <thread>
#include "Common/Serialize/Serializer.h"
#include "Common/CommonTypes.h"
#include "Common/TimeUtil.h"

namespace SaveState {

// This ring buffer of states is for rewind save states, which are kept in RAM.
// Save states are compressed against one of two reference saves (bases_), and the reference
// is switched to a fresh save every N saves, where N is BASE_USAGE_INTERVAL.
// The compression is a simple block based scheme where 0 means to copy a block from the base,
// and 1 means that the following bytes are the next block. See Compress/LockedDecompress.
class StateRingbuffer {
public:
	StateRingbuffer() {
		size_ = REWIND_NUM_STATES;
		states_.resize(size_);
		baseMapping_.resize(size_);
	}

	~StateRingbuffer() {
		if (compressThread_.joinable()) {
			compressThread_.join();
		}
	}

	CChunkFileReader::Error Save();
	CChunkFileReader::Error Restore(std::string *errorString);
	void ScheduleCompress(std::vector<u8> *result, const std::vector<u8> *state, const std::vector<u8> *base);
	void Compress(std::vector<u8> &result, const std::vector<u8> &state, const std::vector<u8> &base);
	void LockedDecompress(std::vector<u8> &result, const std::vector<u8> &compressed, const std::vector<u8> &base);
	void Clear();

	bool Empty() const {
		return next_ == first_;
	}

	void Process();
	void NotifyState();

private:
	const int BLOCK_SIZE = 8192;
	const int REWIND_NUM_STATES = 20;
	// TODO: Instead, based on size of compressed state?
	const int BASE_USAGE_INTERVAL = 15;

	typedef std::vector<u8> StateBuffer;

	int first_ = 0;
	int next_ = 0;
	int size_;

	std::vector<StateBuffer> states_;
	StateBuffer bases_[2];
	std::vector<int> baseMapping_;
	std::mutex lock_;
	std::thread compressThread_;
	std::vector<u8> buffer_;

	int base_ = -1;
	int baseUsage_ = 0;

	double rewindLastTime_ = 0.0f;
};

}  // namespace SaveState
