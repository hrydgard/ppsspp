#pragma once

#include <string>

struct addrinfo;

namespace net {

// Strictly only required on Win32, but all platforms should call it.
void Init();
void Shutdown();

// use free() to free the returned string.
char *DNSResolveTry(const char *host, const char **err);
char *DNSResolve(const char *host);

bool DNSResolve(const std::string &host, const std::string &service, addrinfo **res, std::string &error);
void DNSResolveFree(addrinfo *res);

int inet_pton(int af, const char* src, void* dst);
}  // namespace net
