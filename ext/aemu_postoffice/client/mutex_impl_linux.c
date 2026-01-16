#include <pthread.h>

static pthread_mutex_t sock_alloc_mutex;
void init_sock_alloc_mutex(){
	pthread_mutex_init(&sock_alloc_mutex, NULL);
}
void lock_sock_alloc_mutex(){
	pthread_mutex_lock(&sock_alloc_mutex);
}
void unlock_sock_alloc_mutex(){
	pthread_mutex_unlock(&sock_alloc_mutex);
}
