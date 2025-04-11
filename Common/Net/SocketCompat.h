#pragma once

#include "ppsspp_config.h"

#include "Common/CommonTypes.h"

#if PPSSPP_PLATFORM(WINDOWS)
#include "Common/CommonWindows.h"
#include <io.h>
#include <winsock2.h>
#include <WS2tcpip.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#if !PPSSPP_PLATFORM(SWITCH)
#include <sys/mman.h>
#endif
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#if !PPSSPP_PLATFORM(SWITCH)
#include <ifaddrs.h>
#endif
#include <fcntl.h>
#endif
#include <fcntl.h>
#include <errno.h>

// TODO: move this to some common set
#if PPSSPP_PLATFORM(WINDOWS)
#undef ESHUTDOWN
#undef ECONNABORTED
#undef ECONNRESET
#undef ECONNREFUSED
#undef ENETUNREACH
#undef ENOTCONN
#undef EBADF
#undef EAGAIN
#undef EINPROGRESS
#undef EISCONN
#undef EALREADY
#undef ETIMEDOUT
#undef EOPNOTSUPP
#undef ENOTSOCK
#undef EPROTONOSUPPORT
#undef ESOCKTNOSUPPORT
#undef EPFNOSUPPORT
#undef EAFNOSUPPORT
#undef EINTR
#undef EACCES
#undef EFAULT
#undef EINVAL
#undef ENOSPC
#undef EHOSTDOWN
#undef EADDRINUSE
#undef EADDRNOTAVAIL
#undef ENETUNREACH
#undef EHOSTUNREACH
#undef ENETDOWN
#define socket_errno WSAGetLastError()
#define ESHUTDOWN WSAESHUTDOWN
#define ECONNABORTED WSAECONNABORTED
#define ECONNRESET WSAECONNRESET
#define ECONNREFUSED WSAECONNREFUSED
#define ENETUNREACH WSAENETUNREACH
#define ENOTCONN WSAENOTCONN
#define EBADF WSAEBADF
#define EAGAIN WSAEWOULDBLOCK
#define EINPROGRESS WSAEWOULDBLOCK
#define EISCONN WSAEISCONN
#define EALREADY WSAEALREADY
#define ETIMEDOUT WSAETIMEDOUT
#define EOPNOTSUPP WSAEOPNOTSUPP
#define ENOTSOCK WSAENOTSOCK
#define EPROTONOSUPPORT WSAEPROTONOSUPPORT
#define ESOCKTNOSUPPORT WSAESOCKTNOSUPPORT
#define EPFNOSUPPORT WSAEPFNOSUPPORT
#define EAFNOSUPPORT WSAEAFNOSUPPORT
#define EINTR WSAEINTR
#define EACCES WSAEACCES
#define EFAULT WSAEFAULT
#define EINVAL WSAEINVAL
#define ENOSPC ERROR_INVALID_PARAMETER
#define EHOSTDOWN WSAEHOSTDOWN
#define EADDRINUSE WSAEADDRINUSE
#define EADDRNOTAVAIL WSAEADDRNOTAVAIL
#define ENETUNREACH WSAENETUNREACH
#define EHOSTUNREACH WSAEHOSTUNREACH
#define ENETDOWN WSAENETDOWN
inline bool connectInProgress(int errcode) { return (errcode == WSAEWOULDBLOCK || errcode == WSAEINPROGRESS || errcode == WSAEALREADY || errcode == WSAEINVAL); } // WSAEINVAL should be treated as WSAEALREADY during connect for backward-compatibility with Winsock 1.1
inline bool isDisconnected(int errcode) { return (errcode == WSAECONNRESET || errcode == WSAECONNABORTED || errcode == WSAESHUTDOWN); }
#else
#define socket_errno errno
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#ifndef ESHUTDOWN
#define ESHUTDOWN ENETDOWN
#endif
inline bool connectInProgress(int errcode) { return (errcode == EAGAIN || errcode == EWOULDBLOCK || errcode == EINPROGRESS || errcode == EALREADY); }
inline bool isDisconnected(int errcode) { return (errcode == EPIPE || errcode == ECONNRESET || errcode == ECONNABORTED || errcode == ESHUTDOWN); }
#endif

#ifndef SD_RECEIVE
#define SD_RECEIVE SHUT_RD //0x00
#endif

#ifndef SD_BOTH
#define SD_BOTH SHUT_RDWR //0x02
#endif

#ifndef MSG_NOSIGNAL
// Default value to 0x00 (do nothing) in systems where it's not supported.
#define MSG_NOSIGNAL 0x00
#endif
