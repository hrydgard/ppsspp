#include <zstd.h>

#include "Common/Thread/ThreadUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/StringUtils.h"
#include "Core/SaveState.h"
#include "Core/SaveStateRewind.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Config.h"

namespace SaveState {

CChunkFileReader::Error StateRingbuffer::Save() {
	// The worker is still compressing the last snapshot from the buffers this would overwrite, or a
	// rewind is under way. Snapshots are best effort: try again next frame, rather than wait.
	if (compressPending_ || restoreActive_) {
		return CChunkFileReader::ERROR_NONE;
	}
	rewindLastTime_ = time_now_d();

	const int n = next_;

	std::vector<u8> *compressBuffer = &buffer_;
	CChunkFileReader::Error err;

	if (base_ == -1 || ++baseUsage_ > BASE_USAGE_INTERVAL)
	{
		base_ = (base_ + 1) % ARRAY_SIZE(bases_);
		baseUsage_ = 0;
		baseGeneration_[base_] = nextBaseGeneration_++;
		err = SaveToRam(bases_[base_]);
		// Let's not bother savestating twice.
		compressBuffer = &bases_[base_];
	} else
		err = SaveToRam(buffer_);

	if (err == CChunkFileReader::ERROR_NONE) {
		compressPending_ = true;
		RunOnWorker([this, result = &states_[n].stateBuffer, state = compressBuffer, base = &bases_[base_]]() {
			Compress(*result, *state, *base);
			compressPending_ = false;
		});
		states_[n].emuTime = CoreTiming::GetGlobalTimeUs() / 1000000.0;
		baseMapping_[n] = baseGeneration_[base_];
		next_ = (next_ + 1) % size_;
		if (count_ < size_) {
			count_++;
		}
	} else {
		states_[n].clear();
	}
	// The state a rewind restored isn't the newest any more, so the next rewind comes back to this one.
	newestRestored_ = false;

	// States compressed against a base that has since been replaced can't be decoded any more.
	// They're the oldest ones, so drop them from the end.
	while (count_ > 0) {
		const int oldest = (next_ - count_ + size_) % size_;
		if (BaseAlive(baseMapping_[oldest])) {
			break;
		}
		states_[oldest].clear();
		count_--;
	}
	return err;
}

bool StateRingbuffer::BaseAlive(int generation) const {
	return generation >= 0 && baseGeneration_[generation % ARRAY_SIZE(bases_)] == generation;
}

bool StateRingbuffer::StartRestore() {
	// No valid states left.
	if (Empty())
		return false;

	// Restoring a state doesn't use it up: rewind, play on a bit, rewind again, and you're back at the
	// same state. Rewinding again quickly steps further back instead (except from the oldest state).
	const double now = time_now_d();
	const double emuNow = CoreTiming::GetGlobalTimeUs() / 1000000.0;
	const bool quick = newestRestored_ && now - lastRestoreTime_ < STEP_BACK_SECONDS;
	if (quick) {
		if (count_ > 1) {
			DropNewest();
		}
	} else if (!retryingAfterFailure_) {
		rewindFromEmuTime_ = emuNow;
	}
	retryingAfterFailure_ = false;

	const int n = (next_ - 1 + size_) % size_;
	restoreSlot_ = n;
	restoreTime_ = now;
	restoreActive_ = true;
	restoreDecoded_ = false;
	restoreError_ = CChunkFileReader::ERROR_NONE;

	const int generation = baseMapping_[n];
	if (!BaseAlive(generation)) {
		// Save() drops states whose base is gone, so this shouldn't happen.
		restoreError_ = CChunkFileReader::ERROR_BAD_FILE;
		restoreDecoded_ = true;
		return true;
	}

	// Queued behind the newest state's compression, if that's still going.
	RunOnWorker([this, n, base = &bases_[generation % ARRAY_SIZE(bases_)]]() {
		if (states_[n].empty() || !Decompress(restoreBuffer_, states_[n].stateBuffer, *base)) {
			restoreError_ = CChunkFileReader::ERROR_BAD_FILE;
		}
		restoreDecoded_ = true;
	});
	return true;
}

CChunkFileReader::Error StateRingbuffer::FinishRestore(std::string *errorString, std::string *metadata) {
	const int n = restoreSlot_;
	restoreActive_ = false;
	if (restoreError_ != CChunkFileReader::ERROR_NONE) {
		WARN_LOG(Log::SaveState, "Rewind: state %d can't be decoded", n);
		std::vector<u8>().swap(restoreBuffer_);
		DropNewest();
		return restoreError_;
	}

	CChunkFileReader::Error error = LoadFromRam(restoreBuffer_, errorString);
	// A whole state: don't hold on to it between rewinds.
	std::vector<u8>().swap(restoreBuffer_);
	if (error != CChunkFileReader::ERROR_NONE) {
		// Don't come back to it. HandleLoadFailure() tries the next one, and after a partial load the
		// clock is meaningless, so that keeps rewindFromEmuTime_.
		DropNewest();
		retryingAfterFailure_ = true;
		return error;
	}
	newestRestored_ = true;
	lastRestoreTime_ = restoreTime_;

	auto pa = GetI18NCategory(I18NCat::PAUSE);
	*metadata = pa->T("Rewind");
	// In emulated time, from where the rewinding started: wall-clock time would count pauses,
	// fast-forward, and the time spent in states rewound to earlier.
	const double secondsBack = rewindFromEmuTime_ - states_[n].emuTime;
	if (secondsBack >= 0.0) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		metadata->append(" (");
		metadata->append(ApplySafeSubstitutions(di->T("%1 seconds ago"), (int)(secondsBack + 0.5)));
		metadata->append(")");
	}

