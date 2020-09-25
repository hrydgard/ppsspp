#pragma once

// Seconds.
double time_now_d();

// Sleep. Does not necessarily have millisecond granularity, especially on Windows.
void sleep_ms(int ms);
