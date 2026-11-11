// Frame profiler: per-thread event rings, with per-frame totals worked out from them.

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

#include "ppsspp_config.h"

#include "Common/Profiler/Profiler.h"

#ifdef USE_PROFILER

#include <cstdio>
#include <vector>

#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/TimeUtil.h"

#define MAX_CATEGORIES 64  // Names past this aren't recorded.
#define MAX_LANES 32       // Threads recording at once. Past this, a thread isn't recorded.
#define MAX_DEPTH 32       // Deeper scopes are recorded at this level.
#define LANE_EVENTS 16384  // Must be a power of 2. 256 KB a thread.
#define HISTORY_SIZE 128   // Must be a power of 2.
#define LABEL_COPIES 64
#define RECENT_FRAMES 61
#define MAX_FRAME_SPANS 4096
#define NO_EVENT UINT64_MAX

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
	// Event index of each open scope's Begin, for labelling it. NO_EVENT if it wasn't recorded.
	uint64_t openBegin[MAX_DEPTH]{};
	char labelCopies[LABEL_COPIES][48]{};
	int labelCopyPos = 0;

	// End of frame only (one thread). Replays the events to attribute the time between them to the
	// innermost open scope, and to find the long scopes.
	uint64_t aggRead = 0;
	uint64_t aggLastTicks = 0;
	int aggDepth = 0;
	uint16_t aggStack[MAX_DEPTH]{};
	uint64_t aggBeginTicks[MAX_DEPTH]{};
	const char *aggBeginLabel[MAX_DEPTH]{};
	bool aggBeginValid[MAX_DEPTH]{};
};