	rewindLastTime_ = time_now_d();
	return error;
}

CChunkFileReader::Error StateRingbuffer::Restore(std::string *errorString, std::string *metadata) {
	if (!StartRestore()) {
		return CChunkFileReader::ERROR_BAD_FILE;
	}
	WaitForWorker();
	return FinishRestore(errorString, metadata);
}

void StateRingbuffer::CancelRestore() {
	if (!restoreActive_) {
		return;
	}
	// The decode may still be running into restoreBuffer_.
	WaitForWorker();
	restoreActive_ = false;
	std::vector<u8>().swap(restoreBuffer_);
}

// Only moves the ring back: the state's buffer may still be in use by a worker job, and is reused
// by the next snapshot anyway.
void StateRingbuffer::DropNewest() {
	next_ = (next_ - 1 + size_) % size_;
	count_--;
	newestRestored_ = false;
}

void StateRingbuffer::Compress(std::vector<u8> &result, const std::vector<u8> &state, const std::vector<u8> &base) {
	double start_time = time_now_d();
	std::vector<u8> delta;
	delta.reserve(512 * 1024);
	for (size_t i = 0; i < state.size(); i += BLOCK_SIZE)
	{
		int blockSize = std::min(BLOCK_SIZE, (int)(state.size() - i));
		if (i + blockSize > base.size() || memcmp(&state[i], &base[i], blockSize) != 0) {
			delta.push_back(1);
			delta.insert(delta.end(), state.begin() + i, state.begin() + i + blockSize);
		} else {
			delta.push_back(0);
		}
	}

	// The changed blocks still compress several times over, and quickly at a low level.
	result.resize(ZSTD_compressBound(delta.size()));
	const size_t compressedSize = ZSTD_compress(result.data(), result.size(), delta.data(), delta.size(), ZSTD_COMPRESSION_LEVEL);
	if (ZSTD_isError(compressedSize)) {
		ERROR_LOG(Log::SaveState, "Rewind: compression failed");
		// Left empty, a restore of it fails, and it's dropped.
		result.clear();
	} else {
		result.resize(compressedSize);
	}
	// The bound is about the size of the delta, much more than the result.
	result.shrink_to_fit();

	double taken_s = time_now_d() - start_time;
	DEBUG_LOG(Log::SaveState, "Rewind: Compressed save from %d bytes to %d (%d before zstd) in %0.2f ms.", (int)state.size(), (int)result.size(), (int)delta.size(), taken_s * 1000.0);
}

