#pragma once

#include "Core/HLE/HLE.h"

#if PPSSPP_PLATFORM(WINDOWS)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#endif

bool getDefaultOutboundSockaddr(sockaddr_in& destSockaddrIn, socklen_t& destSocklen);
