#pragma once

#include <cstdint>

// #define USE_PROFILER

#ifdef USE_PROFILER

class DrawBuffer;

void internal_profiler_init();
void internal_profiler_end_frame();

int internal_profiler_enter(const char *category_name, int *thread_id);  // Returns the category number.
void internal_profiler_leave(int thread_id, int category);

const char *Profiler_GetCategoryName(int i);
int Profiler_GetNumCategories();
int Profiler_GetHistoryLength();
int Profiler_GetNumThreads();
void Profiler_GetSlowestThreads(int *data, int count);
void Profiler_GetSlowestHistory(int category, int *slowestThreads, float *data, int count);
void Profiler_GetHistory(int category, int thread, float *data, int count);

class ProfileThis {
public:
	ProfileThis(const char *category) {
		cat_ = internal_profiler_enter(category, &thread_);
	}
	~ProfileThis() {
		internal_profiler_leave(thread_, cat_);
	}
private:
	int cat_;
	int thread_;
};

#define PROFILE_INIT() internal_profiler_init();
#define PROFILE_THIS_SCOPE(cat) ProfileThis _profile_scoped(cat);
#define PROFILE_END_FRAME() internal_profiler_end_frame();

#else

#define PROFILE_INIT()
#define PROFILE_THIS_SCOPE(cat)
#define PROFILE_END_FRAME()

#endif
