// TODO: license

// TODO: fixme move Core/Net to Common/Net
#include "Common/Net/Resolve.h"
#include "Common/Net/SocketCompat.h"
#include "Core/Net/InetCommon.h"
#include "Core/Config.h"
#include "Core/HLE/HLE.h"
#include "Core/MemMapHelpers.h"

#include "Core/HLE/sceNp.h"

#if PPSSPP_PLATFORM(WINDOWS)
#define close closesocket
#endif

bool getDefaultOutboundSockaddr(sockaddr_in& destSockaddrIn, socklen_t& destSocklen) {
    auto fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        ERROR_LOG(Log::sceNet, "getSockAddrFromDefaultSocket: Failed to open socket (%s)", strerror(socket_errno));
        return false;
    }
    sockaddr_in connectingTo;
    memset(&connectingTo, 0, sizeof(connectingTo));
    connectingTo.sin_family = AF_INET;
    connectingTo.sin_port = htons(53);
    connectingTo.sin_addr.s_addr = 0x08080808;
    if (connect(fd, (sockaddr*) &connectingTo, sizeof(connectingTo)) < 0) {
        ERROR_LOG(Log::sceNet, "getSockAddrFromDefaultSocket: Failed to connect to Google (%s)", strerror(socket_errno));
        close(fd);
        return false;
    }
    if (getsockname(fd, (sockaddr*) &destSockaddrIn, &destSocklen) < 0) {
        ERROR_LOG(Log::sceNet, "getSockAddrFromDefaultSocket: Failed to execute getsockname (%s)", strerror(socket_errno));
        close(fd);
        return false;
    }
    close(fd);
    return true;
}
