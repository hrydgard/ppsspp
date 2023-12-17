// TODO: license

#if __linux__ || __APPLE__ || defined(__OpenBSD__)
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#endif

// TODO: fixme move Core/Net to Common/Net
#include "Common/Net/Resolve.h"
#include "Core/Net/InetCommon.h"
#include "Common/Data/Text/Parsers.h"

#include "Common/Serialize/Serializer.h"
#include "Core/Config.h"
#include "Core/HLE/HLE.h"
#include "Core/MemMapHelpers.h"

#include "Core/HLE/proAdhoc.h"

#include <iostream>
#include <shared_mutex>

#include "Core/HLE/sceNp.h"

#if PPSSPP_PLATFORM(WINDOWS)
#define close closesocket
#endif

bool getDefaultOutboundSockaddr(sockaddr_in& destSockaddrIn, socklen_t& destSocklen) {
    auto fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        ERROR_LOG(SCENET, "getSockAddrFromDefaultSocket: Failed to open socket (%s)", strerror(errno));
        return false;
    }
    sockaddr_in connectingTo;
    memset(&connectingTo, 0, sizeof(connectingTo));
    connectingTo.sin_family = AF_INET;
    connectingTo.sin_port = htons(53);
    connectingTo.sin_addr.s_addr = 0x08080808;
    if (connect(fd, (sockaddr*) &connectingTo, sizeof(connectingTo)) < 0) {
        ERROR_LOG(SCENET, "getSockAddrFromDefaultSocket: Failed to connect to Google (%s)", strerror(errno));
        close(fd);
        return false;
    }
    if (getsockname(fd, (sockaddr*) &destSockaddrIn, &destSocklen) < 0) {
        ERROR_LOG(SCENET, "getSockAddrFromDefaultSocket: Failed to execute getsockname (%s)", strerror(errno));
        close(fd);
        return false;
    }
    close(fd);
    return true;
}
