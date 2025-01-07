#pragma once

#include "Core/HLE/HLE.h"

#if PPSSPP_PLATFORM(WINDOWS)
#include <winsock.h>
#endif

#include <memory>
#include <shared_mutex>
#include <unordered_map>

// Using constants instead of numbers for readability reason, since PSP_THREAD_ATTR_KERNEL/USER is located in sceKernelThread.cpp instead of sceKernelThread.h
#ifndef PSP_THREAD_ATTR_KERNEL
#define PSP_THREAD_ATTR_KERNEL 0x00001000
#endif
#ifndef PSP_THREAD_ATTR_USER
#define PSP_THREAD_ATTR_USER 0x80000000
#endif


// Socket Types (based on https://github.com/justincormack/netbsd-src/blob/master/src/sys/sys/socket.h )
#define	PSP_NET_INET_SOCK_STREAM		1			// stream socket 
#define	PSP_NET_INET_SOCK_DGRAM			2			// datagram socket 
#define	PSP_NET_INET_SOCK_RAW			3			// raw-protocol interface // SOCK_RAW is similar to but not compatible with the obsolete AF_INET / SOCK_PACKET // SOCK_RAW have some restrictions on newer Windows https://docs.microsoft.com/en-us/windows/win32/winsock/tcp-ip-raw-sockets-2
#define	PSP_NET_INET_SOCK_RDM			4			// reliably-delivered message 
#define	PSP_NET_INET_SOCK_SEQPACKET		5			// sequenced packet stream 
#define	PSP_NET_INET_SOCK_CONN_DGRAM	6			// connection-orientated datagram
#define PSP_NET_INET_SOCK_DCCP			PSP_NET_INET_SOCK_CONN_DGRAM	// Datagram Congestion Control Protocol
#define PSP_NET_INET_SOCK_PACKET		10			// Linux specific way of getting packets at the dev level. For writing rarp and other similar things on the user level // SOCK_PACKET is an obsolete socket type to receive raw packets directly from the device driver
#define	PSP_NET_INET_SOCK_TYPE_MASK		0x000F		// mask that covers the above
// Flags to be ORed into the type parameter of socket and socketpair and used for the flags parameter of paccept.
#define	PSP_NET_INET_SOCK_CLOEXEC		0x10000000	// set close on exec on socket 
#define	PSP_NET_INET_SOCK_NONBLOCK		0x20000000	// set non blocking i/o socket 
#define	PSP_NET_INET_SOCK_NOSIGPIPE		0x40000000	// don't send sigpipe 
#define	PSP_NET_INET_SOCK_FLAGS_MASK	0xf0000000	// flags mask 

// Option flags per-socket (based on SOL_SOCKET value on PSP (0xffff) seems to be different with linux/android's auto-generated socket.h (1), but similar to posix/gnu/BSD <sys/socket.h> like this https://github.com/eblot/newlib/blob/master/newlib/libc/sys/linux/sys/socket.h ?)
#define	PSP_NET_INET_SO_DEBUG		0x0001		// turn on debugging info recording 
#define	PSP_NET_INET_SO_ACCEPTCONN	0x0002		// socket has had listen() 
#define	PSP_NET_INET_SO_REUSEADDR	0x0004		// allow local address reuse 
#define	PSP_NET_INET_SO_KEEPALIVE	0x0008		// keep connections alive 
#define	PSP_NET_INET_SO_DONTROUTE	0x0010		// just use interface addresses 
#define	PSP_NET_INET_SO_BROADCAST	0x0020		// permit sending of broadcast msgs 
#define	PSP_NET_INET_SO_USELOOPBACK	0x0040		// bypass hardware when possible 
#define	PSP_NET_INET_SO_LINGER		0x0080		// linger on close if data present 
#define	PSP_NET_INET_SO_OOBINLINE	0x0100		// leave received OOB data in line 
#define	PSP_NET_INET_SO_REUSEPORT	0x0200		// allow local address & port reuse 
#define	PSP_NET_INET_SO_TIMESTAMP	0x0400		// timestamp received dgram traffic 
#define	PSP_NET_INET_SO_ONESBCAST	0x0800		// permit sending to 255.255.255.255 

