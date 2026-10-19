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
	rewindLastTime_ = time_now_d();

	// Make sure we're not processing a previous save. That'll cause a hitch though, but at least won't
	// crash due to contention over buffer_.
	if (compressThread_.joinable())
		compressThread_.join();

	std::lock_guard<std::mutex> guard(lock_);

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
		ScheduleCompress(&states_[n].stateBuffer, compressBuffer, &bases_[base_]);
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

CChunkFileReader::Error StateRingbuffer::Restore(std::string *errorString, std::string *metadata) {
	// The newest state may still be compressing.
	if (compressThread_.joinable())
		compressThread_.join();

	std::lock_guard<std::mutex> guard(lock_);

	// No valid states left.
	if (Empty())
		return CChunkFileReader::ERROR_BAD_FILE;

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
	const int generation = baseMapping_[n];
	if (states_[n].empty() || !BaseAlive(generation)) {
		// Save() drops states whose base is gone, so this shouldn't happen.
		WARN_LOG(Log::SaveState, "Rewind: state %d can't be decoded", n);
		DropNewest();
		return CChunkFileReader::ERROR_BAD_FILE;
	}
	const int baseSlot = generation % (int)ARRAY_SIZE(bases_);

	static std::vector<u8> buffer;
	LockedDecompress(buffer, states_[n].stateBuffer, bases_[baseSlot]);
	CChunkFileReader::Error error = LoadFromRam(buffer, errorString);
	if (error != CChunkFileReader::ERROR_NONE) {
		// Don't come back to it. HandleLoadFailure() tries the next one, and after a partial load the
		// clock is meaningless, so that keeps rewindFromEmuTime_.
		DropNewest();
		retryingAfterFailure_ = true;
		return error;
	}
	newestRestored_ = true;
	lastRestoreTime_ = now;

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

	rewindLastTime_ = now;
	return error;
}

void StateRingbuffer::DropNewest() {
	next_ = (next_ - 1 + size_) % size_;
	states_[next_].clear();
	count_--;
	newestRestored_ = false;
}

void StateRingbuffer::ScheduleCompress(std::vector<u8> *result, const std::vector<u8> *state, const std::vector<u8> *base) {
	if (compressThread_.joinable())
		compressThread_.join();
	compressThread_ = std::thread([=] {
		SetCurrentThreadName("SaveStateCompress");

		// Should do no I/O, so no JNI thread context needed.
		Compress(*result, *state, *base);
	});
}

void StateRingbuffer::Compress(std::vector<u8> &result, const std::vector<u8> &state, const std::vector<u8> &base) {
	std::lock_guard<std::mutex> guard(lock_);
	// Bail if we were cleared before locking.
	if (count_ == 0)
		return;

	double start_time = time_now_d();
	result.clear();
	result.reserve(512 * 1024);
	for (size_t i = 0; i < state.size(); i += BLOCK_SIZE)
	{
		int blockSize = std::min(BLOCK_SIZE, (int)(state.size() - i));
		if (i + blockSize > base.size() || memcmp(&state[i], &base[i], blockSize) != 0) {
			result.push_back(1);
			result.insert(result.end(), state.begin() + i, state.begin() + i + blockSize);
		} else {
			result.push_back(0);
		}
	}

	double taken_s = time_now_d() - start_time;
	DEBUG_LOG(Log::SaveState, "Rewind: Compressed save from %d bytes to %d in %0.2f ms.", (int)state.size(), (int)result.size(), taken_s * 1000.0);
}

void StateRingbuffer::LockedDecompress(std::vector<u8> &result, const std::vector<u8> &compressed, const std::vector<u8> &base) {
	result.clear();
	result.reserve(base.size());
	auto basePos = base.begin();
	for (size_t i = 0; i < compressed.size(); ) {
		if (compressed[i] == 0) {
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
			int blockSize = std::min(BLOCK_SIZE, (int)(compressed.size() - i));
			result.insert(result.end(), compressed.begin() + i, compressed.begin() + i + blockSize);
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
	if (compressThread_.joinable())
		compressThread_.join();

	// This lock is mainly for shutdown.
	std::lock_guard<std::mutex> guard(lock_);
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
	base_ = -1;
	baseUsage_ = 0;
	newestRestored_ = false;
	retryingAfterFailure_ = false;
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
