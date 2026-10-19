#pragma once

#include <atomic>
#include "Common/Serialize/Serializer.h"
#include "Common/CommonTypes.h"
#include "Common/TimeUtil.h"

namespace SaveState {

// This ring buffer of states is for rewind save states, which are kept in RAM.
// Save states are compressed against one of two reference saves (bases_), and the reference
// is switched to a fresh save every N saves, where N is BASE_USAGE_INTERVAL.
// The compression is a simple block based scheme where 0 means to copy a block from the base,
// and 1 means that the following bytes are the next block, then zstd on top. See Compress/ApplyDelta.
//
// The emu thread owns the ring itself (which slots hold what). Compressing and decoding run as jobs
// on the savestate worker, and the emu thread leaves the buffers a job uses alone until it's done.
class StateRingbuffer {
public:
	StateRingbuffer() {
		size_ = REWIND_NUM_STATES;
		states_.resize(size_);
		baseMapping_.resize(size_);
	}

	CChunkFileReader::Error Save();
	// A rewind in two halves: StartRestore picks the state and has the worker decode it (false if
	// there's nothing to rewind to), and once RestoreDecoded, FinishRestore applies it.
	bool StartRestore();
	bool RestoreDecoded() const { return restoreDecoded_; }
	CChunkFileReader::Error FinishRestore(std::string *errorString, std::string *metadata);
	// Both halves, waiting for the worker in between.
	CChunkFileReader::Error Restore(std::string *errorString, std::string *metadata);
	// For a rewind that was started and then dropped.
	void CancelRestore();
	void Clear();

	bool Empty() const {
		return count_ == 0;
	}

	void Process();
	void NotifyState();

	double NextStateTimestamp() const;

private:
	bool BaseAlive(int generation) const;
	void DropNewest();
	void Compress(std::vector<u8> &result, const std::vector<u8> &state, const std::vector<u8> &base);
	bool Decompress(std::vector<u8> &result, const std::vector<u8> &compressed, const std::vector<u8> &base);
	void ApplyDelta(std::vector<u8> &result, const std::vector<u8> &delta, const std::vector<u8> &base);

	const int BLOCK_SIZE = 8192;
	const int REWIND_NUM_STATES = 20;
	// TODO: Instead, based on size of compressed state?
	const int BASE_USAGE_INTERVAL = 15;
	// A rewind this soon after the last one steps back past the state that one restored.
	const double STEP_BACK_SECONDS = 0.8;
	// Fast. Higher levels shrink the deltas a little more for a lot more time.
	const int ZSTD_COMPRESSION_LEVEL = 1;

	typedef std::vector<u8> StateBuffer;

	struct RewindState {
		StateBuffer stateBuffer;
		// CoreTiming's clock when the state was taken, in seconds.
		double emuTime = 0.0;

		bool empty() const { return stateBuffer.empty(); }
		void clear() { stateBuffer.clear(); emuTime = 0.0; }
	};

	// The slot the next save goes in, and how many slots before it hold states we can restore.
	int next_ = 0;
	int count_ = 0;
	int size_;

	std::vector<RewindState> states_;
	StateBuffer bases_[2];
	// Which generation each base slot currently holds, and which generation each state was
	// compressed against. There are more states than bases, so states do go stale.
	int baseGeneration_[2] = {-1, -1};
	int nextBaseGeneration_ = 0;
	std::vector<int> baseMapping_;
	std::vector<u8> buffer_;

	int base_ = -1;
	int baseUsage_ = 0;

	double rewindLastTime_ = 0.0f;

	// A snapshot being compressed on the worker, from buffer_ or a base.
	std::atomic<bool> compressPending_{ false };

	// The rewind under way: its slot, when it started, and the state the worker decodes it into.
	bool restoreActive_ = false;
	int restoreSlot_ = -1;
	double restoreTime_ = 0.0;
	std::atomic<bool> restoreDecoded_{ false };
	CChunkFileReader::Error restoreError_ = CChunkFileReader::ERROR_NONE;
	std::vector<u8> restoreBuffer_;

	// Whether the newest state is the one the last rewind restored, and when that was.
	bool newestRestored_ = false;
	double lastRestoreTime_ = 0.0;
	// CoreTiming's clock when the current run of rewinds started, for "seconds ago".
	double rewindFromEmuTime_ = 0.0;
	bool retryingAfterFailure_ = false;
};

}  // namespace SaveState
