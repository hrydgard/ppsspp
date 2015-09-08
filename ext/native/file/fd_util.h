#ifndef _FD_UTIL
#define _FD_UTIL

#include <string.h>
#include <string>

#include "base/basictypes.h"

namespace fd_util {

// Slow as hell and should only be used for prototyping.
ssize_t ReadLine(int fd, char *buffer, size_t buf_size);

// Decently fast.
ssize_t WriteLine(int fd, const char *buffer, size_t buf_size);
ssize_t WriteLine(int fd, const char *buffer);
ssize_t Write(int fd, const std::string &str);

// Returns true if the fd became ready, false if it didn't or
// if there was another error.
bool WaitUntilReady(int fd, double timeout);

void SetNonBlocking(int fd, bool non_blocking);

}  // fd_util

#endif  // _FD_UTIL
