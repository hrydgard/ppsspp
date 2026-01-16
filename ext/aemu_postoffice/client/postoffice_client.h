#ifndef __POSTOFFICE_CLIENT_H
#define __POSTOFFICE_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

enum aemu_postoffice_client_errors {
	AEMU_POSTOFFICE_CLIENT_OK = 0,
	AEMU_POSTOFFICE_CLIENT_UNKNOWN = -1,
	AEMU_POSTOFFICE_CLIENT_OUT_OF_MEMORY = -2,
	AEMU_POSTOFFICE_CLIENT_SESSION_DEAD = -3,
	AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK = -4,
	AEMU_POSTOFFICE_CLIENT_SESSION_DATA_TRUNC = -5,
	AEMU_POSTOFFICE_CLIENT_SESSION_NETWORK = -6
};

struct aemu_post_office_sock_addr{
	uint32_t addr; // network order
	uint16_t port; // network order
};

struct aemu_post_office_sock6_addr{
	uint8_t addr[16]; // network order
	uint16_t port; // network order
};

#ifdef __cplusplus
extern "C" {
#endif

int aemu_post_office_init();

/*
 * Thread safety:
 * multiple threads can perform create/listen/connect
 * only one thread at a time can accept on a created socket
 * only one thread at a time can send on a created socket
 * only one thread at a time can recv/peek on a created socket
 * only one thread at a time can close a created socket
 *
 * violating the above yields undefined results
 */

void *pdp_create_v6(const struct aemu_post_office_sock6_addr *addr, const char *pdp_mac, int pdp_port, int *state);
void *pdp_create_v4(const struct aemu_post_office_sock_addr *addr, const char *pdp_mac, int pdp_port, int *state);
void pdp_delete(void *pdp_handle);
int pdp_send(void *pdp_handle, const char *pdp_mac, int pdp_port, const char *buf, int len, bool non_block);
int pdp_recv(void *pdp_handle, char *pdp_mac, int *pdp_port, char *buf, int *len, bool non_block);
int pdp_peek_next_size(void *pdp_handle);
void *ptp_listen_v6(const struct aemu_post_office_sock6_addr *addr, const char *ptp_mac, int ptp_port, int *state);
void *ptp_listen_v4(const struct aemu_post_office_sock_addr *addr, const char *ptp_mac, int ptp_port, int *state);
void *ptp_accept(void *ptp_listen_handle, char *ptp_mac, int *ptp_port, bool nonblock, int *state);
void *ptp_connect_v6(const struct aemu_post_office_sock6_addr *addr, const char *src_ptp_mac, int ptp_sport, const char *dst_ptp_mac, int ptp_dport, int *state);
void *ptp_connect_v4(const struct aemu_post_office_sock_addr *addr, const char *src_ptp_mac, int ptp_sport, const char *dst_ptp_mac, int ptp_dport, int *state);
int ptp_send(void *ptp_handle, const char *buf, int len, bool non_block);
int ptp_recv(void *ptp_handle, char *buf, int *len, bool non_block);
void ptp_close(void *ptp_handle);
void ptp_listen_close(void *ptp_listen_handle);
int ptp_peek_next_size(void *ptp_handle);

int pdp_get_native_sock(void *pdp_handle);
int ptp_get_native_sock(void *ptp_handle);
int ptp_listen_get_native_sock(void *ptp_listen_handle);

#ifdef __cplusplus
}
#endif

#endif
