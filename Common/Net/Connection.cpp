#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <string>

#include "Common/File/Path.h"

#include "Common/TimeUtil.h"
#include "Common/Log.h"
#include "Common/Net/SocketCompat.h"
#include "Common/Buffer.h"
#include "Common/File/FileDescriptor.h"
#include "Common/SysError.h"
#include "Common/Net/Connection.h"

namespace net {

Connection::~Connection() {
	Disconnect();
	if (resolved_ != nullptr)
		DNSResolveFree(resolved_);
}

// For whatever crazy reason, htons isn't available on android x86 on the build server. so here we go.

// TODO: Fix for big-endian
inline unsigned short myhtons(unsigned short x) {
	return (x >> 8) | (x << 8);
}

const char *DNSTypeAsString(DNSType type) {
	switch (type) {
	case DNSType::IPV4:
		return "IPV4";
	case DNSType::IPV6:
		return "IPV6";
	case DNSType::ANY:
		return "ANY";
	default:
		return "N/A";
	}
}

bool Connection::Resolve(const char *host, int port, DNSType type) {
	if ((intptr_t)sock_ != -1) {
		ERROR_LOG(Log::IO, "Resolve: Already have a socket");
		return false;
	}
	if (!host || port < 1 || port > 65535) {
		ERROR_LOG(Log::IO, "Resolve: Invalid host or port (%d)", port);
		return false;
	}

	host_ = host;
	port_ = port;

	char port_str[16];
	snprintf(port_str, sizeof(port_str), "%d", port);

	std::string processedHostname(host);

	if (customResolve_) {
		processedHostname = customResolve_(host);
	}

	std::string err;
	if (!net::DNSResolve(processedHostname.c_str(), port_str, &resolved_, err, type)) {
		WARN_LOG(Log::IO, "Failed to resolve host '%s': '%s' (%s)", host, err.c_str(), DNSTypeAsString(type));
		// Zero port so that future calls fail.
		port_ = 0;
		return false;
	}

	return true;
}

static void FormatAddr(char *addrbuf, size_t bufsize, const addrinfo *info) {
	switch (info->ai_family) {
	case AF_INET:
	case AF_INET6:
		inet_ntop(info->ai_family, &((sockaddr_in *)info->ai_addr)->sin_addr, addrbuf, bufsize);
		break;
	default:
		snprintf(addrbuf, bufsize, "(Unknown AF %d)", info->ai_family);
		break;
	}
}

bool Connection::Connect(int maxTries, double timeout, bool *cancelConnect) {
	if (port_ <= 0) {
		ERROR_LOG(Log::IO, "Bad port");
		return false;
	}
	sock_ = -1;

	for (int tries = maxTries; tries > 0; --tries) {
		std::vector<uintptr_t> sockets;
		fd_set fds;
		int maxfd = 1;
		FD_ZERO(&fds);
		for (addrinfo *possible = resolved_; possible != nullptr; possible = possible->ai_next) {
			if (possible->ai_family != AF_INET && possible->ai_family != AF_INET6)
				continue;

			int sock = socket(possible->ai_family, SOCK_STREAM, IPPROTO_TCP);
			if ((intptr_t)sock == -1) {
				ERROR_LOG(Log::IO, "Bad socket");
				continue;
			}
			// Windows sockets aren't limited by socket number, just by count, so checking FD_SETSIZE there is wrong.
#if !PPSSPP_PLATFORM(WINDOWS)
			if (sock >= FD_SETSIZE) {
				ERROR_LOG(Log::IO, "Socket doesn't fit in FD_SET: %d   We probably have a leak.", sock);
				closesocket(sock);
				continue;
			}
#endif
			fd_util::SetNonBlocking(sock, true);

			// Start trying to connect (async with timeout.)
			errno = 0;
			if (connect(sock, possible->ai_addr, (int)possible->ai_addrlen) < 0) {
				int errorCode = socket_errno;
				std::string errorString = GetStringErrorMsg(errorCode);
				bool unreachable = errorCode == ENETUNREACH;
				bool inProgress = errorCode == EINPROGRESS || errorCode == EWOULDBLOCK;
				if (!inProgress) {
					char addrStr[128]{};
					FormatAddr(addrStr, sizeof(addrStr), possible);
					if (!unreachable) {
						ERROR_LOG(Log::HTTP, "connect(%d) call to %s failed (%d: %s)", sock, addrStr, errorCode, errorString.c_str());
					} else {
						INFO_LOG(Log::HTTP, "connect(%d): Ignoring unreachable resolved address %s", sock, addrStr);
					}
					closesocket(sock);
					continue;
				}
			}
			sockets.push_back(sock);
			FD_SET(sock, &fds);
			if (maxfd < sock + 1) {
				maxfd = sock + 1;
			}
		}

		int selectResult = 0;
		long timeoutHalfSeconds = floor(2 * timeout);
		while (timeoutHalfSeconds >= 0 && selectResult == 0) {
			struct timeval tv {};
			tv.tv_sec = 0;
			if (timeoutHalfSeconds > 0) {
				// Wait up to 0.5 seconds between cancel checks.
				tv.tv_usec = 500000;
			} else {
				// Wait the remaining <= 0.5 seconds.  Possibly 0, but that's okay.
				tv.tv_usec = (timeout - floor(2 * timeout) / 2) * 1000000.0;
			}
			--timeoutHalfSeconds;

			selectResult = select(maxfd, nullptr, &fds, nullptr, &tv);
			if (cancelConnect && *cancelConnect) {
				WARN_LOG(Log::HTTP, "connect: cancelled (1): %s:%d", host_.c_str(), port_);
				break;
			}
		}
		if (selectResult > 0) {
			// Something connected.  Pick the first one that did (if multiple.)
			for (int sock : sockets) {
				if ((intptr_t)sock_ == -1 && FD_ISSET(sock, &fds)) {
					sock_ = sock;
				} else {
					closesocket(sock);
				}
			}

			// Great, now we're good to go.
			return true;
		} else {
			// Fail. Close all the sockets.
			for (int sock : sockets) {
				closesocket(sock);
			}
		}

		if (cancelConnect && *cancelConnect) {
			WARN_LOG(Log::HTTP, "connect: cancelled (2): %s:%d", host_.c_str(), port_);
			break;
		}

		sleep_ms(1, "connect");
	}

	// Nothing connected, unfortunately.
	return false;
}

void Connection::Disconnect() {
	if ((intptr_t)sock_ != -1) {
		closesocket(sock_);
		sock_ = -1;
	}
}

}	// net
