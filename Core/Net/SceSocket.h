#pragma once

#include "ppsspp_config.h"

#include <unordered_map>
#include <unordered_set>

#if !PPSSPP_PLATFORM(WINDOWS)
#include <sys/socket.h>
#endif

#include "Log.h"

enum Protocol {
    INET_PROTOCOL_UDP = SOCK_DGRAM,
    INET_PROTOCOL_TCP = SOCK_STREAM
};

// TODO: INET_
enum InetSocketOptionName {
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

// TODO: Handle commented out options
static std::unordered_map<InetSocketOptionName, int> gInetSocketOptnameToNativeOptname = {
    { INET_SO_ACCEPTCONN, SO_ACCEPTCONN },
    { INET_SO_REUSEADDR, SO_REUSEADDR },
    { INET_SO_KEEPALIVE, SO_KEEPALIVE },
    { INET_SO_DONTROUTE, SO_DONTROUTE },
    { INET_SO_BROADCAST, SO_BROADCAST },
    // { INET_SO_USELOOPBACK, INET_SO_USELOOPBACK },
    { INET_SO_LINGER, SO_LINGER },
    { INET_SO_OOBINLINE, SO_OOBINLINE },
    { INET_SO_REUSEPORT, SO_REUSEPORT },
    { INET_SO_TIMESTAMP, SO_TIMESTAMP },
    // { INET_SO_ONESBCAST, INET_SO_ONESBCAST },
    { INET_SO_SNDBUF, SO_SNDBUF },
    { INET_SO_RCVBUF, SO_RCVBUF },
    { INET_SO_SNDLOWAT, SO_SNDLOWAT },
    { INET_SO_RCVLOWAT, SO_RCVLOWAT },
    { INET_SO_SNDTIMEO, SO_SNDTIMEO },
    { INET_SO_RCVTIMEO, SO_RCVTIMEO },
    { INET_SO_ERROR, SO_ERROR },
    { INET_SO_TYPE, SO_TYPE },
    // { INET_SO_OVERFLOWED, INET_SO_OVERFLOWED },
    { INET_SO_NONBLOCK, INET_SO_NONBLOCK },
};

enum InetLimit {
    PSP_NET_INET_SOMAXCONN = 128,
};

enum InetMessageFlag {
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

// TODO: move to better place
static std::unordered_map<InetMessageFlag, int> gInetMessageFlagToNativeMessageFlag = {
    { INET_MSG_OOB, MSG_OOB },
    { INET_MSG_PEEK, MSG_PEEK },
    { INET_MSG_DONTROUTE, MSG_DONTROUTE },
#if defined(MSG_EOR)
    { INET_MSG_EOR, MSG_EOR },
#endif
    { INET_MSG_TRUNC, MSG_TRUNC },
    { INET_MSG_CTRUNC, MSG_CTRUNC },
    { INET_MSG_WAITALL, MSG_WAITALL },
    { INET_MSG_DONTWAIT, MSG_DONTWAIT },
#if defined(MSG_BCAST)
    { INET_MSG_BCAST, MSG_BCAST },
#endif
#if defined(MSG_MCAST)
    { INET_MSG_MCAST, MSG_MCAST },
#endif
};

enum InetErrorCode {
    INET_EINPROGRESS = 119,
};

static std::unordered_map<int, InetErrorCode> gNativeErrorCodeToInetErrorCode = {
    { EINPROGRESS, INET_EINPROGRESS }
};

// TODO: document
class SceSocket {
public:
    SceSocket(int sceSocketId, int nativeSocketId) : mSceSocketId(sceSocketId), mNativeSocketId(nativeSocketId) {}

    int GetSceSocketId() const {
        return mSceSocketId;
    }

    int GetNativeSocketId() const {
        return mNativeSocketId;
    }

    // TODO: get mask of options

    bool IsNonBlocking() const {
        return mNonBlocking;
    }

    void SetNonBlocking(const bool nonBlocking) {
        mNonBlocking = nonBlocking;
    }

    Protocol GetProtocol() const {
        return mProtocol;
    }

    void SetProtocol(const Protocol protocol) {
        mProtocol = protocol;
    }

    // TODO: Move me
    static bool IsSockoptNameAllowed(const int optname) {
        return gInetSocketOptnameToNativeOptname.find(static_cast<InetSocketOptionName>(optname)) != gInetSocketOptnameToNativeOptname.end();
    }

    // TODO: rename this or the other
    static int TranslateInetOptnameToNativeOptname(const InetSocketOptionName inetOptname) {
        const auto it = gInetSocketOptnameToNativeOptname.find(inetOptname);
        if (it == gInetSocketOptnameToNativeOptname.end()) {
            return inetOptname;
        }
        return it->second;
    }

    int TranslateInetFlagsToNativeFlags(const int messageFlags) const {
        int nativeFlags = 0; // The actual platform flags
        int foundFlags = 0; // The inet equivalent of the native flags, used to verify that no remaining flags need to be set
        for (const auto [inetFlag, nativeFlag] : gInetMessageFlagToNativeMessageFlag) {
            if ((messageFlags & inetFlag) != 0) {
                nativeFlags |= nativeFlag;
                foundFlags |= inetFlag;
            }
        }

#if !PPSSPP_PLATFORM(WINDOWS)
        if (this->IsNonBlocking()) {
            nativeFlags |= MSG_DONTWAIT;
        }
#endif

        // Check for any inet flags which were not successfully mapped into a native flag
        if (const int missingFlags = messageFlags & ~foundFlags; missingFlags != 0) {
            for (int i = 0; i < sizeof(int) * 8; i++) {
                if (const int val = 1 << i; (missingFlags & val) != 0) {
                    DEBUG_LOG(Log::sceNet, "Encountered unsupported platform flag at bit %i (actual value %04x), undefined behavior may ensue.", i, val);
                }
            }
        }
        // DEBUG_LOG(SCENET, "Translated %04x to %04x", messageFlags, nativeFlags);
        return nativeFlags;
    }

    static int TranslateNativeErrorToInetError(const int nativeError) {
        if (const auto it = gNativeErrorCodeToInetErrorCode.find(nativeError);
            it != gNativeErrorCodeToInetErrorCode.end()) {
            return it->second;
        }
        return nativeError;
    }

private:
    int mSceSocketId;
    int mNativeSocketId;
    Protocol mProtocol;
    bool mNonBlocking = false;
};
