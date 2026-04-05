#include "Common/Thread/ThreadUtil.h"
#include "Core/SaveState.h"
#include "Core/SaveStateRewind.h"
#include "Core/Core.h"
#include "Core/Config.h"

namespace SaveState {

CChunkFileReader::Error StateRingbuffer::Save() {
	rewindLastTime_ = time_now_d();

	// Make sure we're not processing a previous save. That'll cause a hitch though, but at least won't
	// crash due to contention over buffer_.
	if (compressThread_.joinable())
		compressThread_.join();

	std::lock_guard<std::mutex> guard(lock_);

	int n = next_++ % size_;
	if ((next_ % size_) == first_)
		++first_;

	std::vector<u8> *compressBuffer = &buffer_;
	CChunkFileReader::Error err;

	if (base_ == -1 || ++baseUsage_ > BASE_USAGE_INTERVAL)
	{
		base_ = (base_ + 1) % ARRAY_SIZE(bases_);
		baseUsage_ = 0;
		err = SaveToRam(bases_[base_]);
		// Let's not bother savestating twice.
		compressBuffer = &bases_[base_];
	} else
		err = SaveToRam(buffer_);

	if (err == CChunkFileReader::ERROR_NONE)
		ScheduleCompress(&states_[n], compressBuffer, &bases_[base_]);
	else
		states_[n].clear();

	baseMapping_[n] = base_;
	return err;
}

CChunkFileReader::Error StateRingbuffer::Restore(std::string *errorString) {
	std::lock_guard<std::mutex> guard(lock_);

	// No valid states left.
	if (Empty())
		return CChunkFileReader::ERROR_BAD_FILE;

	int n = (--next_ + size_) % size_;
	if (states_[n].empty())
		return CChunkFileReader::ERROR_BAD_FILE;

	static std::vector<u8> buffer;
	LockedDecompress(buffer, states_[n], bases_[baseMapping_[n]]);
	CChunkFileReader::Error error = LoadFromRam(buffer, errorString);
	rewindLastTime_ = time_now_d();
	return error;
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
	if (first_ == 0 && next_ == 0)
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
	for (size_t i = 0; i < compressed.size(); )
	{
		if (compressed[i] == 0)
		{
			++i;
			int blockSize = std::min(BLOCK_SIZE, (int)(base.size() - result.size()));
			result.insert(result.end(), basePos, basePos + blockSize);
			basePos += blockSize;
		} else
		{
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
	first_ = 0;
	next_ = 0;
	for (auto &b : bases_) {
		b.clear();
	}
	baseMapping_.clear();
	baseMapping_.resize(size_);
	for (auto &s : states_) {
		s.clear();
	}
	buffer_.clear();
	base_ = -1;
	baseUsage_ = 0;
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

}  // namespace SaveState
