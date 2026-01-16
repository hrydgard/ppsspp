#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "postoffice_client.h"

#if defined(__unix) || defined(__APPLE__)
#include "log_linux.h"
#include "sock_impl_linux.h"
#include "mutex_impl_linux.h"
#endif

#ifdef __PSP__
#include "log_psp.h"
#include "sock_impl_psp.h"
#include "mutex_impl_psp.h"
#endif

#ifdef _WIN32
#include "log_windows.h"
#include "sock_impl_windows.h"
#include "mutex_impl_windows.h"
#endif

#include "../aemu_postoffice_packets.h"

struct pdp_session{
	char *pdp_mac[6];
	int16_t pdp_port;
	int sock;
	bool dead;
	bool abort;
	char recv_buf[2048];
	bool recving;
	bool sending;
};

struct ptp_listen_session{
	char *ptp_mac[6];
	int16_t ptp_port;
	int sock;
	bool dead;
	bool abort;
	char addr[sizeof(native_sock6_addr) > sizeof(native_sock_addr) ? sizeof(native_sock6_addr) : sizeof(native_sock_addr)];
	int addrlen;
	bool accepting;
};

struct ptp_session{
	int sock;
	bool dead;
	bool abort;
	char recv_buf[50 * 1024];
	int outstanding_data_size;
	int outstanding_data_offset;
	bool recving;
	bool sending;
};

// wonder if games will actually use more than this
// we pre-allocate so that we don't have to use heap, it's kinda big, TODO test if it's too big on a psp, in theory we have plenty on slims
struct pdp_session pdp_sessions[32];
struct ptp_listen_session ptp_listen_sessions[sizeof(pdp_sessions) / sizeof(pdp_sessions[0])];
struct ptp_session ptp_sessions[sizeof(pdp_sessions) / sizeof(pdp_sessions[0])];

int aemu_post_office_init(){
	static bool first_run = true;
	if (first_run){
		first_run = false;
		for (int i = 0;i < sizeof(pdp_sessions) / sizeof(pdp_sessions[0]);i++){
			pdp_sessions[i].sock = -1;
			ptp_listen_sessions[i].sock = -1;
			ptp_sessions[i].sock = -1;
		}
		init_sock_alloc_mutex();
	}else{
		// re-run, close all opened sessions
		for (int i = 0;i < sizeof(pdp_sessions) / sizeof(pdp_sessions[0]);i++){
			if (pdp_sessions[i].sock != -1){
				pdp_delete(&pdp_sessions[i]);
			}
			if (ptp_listen_sessions[i].sock != -1){
				ptp_listen_close(&ptp_listen_sessions[i]);
			}
			if (ptp_sessions[i].sock != -1){
				ptp_close(&ptp_sessions[i]);
			}
		}
	}
	return 0;
}

static int create_and_init_socket(void *addr, int addrlen, const char *init_packet, int init_packet_len, const char *caller_name){
	int sock = native_connect_tcp_sock(addr, addrlen);
	if (sock < 0){
		LOG("%s: tcp connection failed\n", caller_name);
		return sock;
	}

	bool abort = false;
	int write_status = native_send_till_done(sock, (char *)init_packet, init_packet_len, false, &abort);
	if (write_status == -1){
		LOG("%s: failed sending init packet\n", caller_name);
		native_close_tcp_sock(sock);
		return AEMU_POSTOFFICE_CLIENT_SESSION_NETWORK;
	}

	return sock;
}

static void *pdp_create(void *addr, int addrlen, const char *pdp_mac, int pdp_port, int *state){
	struct pdp_session* session = NULL;
	lock_sock_alloc_mutex();
	for(int i = 0;i < sizeof(pdp_sessions) / sizeof(pdp_sessions[0]);i++){
		if (pdp_sessions[i].sock == -1){
			session = &pdp_sessions[i];
			session->sock = 0;
			break;
		}
	}
	unlock_sock_alloc_mutex();
	if (session == NULL){
		LOG("%s: failed allocating memory for pdp session\n", __func__);
		*state = AEMU_POSTOFFICE_CLIENT_OUT_OF_MEMORY;
		return NULL;
	}

	// Prepare init packet
	struct aemu_postoffice_init init_packet = {0};
	init_packet.init_type = AEMU_POSTOFFICE_INIT_PDP;
	memcpy(init_packet.src_addr, pdp_mac, 6);
	init_packet.sport = pdp_port;

	int sock = create_and_init_socket(addr, addrlen, (char *)&init_packet, sizeof(init_packet), __func__);

	if (sock < 0){
		*state = sock;
		session->sock = -1;
		return NULL;
	}

	memcpy(session->pdp_mac, pdp_mac, 6);
	session->pdp_port = pdp_port;
	session->sock = sock;
	session->dead = false;
	session->abort = false;
	session->recving = false;
	session->sending = false;

	*state = AEMU_POSTOFFICE_CLIENT_OK;
	return session;
}