// Additional options (not kept in so_options)
#define PSP_NET_INET_SO_SNDBUF		0x1001		// send buffer size (default value = 16384 bytes)
#define PSP_NET_INET_SO_RCVBUF		0x1002		// receive buffer size (default value = 16384 bytes for TCP/IP, 41600 bytes for UDP/IP)
#define PSP_NET_INET_SO_SNDLOWAT	0x1003		// send low-water mark 
#define PSP_NET_INET_SO_RCVLOWAT	0x1004		// receive low-water mark 
#define PSP_NET_INET_SO_SNDTIMEO	0x1005		// send timeout 
#define PSP_NET_INET_SO_RCVTIMEO	0x1006		// receive timeout 
#define	PSP_NET_INET_SO_ERROR		0x1007		// get error status and clear 
#define	PSP_NET_INET_SO_TYPE		0x1008		// get socket type 
#define PSP_NET_INET_SO_NBIO		0x1009		// SO_NONBLOCK ? // set to non-blocking I/O mode (on true, returning 0x80 when retrieved using getsockopt?)
#define PSP_NET_INET_SO_BIO			0x100a		// set to blocking I/O mode (not using the optval just like SO_NBIO?)
//#define PSP_NET_INET_SO_NONBLOCK	0x100b		// set to blocking or non-blocking I/O mode (using the optval)

// User-settable options (used with setsockopt)
#define	PSP_NET_INET_TCP_NODELAY	0x01		// don't delay send to coalesce packets 
#define	PSP_NET_INET_TCP_MAXSEG		0x02		// set maximum segment size 

// Options for use with [get/set]sockopt at the IP level
#define	PSP_NET_INET_IP_OPTIONS			1    // (buf/ip_opts) set/get IP options 
#define	PSP_NET_INET_IP_HDRINCL			2    // (int) header is included with data 
#define	PSP_NET_INET_IP_TOS				3    // (int) IP type of service and preced. 
#define	PSP_NET_INET_IP_TTL				4    // (int) IP time to live 
#define	PSP_NET_INET_IP_RECVOPTS		5    // (bool) receive all IP opts w/dgram 
#define	PSP_NET_INET_IP_RECVRETOPTS		6    // (bool) receive IP opts for response 
#define	PSP_NET_INET_IP_RECVDSTADDR		7    // (bool) receive IP dst addr w/dgram 
#define	PSP_NET_INET_IP_RETOPTS			8    // (ip_opts) set/get IP options 
#define	PSP_NET_INET_IP_MULTICAST_IF	9    // (in_addr) set/get IP multicast i/f  
#define	PSP_NET_INET_IP_MULTICAST_TTL	10   // (u_char) set/get IP multicast ttl 
#define	PSP_NET_INET_IP_MULTICAST_LOOP	11   // (u_char) set/get IP multicast loopback 
#define	PSP_NET_INET_IP_ADD_MEMBERSHIP	12   // (ip_mreq) add an IP group membership 
#define	PSP_NET_INET_IP_DROP_MEMBERSHIP	13   // (ip_mreq) drop an IP group membership 
#define PSP_NET_INET_IP_PORTRANGE		19   // (int) range to use for ephemeral port 
#define	PSP_NET_INET_IP_RECVIF			20   // (bool) receive reception if w/dgram 
#define	PSP_NET_INET_IP_ERRORMTU		21   // (int) get MTU of last xmit = EMSGSIZE 

#define PSP_NET_INET_IP_IPSEC_POLICY	22   // (struct) get/set security policy 

// Level number for [get/set]sockopt to apply to socket itself
#define	PSP_NET_INET_SOL_SOCKET		0xffff		// options for socket level

