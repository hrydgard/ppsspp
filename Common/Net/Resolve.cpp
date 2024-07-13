#include "ppsspp_config.h"
#include "Common/Net/Resolve.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>


#ifdef _WIN32
#include <WinSock2.h>
#include <Ws2tcpip.h>
#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG 0x0400
#endif
#undef min
#undef max
#else
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <unistd.h>
#endif

#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/Data/Encoding/Utf8.h"

#ifndef HTTPS_NOT_AVAILABLE
#include "ext/naett/naett.h"
#endif

#if PPSSPP_PLATFORM(ANDROID)
#include <jni.h>
extern JavaVM *gJvm;
#endif

namespace net {

static bool g_naettInitialized;

void Init()
{
#ifdef _WIN32
	// WSA does its own internal reference counting, no need to keep track of if we inited or not.
	WSADATA wsaData = {0};
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
	if (!g_naettInitialized) {
#ifndef HTTPS_NOT_AVAILABLE
#if PPSSPP_PLATFORM(ANDROID)
		_assert_(gJvm != nullptr);
		naettInit(gJvm);
#else
		naettInit(NULL);
#endif
#endif
		g_naettInitialized = true;
	}
}

void Shutdown()
{
#ifdef _WIN32
	WSACleanup();
#endif
}

bool DNSResolve(const std::string &host, const std::string &service, addrinfo **res, std::string &error, DNSType type) {
#if PPSSPP_PLATFORM(SWITCH)
	// Force IPv4 lookups.
	if (type == DNSType::IPV6) {
		*res = nullptr;
		return false;
	} else if (type == DNSType::ANY) {
		type = DNSType::IPV4;
	}
#endif

	addrinfo hints = {0};
	// TODO: Might be uses to lookup other values.
	hints.ai_socktype = SOCK_STREAM;
#ifdef __ANDROID__
	hints.ai_flags = AI_ADDRCONFIG;
#else
	// AI_V4MAPPED seems to have issues on some platforms, not sure we should include it:
	// http://stackoverflow.com/questions/1408030/what-is-the-purpose-of-the-ai-v4mapped-flag-in-getaddrinfo
	hints.ai_flags = /*AI_V4MAPPED |*/ AI_ADDRCONFIG;
#endif
	hints.ai_protocol = 0;
	if (type == DNSType::IPV4)
		hints.ai_family = AF_INET;
	else if (type == DNSType::IPV6)
		hints.ai_family = AF_INET6;

	const char *servicep = service.length() == 0 ? nullptr : service.c_str();

	*res = nullptr;
	int result = getaddrinfo(host.c_str(), servicep, &hints, res);
	if (result == EAI_AGAIN) {
		// Temporary failure.  Since this already blocks, let's just try once more.
		sleep_ms(1);
		result = getaddrinfo(host.c_str(), servicep, &hints, res);
	}

	if (result != 0) {
#ifdef _WIN32
		error = ConvertWStringToUTF8(gai_strerror(result));
#else
		error = gai_strerror(result);
#endif
		if (*res != nullptr)
			freeaddrinfo(*res);
		*res = nullptr;
		return false;
	}

	return true;
}

void DNSResolveFree(addrinfo *res)
{
	if (res != NULL)
		freeaddrinfo(res);
}

bool GetIPList(std::vector<std::string> &IP4s) {
	char ipstr[INET6_ADDRSTRLEN]; // We use IPv6 length since it's longer than IPv4
// getifaddrs first appeared in glibc 2.3, On Android officially supported since __ANDROID_API__ >= 24
#if defined(_IFADDRS_H_) || (__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 3) || (__ANDROID_API__ >= 24)
	INFO_LOG(Log::sceNet, "GetIPList from getifaddrs");
	struct ifaddrs* ifAddrStruct = NULL;
	struct ifaddrs* ifa = NULL;

	getifaddrs(&ifAddrStruct);
	if (ifAddrStruct != NULL) {
		for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
			if (!ifa->ifa_addr) {
				continue;
			}
			if (ifa->ifa_addr->sa_family == AF_INET) {
				// is a valid IP4 Address
				if (inet_ntop(AF_INET, &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr, ipstr, sizeof(ipstr)) != 0) {
					IP4s.push_back(ipstr);
				}
			}
			/*else if (ifa->ifa_addr->sa_family == AF_INET6) {
				// is a valid IP6 Address
				if (inet_ntop(AF_INET6, &((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr, ipstr, sizeof(ipstr)) != 0) {
					IP6s.push_back(ipstr);
				}
			}*/
		}
		
		freeifaddrs(ifAddrStruct);
		return true;
	}
#elif defined(SIOCGIFCONF) // Better detection on Linux/UNIX/MacOS/some Android
	INFO_LOG(Log::sceNet, "GetIPList from SIOCGIFCONF");
	static struct ifreq ifreqs[32];
	struct ifconf ifc;
	memset(&ifc, 0, sizeof(ifconf));
	ifc.ifc_req = ifreqs;
	ifc.ifc_len = sizeof(ifreqs);

