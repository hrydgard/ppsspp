#pragma once

#include <cstring>
#include <string>

namespace fd_util {

// Slow as hell and should only be used for prototyping.
size_t ReadLine(int fd, char *buffer, size_t buf_size);

// Decently fast.
size_t WriteLine(int fd, const char *buffer, size_t buf_size);
size_t WriteLine(int fd, const char *buffer);
size_t Write(int fd, const std::string &str);

// Returns true if the fd became ready, false if it didn't or
// if there was another error.
bool WaitUntilReady(int fd, double timeout, bool for_write = false);

void SetNonBlocking(int fd, bool non_blocking);

std::string GetLocalIP(int sock);

}  // fd_util