// "Socket"-level control message types: 
#define	PSP_NET_INET_SCM_RIGHTS		0x01		// access rights (array of int) 
#define	PSP_NET_INET_SCM_CREDS		0x04		// credentials (struct sockcred) 
#define	PSP_NET_INET_SCM_TIMESTAMP	0x08		// timestamp (struct timeval) 

// Protocols
#define	PSP_NET_INET_IPPROTO_IP			0		// dummy for IP 
#define PSP_NET_INET_IPPROTO_HOPOPTS	0		// IP6 hop-by-hop options 
#define PSP_NET_INET_IPPROTO_UNSPEC		0		// 0 will defaulted to the only existing protocol for that particular domain/family and type
#define	PSP_NET_INET_IPPROTO_ICMP		1		// control message protocol 
#define	PSP_NET_INET_IPPROTO_IGMP		2		// group mgmt protocol 
#define	PSP_NET_INET_IPPROTO_GGP		3		// gateway^2 (deprecated) 
#define PSP_NET_INET_IPPROTO_IPV4		4 		// IP header 
#define	PSP_NET_INET_IPPROTO_IPIP		4		// IP inside IP 
#define	PSP_NET_INET_IPPROTO_TCP		6		// tcp 
#define	PSP_NET_INET_IPPROTO_EGP		8		// exterior gateway protocol 
#define	PSP_NET_INET_IPPROTO_PUP		12		// pup 
#define	PSP_NET_INET_IPPROTO_UDP		17		// user datagram protocol 
#define	PSP_NET_INET_IPPROTO_IDP		22		// xns idp 
#define	PSP_NET_INET_IPPROTO_TP			29 		// tp-4 w/ class negotiation 
#define PSP_NET_INET_IPPROTO_IPV6		41		// IP6 header 
#define PSP_NET_INET_IPPROTO_ROUTING	43		// IP6 routing header 
#define PSP_NET_INET_IPPROTO_FRAGMENT	44		// IP6 fragmentation header 
#define PSP_NET_INET_IPPROTO_RSVP		46		// resource reservation 
#define PSP_NET_INET_IPPROTO_GRE		47		// GRE encaps RFC 1701 
#define	PSP_NET_INET_IPPROTO_ESP		50 		// encap. security payload 
#define	PSP_NET_INET_IPPROTO_AH			51 		// authentication header 
#define PSP_NET_INET_IPPROTO_MOBILE		55		// IP Mobility RFC 2004 
#define PSP_NET_INET_IPPROTO_IPV6_ICMP	58		// IPv6 ICMP 
#define PSP_NET_INET_IPPROTO_ICMPV6		58		// ICMP6 
#define PSP_NET_INET_IPPROTO_NONE		59		// IP6 no next header 
#define PSP_NET_INET_IPPROTO_DSTOPTS	60		// IP6 destination option 
#define	PSP_NET_INET_IPPROTO_EON		80		// ISO cnlp 
#define	PSP_NET_INET_IPPROTO_ENCAP		98		// encapsulation header 
#define PSP_NET_INET_IPPROTO_PIM		103		// Protocol indep. multicast 
#define PSP_NET_INET_IPPROTO_IPCOMP		108		// IP Payload Comp. Protocol 

#define	PSP_NET_INET_IPPROTO_RAW		255		// raw IP packet 
#define	PSP_NET_INET_IPPROTO_MAX		256

#define	PSP_NET_INET_IPPROTO_DONE		257		// all job for this packet are done

