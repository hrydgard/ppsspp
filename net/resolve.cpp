#include "net/resolve.h"
#include "base/logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>


#ifdef _WIN32
#include <WinSock2.h>
#include <Ws2tcpip.h>
#undef min
#undef max
#else
#if defined(__FreeBSD__) || defined(__SYMBIAN32__)
#include <netinet/in.h>
#else
#include <arpa/inet.h>
#endif
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif


namespace net {

void Init()
{
#ifdef _WIN32
	// WSA does its own internal reference counting, no need to keep track of if we inited or not.
	WSADATA wsaData = {0};
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

void Shutdown()
{
#ifdef _WIN32
	WSACleanup();
#endif
}

char *DNSResolveTry(const char *host, const char **err)
{
	struct hostent *hent;
	if((hent = gethostbyname(host)) == NULL)
	{
		*err = "Can't get IP";
		return NULL;
	}
	int iplen = 15; //XXX.XXX.XXX.XXX
	char *ip = (char *)malloc(iplen+1);
	memset(ip, 0, iplen+1);
	char *iptoa = inet_ntoa(*(in_addr *)hent->h_addr_list[0]);
	if (iptoa == NULL)
	{
		*err = "Can't resolve host";
		free(ip);
		return NULL;
	}
	strncpy(ip, iptoa, iplen);
	return ip;
}

char *DNSResolve(const char *host)
{
	const char *err;
	char *ip = DNSResolveTry(host, &err);
	if (ip == NULL)
	{
		perror(err);
		exit(1);
	}
	return ip;
}

bool DNSResolve(const std::string &host, const std::string &service, addrinfo **res, std::string &error)
{
	addrinfo hints = {0};
	// TODO: Might be uses to lookup other values.
	hints.ai_socktype = SOCK_STREAM;
#ifdef BLACKBERRY
	hints.ai_flags = 0;
#elif ANDROID
	hints.ai_flags = AI_ADDRCONFIG;
#else
	// AI_V4MAPPED seems to have issues on some platforms, not sure we should include it:
	// http://stackoverflow.com/questions/1408030/what-is-the-purpose-of-the-ai-v4mapped-flag-in-getaddrinfo
	hints.ai_flags = /*AI_V4MAPPED |*/ AI_ADDRCONFIG;
#endif
	hints.ai_protocol = IPPROTO_TCP;

	const char *servicep = service.length() == 0 ? NULL : service.c_str();

	*res = NULL;
	int result = getaddrinfo(host.c_str(), servicep, &hints, res);
	if (result == EAI_AGAIN)
	{
		// Temporary failure.  Since this already blocks, let's just try once more.
#ifdef _WIN32
		Sleep(1);
#else
		sleep(1);
#endif
		result = getaddrinfo(host.c_str(), servicep, &hints, res);
	}

	if (result != 0)
	{
		error = gai_strerror(result);
		if (*res != NULL)
			freeaddrinfo(*res);
		*res = NULL;
		return false;
	}

	return true;
}

void DNSResolveFree(addrinfo *res)
{
	freeaddrinfo(res);
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
