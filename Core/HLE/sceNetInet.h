#pragma once

#include "Core/HLE/HLE.h"
#include "Core/Net/InetSocket.h"

#if PPSSPP_PLATFORM(WINDOWS)
#include <winsock.h>
#endif

#include <memory>
#include <shared_mutex>
#include <unordered_map>

// Sockaddr
typedef struct SceNetInetSockaddr {
    uint8_t sa_len;
    uint8_t sa_family;
    uint8_t sa_data[14];
} PACK SceNetInetSockaddr;

// Sockaddr_in
typedef struct SceNetInetSockaddrIn {
    uint8_t sin_len;
    uint8_t sin_family;
    u16_le sin_port; //uint16_t
    u32_le sin_addr; //uint32_t
    uint8_t sin_zero[8];
} PACK SceNetInetSockaddrIn;

// Polling Event Field
typedef struct SceNetInetPollfd { //similar format to pollfd in 32bit (pollfd in 64bit have different size)
    s32_le fd;
    s16_le events;
    s16_le revents;
} PACK SceNetInetPollfd;

enum {
    // pspnet_inet
    ERROR_NET_INET_ALREADY_INITIALIZED		= 0x80410201,
    ERROR_NET_INET_SOCKET_BUSY				= 0x80410202,
    ERROR_NET_INET_CONFIG_INVALID_ARG		= 0x80410203,
    ERROR_NET_INET_GET_IFADDR				= 0x80410204,
    ERROR_NET_INET_SET_IFADDR				= 0x80410205,
    ERROR_NET_INET_DEL_IFADDR				= 0x80410206,
    ERROR_NET_INET_NO_DEFAULT_ROUTE			= 0x80410207,
    ERROR_NET_INET_GET_ROUTE				= 0x80410208,
    ERROR_NET_INET_SET_ROUTE				= 0x80410209,
    ERROR_NET_INET_FLUSH_ROUTE				= 0x8041020a,
    ERROR_NET_INET_INVALID_ARG				= 0x8041020b,
};

enum PspInetAddressFamily {
    PSP_NET_INET_AF_UNSPEC = 0,
    PSP_NET_INET_AF_LOCAL = 1,
    PSP_NET_INET_AF_INET = 2,
};

enum PspInetSocketLevel {
    PSP_NET_INET_SOL_SOCKET = 0xFFFF,
};

enum PspInetSocketType {
    PSP_NET_INET_SOCK_STREAM = 1,
    PSP_NET_INET_SOCK_DGRAM = 2,
    PSP_NET_INET_SOCK_RAW = 3,
    PSP_NET_INET_SOCK_RDM = 4,
    PSP_NET_INET_SOCK_SEQPACKET = 5,
    PSP_NET_INET_SOCK_DCCP = 6,
    PSP_NET_INET_SOCK_PACKET = 10,
    PSP_NET_INET_SOCK_TYPE_MASK = 0xF,
};

enum PspInetSocketTypeFlag {
    PSP_NET_INET_SOCK_CLOEXEC = 0x10000000,
    PSP_NET_INET_SOCK_NONBLOCK = 0x20000000,
    PSP_NET_INET_SOCK_NOSIGPIPE = 0x40000000,
    PSP_NET_INET_SOCK_FLAGS_MASK = 0xF0000000,
};

// TODO: revisit protocols, not all are necessary
enum PspInetProtocol {
    PSP_NET_INET_IPPROTO_IP = 0, // dummy for IP
    PSP_NET_INET_IPPROTO_UNSPEC = 0, // 0 will defaulted to the only existing protocol for that particular domain/family and type
    PSP_NET_INET_IPPROTO_ICMP = 1, // control message protocol
    PSP_NET_INET_IPPROTO_IGMP = 2, // group mgmt protocol
    PSP_NET_INET_IPPROTO_TCP = 6, // tcp
    PSP_NET_INET_IPPROTO_EGP = 8, // exterior gateway protocol
    PSP_NET_INET_IPPROTO_PUP = 12, // pup
    PSP_NET_INET_IPPROTO_UDP = 17, // user datagram protocol
    PSP_NET_INET_IPPROTO_IDP = 22, // xns idp
    PSP_NET_INET_IPPROTO_RAW = 255, // raw IP packet
};

