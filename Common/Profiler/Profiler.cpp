// Frame profiler: per-thread event rings, with per-frame totals worked out from them.

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

#include "ppsspp_config.h"

#include "Common/Profiler/Profiler.h"

#ifdef USE_PROFILER

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/TimeUtil.h"

#define MAX_CATEGORIES 64  // Names past this aren't recorded.
#define MAX_LANES 32       // Threads recording at once. Past this, a thread isn't recorded.
#define MAX_DEPTH 32       // Deeper scopes are recorded at this level.
#define LANE_EVENTS 16384  // Must be a power of 2. 256 KB a thread.
#define HISTORY_SIZE 128   // Must be a power of 2.

namespace {

struct Lane {
	ProfilerEvent events[LANE_EVENTS];
	// Count of events ever written. Only the owning thread writes it.
	std::atomic<uint64_t> written{};
	std::atomic<bool> inUse{};
	// Guarded by g_lanesMutex.
	char name[32]{};

	// Owning thread only.
	int depth = 0;

	// End of frame only (one thread). Replays the events to attribute the time between them to the
	// innermost open scope.
	uint64_t aggRead = 0;
	uint64_t aggLastTicks = 0;
	int aggDepth = 0;
	uint16_t aggStack[MAX_DEPTH]{};
};

struct FrameTotals {
	float seconds[MAX_LANES][MAX_CATEGORIES];
	int counts[MAX_LANES][MAX_CATEGORIES];
};

// Lanes are allocated as threads first record and never freed, so readers can walk them unlocked.
// A thread that exits gives its lane back for the next new thread.
Lane *g_lanes[MAX_LANES];
std::atomic<int> g_numLanes;
std::mutex g_lanesMutex;

const char *g_categories[MAX_CATEGORIES];
std::atomic<int> g_numCategories;
std::mutex g_categoriesMutex;

std::atomic<bool> g_paused;
bool g_initialized;

// End of frame only, apart from reads of the finished frames.
FrameTotals *g_history;
uint64_t g_frameEnds[HISTORY_SIZE];
int g_historyPos;
double g_secondsPerTick;
uint64_t g_startTicks;

struct LaneOwner {
	Lane *lane = nullptr;
	bool failed = false;
	~LaneOwner() {
		if (lane) {
			lane->inUse.store(false, std::memory_order_release);
		}
	}
};

thread_local LaneOwner t_laneOwner;

Lane *ClaimLane() {
	std::lock_guard<std::mutex> guard(g_lanesMutex);
	const int count = g_numLanes.load(std::memory_order_relaxed);
	Lane *lane = nullptr;
	for (int i = 0; i < count; i++) {
		if (!g_lanes[i]->inUse.load(std::memory_order_acquire)) {
			lane = g_lanes[i];
			break;
		}
	}
	if (!lane) {
		if (count >= MAX_LANES) {
			return nullptr;
		}
		lane = new Lane();
		g_lanes[count] = lane;
		g_numLanes.store(count + 1, std::memory_order_release);
	}
	lane->inUse.store(true, std::memory_order_relaxed);
	lane->depth = 0;
	const char *name = GetCurrentThreadName();
	truncate_cpy(lane->name, (name && name[0]) ? name : "(unnamed)");
	return lane;
}

inline Lane *CurrentLane() {
	LaneOwner &owner = t_laneOwner;
	if (!owner.lane && !owner.failed) {
		owner.lane = ClaimLane();
		owner.failed = owner.lane == nullptr;
	}
	return owner.lane;
}

inline void Record(Lane *lane, ProfilerEventType type, int category, int level) {
	const uint64_t w = lane->written.load(std::memory_order_relaxed);
	ProfilerEvent &e = lane->events[w & (LANE_EVENTS - 1)];
	e.ticks = time_now_fast_ticks();
	e.category = (uint16_t)category;
	e.type = type;
	e.level = (int8_t)std::min(level, MAX_DEPTH - 1);
	lane->written.store(w + 1, std::memory_order_release);
}

// Copies event index i of the lane. False if the writer has overwritten (or may be overwriting) it.
inline bool ReadEvent(const Lane *lane, uint64_t i, ProfilerEvent *out) {
	*out = lane->events[i & (LANE_EVENTS - 1)];
	std::atomic_thread_fence(std::memory_order_acquire);
	const uint64_t w = lane->written.load(std::memory_order_relaxed);
	return i + LANE_EVENTS > w && out->category < MAX_CATEGORIES && out->level >= 0 && out->level < MAX_DEPTH;
}

inline uint64_t OldestReadable(uint64_t written) {
	return written > LANE_EVENTS - 1 ? written - (LANE_EVENTS - 1) : 0;
}

void Attribute(Lane *lane, int laneIndex, FrameTotals &frame, uint64_t until) {
	if (lane->aggDepth > 0 && until > lane->aggLastTicks) {
		frame.seconds[laneIndex][lane->aggStack[lane->aggDepth - 1]] += (float)((until - lane->aggLastTicks) * g_secondsPerTick);
	}
	if (until > lane->aggLastTicks) {
		lane->aggLastTicks = until;
	}
}

void AggregateLane(Lane *lane, int laneIndex, FrameTotals &frame, uint64_t frameEnd) {
	const uint64_t written = lane->written.load(std::memory_order_acquire);
	uint64_t i = lane->aggRead;
	if (i < OldestReadable(written)) {
		// Lost some, so the open scopes are unknown. The next event's level resyncs them.
		i = OldestReadable(written);
		lane->aggDepth = 0;
	}
	for (; i < written; i++) {
		ProfilerEvent e;
		if (!ReadEvent(lane, i, &e)) {
			lane->aggDepth = 0;
			continue;
		}
		if (e.ticks > frameEnd) {
			break;
		}
		if (lane->aggDepth == 0) {
			// Nothing open (or resyncing): nothing to attribute up to here.
			lane->aggLastTicks = e.ticks;
		}
		Attribute(lane, laneIndex, frame, e.ticks);
		if (e.type == ProfilerEventType::Begin) {
			lane->aggStack[e.level] = e.category;
			lane->aggDepth = e.level + 1;
		} else {
			lane->aggDepth = e.level;
			frame.counts[laneIndex][e.category]++;
		}
	}
	lane->aggRead = i;
	Attribute(lane, laneIndex, frame, frameEnd);
}

int HistoryIndex(int i, int count) {
	// i in [0, count), where count - 1 is the last finished frame.
	return (g_historyPos - count + i + HISTORY_SIZE) & (HISTORY_SIZE - 1);
}

}  // namespace

