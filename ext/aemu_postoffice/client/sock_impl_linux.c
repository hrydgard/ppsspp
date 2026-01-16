#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <string.h>
#include <stdio.h>

#include "postoffice_client.h"
#include "sock_impl_linux.h"
#include "log_linux.h"

void to_native_sock_addr(native_sock_addr *dst, const struct aemu_post_office_sock_addr *src){
	dst->sin_family = AF_INET;
	dst->sin_addr.s_addr = src->addr;
	dst->sin_port = src->port;
}

void to_native_sock6_addr(native_sock6_addr *dst, const struct aemu_post_office_sock6_addr *src){
	dst->sin6_family = AF_INET6;
	dst->sin6_port = src->port;
	dst->sin6_flowinfo = 0;
	memcpy(dst->sin6_addr.s6_addr, src->addr, 16);
	dst->sin6_scope_id = 0;
}

int native_connect_tcp_sock(void *addr, int addrlen){
	native_sock_addr *native_addr = addr;
	int sock = socket(native_addr->sin_family, SOCK_STREAM, 0);
	if (sock == -1){
		LOG("%s: failed creating socket, %s\n", __func__, strerror(errno));
		return AEMU_POSTOFFICE_CLIENT_SESSION_NETWORK;
	}

	// Connect
	int connect_status = connect(sock, addr, addrlen);
	if (connect_status == -1){
		LOG("%s: failed connecting, %s\n", __func__, strerror(errno));
		close(sock);
		return AEMU_POSTOFFICE_CLIENT_SESSION_NETWORK;
	}

	// Set socket options
	int sockopt = 1;
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &sockopt, sizeof(sockopt));
	int flags = fcntl(sock, F_GETFL, 0);
	flags |= O_NONBLOCK;
	fcntl(sock, F_SETFL, flags);

	return sock;
}

int native_send_till_done(int fd, const char *buf, int len, bool non_block, bool *abort){
	int write_offset = 0;
	while(write_offset != len){
		if (*abort){
			return NATIVE_SOCK_ABORTED;
		}
		int write_status = send(fd, &buf[write_offset], len - write_offset, 0);
		if (write_status == -1){
			int err = errno;
			if (err == EAGAIN || err == EWOULDBLOCK){
				if (non_block && write_offset == 0){
					return AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK;
				}
				// Continue block sending, either in block mode or we already received part of the message
				sleep(0);
				continue;
			}
			// Other errors
			LOG("%s: failed sending, %s\n", __func__, strerror(errno));
			return write_status;
		}
		write_offset += write_status;
	}
	return write_offset;
}

int native_recv_till_done(int fd, char *buf, int len, bool non_block, bool *abort){
	int read_offset = 0;
	while(read_offset != len){
		if (*abort){
			return NATIVE_SOCK_ABORTED;
		}
		int recv_status = recv(fd, &buf[read_offset], len - read_offset, 0);
		if (recv_status == 0){
			return recv_status;
		}
		if (recv_status < 0){
			int err = errno;
			if (err == EAGAIN || err == EWOULDBLOCK){
				if (non_block && read_offset == 0){
					return AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK;
				}
				// Continue block receving, either in block mode or we already sent part of the message
				sleep(0);
				continue;
			}
			// Other errors
			LOG("%s: failed receving, %s\n", __func__, strerror(errno));
			return recv_status;
		}
		read_offset += recv_status;
	}
	return read_offset;
}

int native_close_tcp_sock(int sock){
	return close(sock);
}

int native_peek(int fd, char *buf, int len){
	int read_result = recv(fd, buf, len, MSG_PEEK);
	if (read_result == 0){
		return 0;
	}
	if (read_result == -1){
		int err = errno;
		if (err == EAGAIN || err == EWOULDBLOCK){
			return AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK;
		}
		LOG("%s: failed peeking, %s\n", __func__, strerror(errno));
		return -1;
	}
	return read_result;
}