// TODO: INET_
enum PspInetSocketOptionName {
    // TODO: also specify minimum socket size
    INET_SO_ACCEPTCONN   = 0x0002, // socket has had listen()
    INET_SO_REUSEADDR    = 0x0004, // allow local address reuse
    INET_SO_KEEPALIVE    = 0x0008, // keep connections alive
    INET_SO_DONTROUTE    = 0x0010, // just use interface addresses
    INET_SO_BROADCAST    = 0x0020, // permit sending of broadcast msgs
    INET_SO_USELOOPBACK  = 0x0040, // bypass hardware when possible
    INET_SO_LINGER       = 0x0080, // linger on close if data present
    INET_SO_OOBINLINE    = 0x0100, // leave received OOB data in line
    INET_SO_REUSEPORT    = 0x0200, // allow local address & port reuse
    INET_SO_TIMESTAMP    = 0x0400, // timestamp received dgram traffic
    INET_SO_ONESBCAST    = 0x0800, // allow broadcast to 255.255.255.255
    INET_SO_SNDBUF       = 0x1001, // send buffer size
    INET_SO_RCVBUF       = 0x1002, // receive buffer size
    INET_SO_SNDLOWAT     = 0x1003, // send low-water mark
    INET_SO_RCVLOWAT     = 0x1004, // receive low-water mark
    INET_SO_SNDTIMEO     = 0x1005, // send timeout
    INET_SO_RCVTIMEO     = 0x1006, // receive timeout
    INET_SO_ERROR        = 0x1007, // get error status and clear
    INET_SO_TYPE         = 0x1008, // get socket type
    INET_SO_OVERFLOWED   = 0x1009, // datagrams: return packets dropped
    INET_SO_NONBLOCK     = 0x1009, // non-blocking I/O
};

enum PspInetLimit {
    PSP_NET_INET_SOMAXCONN = 128,
};

enum PspInetMessageFlag {
    INET_MSG_OOB = 1,
    INET_MSG_PEEK = 1 << 1,
    INET_MSG_DONTROUTE = 1 << 2,
    INET_MSG_EOR = 1 << 3,
    INET_MSG_TRUNC = 1 << 4,
    INET_MSG_CTRUNC = 1 << 5,
    INET_MSG_WAITALL = 1 << 6,
    INET_MSG_DONTWAIT = 1 << 7,
    INET_MSG_BCAST = 1 << 8,
    INET_MSG_MCAST = 1 << 9
};

enum InetErrorCode {
	INET_EAGAIN = 0x0B,
	INET_ETIMEDOUT = 0x74,
    INET_EINPROGRESS = 119,
	INET_EISCONN = 0x7F,
};

class SceNetInet {
public:
    static bool Init();
    static bool Shutdown();
    static std::shared_ptr<SceNetInet> Get() {
        return gInstance;
    }

	static bool Inited() {
		return gInstance.get() != nullptr;
	}

    // TODO: document that errno should be set to EAFNOSUPPORT when this returns false
    static bool TranslateInetAddressFamilyToNative(int &destAddressFamily, int srcAddressFamily);
    static inline bool TranslateInetSocketLevelToNative(int &destSocketLevel, int srcSocketLevel);
    // TODO: document that errno should be set to ESOMETHING when this returns false
    static bool TranslateInetSocketTypeToNative(int &destSocketType, bool &destNonBlocking, int srcSocketType);
    // TODO: document that errno should be set to EPROTONOSUPPORT when this returns false
    static bool TranslateInetProtocolToNative(int &destProtocol, int srcProtocol);
    // TODO: document that errno should be set to EPROTONOSUPPORT when this returns false
    static bool TranslateInetOptnameToNativeOptname(int &destOptname, int inetOptname);
    static int TranslateInetFlagsToNativeFlags(int messageFlags, bool nonBlocking);
    static int TranslateNativeErrorToInetError(int nativeError);

    // TODO: document

    int GetLastError();
    void SetLastError(int error);
    int SetLastErrorToMatchPlatform();
    std::shared_ptr<InetSocket> CreateAndAssociateInetSocket(int nativeSocketId, int protocol, bool nonBlocking);
    std::shared_ptr<InetSocket> GetInetSocket(int inetSocketId);
    bool GetNativeSocketIdForInetSocketId(int &nativeSocketId, int inetSocketId);
    bool EraseNativeSocket(int inetSocketId);
    bool TranslateInetFdSetToNativeFdSet(int& maxFd, fd_set &destFdSet, u32 fdsPtr) const;

private:
    void CloseAllRemainingSockets() const;

    static std::shared_ptr<SceNetInet> gInstance;
    static std::shared_mutex gLock;
    static std::unordered_map<PspInetAddressFamily, int> gInetAddressFamilyToNativeAddressFamily;
    // TODO: document that this does not include flags
    static std::unordered_map<PspInetSocketType, int> gInetSocketTypeToNativeSocketType;
    static std::unordered_map<PspInetProtocol, int> gInetProtocolToNativeProtocol;
    // TODO: Handle commented out options
    static std::unordered_map<PspInetSocketOptionName, int> gInetSocketOptnameToNativeOptname;
    static std::unordered_map<PspInetMessageFlag, int> gInetMessageFlagToNativeMessageFlag;

    static std::unordered_map<int, InetErrorCode> gNativeErrorCodeToInetErrorCode;

    int mLastError = 0;
    std::unordered_map<int, std::shared_ptr<InetSocket>> mInetSocketIdToNativeSocket;
    int mCurrentInetSocketId = 0;
    std::shared_mutex mLock;
};

void Register_sceNetInet();
