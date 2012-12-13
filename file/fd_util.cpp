#include "file/fd_util.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/time.h>
#ifdef __SYMBIAN32__
#include <sys/select.h>
#endif
#else
#include <io.h>
#include <winsock2.h>
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
    ssize_t nwritten;
    if ((nwritten = write(fd, buffer, nleft)) <= 0) {
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

bool WaitUntilReady(int fd, double timeout) {
  struct timeval tv;
  tv.tv_sec = floor(timeout);
  tv.tv_usec = (timeout - floor(timeout)) * 1000000.0;

  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(fd, &fds);
  // First argument to select is the highest socket in the set + 1.
  int rval = select(fd + 1, &fds, NULL, NULL, &tv);
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
		exit(EXIT_FAILURE);
	}
	if (non_blocking) {
    opts = (opts | O_NONBLOCK);
  } else {
    opts = (opts & ~O_NONBLOCK);
  }

	if (fcntl(sock, F_SETFL, opts) < 0) {
		perror("fcntl(F_SETFL)");
		exit(EXIT_FAILURE);
	}
#else
  WLOG("NonBlocking mode not supported on Win32");
#endif
}

}  // fd_util
