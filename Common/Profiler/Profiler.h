#pragma once

#include <cstdint>
#include <vector>

// #define USE_PROFILER

#ifdef USE_PROFILER

// Each thread that enters a scope records begin and end events into a ring of its own (a lane), with
// no locking. Per-frame totals are worked out from those events at the end of each frame, and the
// ImGui debugger's timeline draws them directly.

void internal_profiler_init();
void internal_profiler_end_frame();

// Returns the category number for a name, adding it if new. Names are compared by pointer first, so
// a string literal is cheap to look up.
int Profiler_GetCategory(const char *name);
void Profiler_Enter(int category);
void Profiler_Leave(int category);

class ProfileThis {
public:
	explicit ProfileThis(int category) : category_(category) {
		Profiler_Enter(category);
	}
	~ProfileThis() {
		Profiler_Leave(category_);
	}
private:
	int category_;
};

#define PROFILE_INIT() internal_profiler_init();
// Named by line, so a scope can hold more than one.
#define PROFILE_CONCAT_INNER(a, b) a##b
#define PROFILE_CONCAT(a, b) PROFILE_CONCAT_INNER(a, b)
// cat must be the same string every time this line runs (normally a literal): it's looked up once.
#define PROFILE_THIS_SCOPE(cat) \
	static const int PROFILE_CONCAT(_profile_cat, __LINE__) = Profiler_GetCategory(cat); \
	ProfileThis PROFILE_CONCAT(_profile_scoped, __LINE__)(PROFILE_CONCAT(_profile_cat, __LINE__));
// For a name that varies between calls.
#define PROFILE_THIS_SCOPE_DYNAMIC(cat) ProfileThis PROFILE_CONCAT(_profile_scoped, __LINE__)(Profiler_GetCategory(cat));
#define PROFILE_END_FRAME() internal_profiler_end_frame();

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

// While paused, nothing is recorded and the history stands still.
void Profiler_SetPaused(bool paused);
bool Profiler_IsPaused();

// For the timeline.
enum class ProfilerEventType : uint8_t {
	Begin,
	End,
};

struct ProfilerEvent {
	uint64_t ticks;
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
#define PROFILE_THIS_SCOPE_DYNAMIC(cat)
#define PROFILE_END_FRAME()

#endif