// Address families
#define	PSP_NET_INET_AF_UNSPEC		0		// unspecified 
#define	PSP_NET_INET_AF_LOCAL		1		// local to host (pipes, portals) 
#define	PSP_NET_INET_AF_UNIX		PSP_NET_INET_AF_LOCAL	// backward compatibility 
#define	PSP_NET_INET_AF_INET		2		// internetwork: UDP, TCP, etc. 
#define	PSP_NET_INET_AF_IMPLINK		3		// arpanet imp addresses 
#define	PSP_NET_INET_AF_PUP			4		// pup protocols: e.g. BSP 
#define	PSP_NET_INET_AF_CHAOS		5		// mit CHAOS protocols 
#define	PSP_NET_INET_AF_NS			6		// XEROX NS protocols 
#define	PSP_NET_INET_AF_ISO			7		// ISO protocols 
#define	PSP_NET_INET_AF_OSI			PSP_NET_INET_AF_ISO	 
#define	PSP_NET_INET_AF_ECMA		8		// european computer manufacturers 
#define	PSP_NET_INET_AF_DATAKIT		9		// datakit protocols 
#define	PSP_NET_INET_AF_CCITT		10		// CCITT protocols, X.25 etc 
#define	PSP_NET_INET_AF_SNA			11		// IBM SNA 
#define PSP_NET_INET_AF_DECnet		12		// DECnet 
#define PSP_NET_INET_AF_DLI			13		// DEC Direct data link interface 
#define PSP_NET_INET_AF_LAT			14		// LAT 
#define	PSP_NET_INET_AF_HYLINK		15		// NSC Hyperchannel 
#define	PSP_NET_INET_AF_APPLETALK	16		// Apple Talk 
#define	PSP_NET_INET_AF_ROUTE		17		// Internal Routing Protocol 
#define	PSP_NET_INET_AF_LINK		18		// Link layer interface 

#define	PSP_NET_INET_AF_COIP		20		// connection-oriented IP, aka ST II 
#define	PSP_NET_INET_AF_CNT			21		// Computer Network Technology 

#define	PSP_NET_INET_AF_IPX			23		// Novell Internet Protocol 
#define	PSP_NET_INET_AF_INET6		24		// IP version 6 

#define PSP_NET_INET_AF_ISDN		26		// Integrated Services Digital Network
#define PSP_NET_INET_AF_E164		PSP_NET_INET_AF_ISDN		// CCITT E.164 recommendation 
#define PSP_NET_INET_AF_NATM		27		// native ATM access 
#define PSP_NET_INET_AF_ARP			28		// (rev.) addr. res. prot. (RFC 826) 

#define	PSP_NET_INET_AF_MAX			31

