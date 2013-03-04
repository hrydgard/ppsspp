#ifndef _NET_RESOLVE_H
#define _NET_RESOLVE_H

namespace net {

// Strictly only required on Win32, but all platforms should call it.
void Init();
void Shutdown();

// use free() to free the returned string.
char *DNSResolveTry(const char *host, const char **err);
char *DNSResolve(const char *host);

int inet_pton(int af, const char* src, void* dst);
}  // namespace net
#endif
