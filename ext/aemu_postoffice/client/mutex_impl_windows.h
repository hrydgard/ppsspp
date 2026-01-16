#ifndef __MUTEX_IMPL_WINDOWS_H
#define __MUTEX_IMPL_WINDOWS_H

void init_sock_alloc_mutex();
void lock_sock_alloc_mutex();
void unlock_sock_alloc_mutex();

void delay(int ms);

#endif