bool StateRingbuffer::Decompress(std::vector<u8> &result, const std::vector<u8> &compressed, const std::vector<u8> &base) {
	const unsigned long long deltaSize = ZSTD_getFrameContentSize(compressed.data(), compressed.size());
	// A delta is never bigger than the state it encodes, plus a flag byte per block.
	if (deltaSize == ZSTD_CONTENTSIZE_ERROR || deltaSize == ZSTD_CONTENTSIZE_UNKNOWN || deltaSize > base.size() * 2) {
		return false;
	}
	std::vector<u8> delta((size_t)deltaSize);
	const size_t decompressedSize = ZSTD_decompress(delta.data(), delta.size(), compressed.data(), compressed.size());
	if (ZSTD_isError(decompressedSize) || decompressedSize != delta.size()) {
		return false;
	}
	ApplyDelta(result, delta, base);
	return true;
}

void StateRingbuffer::ApplyDelta(std::vector<u8> &result, const std::vector<u8> &delta, const std::vector<u8> &base) {
	result.clear();
	result.reserve(base.size());
	auto basePos = base.begin();
	for (size_t i = 0; i < delta.size(); ) {
		if (delta[i] == 0) {
			++i;
			// Bound against what's actually left of the base: the subtraction this used to do
			// (base.size() - result.size()) wraps once the output is longer than the base.
			const int blockSize = (int)std::min((size_t)BLOCK_SIZE, (size_t)(base.end() - basePos));
			if (blockSize <= 0) {
				break;
			}
			result.insert(result.end(), basePos, basePos + blockSize);
			basePos += blockSize;
		} else {
			++i;
			int blockSize = std::min(BLOCK_SIZE, (int)(delta.size() - i));
			result.insert(result.end(), delta.begin() + i, delta.begin() + i + blockSize);
			i += blockSize;
			// This check is to avoid advancing basePos out of range, which MSVC catches.
			// When this happens, we're at the end of decoding anyway.
			if (base.end() - basePos >= blockSize) {
				basePos += blockSize;
			}
		}
	}
}

void StateRingbuffer::Clear() {
	// Worker jobs use the buffers below.
	WaitForWorker();

	next_ = 0;
	count_ = 0;
	for (auto &b : bases_) {
		b.clear();
	}
	for (int &g : baseGeneration_) {
		g = -1;
	}
	nextBaseGeneration_ = 0;
	baseMapping_.clear();
	baseMapping_.resize(size_);
	for (auto &s : states_) {
		s.clear();
	}
	buffer_.clear();
	std::vector<u8>().swap(restoreBuffer_);
	base_ = -1;
	baseUsage_ = 0;
	newestRestored_ = false;
	retryingAfterFailure_ = false;
	restoreActive_ = false;
	rewindLastTime_ = time_now_d();
}

void StateRingbuffer::Process() {
	if (g_Config.iRewindSnapshotInterval <= 0) {
		return;
	}
	if (coreState != CORE_RUNNING_CPU) {
		return;
	}

	// For fast-forwarding, otherwise they may be useless and too close.
	double now = time_now_d();
	double diff = now - rewindLastTime_;
	if (diff < g_Config.iRewindSnapshotInterval)
		return;

	DEBUG_LOG(Log::SaveState, "Saving rewind state");
	Save();
}

void StateRingbuffer::NotifyState() {
	// Prevent saving snapshots immediately after loading or saving a state.
	rewindLastTime_ = time_now_d();
}

double StateRingbuffer::NextStateTimestamp() const {
	return rewindLastTime_ + g_Config.iRewindSnapshotInterval;
}

}  // namespace SaveState
