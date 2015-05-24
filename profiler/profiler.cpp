// Ultra-lightweight category profiler with history.

#include <vector>
#include <string>
#include <map>

#include <string.h>

#include "base/logging.h"
#include "base/timeutil.h"
#include "gfx_es2/draw_buffer.h"
#include "profiler/profiler.h"

#define MAX_CATEGORIES 16 // Can be any number
#define MAX_DEPTH 16      // Can be any number
#define HISTORY_SIZE 256  // Must be power of 2

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
	}
	float time_taken[MAX_CATEGORIES];
	int count[MAX_CATEGORIES];
};

struct Profiler {
	int frameCount;
	int historyPos;
	int depth;
	int parentCategory[MAX_DEPTH];
	double eventStart[MAX_CATEGORIES];
	double curFrameStart;
};

static Profiler profiler;
static Category categories[MAX_CATEGORIES];
static CategoryFrame *history;

void internal_profiler_init() {
	memset(&profiler, 0, sizeof(profiler));
	history = new CategoryFrame[HISTORY_SIZE];
}

int internal_profiler_find_cat(const char *category_name) {
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

	if (i < MAX_CATEGORIES && category_name) {
		categories[i].name = category_name;
		return i;
	}

	return -1;
}

// Suspend, also used to prepare for leaving.
static void internal_profiler_suspend(int category) {
	double diff = real_time_now() - profiler.eventStart[category];
	history[profiler.historyPos].time_taken[category] += (float)diff;
	profiler.eventStart[category] = 0.0;
}

// Resume, also used as part of entering.
static void internal_profiler_resume(int category) {
	profiler.eventStart[category] = real_time_now();
}

int internal_profiler_enter(const char *category_name) {
	int category = internal_profiler_find_cat(category_name);
	if (category != -1) {
		if (profiler.eventStart[category] == 0.0f) {
			int parent = profiler.parentCategory[profiler.depth];
			// Temporarily suspend the parent on entering a child.
			if (parent != 0) {
				internal_profiler_suspend(parent);
			}
			internal_profiler_resume(category);

			profiler.depth++;
			profiler.parentCategory[profiler.depth] = category;
		}
	}

	return category;
}

void internal_profiler_leave(int category) {
	if (category == -1 || !history) {
		return;
	}
	if (category < 0 || category >= MAX_CATEGORIES) {
		ELOG("Bad category index %d", category);
	}
	internal_profiler_suspend(category);
	history[profiler.historyPos].count[category]++;

	profiler.depth--;
	int parent = profiler.parentCategory[profiler.depth];
	if (parent != 0) {
		// Resume tracking the parent.
		internal_profiler_resume(parent);
	}
}

void internal_profiler_end_frame() {
	profiler.curFrameStart = real_time_now();
	profiler.historyPos++;
	profiler.historyPos &= ~HISTORY_SIZE;
	memset(&history[profiler.historyPos], 0, sizeof(history[profiler.historyPos]));
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

void Profiler_GetHistory(int category, float *data, int count) {
	for (int i = 0; i < HISTORY_SIZE; i++) {
		int x = i - count + profiler.historyPos + 1;
		if (x < 0)
			x += HISTORY_SIZE;
		if (x >= HISTORY_SIZE)
			x -= HISTORY_SIZE;
		data[i] = history[x].time_taken[category];
	}
}