void internal_profiler_init() {
	if (g_initialized) {
		return;
	}
	g_secondsPerTick = fast_ticks_to_nanos() * 1e-9;
	g_history = new FrameTotals[HISTORY_SIZE]();
	g_startTicks = time_now_fast_ticks();
	g_initialized = true;
}

int Profiler_GetCategory(const char *name) {
	const int count = g_numCategories.load(std::memory_order_acquire);
	for (int i = 0; i < count; i++) {
		if (g_categories[i] == name) {
			return i;
		}
	}
	std::lock_guard<std::mutex> guard(g_categoriesMutex);
	const int locked = g_numCategories.load(std::memory_order_relaxed);
	for (int i = 0; i < locked; i++) {
		if (!strcmp(g_categories[i], name)) {
			return i;
		}
	}
	if (locked >= MAX_CATEGORIES) {
		return -1;
	}
	g_categories[locked] = name;
	g_numCategories.store(locked + 1, std::memory_order_release);
	return locked;
}

void Profiler_Enter(int category) {
	if (category < 0 || !g_initialized) {
		return;
	}
	Lane *lane = CurrentLane();
	if (!lane) {
		return;
	}
	const int level = lane->depth++;
	if (!g_paused.load(std::memory_order_relaxed)) {
		Record(lane, ProfilerEventType::Begin, category, level);
	}
}

void Profiler_Leave(int category) {
	if (category < 0 || !g_initialized) {
		return;
	}
	Lane *lane = CurrentLane();
	// A scope entered before init has nothing to close.
	if (!lane || lane->depth == 0) {
		return;
	}
	const int level = --lane->depth;
	if (!g_paused.load(std::memory_order_relaxed)) {
		Record(lane, ProfilerEventType::End, category, level);
	}
}

void internal_profiler_end_frame() {
	if (!g_initialized || g_paused.load(std::memory_order_relaxed)) {
		return;
	}
	const uint64_t now = time_now_fast_ticks();
	const int pos = g_historyPos;
	FrameTotals &frame = g_history[pos];
	memset(&frame, 0, sizeof(frame));
	const int numLanes = g_numLanes.load(std::memory_order_acquire);
	for (int i = 0; i < numLanes; i++) {
		AggregateLane(g_lanes[i], i, frame, now);
	}
	g_frameEnds[pos] = now;
	g_historyPos = (pos + 1) & (HISTORY_SIZE - 1);
}

