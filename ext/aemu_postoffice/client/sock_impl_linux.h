#ifndef __SOCK_IMPL_LINUX_H
#define __SOCK_IMPL_LINUX_H

#include <netinet/in.h>

#include "postoffice_client.h"

#define NATIVE_SOCK_ABORTED -100

typedef struct sockaddr_in native_sock_addr;
typedef struct sockaddr_in6 native_sock6_addr;

void to_native_sock_addr(native_sock_addr *dst, const struct aemu_post_office_sock_addr *src);
void to_native_sock6_addr(native_sock6_addr *dst, const struct aemu_post_office_sock6_addr *src);

int native_connect_tcp_sock(void *addr, int addrlen);
int native_close_tcp_sock(int sock);
int native_send_till_done(int fd, const char *buf, int len, bool non_block, bool *abort);
int native_recv_till_done(int fd, char *buf, int len, bool non_block, bool *abort);
int native_peek(int fd, char *buf, int len);

#endif
