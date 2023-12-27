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

class SceNetInet {
public:
    static bool Init();
    static bool Shutdown();
    static std::shared_ptr<SceNetInet> Get() {
        return gInstance;
    }

    // TODO: document that errno should be set to EAFNOSUPPORT when this returns false
    static bool TranslateInetAddressFamilyToNative(int &destAddressFamily, int srcAddressFamily);
    static inline bool TranslateInetSocketLevelToNative(int &destSocketLevel, int srcSocketLevel);
    // TODO: document that errno should be set to ESOMETHING when this returns false
    static bool TranslateInetSocketTypeToNative(int &destSocketType, bool &destNonBlocking, int srcSocketType);
    // TODO: document that errno should be set to EPROTONOSUPPORT when this returns false
    static bool TranslateInetProtocolToNative(int &destProtocol, int srcProtocol);

    // TODO: document

    int GetLastError();
    void SetLastError(int error);
    int SetLastErrorToMatchPlatform();
    std::shared_ptr<InetSocket> CreateAndAssociateInetSocket(int nativeSocketId, bool nonBlocking);
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

    int mLastError = 0;
    std::unordered_map<int, std::shared_ptr<InetSocket>> mInetSocketIdToNativeSocket;
    int mCurrentInetSocketId = 0;
    std::shared_mutex mLock;
};

void Register_sceNetInet();
