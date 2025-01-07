#pragma once

#include "ppsspp_config.h"

#include "Core/HLE/HLE.h"

#include "Common/Net/SocketCompat.h"

#include <memory>

// Using constants instead of numbers for readability reason, since PSP_THREAD_ATTR_KERNEL/USER is located in sceKernelThread.cpp instead of sceKernelThread.h
#ifndef PSP_THREAD_ATTR_KERNEL
#define PSP_THREAD_ATTR_KERNEL 0x00001000
#endif
#ifndef PSP_THREAD_ATTR_USER
#define PSP_THREAD_ATTR_USER 0x80000000
#endif

// Similar to https://ftp.netbsd.org/pub/NetBSD/NetBSD-current/src/sys/sys/fd_set.h
#define		PSP_NET_INET_FD_SETSIZE		256		// PSP can support upto 256 fd(s) while the default FD_SETSIZE on Windows is only 64 
#define		PSP_NET_INET_NFDBITS		32		// Default: 32 = sizeof(u32) * 8 (8-bits of each byte) = number of bits for each element in fds_bits
#define		PSP_NET_INET_NFDBITS_SHIFT	5		// 2^5 = 32 
#define		PSP_NET_INET_NFDBITS_MASK	0x1F	// 0x1F = 5 bit mask (NFDBITS - 1)
#define		PSP_NET_INET_FD_MASK		0xFF	// Making sure FD value in the range of 0-255 to prevent out-of-bound

#define		NetInetFD_SET(n, p) \
				((p)->fds_bits[((n) & PSP_NET_INET_FD_MASK)>>PSP_NET_INET_NFDBITS_SHIFT] |= (1 << ((n) & PSP_NET_INET_NFDBITS_MASK))) // (1 << ((n) % PSP_NET_INET_NFDBITS))
#define		NetInetFD_CLR(n, p) \
				((p)->fds_bits[((n) & PSP_NET_INET_FD_MASK)>>PSP_NET_INET_NFDBITS_SHIFT] &= ~(1 << ((n) & PSP_NET_INET_NFDBITS_MASK)))
#define		NetInetFD_ISSET(n, p) \
				((p)->fds_bits[((n) & PSP_NET_INET_FD_MASK)>>PSP_NET_INET_NFDBITS_SHIFT] & (1 << ((n) & PSP_NET_INET_NFDBITS_MASK)))
#define		NetInetFD_ZERO(p) \
				(void)memset((p), 0, sizeof(*(p)))

// There might be padding (padded to 4 bytes) between each cmsghdr? Similar to http://www.masterraghu.com/subjects/np/introduction/unix_network_programming_v1.3/ch14lev1sec6.html#ch14lev1sec6
struct InetCmsghdr {
	s32_le cmsg_len;   // length in bytes, including this structure, includes padding between cmsg_type and cmsg_data[] ?
	s32_le cmsg_level; // originating protocol 
	s32_le cmsg_type;  // protocol-specific type 
	// followed by unsigned char cmsg_data[], there might be 4-bytes padding between cmsg_type and cmsg_data[] ?
};

struct SceNetInetTimeval {
	u32_le tv_sec;         // seconds 
	u32_le tv_usec;        // and microseconds 
};

#ifdef _MSC_VER
#pragma pack(push,1)
#endif

// FdSet
typedef struct SceNetInetFdSet {
	u32_le	fds_bits[(PSP_NET_INET_FD_SETSIZE + (PSP_NET_INET_NFDBITS - 1)) / PSP_NET_INET_NFDBITS]; // Default: 8 = ((PSP_NET_INET_FD_SETSIZE(256) + (PSP_NET_INET_NFDBITS-1)) / PSP_NET_INET_NFDBITS(32)) elements of 32-bit array to represents 256(FD_SETSIZE) bits of fd's bitmap
} PACK SceNetInetFdSet;

// Sockaddr
typedef struct SceNetInetSockaddr {
	uint8_t sa_len; // length of this struct or sa_data only?
	uint8_t sa_family;
	uint8_t sa_data[14]; // up to 14 bytes of data?
} PACK SceNetInetSockaddr;

// Sockaddr_in
typedef struct SceNetInetSockaddrIn {
	uint8_t sin_len; // length of this struct?
	uint8_t sin_family;
	u16_le sin_port; //uint16_t
	u32_le sin_addr; //uint32_t
	uint8_t sin_zero[8]; // zero-filled padding?
} PACK SceNetInetSockaddrIn;

// Similar to iovec struct on 32-bit platform from BSD's uio.h/_iovec.h
typedef struct SceNetIovec {
	u32_le iov_base;	// Base address (pointer/void* of buffer)
	u32_le iov_len;		// Length
} PACK SceNetIovec;

