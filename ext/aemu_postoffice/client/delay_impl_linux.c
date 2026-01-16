#include <time.h>

void delay(int ms){
	const struct timespec duration = {
		.tv_sec = 0,
		.tv_nsec = ms * 1000000,
	};
	nanosleep(&duration, NULL);
}
