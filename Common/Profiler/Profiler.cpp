// Ultra-lightweight category profiler with history.

#include <algorithm>
#include <mutex>
#include <vector>
#include <cstring>

#include "ppsspp_config.h"

#include "Common/Render/DrawBuffer.h"

#include "Common/TimeUtil.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Log.h"

#define MAX_CATEGORIES 64 // Can be any number, represents max profiled names.
#define MAX_DEPTH 16      // Can be any number, represents max nesting depth of profiled names.
#if PPSSPP_PLATFORM(IOS) && defined(__IPHONE_OS_VERSION_MIN_REQUIRED) && __IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_9_0
// iOS did not support C++ thread_local before iOS 9
#define MAX_THREADS 1     // Can be any number, represents concurrent threads calling the profiler.
#else
#define MAX_THREADS 4     // Can be any number, represents concurrent threads calling the profiler.
#endif
#define HISTORY_SIZE 128 // Must be power of 2

#ifndef _DEBUG
// If the compiler can collapse identical strings, we don't even need the strcmp.
#define UNIFIED_CONST_STR
#endif

struct Category {
	const char *name;
};

struct CategoryFrame {
	CategoryFrame() {
		memset(time_taken, 0, sizeof(time_taken));
		memset(count, 0, sizeof(count));
	}
	float time_taken[MAX_CATEGORIES];
	int count[MAX_CATEGORIES];
};

struct Profiler {
	int historyPos;
	int depth[MAX_THREADS];
	int parentCategory[MAX_THREADS][MAX_DEPTH];
	double eventStart[MAX_THREADS][MAX_CATEGORIES];
	double curFrameStart;
};

static Profiler profiler;
static Category categories[MAX_CATEGORIES];
static std::mutex categoriesLock;
static int threadIdAfterLast = 0;
static std::mutex threadsLock;
static CategoryFrame *history;
#if MAX_THREADS > 1
thread_local int profilerThreadId = -1;
#else
static int profilerThreadId = 0;
#endif

void internal_profiler_init() {
	memset(&profiler, 0, sizeof(profiler));
#if MAX_THREADS == 1
	threadIdAfterLast = 1;
#endif
	for (int i = 0; i < MAX_THREADS; i++) {
		for (int j = 0; j < MAX_DEPTH; j++) {
			profiler.parentCategory[i][j] = -1;
		}
	}
	history = new CategoryFrame[HISTORY_SIZE * MAX_THREADS];
}

static int internal_profiler_find_thread() {
	int thread_id = profilerThreadId;
	if (thread_id != -1) {
		return thread_id;
	}

	std::lock_guard<std::mutex> guard(threadsLock);
	if (threadIdAfterLast < MAX_THREADS) {
		thread_id = threadIdAfterLast++;
		profilerThreadId = thread_id;
		return thread_id;
	}

	// Just keep reusing the last one.
	return threadIdAfterLast - 1;
}