// Infrastructure ERRNO Values (similar to this https://github.com/eblot/newlib/blob/master/newlib/libc/include/sys/errno.h ?)
#define	ERROR_INET_EINTR			4	// Interrupted system call 
#define ERROR_INET_EBADF			9	//0x09  // Or was it 0x80010009 (SCE_ERROR_ERRNO_EBADF/SCE_KERNEL_ERROR_ERRNO_INVALID_FILE_DESCRIPTOR) ?
#define ERROR_INET_EAGAIN			11	//0x0B  // Or was it 0x8001000B (SCE_ERROR_ERRNO_EAGAIN) ?
#define ERROR_INET_EWOULDBLOCK		EAGAIN // Operation would block
#define	ERROR_INET_EACCES			13	// Permission denied 
#define	ERROR_INET_EFAULT			14	// Bad address 
#define	ERROR_INET_EINVAL			22	// Invalid argument 
#define	ERROR_INET_ENOSPC			28	// No space left on device
#define	ERROR_INET_EPIPE			32	// Broken pipe
#define	ERROR_INET_ENOMSG			35	// No message of desired type
#define ERROR_INET_ENOLINK			67	// The link has been severed
#define ERROR_INET_EPROTO			71	// Protocol error
#define ERROR_INET_EBADMSG			77	// Trying to read unreadable message
#define ERROR_INET_EOPNOTSUPP		95	// Operation not supported on transport endpoint
#define ERROR_INET_EPFNOSUPPORT		96	// Protocol family not supported
#define ERROR_INET_ECONNRESET		104 // Connection reset by peer
#define ERROR_INET_ENOBUFS			105 // No buffer space available
#define ERROR_INET_EAFNOSUPPORT		106 // EISCONN ? // Address family not supported by protocol family
#define ERROR_INET_EPROTOTYPE		107	// Protocol wrong type for socket
#define ERROR_INET_ENOTSOCK			108	// Socket operation on non-socket
#define ERROR_INET_ENOPROTOOPT		109	// Protocol not available
#define ERROR_INET_ESHUTDOWN		110	// Can't send after socket shutdown
#define ERROR_INET_ECONNREFUSED		111	// Connection refused
#define ERROR_INET_EADDRINUSE		112	// Address already in use 
#define ERROR_INET_ECONNABORTED		113	// Connection aborted 
#define ERROR_INET_ENETUNREACH		114	// Network is unreachable 
#define ERROR_INET_ENETDOWN			115	// Network interface is not configured
#define ERROR_INET_ETIMEDOUT		116	// Connection timed out
#define ERROR_INET_EHOSTDOWN		117	// Host is down 
#define ERROR_INET_EHOSTUNREACH		118	// Host is unreachable
#define ERROR_INET_EINPROGRESS		119	// Connection already in progress
#define ERROR_INET_EALREADY			120	// Socket already connected 
#define ERROR_INET_EDESTADDRREQ		121	// Destination address required 
#define ERROR_INET_EMSGSIZE			122	// Message too long 
#define ERROR_INET_EPROTONOSUPPORT	123	// Unknown protocol
#define ERROR_INET_ESOCKTNOSUPPORT	124	// Socket type not supported (linux?)
#define ERROR_INET_EADDRNOTAVAIL	125	// Address not available 
#define ERROR_INET_ENETRESET		126
#define ERROR_INET_EISCONN			127	// Socket is already connected
#define ERROR_INET_ENOTCONN			128	// Socket is not connected 
#define ERROR_INET_ETOOMANYREFS		129
#define ERROR_INET_ENOTSUP			134 // Not supported

// Maximum queue length specifiable by listen(2)
#define	PSP_NET_INET_SOMAXCONN		128

// On-Demand Flags
#define	PSP_NET_INET_MSG_OOB		0x1			// process out-of-band data 
#define	PSP_NET_INET_MSG_PEEK		0x2			// peek at incoming message 
#define	PSP_NET_INET_MSG_DONTROUTE	0x4			// send without using routing tables 
#define	PSP_NET_INET_MSG_EOR		0x8			// data completes record 
#define	PSP_NET_INET_MSG_TRUNC		0x10		// data discarded before delivery 
#define	PSP_NET_INET_MSG_CTRUNC		0x20		// control data lost before delivery 
#define	PSP_NET_INET_MSG_WAITALL	0x40		// wait for full request or error 
#define	PSP_NET_INET_MSG_DONTWAIT	0x80		// this message should be nonblocking 
#define	PSP_NET_INET_MSG_BCAST		0x100		// this message was rcvd using link-level brdcst 
#define	PSP_NET_INET_MSG_MCAST		0x200		// this message was rcvd using link-level mcast 

// Poll Event Flags (used on events)
#define INET_POLLIN			0x001		// There is data to read.  
#define INET_POLLPRI		0x002		// There is urgent data to read.  
#define INET_POLLOUT		0x004		// Writing now will not block.  

#define INET_POLLRDNORM		0x040		// Equivalent to POLLIN ? just like _XOPEN_SOURCE? (mapped to read fds_set)
#define INET_POLLWRNORM		0x100		//0x0004 ? // Equivalent to POLLOUT ? just like _XOPEN_SOURCE? (mapped to write fds_set)

#define INET_POLLRDBAND		0x080		// Priority data may be read.  (mapped to exception fds_set)
#define INET_POLLWRBAND		0x200		// Priority data may be written.  (mapped to write fds_set?)

