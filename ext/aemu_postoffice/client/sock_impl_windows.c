#include <winsock2.h>
#include <windows.h>

#include <string.h>
#include <stdio.h>

#include "postoffice_client.h"
#include "sock_impl_windows.h"
#include "log_windows.h"

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

static void init_winsock2(){
	static bool initialized = false;
	if (!initialized){
		initialized = true;
		WSADATA data;
		int init_result = WSAStartup(MAKEWORD(2,2), &data);
		if (init_result != 0){
			printf("%s: warning: WSAStartup seems to have failed, %d\n", __func__, init_result);
		}
	}
}

int native_connect_tcp_sock(void *addr, int addrlen){
	init_winsock2();

	native_sock_addr *native_addr = addr;
	int sock = socket(native_addr->sin_family, SOCK_STREAM, 0);
	if (sock == -1){
		LOG("%s: failed creating socket, %d\n", __func__, WSAGetLastError());
		return AEMU_POSTOFFICE_CLIENT_SESSION_NETWORK;
	}

	// Connect
	int connect_status = connect(sock, addr, addrlen);
	if (connect_status == -1){
		LOG("%s: failed connecting, %d\n", __func__, WSAGetLastError());
		closesocket(sock);
		return AEMU_POSTOFFICE_CLIENT_SESSION_NETWORK;
	}

	// Set socket options
	int sockopt = 1;
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &sockopt, sizeof(sockopt));
	u_long ioctlopt = 1;
	ioctlsocket(sock, FIONBIO, &ioctlopt);

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
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS){
				if (non_block && write_offset == 0){
					return AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK;
				}
				// Continue block sending, either in block mode or we already received part of the message
				Sleep(0);
				continue;
			}
			// Other errors
			LOG("%s: failed sending, %d\n", __func__, err);
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
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS){
				if (non_block && read_offset == 0){
					return AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK;
				}
				// Continue block receving, either in block mode or we already sent part of the message
				Sleep(0);
				continue;
			}
			// Other errors
			LOG("%s: failed receving, %d\n", __func__, err);
			return recv_status;
		}
		read_offset += recv_status;
	}
	return read_offset;
}

int native_close_tcp_sock(int sock){
	return closesocket(sock);
}

int native_peek(int fd, char *buf, int len){
	int read_result = recv(fd, buf, len, MSG_PEEK);
	if (read_result == 0){
		return 0;
	}
	if (read_result == -1){
		int err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS){
			return AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK;
		}
		LOG("%s: failed peeking, %d\n", __func__, WSAGetLastError());
		return -1;
	}
	return read_result;
}