// A scope that ended in the current frame, long enough to log.
struct Span {
	int lane;
	int category;
	int level;
	uint64_t begin;
	uint64_t end;
	const char *label;
	uint64_t self;  // Minus the logged spans nested in it.
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

bool g_initialized;

// End of frame only, apart from reads of the finished frames.
FrameTotals *g_history;
uint64_t g_frameEnds[HISTORY_SIZE];
int g_historyPos;
double g_secondsPerTick;
uint64_t g_startTicks;

// Logging. Set before init or from the end-of-frame thread.
double g_hitchMs;
double g_spanMs;
FILE *g_logFile;
uint64_t g_lastFrameEnd;
uint64_t g_frameNumber;
std::vector<Span> g_frameSpans;

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

// Returns the event's index.
inline uint64_t Record(Lane *lane, ProfilerEventType type, int category, int level) {
	const uint64_t w = lane->written.load(std::memory_order_relaxed);
	ProfilerEvent &e = lane->events[w & (LANE_EVENTS - 1)];
	e.ticks = time_now_fast_ticks();
	e.label = nullptr;
	e.category = (uint16_t)category;
	e.type = type;
	e.level = (int8_t)level;
	lane->written.store(w + 1, std::memory_order_release);
	return w;
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

void LoseTrack(Lane *lane) {
	lane->aggDepth = 0;
	memset(lane->aggBeginValid, 0, sizeof(lane->aggBeginValid));
}

void AggregateLane(Lane *lane, int laneIndex, FrameTotals &frame, uint64_t frameEnd, uint64_t spanTicks) {
	const uint64_t written = lane->written.load(std::memory_order_acquire);
	uint64_t i = lane->aggRead;
	if (i < OldestReadable(written)) {
		// Lost some, so the open scopes are unknown. The next event's level resyncs them.
		i = OldestReadable(written);
		LoseTrack(lane);
	}
	for (; i < written; i++) {
		ProfilerEvent e;
		if (!ReadEvent(lane, i, &e)) {
			LoseTrack(lane);
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
		const int level = e.level;
		if (e.type == ProfilerEventType::Begin) {
			lane->aggStack[level] = e.category;
			lane->aggDepth = level + 1;
			lane->aggBeginTicks[level] = e.ticks;
			// The label may come after this read, if a frame ended in between. Rare enough.
			lane->aggBeginLabel[level] = e.label;
			lane->aggBeginValid[level] = true;
		} else {
			lane->aggDepth = level;
			frame.counts[laneIndex][e.category]++;
			if (lane->aggBeginValid[level] && lane->aggStack[level] == e.category) {
				const uint64_t length = e.ticks - lane->aggBeginTicks[level];
				if (spanTicks && length >= spanTicks && g_frameSpans.size() < MAX_FRAME_SPANS) {
					g_frameSpans.push_back(Span{ laneIndex, e.category, level, lane->aggBeginTicks[level], e.ticks, lane->aggBeginLabel[level], length });
				}
			}
			lane->aggBeginValid[level] = false;
		}
	}
	lane->aggRead = i;
	Attribute(lane, laneIndex, frame, frameEnd);
}

int HistoryIndex(int i, int count) {
	// i in [0, count), where count - 1 is the last finished frame.
	return (g_historyPos - count + i + HISTORY_SIZE) & (HISTORY_SIZE - 1);
}

// libretro's stdio replacements have no fputs.
void WriteString(const char *s, FILE *f) {
	fwrite(s, 1, strlen(s), f);
}

// Writes s as a JSON string.
void WriteJsonString(FILE *f, const char *s) {
	fputc('"', f);
	for (; *s; s++) {
		const unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\') {
			fputc('\\', f);
			fputc(c, f);
		} else if (c < 0x20) {
			fprintf(f, "\\u%04x", c);
		} else {
			fputc(c, f);
		}
	}
	fputc('"', f);
}

double TicksToMs(uint64_t ticks) {
	return ticks * g_secondsPerTick * 1000.0;
}

// Recent frame lengths, for what's normal for the game right now.
double g_recentFrameMs[RECENT_FRAMES];
int g_recentFramePos;

void LogFrame(const FrameTotals &frame, int numLanes, uint64_t frameStart, uint64_t frameEnd) {
	const double frameMs = TicksToMs(frameEnd - frameStart);
	// Slow both in absolute terms and for the game: one that runs at 30 fps takes 33 ms every frame.
	double sorted[RECENT_FRAMES];
	memcpy(sorted, g_recentFrameMs, sizeof(sorted));
	std::nth_element(sorted, sorted + RECENT_FRAMES / 2, sorted + RECENT_FRAMES);
	const double typicalMs = sorted[RECENT_FRAMES / 2];
	g_recentFrameMs[g_recentFramePos] = frameMs;
	g_recentFramePos = (g_recentFramePos + 1) % RECENT_FRAMES;
	const bool hitch = frameMs >= g_hitchMs && frameMs >= typicalMs * 1.5;
	const int numCategories = g_numCategories.load(std::memory_order_acquire);

	char names[MAX_LANES][32];
	{
		std::lock_guard<std::mutex> guard(g_lanesMutex);
		for (int i = 0; i < numLanes; i++) {
			truncate_cpy(names[i], g_lanes[i]->name);
		}
	}

	// Self time of each span: what's left after the logged spans directly inside it.
	std::sort(g_frameSpans.begin(), g_frameSpans.end(), [](const Span &a, const Span &b) {
		return a.lane != b.lane ? a.lane < b.lane : (a.begin != b.begin ? a.begin < b.begin : a.level < b.level);
	});
	for (size_t i = 0; i < g_frameSpans.size(); i++) {
		Span &parent = g_frameSpans[i];
		for (size_t j = i + 1; j < g_frameSpans.size(); j++) {
			const Span &child = g_frameSpans[j];
			if (child.lane != parent.lane || child.begin >= parent.end) {
				break;
			}
			if (child.level == parent.level + 1 && child.end <= parent.end) {
				parent.self -= std::min(parent.self, child.end - child.begin);
			}
		}
	}

	if (FILE *f = g_logFile) {
		fprintf(f, "{\"f\":%llu,\"t\":%0.4f,\"ms\":%0.2f,\"typ\":%0.1f,%s\"th\":{", (unsigned long long)g_frameNumber, TicksToMs(frameEnd - g_startTicks) * 0.001, frameMs, typicalMs, hitch ? "\"slow\":1," : "");
		bool firstLane = true;
		for (int l = 0; l < numLanes; l++) {
			bool firstCat = true;
			for (int c = 0; c < numCategories; c++) {
				if (frame.seconds[l][c] <= 0.0f && frame.counts[l][c] == 0) {
					continue;
				}
				if (firstCat) {
					WriteString(firstLane ? "" : ",", f);
					WriteJsonString(f, names[l]);
					WriteString(":{", f);
					firstLane = false;
				}
				WriteString(firstCat ? "" : ",", f);
				WriteJsonString(f, g_categories[c]);
				fprintf(f, ":[%0.3f,%d]", frame.seconds[l][c] * 1000.0f, frame.counts[l][c]);
				firstCat = false;
			}
			if (!firstCat) {
				fputc('}', f);
			}
		}
		WriteString("}}\n", f);
		if (hitch) {
			for (const Span &s : g_frameSpans) {
				fprintf(f, "{\"span\":%llu,\"th\":", (unsigned long long)g_frameNumber);
				WriteJsonString(f, names[s.lane]);
				WriteString(",\"c\":", f);
				WriteJsonString(f, g_categories[s.category]);
				if (s.label) {
					WriteString(",\"n\":", f);
					WriteJsonString(f, s.label);
				}
				fprintf(f, ",\"l\":%d,\"t\":%0.4f,\"ms\":%0.3f,\"self\":%0.3f}\n", s.level, TicksToMs(s.begin - g_startTicks) * 0.001, TicksToMs(s.end - s.begin), TicksToMs(s.self));
			}
			fflush(f);
		} else if ((g_frameNumber & 63) == 0) {
			fflush(f);
		}
	}

	if (!hitch) {
		return;
	}
	// The log line: the spans that took the most time themselves, leaving out plain sleeps.
	std::vector<const Span *> worst;
	for (const Span &s : g_frameSpans) {
		if (strcmp(g_categories[s.category], "sleep") != 0) {
			worst.push_back(&s);
		}
	}
	std::sort(worst.begin(), worst.end(), [](const Span *a, const Span *b) { return a->self > b->self; });
	int vblanks = 0;
	for (int c = 0; c < numCategories; c++) {
		if (!strcmp(g_categories[c], "vblank")) {
			for (int l = 0; l < numLanes; l++) {
				vblanks += frame.counts[l][c];
			}
		}
	}
	char buf[512];
	size_t len = snprintf(buf, sizeof(buf), "Profiler: Frame %llu took %0.1f ms (typically %0.1f), %d vblanks.", (unsigned long long)g_frameNumber, frameMs, typicalMs, vblanks);
	for (size_t i = 0; i < worst.size() && i < 6 && len < sizeof(buf); i++) {
		const Span &s = *worst[i];
		len += snprintf(buf + len, sizeof(buf) - len, " %s %s%s%s %0.1f/%0.1f ms.", names[s.lane], g_categories[s.category], s.label ? ":" : "", s.label ? s.label : "", TicksToMs(s.self), TicksToMs(s.end - s.begin));
	}
	INFO_LOG(Log::System, "%s", buf);
}

}  // namespace

std::atomic<bool> g_profilerEnabled;

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
	const int level = std::min(lane->depth++, MAX_DEPTH - 1);
	lane->openBegin[level] = Record(lane, ProfilerEventType::Begin, category, level);
}

