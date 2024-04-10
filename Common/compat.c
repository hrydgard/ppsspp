#include <stdlib.h>

void *av_malloc(size_t size) {
	return malloc(size);
}

void *av_mallocz(size_t size) {
	return calloc(size, 1);
}

void av_free(void *p) {
	free(p);
}

void av_freep(void **p) {
	void *pp = *p;
	free(pp);
	*p = 0;
}