#define INET_POLLERR		0x008		// Error condition.  (can appear on revents regardless of events?)
#define INET_POLLHUP		0x010		// Hung up.  (can appear on revents regardless of events?)
#define INET_POLLNVAL		0x020		// Invalid polling request.  (can appear on revents regardless of events?)

// Types of socket shutdown(2)
#define	PSP_NET_INET_SHUT_RD		0		// Disallow further receives.
#define	PSP_NET_INET_SHUT_WR		1		// Disallow further sends. 
#define	PSP_NET_INET_SHUT_RDWR		2		// Disallow further sends/receives.

#ifndef SHUT_RD
#define SHUT_RD SD_RECEIVE	//0x00
#endif
#ifndef SHUT_WR
#define SHUT_WR SD_SEND		//0x01
#endif
#ifndef SHUT_RDWR
#define SHUT_RDWR SD_BOTH	//0x02
#endif

enum {
	// from pspsdk's pspnet.h which is similar to https://docs.vitasdk.org/net_2net_8h_source.html
	SCE_NET_ERROR_EPERM = 0x80410101,
	SCE_NET_ERROR_ENOENT = 0x80410102,
	SCE_NET_ERROR_ESRCH = 0x80410103,
	SCE_NET_ERROR_EINTR = 0x80410104,
	SCE_NET_ERROR_EIO = 0x80410105,
	SCE_NET_ERROR_ENXIO = 0x80410106,
	SCE_NET_ERROR_E2BIG = 0x80410107,

