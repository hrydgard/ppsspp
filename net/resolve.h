#ifndef _NET_RESOLVE_H
#define _NET_RESOLVE_H

namespace net {

// Required on Win32
void Init();

// use free() to free the returned string.
char *DNSResolve(const char *host);
}  // namespace net
#endif
