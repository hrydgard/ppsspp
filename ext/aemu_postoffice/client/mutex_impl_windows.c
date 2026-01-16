#include <windows.h>
#include "log_windows.h"

static HANDLE sock_alloc_mutex = NULL;

void init_sock_alloc_mutex(){
	sock_alloc_mutex = CreateMutex(NULL, FALSE, NULL);
	if (sock_alloc_mutex == NULL){
		LOG("%s: failed creating mutex, 0x%x\n", __func__, GetLastError());
	}
}

void lock_sock_alloc_mutex(){
	if (sock_alloc_mutex != NULL){
		WaitForSingleObject(sock_alloc_mutex, INFINITE);
	}
}

void unlock_sock_alloc_mutex(){
	if (sock_alloc_mutex != NULL){
		ReleaseMutex(sock_alloc_mutex);
	}
}