	SCE_NET_ERROR_ENOEXEC = 0x80410108,
	SCE_NET_ERROR_EBADF = 0x80410109,
	SCE_NET_ERROR_ECHILD = 0x8041010A,
	SCE_NET_ERROR_EDEADLK = 0x8041010B,
	SCE_NET_ERROR_ENOMEM = 0x8041010C,
	SCE_NET_ERROR_EACCES = 0x8041010D,
	SCE_NET_ERROR_EFAULT = 0x8041010E,
	SCE_NET_ERROR_ENOTBLK = 0x8041010F,
	SCE_NET_ERROR_EBUSY = 0x80410110,
	SCE_NET_ERROR_EEXIST = 0x80410111,
	SCE_NET_ERROR_EXDEV = 0x80410112,
	SCE_NET_ERROR_ENODEV = 0x80410113,
	SCE_NET_ERROR_ENOTDIR = 0x80410114,
	SCE_NET_ERROR_EISDIR = 0x80410115,
	SCE_NET_ERROR_EINVAL = 0x80410116,
	SCE_NET_ERROR_ENFILE = 0x80410117,
	SCE_NET_ERROR_EMFILE = 0x80410118,
	SCE_NET_ERROR_ENOTTY = 0x80410119,
	SCE_NET_ERROR_ETXTBSY = 0x8041011A,
	SCE_NET_ERROR_EFBIG = 0x8041011B,
	SCE_NET_ERROR_ENOSPC = 0x8041011C,
	SCE_NET_ERROR_ESPIPE = 0x8041011D,
	SCE_NET_ERROR_EROFS = 0x8041011E,
	SCE_NET_ERROR_EMLINK = 0x8041011F,
	SCE_NET_ERROR_EPIPE = 0x80410120,
	SCE_NET_ERROR_EDOM = 0x80410121,
	SCE_NET_ERROR_ERANGE = 0x80410122,
	SCE_NET_ERROR_EAGAIN = 0x80410123,
	SCE_NET_ERROR_EWOULDBLOCK = 0x80410123,
	SCE_NET_ERROR_EINPROGRESS = 0x80410124,
	SCE_NET_ERROR_EALREADY = 0x80410125,
	SCE_NET_ERROR_ENOTSOCK = 0x80410126,
	SCE_NET_ERROR_EDESTADDRREQ = 0x80410127,
	SCE_NET_ERROR_EMSGSIZE = 0x80410128,
	SCE_NET_ERROR_EPROTOTYPE = 0x80410129,
	SCE_NET_ERROR_ENOPROTOOPT = 0x8041012A,
	SCE_NET_ERROR_EPROTONOSUPPORT = 0x8041012B,
	SCE_NET_ERROR_ESOCKTNOSUPPORT = 0x8041012C,
	SCE_NET_ERROR_EOPNOTSUPP = 0x8041012D,
	SCE_NET_ERROR_EPFNOSUPPORT = 0x8041012E,
	SCE_NET_ERROR_EAFNOSUPPORT = 0x8041012F,
	SCE_NET_ERROR_EADDRINUSE = 0x80410130,
	SCE_NET_ERROR_EADDRNOTAVAIL = 0x80410131,
	SCE_NET_ERROR_ENETDOWN = 0x80410132,
	SCE_NET_ERROR_ENETUNREACH = 0x80410133,
	SCE_NET_ERROR_ENETRESET = 0x80410134,
	SCE_NET_ERROR_ECONNABORTED = 0x80410135,
	SCE_NET_ERROR_ECONNRESET = 0x80410136,
	SCE_NET_ERROR_ENOBUFS = 0x80410137,
	SCE_NET_ERROR_EISCONN = 0x80410138,
	SCE_NET_ERROR_ENOTCONN = 0x80410139,
	SCE_NET_ERROR_ESHUTDOWN = 0x8041013A,
	SCE_NET_ERROR_ETOOMANYREFS = 0x8041013B,
	SCE_NET_ERROR_ETIMEDOUT = 0x8041013C,
	SCE_NET_ERROR_ECONNREFUSED = 0x8041013D,
	SCE_NET_ERROR_ELOOP = 0x8041013E,
	SCE_NET_ERROR_ENAMETOOLONG = 0x8041013F,
	SCE_NET_ERROR_EHOSTDOWN = 0x80410140,
	SCE_NET_ERROR_EHOSTUNREACH = 0x80410141,
	SCE_NET_ERROR_ENOTEMPTY = 0x80410142,
	SCE_NET_ERROR_EPROCLIM = 0x80410143,
	SCE_NET_ERROR_EUSERS = 0x80410144,
	SCE_NET_ERROR_EDQUOT = 0x80410145,
	SCE_NET_ERROR_ESTALE = 0x80410146,
	SCE_NET_ERROR_EREMOTE = 0x80410147,
	SCE_NET_ERROR_EBADRPC = 0x80410148,
	SCE_NET_ERROR_ERPCMISMATCH = 0x80410149,
	SCE_NET_ERROR_EPROGUNAVAIL = 0x8041014A,
	SCE_NET_ERROR_EPROGMISMATCH = 0x8041014B,
	SCE_NET_ERROR_EPROCUNAVAIL = 0x8041014C,
	SCE_NET_ERROR_ENOLCK = 0x8041014D,
	SCE_NET_ERROR_ENOSYS = 0x8041014E,
	SCE_NET_ERROR_EFTYPE = 0x8041014F,
	SCE_NET_ERROR_EAUTH = 0x80410150,
	SCE_NET_ERROR_ENEEDAUTH = 0x80410151,
	SCE_NET_ERROR_EIDRM = 0x80410152,
	SCE_NET_ERROR_ENOMS = 0x80410153,
	SCE_NET_ERROR_EOVERFLOW = 0x80410154,
	SCE_NET_ERROR_EILSEQ = 0x80410155,
	SCE_NET_ERROR_ENOTSUP = 0x80410156,
	SCE_NET_ERROR_ECANCELED = 0x80410157,
	SCE_NET_ERROR_EBADMSG = 0x80410158,
	SCE_NET_ERROR_ENODATA = 0x80410159,
	SCE_NET_ERROR_ENOSR = 0x8041015A,
	SCE_NET_ERROR_ENOSTR = 0x8041015B,
	SCE_NET_ERROR_ETIME = 0x8041015C,