void Profiler_Label(const char *text) {
	if (!g_initialized || !text) {
		return;
	}
	Lane *lane = CurrentLane();
	if (!lane || lane->depth == 0) {
		return;
	}
	const uint64_t index = lane->openBegin[std::min(lane->depth - 1, MAX_DEPTH - 1)];
	// Unless the ring has moved past it.
	if (index != NO_EVENT && index + LANE_EVENTS > lane->written.load(std::memory_order_relaxed)) {
		lane->events[index & (LANE_EVENTS - 1)].label = text;
	}
}

void Profiler_LabelCopy(const char *text) {
	if (!g_initialized || !text) {
		return;
	}
	Lane *lane = CurrentLane();
	if (!lane || lane->depth == 0) {
		return;
	}
	char *copy = lane->labelCopies[lane->labelCopyPos];
	lane->labelCopyPos = (lane->labelCopyPos + 1) % LABEL_COPIES;
	truncate_cpy(copy, sizeof(lane->labelCopies[0]), text);
	Profiler_Label(copy);
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
	const int level = std::min(--lane->depth, MAX_DEPTH - 1);
	Record(lane, ProfilerEventType::End, category, level);
}

void internal_profiler_end_frame() {
	if (!g_initialized) {
		return;
	}
	if (!Profiler_IsEnabled()) {
		// So the first frame after turning it back on isn't taken for a long one.
		g_lastFrameEnd = 0;
		return;
	}
	const uint64_t now = time_now_fast_ticks();
	const int pos = g_historyPos;
	FrameTotals &frame = g_history[pos];
	memset(&frame, 0, sizeof(frame));
	const int numLanes = g_numLanes.load(std::memory_order_acquire);
	const bool logging = g_hitchMs > 0.0;
	const uint64_t spanTicks = logging ? std::max<uint64_t>(1, (uint64_t)(g_spanMs * 0.001 / g_secondsPerTick)) : 0;
	g_frameSpans.clear();
	for (int i = 0; i < numLanes; i++) {
		AggregateLane(g_lanes[i], i, frame, now, spanTicks);
	}
	g_frameEnds[pos] = now;
	g_historyPos = (pos + 1) & (HISTORY_SIZE - 1);

	if (logging && g_lastFrameEnd != 0) {
		LogFrame(frame, numLanes, g_lastFrameEnd, now);
	}
	g_lastFrameEnd = now;
	g_frameNumber++;
}

void Profiler_SetLogging(const char *path, double hitchMs, double spanMs) {
	if (g_logFile) {
		fclose(g_logFile);
		g_logFile = nullptr;
	}
	g_hitchMs = hitchMs;
	g_spanMs = spanMs;
	if (hitchMs > 0.0) {
		Profiler_SetEnabled(true);
	}
	if (hitchMs > 0.0 && path && path[0]) {
		g_logFile = File::OpenCFile(Path(path), "wb");
		if (!g_logFile) {
			ERROR_LOG(Log::System, "Profiler: Couldn't open %s for the log", path);
		}
	}
	INFO_LOG(Log::System, "Profiler: Logging frames of %0.1f ms or more%s%s", hitchMs, g_logFile ? " to " : "", g_logFile ? path : "");
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

void Profiler_SetEnabled(bool enabled) {
	g_profilerEnabled.store(enabled, std::memory_order_relaxed);
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
