#pragma once

// http://linux.die.net/man/3/clock_gettime

// This time implementation caches the time for max performance (call time_now() as much as you like).
// You need to call time_update() once per frame (or whenever you need the correct time right now).

void time_update();

// Seconds.
float time_now();
double time_now_d();

// Slower than the above cached time functions
double real_time_now();

int time_now_ms();

void sleep_ms(int ms);