	SCE_NET_ERROR_EADHOC = 0x804101A0,
	SCE_NET_ERROR_EDISABLEDIF = 0x804101A1,
	SCE_NET_ERROR_ERESUME = 0x804101A2,

	SCE_NET_ERROR_ENOTINIT = 0x804101C8,
	SCE_NET_ERROR_ENOLIBMEM = 0x804101C9,
	SCE_NET_ERROR_ERESERVED202 = 0x804101CA,
	SCE_NET_ERROR_ECALLBACK = 0x804101CB,
	SCE_NET_ERROR_EINTERNAL = 0x804101CC,
	SCE_NET_ERROR_ERETURN = 0x804101CD,

	SCE_NET_ERROR_RESOLVER_EINTERNAL = 0x804101DC,
	SCE_NET_ERROR_RESOLVER_EBUSY = 0x804101DD,
	SCE_NET_ERROR_RESOLVER_ENOSPACE = 0x804101DE,
	SCE_NET_ERROR_RESOLVER_EPACKET = 0x804101DF,
	SCE_NET_ERROR_RESOLVER_ERESERVED22 = 0x804101E0,
	SCE_NET_ERROR_RESOLVER_ENODNS = 0x804101E1,
	SCE_NET_ERROR_RESOLVER_ETIMEDOUT = 0x804101E2,
	SCE_NET_ERROR_RESOLVER_ENOSUPPORT = 0x804101E3,
	SCE_NET_ERROR_RESOLVER_EFORMAT = 0x804101E4,
	SCE_NET_ERROR_RESOLVER_ESERVERFAILURE = 0x804101E5,
	SCE_NET_ERROR_RESOLVER_ENOHOST = 0x804101E6,
	SCE_NET_ERROR_RESOLVER_ENOTIMPLEMENTED = 0x804101E7,
	SCE_NET_ERROR_RESOLVER_ESERVERREFUSED = 0x804101E8,
	SCE_NET_ERROR_RESOLVER_ENORECORD = 0x804101E9,
	SCE_NET_ERROR_RESOLVER_EALIGNMENT = 0x804101EA,

	// pspnet_inet
	ERROR_NET_INET_ALREADY_INITIALIZED = 0x80410201,
	ERROR_NET_INET_SOCKET_BUSY = 0x80410202,
	ERROR_NET_INET_CONFIG_INVALID_ARG = 0x80410203,
	ERROR_NET_INET_GET_IFADDR = 0x80410204,
	ERROR_NET_INET_SET_IFADDR = 0x80410205,
	ERROR_NET_INET_DEL_IFADDR = 0x80410206,
	ERROR_NET_INET_NO_DEFAULT_ROUTE = 0x80410207,
	ERROR_NET_INET_GET_ROUTE = 0x80410208,
	ERROR_NET_INET_SET_ROUTE = 0x80410209,
	ERROR_NET_INET_FLUSH_ROUTE = 0x8041020a,
	ERROR_NET_INET_INVALID_ARG = 0x8041020b,
};

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
typedef struct InetSocket {
	s32_le id; // posix socket id
	s32_le domain; // AF_INET/PF_INET/etc
	s32_le type; // SOCK_STREAM/SOCK_DGRAM/etc
	s32_le protocol; // TCP/UDP/etc
	s32_le nonblocking; // non-blocking flag (ie. FIONBIO) to keep track of the blocking mode since Windows doesn't have getter for this
	s32_le so_broadcast; // broadcast flag (ie. SO_BROADCAST) to keep track of the broadcast flag, since we're using fake broadcast
	s32_le tcp_state; // to keep track TCP connection state
} PACK InetSocket;

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
int sceNetInetTerm();
int sceNetApctlTerm();
int sceNetApctlDisconnect();

void Register_sceNetInet();
