// UNFINISHED but kinda working

#include <vector>
#include <string>
#include <map>

#include <string.h>

#include "base/logging.h"
#include "gfx_es2/draw_buffer.h"
#include "base/timeutil.h"

using namespace std;


struct Section {
  const char *name;
  int level;
  double start;
  double end;
};
static vector<Section> current_frame;

static double frame_start;
#define NUM_LEVELS 16
static Section cur_section[NUM_LEVELS];

static int level;

void _profiler_init() {
  level = 0;
}

void _profiler_enter(const char *section_name) {
  cur_section[level].name = section_name;
  cur_section[level].start = real_time_now();
  level++;
}

void _profiler_leave(const char *section_name) {
  --level;
  cur_section[level].end = real_time_now();
  if (strcmp(section_name, cur_section[level].name)) {
    FLOG("Can't enter %s when %s is active, only one at a time!",
      section_name, cur_section[level].name); 
  }
  cur_section[level].level = level;
  current_frame.push_back(cur_section[level]);
}

void _profiler_begin_frame() {
  frame_start = real_time_now();
}

void _profiler_end_frame() {
  current_frame.clear();
}

void _profiler_log() {
  const char *spaces = "                ";
  ILOG("Profiler output ====================");
  for (int i = 0; i < (int)current_frame.size(); i++) {
    double start = current_frame[i].start - frame_start;
    double elapsed = current_frame[i].end - current_frame[i].start;
    double start_ms = (start*1000);
    double elapsed_ms = (elapsed*1000);
    ILOG("%s%s: %0.3f ms", spaces + 15-current_frame[i].level, current_frame[i].name, elapsed_ms);
  }
}

void _profiler_draw(DrawBuffer *draw2d, int font) {
  uint32_t colors[4] = { 0xFFc0a030, 0xFF30a0c0, 0xFF30C050, 0xFFc02080 };
  for (int i = 0; i < (int)current_frame.size(); i++) {
    const Section &section = current_frame[i];
    double start = section.start - frame_start;
    double elapsed = section.end - current_frame[i].start;

    uint32_t color = colors[i&3];
    float y1 = i * 32, y2 = (i+1)*32;
    float x1 = start / 0.0166666;
    float x2 = (start + elapsed) / 0.01666666;
    draw2d->Rect(x1, y1, x2, y2, color);
  }
}