void *pdp_create_v6(const struct aemu_post_office_sock6_addr *addr, const char *pdp_mac, int pdp_port, int *state){
	native_sock6_addr native_addr;
	to_native_sock6_addr(&native_addr, addr);

	return pdp_create(&native_addr, sizeof(native_addr), pdp_mac, pdp_port, state);
}

void *pdp_create_v4(const struct aemu_post_office_sock_addr *addr, const char *pdp_mac, int pdp_port, int *state){
	native_sock_addr native_addr;
	to_native_sock_addr(&native_addr, addr);

	return pdp_create(&native_addr, sizeof(native_addr), pdp_mac, pdp_port, state);
}

int pdp_send(void *pdp_handle, const char *pdp_mac, int pdp_port, const char *buf, int len, bool non_block){
	if (pdp_handle == NULL){
		return -1;
	}
	struct pdp_session *session = (struct pdp_session *)pdp_handle;
	if (session->dead || session->abort){
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (len > sizeof(session->recv_buf)){
		LOG("%s: failed sending data, data too big, %d\n", __func__, len);
		return AEMU_POSTOFFICE_CLIENT_OUT_OF_MEMORY;
	}

	// Write header
	struct aemu_postoffice_pdp pdp_header = {
		.port = pdp_port,
		.size = len
	};
	memcpy(pdp_header.addr, pdp_mac, 6);

	session->sending = true;
	int send_status = native_send_till_done(session->sock, (char *)&pdp_header, sizeof(pdp_header), non_block, &session->abort);
	session->sending = false;
	if (send_status == AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK){
		return AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK;
	}
	if (send_status == NATIVE_SOCK_ABORTED){
		// getting aborted
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (send_status < 0){
		// Error
		LOG("%s: failed sending header\n", __func__);
		session->dead = true;
		native_close_tcp_sock(session->sock);
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	session->sending = true;
	send_status = native_send_till_done(session->sock, buf, len, false, &session->abort);
	session->sending = false;
	if (send_status == NATIVE_SOCK_ABORTED){
		// getting aborted
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (send_status < 0){
		// Error
		LOG("%s: failed sending data\n", __func__);
		session->dead = true;
		native_close_tcp_sock(session->sock);
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	return AEMU_POSTOFFICE_CLIENT_OK;
}

int pdp_recv(void *pdp_handle, char *pdp_mac, int *pdp_port, char *buf, int *len, bool non_block){
	if (pdp_handle == NULL){
		return -1;
	}
	struct pdp_session *session = (struct pdp_session *)pdp_handle;
	if (session->dead || session->abort){
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}	

	if (*len > 2048){
		return AEMU_POSTOFFICE_CLIENT_OUT_OF_MEMORY;
	}

	struct aemu_postoffice_pdp pdp_header;
	session->recving = true;
	int recv_status = native_recv_till_done(session->sock, (char *)&pdp_header, sizeof(pdp_header), non_block, &session->abort);
	session->recving = false;
	if (recv_status == AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK){
		return AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK;
	}
	if (recv_status == NATIVE_SOCK_ABORTED){
		// getting aborted
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (recv_status == 0){
		LOG("%s: remote closed the socket\n", __func__);
		native_close_tcp_sock(session->sock);
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (recv_status == -1){
		LOG("%s: failed receiving data\n", __func__);
		native_close_tcp_sock(session->sock);
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	// We have a header
	if (pdp_header.size > sizeof(session->recv_buf)){
		// The other side is sending packets that are too big
		LOG("%s: failed receiving data, data too big %d\n", __func__, pdp_header.size);
		native_close_tcp_sock(session->sock);
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (pdp_mac != NULL)
		memcpy(pdp_mac, pdp_header.addr, 6);
	if (pdp_port != NULL)
		*pdp_port = pdp_header.port;

	session->recving = true;
	recv_status = native_recv_till_done(session->sock, session->recv_buf, pdp_header.size, false, &session->abort);
	session->recving = false;
	if (recv_status == NATIVE_SOCK_ABORTED){
		// getting aborted
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (recv_status == 0){
		LOG("%s: remote closed the socket\n", __func__);
		native_close_tcp_sock(session->sock);
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (recv_status == -1){
		LOG("%s: failed receiving data\n", __func__);
		native_close_tcp_sock(session->sock);
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	// We have data
	memcpy(buf, session->recv_buf, *len);
	if (pdp_header.size > *len){
		return AEMU_POSTOFFICE_CLIENT_SESSION_DATA_TRUNC;
	}
	*len = pdp_header.size;
	return AEMU_POSTOFFICE_CLIENT_OK;
}

void pdp_delete(void *pdp_handle){
	if (pdp_handle == NULL){
		return;
	}
	struct pdp_session *session = (struct pdp_session *)pdp_handle;

	// abort on-going ops
	session->abort = true;

	// make sure we are clear of send/recv operations
	do{
		delay(50);
	}while(session->sending || session->recving);

	if (!session->dead)
		native_close_tcp_sock(session->sock);
	session->sock = -1;
}

int pdp_peek_next_size(void *pdp_handle){
	struct pdp_session *session = pdp_handle;

	if (session->dead || session->abort){
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	aemu_postoffice_pdp header = {0};
	session->recving = true;
	int peek_result = native_peek(session->sock, (char *)&header, sizeof(header));
	session->recving = false;
	if (peek_result == AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK){
		return 0;
	}
	if (session->abort){
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (peek_result <= 0){
		session->dead = true;
		native_close_tcp_sock(session->sock);
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (peek_result != sizeof(header)){
		return 0;
	}

	return header.size;
}

static void *ptp_listen(void *addr, int addrlen, const char *ptp_mac, int ptp_port, int *state){
	struct ptp_listen_session* session = NULL;
	lock_sock_alloc_mutex();
	for(int i = 0;i < sizeof(ptp_listen_sessions) / sizeof(ptp_listen_sessions[0]);i++){
		if (ptp_listen_sessions[i].sock == -1){
			session = &ptp_listen_sessions[i];
			session->sock = 0;
			break;
		}
	}
	unlock_sock_alloc_mutex();
	if (session == NULL){
		LOG("%s: failed allocating memory for ptp listen session\n", __func__);
		*state = AEMU_POSTOFFICE_CLIENT_OUT_OF_MEMORY;
		return NULL;
	}

	// Prepare init packet
	struct aemu_postoffice_init init_packet = {0};
	init_packet.init_type = AEMU_POSTOFFICE_INIT_PTP_LISTEN;
	memcpy(init_packet.src_addr, ptp_mac, 6);
	init_packet.sport = ptp_port;

	int sock = create_and_init_socket(addr, addrlen, (char *)&init_packet, sizeof(init_packet), __func__);

	if (sock < 0){
		*state = sock;
		session->sock = -1;
		return NULL;
	}

	memcpy(session->ptp_mac, ptp_mac, 6);
	session->ptp_port = ptp_port;
	session->sock = sock;
	memcpy(session->addr, addr, addrlen);
	session->addrlen = addrlen;
	session->dead = false;
	session->abort = false;
	session->accepting = false;

	*state = AEMU_POSTOFFICE_CLIENT_OK;
	return session;
}

void *ptp_listen_v6(const struct aemu_post_office_sock6_addr *addr, const char *ptp_mac, int ptp_port, int *state){
	native_sock6_addr native_addr;
	to_native_sock6_addr(&native_addr, addr);

	return ptp_listen(&native_addr, sizeof(native_addr), ptp_mac, ptp_port, state);
}

void *ptp_listen_v4(const struct aemu_post_office_sock_addr *addr, const char *ptp_mac, int ptp_port, int *state){
	native_sock_addr native_addr;
	to_native_sock_addr(&native_addr, addr);

	return ptp_listen(&native_addr, sizeof(native_addr), ptp_mac, ptp_port, state);
}

void *ptp_accept(void *ptp_listen_handle, char *ptp_mac, int *ptp_port, bool nonblock, int *state){
	if (ptp_listen_handle == NULL){
		*state = AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
		return NULL;
	}

	struct ptp_listen_session *session = (struct ptp_listen_session *)ptp_listen_handle;
	if (session->dead){
		*state = AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
		return NULL;
	}

	struct aemu_postoffice_ptp_connect connect_packet;
	session->accepting = true;
	int recv_status = native_recv_till_done(session->sock, (char *)&connect_packet, sizeof(connect_packet), nonblock, &session->abort);
	session->accepting = false;
	if (recv_status == NATIVE_SOCK_ABORTED){
		// getting aborted
		*state = AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
		return NULL;
	}
	if (recv_status == AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK){
		*state = AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK;
		return NULL;
	}
	if (recv_status == 0){
		LOG("%s: the other side closed the listen socket\n", __func__);
		session->dead = true;
		native_close_tcp_sock(session->sock);
		*state = AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
		return NULL;
	}
	if (recv_status <= 0){
		LOG("%s: socket error, %d\n", __func__, recv_status);
		session->dead = true;
		native_close_tcp_sock(session->sock);
		*state = AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
		return NULL;
	}

	// Allocate memory
	struct ptp_session *new_session = NULL;
	lock_sock_alloc_mutex();
	for(int i = 0;i < sizeof(ptp_sessions) / sizeof(ptp_sessions[0]);i++){
		if (ptp_sessions[i].sock == -1){
			new_session = &ptp_sessions[i];
			new_session->sock = 0;
			break;
		}
	}
	unlock_sock_alloc_mutex();
	if (new_session == NULL){
		*state = AEMU_POSTOFFICE_CLIENT_OUT_OF_MEMORY;
		return NULL;
	}

	// Prepare init packet
	struct aemu_postoffice_init init_packet;
	init_packet.init_type = AEMU_POSTOFFICE_INIT_PTP_ACCEPT;
	memcpy(init_packet.src_addr, session->ptp_mac, 6);
	init_packet.sport = session->ptp_port;
	memcpy(init_packet.dst_addr, connect_packet.addr, 6);
	init_packet.dport = connect_packet.port;

	int sock = create_and_init_socket(session->addr, session->addrlen, (char *)&init_packet, sizeof(init_packet), __func__);

	if (sock < 0){
		*state = sock;
		new_session->sock = -1;
		return NULL;
	}

	// Consume the ack packet
	bool abort = false;
	int read_status = native_recv_till_done(sock, (char *)&connect_packet, sizeof(connect_packet), false, &abort);
	if (read_status == 0){
		LOG("%s: remove closed the socket during initial recv\n", __func__);
		*state = AEMU_POSTOFFICE_CLIENT_SESSION_NETWORK;
		native_close_tcp_sock(sock);
		new_session->sock = -1;
		return NULL;
	}
	if (read_status == -1){
		LOG("%s: socket error receiving initial packet\n", __func__);
		*state = AEMU_POSTOFFICE_CLIENT_SESSION_NETWORK;
		native_close_tcp_sock(sock);
		new_session->sock = -1;
		return NULL;
	}

	// Now the session is ready
	new_session->sock = sock;
	new_session->dead = false;
	new_session->abort = false;
	new_session->sending = false;
	new_session->recving = false;
	new_session->outstanding_data_size = 0;
	*state = AEMU_POSTOFFICE_CLIENT_OK;
	*ptp_port = connect_packet.port;
	memcpy(ptp_mac, connect_packet.addr, 6);
	return new_session;
}

static void *ptp_connect(void *addr, int addrlen, const char *src_ptp_mac, int ptp_sport, const char *dst_ptp_mac, int ptp_dport, int *state){
	// Allocate memory
	struct ptp_session *new_session = NULL;
	lock_sock_alloc_mutex();
	for(int i = 0;i < sizeof(ptp_sessions) / sizeof(ptp_sessions[0]);i++){
		if (ptp_sessions[i].sock == -1){
			new_session = &ptp_sessions[i];
			new_session->sock = 0;
			break;
		}
	}
	unlock_sock_alloc_mutex();
	if (new_session == NULL){
		*state = AEMU_POSTOFFICE_CLIENT_OUT_OF_MEMORY;
		return NULL;
	}

	// Prepare init packet
	struct aemu_postoffice_init init_packet;
	init_packet.init_type = AEMU_POSTOFFICE_INIT_PTP_CONNECT;
	memcpy(init_packet.src_addr, src_ptp_mac, 6);
	init_packet.sport = ptp_sport;
	memcpy(init_packet.dst_addr, dst_ptp_mac, 6);
	init_packet.dport = ptp_dport;

	int sock = create_and_init_socket(addr, addrlen, (char *)&init_packet, sizeof(init_packet), __func__);

	if (sock < 0){
		*state = sock;
		new_session->sock = -1;
		return NULL;
	}

	// Consume the ack packet
	struct aemu_postoffice_ptp_connect connect_packet;
	bool abort = false;
	int read_status = native_recv_till_done(sock, (char *)&connect_packet, sizeof(connect_packet), false, &abort);
	if (read_status == 0){
		LOG("%s: remove closed the socket during initial recv\n", __func__);
		*state = AEMU_POSTOFFICE_CLIENT_SESSION_NETWORK;
		native_close_tcp_sock(sock);
		new_session->sock = -1;
		return NULL;
	}
	if (read_status == -1){
		LOG("%s: socket error receiving initial packet\n", __func__);
		*state = AEMU_POSTOFFICE_CLIENT_SESSION_NETWORK;
		native_close_tcp_sock(sock);
		new_session->sock = -1;
		return NULL;
	}

	// Now the session is ready
	new_session->sock = sock;
	new_session->dead = false;
	new_session->abort = false;
	new_session->sending = false;
	new_session->recving = false;
	new_session->outstanding_data_size = 0;
	*state = AEMU_POSTOFFICE_CLIENT_OK;
	return new_session;
}

void *ptp_connect_v6(const struct aemu_post_office_sock6_addr *addr, const char *src_ptp_mac, int ptp_sport, const char *dst_ptp_mac, int ptp_dport, int *state){
	native_sock6_addr native_addr;
	to_native_sock6_addr(&native_addr, addr);

	return ptp_connect(&native_addr, sizeof(native_addr), src_ptp_mac, ptp_sport, dst_ptp_mac, ptp_dport, state);
}

void *ptp_connect_v4(const struct aemu_post_office_sock_addr *addr, const char *src_ptp_mac, int ptp_sport, const char *dst_ptp_mac, int ptp_dport, int *state){
	native_sock_addr native_addr;
	to_native_sock_addr(&native_addr, addr);

	return ptp_connect(&native_addr, sizeof(native_addr), src_ptp_mac, ptp_sport, dst_ptp_mac, ptp_dport, state);
}

int ptp_send(void *ptp_handle, const char *buf, int len, bool non_block){
	if (ptp_handle == NULL){
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	struct ptp_session *session = (struct ptp_session *)ptp_handle;
	if (session->dead || session->abort){
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (len > sizeof(session->recv_buf)){
		LOG("%s: failed sending data, data too big, %d\n", __func__, len);
		return AEMU_POSTOFFICE_CLIENT_OUT_OF_MEMORY;
	}

	struct aemu_postoffice_ptp_data header = {
		.size = len
	};

	session->sending = true;
	int send_status = native_send_till_done(session->sock, (char *)&header, sizeof(header), non_block, &session->abort);
	session->sending = false;
	if (send_status == NATIVE_SOCK_ABORTED){
		// getting aborted
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}
	if (send_status == AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK){
		return AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK;
	}

	if (send_status < 0){
		LOG("%s: failed sending header\n", __func__);
		native_close_tcp_sock(session->sock);
		session->dead = true;
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	session->sending = true;
	send_status = native_send_till_done(session->sock, buf, len, false, &session->abort);
	session->sending = false;
	if (send_status == NATIVE_SOCK_ABORTED){
		// getting aborted
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}
	if (send_status < 0){
		LOG("%s: failed sending data\n", __func__);
		native_close_tcp_sock(session->sock);
		session->dead = true;
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	return AEMU_POSTOFFICE_CLIENT_OK;
}

int ptp_recv(void *ptp_handle, char *buf, int *len, bool non_block){
	if (ptp_handle == NULL){
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	struct ptp_session *session = (struct ptp_session *)ptp_handle;
	if (session->dead || session->abort){
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (*len > sizeof(session->recv_buf)){
		LOG("%s: failed receiving data, data too big, %d\n", __func__, *len);
		return AEMU_POSTOFFICE_CLIENT_OUT_OF_MEMORY;
	}

	// check if we have outstanding transfer
	if (session->outstanding_data_size != 0){
		memcpy(buf, &session->recv_buf[session->outstanding_data_offset], *len);
		if (session->outstanding_data_size > *len){
			session->outstanding_data_size -= *len;
			session->outstanding_data_offset += *len;
			return AEMU_POSTOFFICE_CLIENT_SESSION_DATA_TRUNC;
		}

		*len = session->outstanding_data_size;
		session->outstanding_data_size = 0;
		return AEMU_POSTOFFICE_CLIENT_OK;
	}

	struct aemu_postoffice_ptp_data header = {0};

	session->recving = true;
	int recv_status = native_recv_till_done(session->sock, (char *)&header, sizeof(header), non_block, &session->abort);
	session->recving = false;
	if (recv_status == NATIVE_SOCK_ABORTED){
		// getting aborted
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}
	if (recv_status == AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK){
		return AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK;
	}

	if (recv_status == 0){
		LOG("%s: remote closed the socket\n", __func__);
		native_close_tcp_sock(session->sock);
		session->dead = true;
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (recv_status < 0){
		LOG("%s: failed receiving header\n", __func__);
		native_close_tcp_sock(session->sock);
		session->dead = true;
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (header.size > sizeof(session->recv_buf)){
		LOG("%s: incoming data too big\n", __func__);
		native_close_tcp_sock(session->sock);
		session->dead = true;
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	session->recving = true;
	recv_status = native_recv_till_done(session->sock, session->recv_buf, header.size, false, &session->abort);
	session->recving = false;
	if (recv_status == NATIVE_SOCK_ABORTED){
		// getting aborted
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (recv_status == 0){
		LOG("%s: remote closed the socket\n", __func__);
		native_close_tcp_sock(session->sock);
		session->dead = true;
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (recv_status < 0){
		LOG("%s: failed receiving data\n", __func__);
		native_close_tcp_sock(session->sock);
		session->dead = true;
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	memcpy(buf, session->recv_buf, *len);
	if (*len < header.size){
		session->outstanding_data_offset = *len;
		session->outstanding_data_size = header.size - *len;
		return AEMU_POSTOFFICE_CLIENT_SESSION_DATA_TRUNC;
	}
	*len = header.size;
	return AEMU_POSTOFFICE_CLIENT_OK;
}

void ptp_close(void *ptp_handle){
	if (ptp_handle == NULL){
		return;
	}

	struct ptp_session *session = (struct ptp_session *)ptp_handle;

	// abort on-going ops
	session->abort = true;

	// make sure we are clear of send/recv operations
	do{
		delay(50);
	}while(session->sending || session->recving);

	if (!session->dead)
		native_close_tcp_sock(session->sock);
	session->sock = -1;
}

void ptp_listen_close(void *ptp_listen_handle){
	if (ptp_listen_handle == NULL){
		return;
	}

	struct ptp_listen_session *session = (struct ptp_listen_session *)ptp_listen_handle;

	// abort on-going ops
	session->abort = true;

	// make sure we are clear of send/recv operations
	do{
		delay(50);
	}while(session->accepting);

	if (!session->dead)
		native_close_tcp_sock(session->sock);
	session->sock = -1;
}

int pdp_get_native_sock(void *pdp_handle){
	struct pdp_session *session = pdp_handle;
	return session->sock;
}

int ptp_get_native_sock(void *ptp_handle){
	struct ptp_session *session = ptp_handle;
	return session->sock;
}

int ptp_listen_get_native_sock(void *ptp_listen_handle){
	struct ptp_listen_session *session = ptp_listen_handle;
	return session->sock;
}

int ptp_peek_next_size(void *ptp_handle){
	struct ptp_session *session = ptp_handle;

	if (session->dead || session->abort){
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	aemu_postoffice_ptp_data header = {0};
	session->recving = true;
	int peek_result = native_peek(session->sock, (char *)&header, sizeof(header));
	session->recving = false;
	if (peek_result == AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK){
		return 0;
	}
	if (session->abort){
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (peek_result <= 0){
		session->dead = true;
		native_close_tcp_sock(session->sock);
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}

	if (peek_result != sizeof(header)){
		return 0;
	}

	return header.size;
}