const char *Profiler_GetCategoryName(int i) {
	return i >= 0 && i < g_numCategories.load(std::memory_order_acquire) ? g_categories[i] : "N/A";
}

int Profiler_GetHistoryLength() {
	// Leaves out the slot the next frame end will write.
	return HISTORY_SIZE - 1;
}

int Profiler_GetNumCategories() {
	return g_numCategories.load(std::memory_order_acquire);
}

int Profiler_GetNumThreads() {
	return g_numLanes.load(std::memory_order_acquire);
}

const char *Profiler_GetThreadName(int thread, char *buf, size_t bufSize) {
	std::lock_guard<std::mutex> guard(g_lanesMutex);
	if (thread < 0 || thread >= g_numLanes.load(std::memory_order_relaxed) || bufSize == 0) {
		return "";
	}
	truncate_cpy(buf, bufSize, g_lanes[thread]->name);
	return buf;
}

void Profiler_GetSlowestThreads(int *data, int count) {
	const int numCategories = Profiler_GetNumCategories();
	const int numLanes = Profiler_GetNumThreads();
	for (int i = 0; i < count; i++) {
		const FrameTotals &frame = g_history[HistoryIndex(i, count)];
		float slowestTime = 0.0f;
		data[i] = 0;
		for (int lane = 0; lane < numLanes; ++lane) {
			float sum = 0.0f;
			for (int c = 0; c < numCategories; ++c) {
				sum += frame.seconds[lane][c];
			}
			if (sum > slowestTime) {
				slowestTime = sum;
				data[i] = lane;
			}
		}
	}
}

void Profiler_GetSlowestHistory(int category, int *slowestThreads, float *data, int count) {
	for (int i = 0; i < count; i++) {
		data[i] = category >= 0 && category < MAX_CATEGORIES ? g_history[HistoryIndex(i, count)].seconds[slowestThreads[i]][category] : 0.0f;
	}
}

void Profiler_GetHistory(int category, int thread, float *data, int count) {
	const bool valid = category >= 0 && category < MAX_CATEGORIES && thread >= 0 && thread < MAX_LANES;
	for (int i = 0; i < count; i++) {
		data[i] = valid ? g_history[HistoryIndex(i, count)].seconds[thread][category] : 0.0f;
	}
}

void Profiler_GetHistoryCounts(int category, int thread, int *data, int count) {
	const bool valid = category >= 0 && category < MAX_CATEGORIES && thread >= 0 && thread < MAX_LANES;
	for (int i = 0; i < count; i++) {
		data[i] = valid ? g_history[HistoryIndex(i, count)].counts[thread][category] : 0;
	}
}

void Profiler_SetPaused(bool paused) {
	g_paused.store(paused, std::memory_order_relaxed);
}

bool Profiler_IsPaused() {
	return g_paused.load(std::memory_order_relaxed);
}

uint64_t Profiler_Now() {
	return time_now_fast_ticks();
}

uint64_t Profiler_GetStartTicks() {
	return g_startTicks;
}

double Profiler_TicksToSeconds(int64_t ticks) {
	return ticks * g_secondsPerTick;
}

int64_t Profiler_SecondsToTicks(double seconds) {
	return g_secondsPerTick > 0.0 ? (int64_t)(seconds / g_secondsPerTick) : 0;
}

void Profiler_GetEvents(int thread, uint64_t from, uint64_t to, std::vector<ProfilerEvent> *events) {
	if (thread < 0 || thread >= g_numLanes.load(std::memory_order_acquire)) {
		return;
	}
	const Lane *lane = g_lanes[thread];
	const uint64_t written = lane->written.load(std::memory_order_acquire);
	// Ticks only grow along a lane, so binary search for the first event at or after from.
	uint64_t lo = OldestReadable(written), hi = written;
	while (lo < hi) {
		const uint64_t mid = lo + (hi - lo) / 2;
		ProfilerEvent e;
		if (!ReadEvent(lane, mid, &e) || e.ticks < from) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	for (uint64_t i = lo; i < written; i++) {
		ProfilerEvent e;
		if (!ReadEvent(lane, i, &e)) {
			continue;
		}
		if (e.ticks > to) {
			break;
		}
		events->push_back(e);
	}
}

void Profiler_GetFrameEnds(std::vector<uint64_t> *ends) {
	for (int i = 0; i < HISTORY_SIZE - 1; i++) {
		const uint64_t t = g_frameEnds[HistoryIndex(i, HISTORY_SIZE - 1)];
		if (t != 0) {
			ends->push_back(t);
		}
	}
}

#endif