	int sd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sd < 0) {
		ERROR_LOG(Log::sceNet, "GetIPList failed to create socket (result = %i, errno = %i)", sd, errno);
		return false;
	}

	int r = ioctl(sd, SIOCGIFCONF, (char*)&ifc);
	if (r != 0) {
		ERROR_LOG(Log::sceNet, "GetIPList failed ioctl/SIOCGIFCONF (result = %i, errno = %i)", r, errno);
		return false;
	}

	struct ifreq* item;
	struct sockaddr* addr;

	for (int i = 0; i < ifc.ifc_len / sizeof(struct ifreq); ++i)
	{
		item = &ifreqs[i];
		addr = &(item->ifr_addr);

		// Get the IP address
		r = ioctl(sd, SIOCGIFADDR, item);
		if (r != 0)
		{
			ERROR_LOG(Log::sceNet, "GetIPList failed ioctl/SIOCGIFADDR (i = %i, result = %i, errno = %i)", i, r, errno);
		}

		if (ifreqs[i].ifr_addr.sa_family == AF_INET) {
			// is a valid IP4 Address
			if (inet_ntop(AF_INET, &((struct sockaddr_in*)addr)->sin_addr, ipstr, sizeof(ipstr)) != 0) {
				IP4s.push_back(ipstr);
			}
		}
		/*else if (ifreqs[i].ifr_addr.sa_family == AF_INET6) {
			// is a valid IP6 Address
			if (inet_ntop(AF_INET6, &((struct sockaddr_in6*)addr)->sin6_addr, ipstr, sizeof(ipstr)) != 0) {
				IP6s.push_back(ipstr);
			}
		}*/
	}

	close(sd);
	return true;
#else // Fallback to POSIX/Cross-platform way but may not works well on Linux (ie. only shows 127.0.0.1)
	INFO_LOG(Log::sceNet, "GetIPList from Fallback");
	struct addrinfo hints, * res, * p;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // AF_INET or AF_INET6 to force version
	hints.ai_socktype = SOCK_DGRAM;

	// Get local host name
	char szHostName[256] = "";
	if (::gethostname(szHostName, sizeof(szHostName))) {
		// Error handling 
	}

	int status;
	if ((status = getaddrinfo(szHostName, NULL, &hints, &res)) != 0) {
		//fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
		return false;
	}
	for (p = res; p != NULL; p = p->ai_next) {
		if (p->ai_family == AF_INET) {
			// is a valid IP4 Address
			if (inet_ntop(p->ai_family, &(((struct sockaddr_in*)p->ai_addr)->sin_addr), ipstr, sizeof(ipstr)) != 0) {
				IP4s.push_back(ipstr);
			}
		}
		/*else if (p->ai_family == AF_INET6) {
			// is a valid IP6 Address
			if (inet_ntop(p->ai_family, &(((struct sockaddr_in6*)p->ai_addr)->sin6_addr), ipstr, sizeof(ipstr)) != 0) {
				IP6s.push_back(ipstr);
			}
		}*/
	}

	freeaddrinfo(res); // free the linked list
	return true;
#endif
	return false;
}

int inet_pton(int af, const char* src, void* dst)
{
	if (af == AF_INET)
	{
		unsigned char *ip = (unsigned char *)dst;
		int k = 0, x = 0;
		char ch;
		for (int i = 0; (ch = src[i]) != 0; i++)
		{
			if (ch == '.')
			{
				ip[k++] = x;
				if (k == 4)
					return 0;
				x = 0;
			}
			else if (ch < '0' || ch > '9')
				return 0;
			else
				x = x * 10 + ch - '0';
			if (x > 255)
				return 0;
		}
		ip[k++] = x;
		if (k != 4)
			return 0;
	}
	else if (af == AF_INET6)
	{
		unsigned short* ip = ( unsigned short* )dst;
		int i;
		for (i = 0; i < 8; i++) ip[i] = 0;
		int k = 0;
		unsigned int x = 0;
		char ch;
		int marknum = 0;
		for (i = 0; src[i] != 0; i++)
		{
			if (src[i] == ':')
				marknum++;
		}
		for (i = 0; (ch = src[i]) != 0; i++)
		{
			if (ch == ':')
			{
				x = ((x & 0xFF00) >> 8) | ((x & 0x00FF) << 8);
				ip[k++] = x;
				if (k == 8)
					return 0;
				x = 0;
				if (i > 0 && src[i - 1] == ':')
					k += 7 - marknum;
			}
			else if (ch >= '0' && ch <= '9')
				x = x * 16 + ch - '0';
			else if (ch >= 'a' && ch <= 'f')
				x = x * 16 + ch - 'a' + 10;
			else if (ch >= 'A' && ch <= 'F')
				x = x * 16 + ch - 'A' + 10;
			else
				return 0;
			if (x > 0xFFFF)
				return 0;
		}
		x = ((x & 0xFF00) >> 8) | ((x & 0x00FF) << 8);
		ip[k++] = x;
		if (k != 8)
			return 0;
	}
	return 1;
}

}
