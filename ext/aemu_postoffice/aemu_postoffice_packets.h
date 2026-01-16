#ifndef __AEMU_POSTOFFICE_PACKETS_H
#define __AEMU_POSTOFFICE_PACKETS_H

#include <stdint.h>

// structs here are gcc little endian

typedef enum{
	AEMU_POSTOFFICE_INIT_PDP,
	AEMU_POSTOFFICE_INIT_PTP_LISTEN,
	AEMU_POSTOFFICE_INIT_PTP_CONNECT,
	AEMU_POSTOFFICE_INIT_PTP_ACCEPT
} aemu_postoffice_init_type;

#pragma pack(push, 1)

// client -> server
typedef struct aemu_postoffice_init{
	int32_t init_type;
	// 2 bytes on padding on the end, only 6 bytes are used
	char src_addr[8];
	uint16_t sport;
	// 2 bytes on padding on the end
	char dst_addr[8];
	uint16_t dport;
} aemu_postoffice_init;

// client <-> server
typedef struct aemu_postoffice_pdp{
	// 2 bytes on padding on the end
	char addr[8];
	uint16_t port;
	uint32_t size;
} aemu_postoffice_pdp;

// server -> client
typedef struct aemu_postoffice_ptp_connect{
	// 2 bytes on padding on the end
	char addr[8];
	uint16_t port;
} aemu_postoffice_ptp_connect;

typedef struct aemu_postoffice_ptp_data{
	uint32_t size;
} aemu_postoffice_ptp_data;

#pragma pack(pop)

#endif