int internal_profiler_find_cat(const char *category_name, bool create_missing) {
	int i;
	for (i = 0; i < MAX_CATEGORIES; i++) {
		const char *catname = categories[i].name;
		if (!catname)
			break;
#ifdef UNIFIED_CONST_STR
		if (catname == category_name) {
#else
		if (!strcmp(catname, category_name)) {
#endif
			return i;
		}
	}

	if (i < MAX_CATEGORIES && category_name && create_missing) {
		std::lock_guard<std::mutex> guard(categoriesLock);
		int race_check = internal_profiler_find_cat(category_name, false);
		if (race_check == -1)
			categories[i].name = category_name;
		return i;
	}

	return -1;
}

// Suspend, also used to prepare for leaving.
static void internal_profiler_suspend(int thread_id, int category, double now) {
	double diff = now - profiler.eventStart[thread_id][category];
	history[MAX_THREADS * profiler.historyPos + thread_id].time_taken[category] += (float)diff;
	profiler.eventStart[thread_id][category] = 0.0;
}

// Resume, also used as part of entering.
static void internal_profiler_resume(int thread_id, int category, double now) {
	profiler.eventStart[thread_id][category] = now;
}

int internal_profiler_enter(const char *category_name, int *out_thread_id) {
	int category = internal_profiler_find_cat(category_name, true);
	int thread_id = internal_profiler_find_thread();
	if (category == -1 || !history) {
		return category;
	}

	int &depth = profiler.depth[thread_id];
	if (profiler.eventStart[thread_id][category] == 0.0f) {
		double now = time_now_d();
		int parent = profiler.parentCategory[thread_id][depth];
		// Temporarily suspend the parent on entering a child.
		if (parent != -1) {
			internal_profiler_suspend(thread_id, parent, now);
		}
		internal_profiler_resume(thread_id, category, now);
	} else {
		DEBUG_LOG(Log::System, "profiler: recursive enter (%i - %s)", category, category_name);
	}

	depth++;
	profiler.parentCategory[thread_id][depth] = category;

	*out_thread_id = thread_id;
	return category;
}

void internal_profiler_leave(int thread_id, int category) {
	if (category == -1 || !history) {
		return;
	}

	int &depth = profiler.depth[thread_id];
	if (category < 0 || category >= MAX_CATEGORIES) {
		ERROR_LOG(Log::System, "Bad category index %d", category);
		depth--;
		return;
	}

	double now = time_now_d();

	depth--;
	_assert_msg_(depth >= 0, "Profiler enter/leave mismatch!");

	int parent = profiler.parentCategory[thread_id][depth];
	// When there's recursion, we don't suspend or resume.
	if (parent != category) {
		internal_profiler_suspend(thread_id, category, now);
		history[MAX_THREADS * profiler.historyPos + thread_id].count[category]++;

		if (parent != -1) {
			// Resume tracking the parent.
			internal_profiler_resume(thread_id, parent, now);
		}
	}
}

void internal_profiler_end_frame() {
	int thread_id = internal_profiler_find_thread();
	_assert_msg_(profiler.depth[thread_id] == 0, "Can't be inside a profiler scope at end of frame!");
	profiler.curFrameStart = time_now_d();
	profiler.historyPos++;
	profiler.historyPos &= (HISTORY_SIZE - 1);
	memset(&history[MAX_THREADS * profiler.historyPos], 0, sizeof(CategoryFrame) * MAX_THREADS);
}

const char *Profiler_GetCategoryName(int i) {
	return i >= 0 ? categories[i].name : "N/A";
}

int Profiler_GetHistoryLength() {
	return HISTORY_SIZE;
}

int Profiler_GetNumCategories() {
	for (int i = 0; i < MAX_CATEGORIES; i++) {
		if (!categories[i].name)
			return i;
	}
	return 0;
}

int Profiler_GetNumThreads() {
	return threadIdAfterLast;
}

void Profiler_GetSlowestThreads(int *data, int count) {
	int numCategories = Profiler_GetNumCategories();
	for (int i = 0; i < HISTORY_SIZE; i++) {
		int x = i - count + profiler.historyPos + 1;
		while (x < 0)
			x += HISTORY_SIZE;
		while (x >= HISTORY_SIZE)
			x -= HISTORY_SIZE;

		float slowestTime = 0.0f;
		data[i] = 0;
		for (int thread = 0; thread < threadIdAfterLast; ++thread) {
			float sum = 0.0f;
			for (int c = 0; c < numCategories; ++c) {
				sum += history[MAX_THREADS * x + thread].time_taken[c];
			}
			if (sum > slowestTime) {
				slowestTime = sum;
				data[i] = thread;
			}
		}
	}
}

void Profiler_GetSlowestHistory(int category, int *slowestThreads, float *data, int count) {
	for (int i = 0; i < HISTORY_SIZE; i++) {
		int x = i - count + profiler.historyPos + 1;
		while (x < 0)
			x += HISTORY_SIZE;
		while (x >= HISTORY_SIZE)
			x -= HISTORY_SIZE;

		int thread = slowestThreads[i];
		data[i] = history[MAX_THREADS * x + thread].time_taken[category];
	}
}

void Profiler_GetHistory(int category, int thread, float *data, int count) {
	for (int i = 0; i < HISTORY_SIZE; i++) {
		int x = i - count + profiler.historyPos + 1;
		while (x < 0)
			x += HISTORY_SIZE;
		while (x >= HISTORY_SIZE)
			x -= HISTORY_SIZE;
		data[i] = history[MAX_THREADS * x + thread].time_taken[category];
	}
}
