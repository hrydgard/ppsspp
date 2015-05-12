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
#define HISTORY_SIZE 256  // Must be power of 2

#ifndef _DEBUG
// If the compiler can collapse identical strings, we don't even need the strcmp.
#define UNIFIED_CONST_STR
#endif

struct CategoryFrame {
	float time_taken[MAX_CATEGORIES];
};

struct Profiler {
	int frameCount;
	int historyPos;
	double eventStart;
	double curFrameStart;
};

static Profiler profiler;
static Category categories[MAX_CATEGORIES];
static CategoryFrame *history;

void internal_profiler_init() {
	history = new CategoryFrame[HISTORY_SIZE];
	for (int i = 0; i < MAX_CATEGORIES; i++) {
		categories[i].color = 0x358236 * i;
	}
}

int internal_profiler_enter(const char *section_name) {
	for (int i = 0; i < MAX_CATEGORIES; i++) {
		const char *catname = categories[i].name;
		if (!catname)
			break;
#ifdef UNIFIED_CONST_STR
		if (catname == section_name) {
#else
		if (!strcmp(catname, section_name)) {
#endif
			profiler.eventStart = time_now_d();
			return i;
		}
	}
	return -1;
}

void internal_profiler_leave(int category) {
	if (category < 0)
		return;
	double diff = time_now_d() - profiler.eventStart;
	history[profiler.historyPos].time_taken[category] += (float)diff;
}

void internal_profiler_end_frame() {
	profiler.curFrameStart = real_time_now();
	profiler.historyPos++;
	profiler.historyPos &= ~HISTORY_SIZE;
}

const char *GetSectionName(int i) {
	return i >= 0 ? categories[i].name : "N/A";
}

int GetNumSections() {
	for (int i = 0; i < MAX_CATEGORIES; i++) {
		if (!categories[i].name)
			return i;
	}
	return 0;
}