// Similar to msghdr struct on 32-bit platform from BSD's socket.h
typedef struct InetMsghdr {
	u32_le msg_name;					// optional address (pointer/void* to sockaddr_in/SceNetInetSockaddrIn/SceNetInetSockaddr struct?)
	u32_le msg_namelen;					// size of optional address 
	u32_le msg_iov;						// pointer to iovec/SceNetIovec (ie. struct iovec*/PSPPointer<SceNetIovec>), scatter/gather array (buffers are concatenated before sent?)
	s32_le msg_iovlen;					// # elements in msg_iov 
	u32_le msg_control;					// pointer (ie. void*/PSPPointer<InetCmsghdr>) to ancillary data (multiple of cmsghdr/InetCmsghdr struct?), see below 
	u32_le msg_controllen;				// ancillary data buffer len, includes padding between each cmsghdr/InetCmsghdr struct?
	s32_le msg_flags;					// flags on received message (ignored on sendmsg?)
} PACK InetMsgHdr;

// Structure used for manipulating linger option
typedef struct SceNetInetLinger {
	s32_le	l_onoff;		// option on/off 
	s32_le	l_linger;		// linger time in seconds 
} PACK SceNetInetLinger;

// Polling Event Field
typedef struct SceNetInetPollfd { //similar format to pollfd in 32bit (pollfd in 64bit have different size)
	s32_le fd;
	s16_le events;
	s16_le revents;
} PACK SceNetInetPollfd;

// TCP & UDP Socket Union (Internal use only)
/*
typedef struct InetSocket {
	s32_le id; // posix socket id
	s32_le domain; // AF_INET/PF_INET/etc
	s32_le type; // SOCK_STREAM/SOCK_DGRAM/etc
	s32_le protocol; // TCP/UDP/etc
	s32_le nonblocking; // non-blocking flag (ie. FIONBIO) to keep track of the blocking mode since Windows doesn't have getter for this
	s32_le so_broadcast; // broadcast flag (ie. SO_BROADCAST) to keep track of the broadcast flag, since we're using fake broadcast
	s32_le tcp_state; // to keep track TCP connection state
} PACK InetSocket;
*/

#ifdef _MSC_VER
#pragma pack(pop)
#endif

// ----------------- DNS Header -----------------------------
// Based on https://web.archive.org/web/20201204080751/https://www.binarytides.com/dns-query-code-in-c-with-winsock/
typedef struct
{
	unsigned short id;		   // identification number
	unsigned char rd : 1;      // recursion desired
	unsigned char tc : 1;      // truncated message
	unsigned char aa : 1;      // authoritive answer
	unsigned char opcode : 4;  // purpose of message
	unsigned char qr : 1;      // query/response flag
	unsigned char rcode : 4;   // response code
	unsigned char cd : 1;      // checking disabled
	unsigned char ad : 1;      // authenticated data
	unsigned char z : 1;       // its z! reserved
	unsigned char ra : 1;      // recursion available
	unsigned short q_count;    // number of question entries
	unsigned short ans_count;  // number of answer entries
	unsigned short auth_count; // number of authority entries
	unsigned short add_count;  // number of resource entries
} DNS_HEADER;

typedef struct
{
	unsigned short qtype;
	unsigned short qclass;
} QUESTION;

typedef struct
{
	unsigned short type;
	unsigned short _class;
	unsigned int ttl;
	unsigned short data_len;
} R_DATA;

typedef struct
{
	unsigned char* name;
	R_DATA* resource;
	unsigned char* rdata;
} RES_RECORD;

typedef struct
{
	unsigned char* name;
	QUESTION* ques;
} QUERY;
// ---------------------------------------------------------

class PointerWrap;

extern bool netInited;
extern bool netInetInited;
extern bool netApctlInited;
extern u32 netApctlState;
extern SceNetApctlInfoInternal netApctlInfo;
extern std::string defaultNetConfigName;
extern std::string defaultNetSSID;

void Register_sceNetInet();

void __NetInetShutdown();

int NetApctl_GetState();

int sceNetApctlConnect(int connIndex);
int sceNetInetPoll(u32 fdsPtr, u32 nfds, int timeout);
int sceNetApctlTerm();
int sceNetApctlDisconnect();

enum class SocketState {
	Unused,
	Used,
};

// Internal socket state tracking
struct InetSocket {
	SOCKET sock;  // native socket
	SocketState state;
	// NOTE: These are the PSP types for now
	int domain;
	int type;
	int protocol;
};

extern InetSocket g_inetSockets[256];
