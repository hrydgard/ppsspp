#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

// Compiled into every build (unless it defines NO_PROFILER), and off until something turns it on
// (Profiler_SetEnabled), which costs a scope one load and a branch. Check USE_PROFILER only after
// including this file.
#if !defined(USE_PROFILER) && !defined(NO_PROFILER)
#define USE_PROFILER
#endif

#ifdef USE_PROFILER

// Each thread that enters a scope records begin and end events into a ring of its own (a lane), with
// no locking. Per-frame totals are worked out from those events at the end of each frame, and the
// ImGui debugger's timeline draws them directly.

void internal_profiler_init();
void internal_profiler_end_frame();

extern std::atomic<bool> g_profilerEnabled;
// While off, nothing is recorded and the history stands still.
void Profiler_SetEnabled(bool enabled);
inline bool Profiler_IsEnabled() {
	return g_profilerEnabled.load(std::memory_order_relaxed);
}

// Returns the category number for a name, adding it if new. Names are compared by pointer first, so
// a string literal is cheap to look up.
int Profiler_GetCategory(const char *name);
void Profiler_Enter(int category);
void Profiler_Leave(int category);

// The macros below check that the profiler is on before anything else, so that's all a scope costs
// while it's off. category is -1 then.
class ProfileThis {
public:
	explicit ProfileThis(int category) : category_(category) {
		if (category_ >= 0) {
			Profiler_Enter(category_);
		}
	}
	~ProfileThis() {
		// Left even if the profiler was turned off meanwhile, so Begins and Ends pair up.
		if (category_ >= 0) {
			Profiler_Leave(category_);
		}
	}
private:
	int category_;
};

#define PROFILE_INIT() internal_profiler_init();
// Named by line, so a scope can hold more than one.
#define PROFILE_CONCAT_INNER(a, b) a##b
#define PROFILE_CONCAT(a, b) PROFILE_CONCAT_INNER(a, b)
// cat must be a literal (or another constant): it's looked up once, the first time the line runs with the
// profiler on. Checking that first skips the static's thread-safe init guard while off. For what varies
// between calls (which file, which syscall), add a PROFILE_LABEL.
#define PROFILE_THIS_SCOPE(cat) \
	ProfileThis PROFILE_CONCAT(_profile_scoped, __LINE__)(Profiler_IsEnabled() ? [] { \
		static const int category = Profiler_GetCategory(cat); \
		return category; \
	}() : -1);
#define PROFILE_END_FRAME() internal_profiler_end_frame();

// Names this thread's innermost open scope (which syscall, which file) in the log and the timeline.
// The text must outlive the profiler, like a literal or a name in a static table.
void Profiler_Label(const char *text);
// The same for text that won't live that long: copies the first 47 characters into the thread's lane,
// which keeps the last 64 copies.
void Profiler_LabelCopy(const char *text);
#define PROFILE_LABEL(text) do { if (Profiler_IsEnabled()) { Profiler_Label(text); } } while (false)
#define PROFILE_LABEL_COPY(text) do { if (Profiler_IsEnabled()) { Profiler_LabelCopy(text); } } while (false)

// Logs stalls without the UI. Any frame (between two PROFILE_END_FRAME calls) of at least hitchMs gets
// a line in the log naming its longest scopes. With a path, every frame's per-thread totals go to that
// file as JSON lines, and for each frame of at least hitchMs, every scope of at least spanMs that ended
// in it. Tools/profile_report.py summarizes the file. Turns the profiler on, unless hitchMs <= 0, which
// turns the logging off.
void Profiler_SetLogging(const char *path, double hitchMs, double spanMs);

// Per-frame totals, in seconds of time spent in each category itself (not in scopes nested in it).
// A frame is the time between two PROFILE_END_FRAME() calls; history index count - 1 is the last one.
const char *Profiler_GetCategoryName(int i);
int Profiler_GetNumCategories();
int Profiler_GetHistoryLength();
int Profiler_GetNumThreads();
const char *Profiler_GetThreadName(int thread, char *buf, size_t bufSize);
void Profiler_GetSlowestThreads(int *data, int count);
void Profiler_GetSlowestHistory(int category, int *slowestThreads, float *data, int count);
void Profiler_GetHistory(int category, int thread, float *data, int count);
// How many times each scope of the category was left in the frame.
void Profiler_GetHistoryCounts(int category, int thread, int *data, int count);

// For the timeline.
enum class ProfilerEventType : uint8_t {
	Begin,
	End,
};

struct ProfilerEvent {
	uint64_t ticks;
	// From PROFILE_LABEL, on a Begin. Null if none.
	const char *label;
	uint16_t category;
	ProfilerEventType type;
	// Nesting depth, 0 at the outermost scope. Begin and End of one scope have the same level.
	int8_t level;
	uint32_t padding;
};

uint64_t Profiler_Now();
// When internal_profiler_init() ran.
uint64_t Profiler_GetStartTicks();
double Profiler_TicksToSeconds(int64_t ticks);
int64_t Profiler_SecondsToTicks(double seconds);
// Appends the thread's events with ticks in [from, to], oldest first. Events that have left the ring
// are gone, so the first ones may be Ends whose Begin isn't there.
void Profiler_GetEvents(int thread, uint64_t from, uint64_t to, std::vector<ProfilerEvent> *events);
// When each of the history's frames ended, oldest first.
void Profiler_GetFrameEnds(std::vector<uint64_t> *ends);

#else

#define PROFILE_INIT()
#define PROFILE_THIS_SCOPE(cat)
#define PROFILE_END_FRAME()
#define PROFILE_LABEL(text)
#define PROFILE_LABEL_COPY(text)

#endif
