#include "file/fd_util.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#else
#include <io.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include <fcntl.h>

#include "base/logging.h"

namespace fd_util {

// Slow as hell and should only be used for prototyping.
// Reads from a socket, up to an '\n'. This means that if the line ends
// with '\r', the '\r' will be returned.
ssize_t ReadLine(int fd, char *vptr, size_t buf_size) {
  char *buffer = vptr;
  size_t n;
  for (n = 1; n < buf_size; n++) {
    char c;
    ssize_t rc;
    if ((rc = read(fd, &c, 1)) == 1) {
      *buffer++ = c;
      if (c == '\n')
        break;
    }
    else if (rc == 0) {
      if (n == 1)
        return 0;
      else
        break;
    }
    else {
      if (errno == EINTR)
        continue;
      FLOG("Error in Readline()");
    }
  }

  *buffer = 0;
  return n;
}

// Misnamed, it just writes raw data in a retry loop.
ssize_t WriteLine(int fd, const char *vptr, size_t n) {
  const char *buffer = vptr;
  size_t nleft = n;

  while (nleft > 0) {
    int nwritten;
    if ((nwritten = (int)write(fd, buffer, (unsigned int)nleft)) <= 0) {
      if (errno == EINTR)
        nwritten = 0;
      else
        FLOG("Error in Writeline()");
    }
    nleft  -= nwritten;
    buffer += nwritten;
  }

  return n;
}

ssize_t WriteLine(int fd, const char *buffer) {
  return WriteLine(fd, buffer, strlen(buffer));
}

ssize_t Write(int fd, const std::string &str) {
  return WriteLine(fd, str.c_str(), str.size());
}

bool WaitUntilReady(int fd, double timeout, bool for_write) {
  struct timeval tv;
  tv.tv_sec = floor(timeout);
  tv.tv_usec = (timeout - floor(timeout)) * 1000000.0;

  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(fd, &fds);
  // First argument to select is the highest socket in the set + 1.
  int rval;
  if (for_write) {
	  rval = select(fd + 1, NULL, &fds, NULL, &tv);
  } else {
	  rval = select(fd + 1, &fds, NULL, NULL, &tv);
  }
  if (rval < 0) {
    // Error calling select.
    return false;
  } else if (rval == 0) {
    // Timeout.
    return false;
  } else {
    // Socket is ready.
    return true;
  }
}

void SetNonBlocking(int sock, bool non_blocking) {
#ifndef _WIN32
	int opts = fcntl(sock, F_GETFL);
  if (opts < 0) {
		perror("fcntl(F_GETFL)");
		ELOG("Error getting socket status while changing nonblocking status");
	}
	if (non_blocking) {
    opts = (opts | O_NONBLOCK);
  } else {
    opts = (opts & ~O_NONBLOCK);
  }

	if (fcntl(sock, F_SETFL, opts) < 0) {
		perror("fcntl(F_SETFL)");
		ELOG("Error setting socket nonblocking status");
	}
#else
	u_long val = non_blocking ? 1 : 0;
	if (ioctlsocket(sock, FIONBIO, &val) != 0) {
		ELOG("Error setting socket nonblocking status");
	}
#endif
}

std::string GetLocalIP(int sock) {
	union {
		struct sockaddr sa;
		struct sockaddr_in ipv4;
		struct sockaddr_in6 ipv6;
	} server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	socklen_t len = sizeof(server_addr);
	if (getsockname(sock, (struct sockaddr *)&server_addr, &len) == 0) {
		char temp[64];
		void *addr;
		if (server_addr.sa.sa_family == AF_INET6) {
			addr = &server_addr.ipv6.sin6_addr;
		} else {
			addr = &server_addr.ipv4.sin_addr;
		}
		const char *result = inet_ntop(server_addr.sa.sa_family, addr, temp, sizeof(temp));
		if (result) {
			return result;
		}
	}
	return "";
}

}  // fd_util
