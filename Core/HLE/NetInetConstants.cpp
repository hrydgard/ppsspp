#include "ppsspp_config.h"

#include <string>
#include "Common/Net/SocketCompat.h"
#include "Common/StringUtils.h"
#include "Core/HLE/NetInetConstants.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/HLE.h"
#ifdef __MINGW32__
#include <mswsock.h>
#endif

int convertMsgFlagPSP2Host(int flag) {
	switch (flag) {
	case PSP_NET_INET_MSG_OOB:
		return MSG_OOB;
	case PSP_NET_INET_MSG_PEEK:
		return MSG_PEEK;
	case PSP_NET_INET_MSG_DONTROUTE:
		return MSG_DONTROUTE;
#if defined(MSG_EOR)
	case PSP_NET_INET_MSG_EOR:
		return MSG_EOR;
#endif
	case PSP_NET_INET_MSG_TRUNC:
		return MSG_TRUNC;
	case PSP_NET_INET_MSG_CTRUNC:
		return MSG_CTRUNC;
	case PSP_NET_INET_MSG_WAITALL:
		return MSG_WAITALL;
#if defined(MSG_DONTWAIT)
	case PSP_NET_INET_MSG_DONTWAIT:
		return MSG_DONTWAIT;
#endif
#if defined(MSG_BCAST)
	case PSP_NET_INET_MSG_BCAST:
		return MSG_BCAST;
#endif
#if defined(MSG_MCAST)
	case PSP_NET_INET_MSG_MCAST:
		return MSG_MCAST;
#endif
	}
	return hleLogError(Log::sceNet, flag, "Unknown MSG flag");
}

int convertMsgFlagHost2PSP(int flag) {
	switch (flag) {
	case MSG_OOB:
		return PSP_NET_INET_MSG_OOB;
	case MSG_PEEK:
		return PSP_NET_INET_MSG_PEEK;
	case MSG_DONTROUTE:
		return PSP_NET_INET_MSG_DONTROUTE;
#if defined(MSG_EOR)
	case MSG_EOR:
		return PSP_NET_INET_MSG_EOR;
#endif
	case MSG_TRUNC:
		return PSP_NET_INET_MSG_TRUNC;
	case MSG_CTRUNC:
		return PSP_NET_INET_MSG_CTRUNC;
	case MSG_WAITALL:
		return PSP_NET_INET_MSG_WAITALL;
#if defined(MSG_DONTWAIT)
	case MSG_DONTWAIT:
		return PSP_NET_INET_MSG_DONTWAIT;
#endif
#if defined(MSG_BCAST)
	case MSG_BCAST:
		return PSP_NET_INET_MSG_BCAST;
#endif
#if defined(MSG_MCAST)
	case MSG_MCAST:
		return PSP_NET_INET_MSG_MCAST;
#endif
	}
	return hleLogError(Log::sceNet, flag, "Unknown MSG flag");
}

int convertMSGFlagsPSP2Host(int flags) {
	// Only takes compatible one
	int flgs = 0;
	if (flags & PSP_NET_INET_MSG_OOB) {
		flgs |= MSG_OOB;
	}
	if (flags & PSP_NET_INET_MSG_PEEK) {
		flgs |= MSG_PEEK;
	}
	if (flags & PSP_NET_INET_MSG_DONTROUTE) {
		flgs |= MSG_DONTROUTE;
	}
#if defined(MSG_EOR)
	if (flags & PSP_NET_INET_MSG_EOR) {
		flgs |= MSG_EOR;
	}
#endif
	if (flags & PSP_NET_INET_MSG_TRUNC) {
		flgs |= MSG_TRUNC;
	}
	if (flags & PSP_NET_INET_MSG_CTRUNC) {
		flgs |= MSG_CTRUNC;
	}
	if (flags & PSP_NET_INET_MSG_WAITALL) {
		flgs |= MSG_WAITALL;
	}
#if defined(MSG_DONTWAIT)
	if (flags & PSP_NET_INET_MSG_DONTWAIT) {
		flgs |= MSG_DONTWAIT;
	}
#endif
#if defined(MSG_BCAST)
	if (flags & PSP_NET_INET_MSG_BCAST) {
		flgs |= MSG_BCAST;
	}
#endif
#if defined(MSG_MCAST)
	if (flags & PSP_NET_INET_MSG_MCAST) {
		flgs |= MSG_MCAST;
	}
#endif

	return flgs;
}

int convertMSGFlagsHost2PSP(int flags) {
	// Only takes compatible one
	int flgs = 0;
	if (flags & MSG_OOB) {
		flgs |= PSP_NET_INET_MSG_OOB;
	}
	if (flags & MSG_PEEK) {
		flgs |= PSP_NET_INET_MSG_PEEK;
	}
	if (flags & MSG_DONTROUTE) {
		flgs |= PSP_NET_INET_MSG_DONTROUTE;
	}
#if defined(MSG_EOR)
	if (flags & MSG_EOR) {
		flgs |= PSP_NET_INET_MSG_EOR;
	}
#endif
	if (flags & MSG_TRUNC) {
		flgs |= PSP_NET_INET_MSG_TRUNC;
	}
	if (flags & MSG_CTRUNC) {
		flgs |= PSP_NET_INET_MSG_CTRUNC;
	}
	if (flags & MSG_WAITALL) {
		flgs |= PSP_NET_INET_MSG_WAITALL;
	}
#if defined(MSG_DONTWAIT)
	if (flags & MSG_DONTWAIT) {
		flgs |= PSP_NET_INET_MSG_DONTWAIT;
	}
#endif
#if defined(MSG_BCAST)
	if (flags & MSG_BCAST) {
		flgs |= PSP_NET_INET_MSG_BCAST;
	}
#endif
#if defined(MSG_MCAST)
	if (flags & MSG_MCAST) {
		flgs |= PSP_NET_INET_MSG_MCAST;
	}
#endif

	return flgs;
}

int convertSocketDomainPSP2Host(int domain) {
	switch (domain) {
	case PSP_NET_INET_AF_UNSPEC:
		return AF_UNSPEC;
	case PSP_NET_INET_AF_LOCAL:
		return AF_UNIX;
	case PSP_NET_INET_AF_INET:
		return AF_INET;
	}
	return hleLogError(Log::sceNet, domain, "Unknown Socket Domain");
}

int convertSocketDomainHost2PSP(int domain) {
	switch (domain) {
	case AF_UNSPEC:
		return PSP_NET_INET_AF_UNSPEC;
	case AF_UNIX:
		return PSP_NET_INET_AF_LOCAL;
	case AF_INET:
		return PSP_NET_INET_AF_INET;
	}
	return hleLogError(Log::sceNet, domain, "Unknown Socket Domain");
}

std::string inetSocketDomain2str(int domain) {
	switch (domain) {
	case PSP_NET_INET_AF_UNSPEC:
		return "AF_UNSPEC";
	case PSP_NET_INET_AF_UNIX:
		return "AF_UNIX";
	case PSP_NET_INET_AF_INET:
		return "AF_INET";
	}
	return StringFromFormat("AF_%08x", domain);
}

int convertSocketTypePSP2Host(int type) {
	// FIXME: Masked with 0x0F since there might be additional flags mixed in socket type that need to be converted too
	switch (type & PSP_NET_INET_SOCK_TYPE_MASK) {
	case PSP_NET_INET_SOCK_STREAM:
		return SOCK_STREAM;
	case PSP_NET_INET_SOCK_DGRAM:
		return SOCK_DGRAM;
	case PSP_NET_INET_SOCK_RAW:
		// FIXME: SOCK_RAW have some restrictions on newer Windows?
		return SOCK_RAW;
	case PSP_NET_INET_SOCK_RDM:
		return SOCK_RDM;
	case PSP_NET_INET_SOCK_SEQPACKET:
		return SOCK_SEQPACKET;
	case PSP_NET_INET_SOCK_CONN_DGRAM:	// PSP_NET_INET_SOCK_DCCP?
		return SOCK_DGRAM;				// SOCK_RAW?
	case PSP_NET_INET_SOCK_PACKET:
		return SOCK_STREAM;				// SOCK_RAW?
	}

	return hleLogError(Log::sceNet, type, "Unknown Socket Type") & PSP_NET_INET_SOCK_TYPE_MASK;
}

int convertSocketTypeHost2PSP(int type) {
	// FIXME: Masked with 0x0F since there might be additional flags mixed in socket type that need to be converted too
	switch (type & PSP_NET_INET_SOCK_TYPE_MASK) {
	case SOCK_STREAM:
		return PSP_NET_INET_SOCK_STREAM;
	case SOCK_DGRAM:
		return PSP_NET_INET_SOCK_DGRAM;
	case SOCK_RAW:
		return PSP_NET_INET_SOCK_RAW;
	case SOCK_RDM:
		return PSP_NET_INET_SOCK_RDM;
	case SOCK_SEQPACKET:
		return PSP_NET_INET_SOCK_SEQPACKET;
#if defined(CONN_DGRAM)
	case CONN_DGRAM: // SOCK_DCCP
		return PSP_NET_INET_SOCK_CONN_DGRAM; // PSP_NET_INET_SOCK_DCCP
#endif
#if defined(SOCK_PACKET)
	case SOCK_PACKET:
		return PSP_NET_INET_SOCK_PACKET;
#endif
	}

	return hleLogError(Log::sceNet, type, "Unknown Socket Type") & PSP_NET_INET_SOCK_TYPE_MASK;
}

std::string inetSocketType2str(int type) {
	switch (type & PSP_NET_INET_SOCK_TYPE_MASK) {
	case PSP_NET_INET_SOCK_STREAM:
		return "SOCK_STREAM";
	case PSP_NET_INET_SOCK_DGRAM:
		return "SOCK_DGRAM";
	case PSP_NET_INET_SOCK_RAW:
		return "SOCK_RAW";
	case PSP_NET_INET_SOCK_RDM:
		return "SOCK_RDM";
	case PSP_NET_INET_SOCK_SEQPACKET:
		return "SOCK_SEQPACKET";
	case PSP_NET_INET_SOCK_DCCP:
		return "SOCK_DCCP/SOCK_CONN_DGRAM?";
	case PSP_NET_INET_SOCK_PACKET:
		return "SOCK_PACKET?";
	}
	return StringFromFormat("SOCK_%08x", type);
}

int convertSocketProtoPSP2Host(int protocol) {
	switch (protocol) {
	case PSP_NET_INET_IPPROTO_UNSPEC:
		return PSP_NET_INET_IPPROTO_UNSPEC; // 0 only valid if there is only 1 protocol available for a particular domain/family and type?
	case PSP_NET_INET_IPPROTO_ICMP:
		return IPPROTO_ICMP;
	case PSP_NET_INET_IPPROTO_IGMP:
		return IPPROTO_IGMP;
	case PSP_NET_INET_IPPROTO_TCP:
		return IPPROTO_TCP;
	case PSP_NET_INET_IPPROTO_EGP:
		return IPPROTO_EGP;
	case PSP_NET_INET_IPPROTO_PUP:
		return IPPROTO_PUP;
	case PSP_NET_INET_IPPROTO_UDP:
		return IPPROTO_UDP;
	case PSP_NET_INET_IPPROTO_IDP:
		return IPPROTO_IDP;
	case PSP_NET_INET_IPPROTO_RAW:
		return IPPROTO_RAW;
	}
	return hleLogError(Log::sceNet, protocol, "Unknown Socket Protocol");
}

int convertSocketProtoHost2PSP(int protocol) {
	switch (protocol) {
	case PSP_NET_INET_IPPROTO_UNSPEC:
		return PSP_NET_INET_IPPROTO_UNSPEC; // 0 only valid if there is only 1 protocol available for a particular domain/family and type?
	case IPPROTO_ICMP:
		return PSP_NET_INET_IPPROTO_ICMP;
	case IPPROTO_IGMP:
		return PSP_NET_INET_IPPROTO_IGMP;
	case IPPROTO_TCP:
		return PSP_NET_INET_IPPROTO_TCP;
	case IPPROTO_EGP:
		return PSP_NET_INET_IPPROTO_EGP;
	case IPPROTO_PUP:
		return PSP_NET_INET_IPPROTO_PUP;
	case IPPROTO_UDP:
		return PSP_NET_INET_IPPROTO_UDP;
	case IPPROTO_IDP:
		return PSP_NET_INET_IPPROTO_IDP;
	case IPPROTO_RAW:
		return PSP_NET_INET_IPPROTO_RAW;
	}
	return hleLogError(Log::sceNet, protocol, "Unknown Socket Protocol");
}

std::string inetSocketProto2str(int protocol) {
	switch (protocol) {
	case PSP_NET_INET_IPPROTO_UNSPEC:
		return "IPPROTO_UNSPEC (DEFAULT?)"; // defaulted to IPPROTO_TCP for SOCK_STREAM and IPPROTO_UDP for SOCK_DGRAM
	case PSP_NET_INET_IPPROTO_ICMP:
		return "IPPROTO_ICMP";
	case PSP_NET_INET_IPPROTO_IGMP:
		return "IPPROTO_IGMP";
	case PSP_NET_INET_IPPROTO_TCP:
		return "IPPROTO_TCP";
	case PSP_NET_INET_IPPROTO_EGP:
		return "IPPROTO_EGP";
	case PSP_NET_INET_IPPROTO_PUP:
		return "IPPROTO_PUP";
	case PSP_NET_INET_IPPROTO_UDP:
		return "IPPROTO_UDP";
	case PSP_NET_INET_IPPROTO_IDP:
		return "IPPROTO_IDP";
	case PSP_NET_INET_IPPROTO_RAW:
		return "IPPROTO_RAW";
	}
	return StringFromFormat("IPPROTO_%08x", protocol);
}

int convertCMsgTypePSP2Host(int type, int level) {
	if (level == PSP_NET_INET_IPPROTO_IP) {
		switch (type) {
#if defined(IP_RECVDSTADDR)
		case PSP_NET_INET_IP_RECVDSTADDR:
			return IP_RECVDSTADDR;
#endif
#if defined(IP_RECVIF)
		case PSP_NET_INET_IP_RECVIF:
			return IP_RECVIF;
#endif
		}
	} else if (level == PSP_NET_INET_SOL_SOCKET) {
#if defined(SCM_RIGHTS)
		if (type == PSP_NET_INET_SCM_RIGHTS)
			return SCM_RIGHTS;
#endif
#if defined(SCM_CREDS)
		if (type == PSP_NET_INET_SCM_CREDS)
			return SCM_CREDS;
#endif
#if defined(SCM_TIMESTAMP)
		if (type == PSP_NET_INET_SCM_TIMESTAMP)
			return SCM_TIMESTAMP;
#endif
	}
	return hleLogError(Log::sceNet, type, "Unknown CMSG_TYPE (Level = %08x)", level);
}

int convertCMsgTypeHost2PSP(int type, int level) {
	if (level == IPPROTO_IP) {
		switch (type) {
#if defined(IP_RECVDSTADDR)
		case IP_RECVDSTADDR:
			return PSP_NET_INET_IP_RECVDSTADDR;
#endif
#if defined(IP_RECVIF)
		case IP_RECVIF:
			return PSP_NET_INET_IP_RECVIF;
#endif
		}
	} else if (level == SOL_SOCKET) {
#if defined(SCM_RIGHTS)
		if (type == SCM_RIGHTS)
			return PSP_NET_INET_SCM_RIGHTS;
#endif
#if defined(SCM_CREDS)
		if (type == SCM_CREDS)
			return PSP_NET_INET_SCM_CREDS;
#endif
#if defined(SCM_TIMESTAMP)
		if (type == SCM_TIMESTAMP)
			return PSP_NET_INET_SCM_TIMESTAMP;
#endif
	}
	return hleLogError(Log::sceNet, type, "Unknown CMSG_TYPE (Level = %08x)", level);
}

int convertSockoptLevelPSP2Host(int level) {
	switch (level) {
	case PSP_NET_INET_IPPROTO_IP:
		return IPPROTO_IP;
	case PSP_NET_INET_IPPROTO_TCP:
		return IPPROTO_TCP;
	case PSP_NET_INET_IPPROTO_UDP:
		return IPPROTO_UDP;
	case PSP_NET_INET_SOL_SOCKET:
		return SOL_SOCKET;
	}
	return hleLogError(Log::sceNet, level, "Unknown SockOpt Level");
}

int convertSockoptLevelHost2PSP(int level) {
	switch (level) {
	case IPPROTO_IP:
		return PSP_NET_INET_IPPROTO_IP;
	case IPPROTO_TCP:
		return PSP_NET_INET_IPPROTO_TCP;
	case IPPROTO_UDP:
		return PSP_NET_INET_IPPROTO_UDP;
	case SOL_SOCKET:
		return PSP_NET_INET_SOL_SOCKET;
	}
	return hleLogError(Log::sceNet, level, "Unknown SockOpt Level");
}

std::string inetSockoptLevel2str(int level) {
	switch (level) {
	case PSP_NET_INET_IPPROTO_IP:
		return "IPPROTO_IP";
	case PSP_NET_INET_IPPROTO_TCP:
		return "IPPROTO_TCP";
	case PSP_NET_INET_IPPROTO_UDP:
		return "IPPROTO_UDP";
	case PSP_NET_INET_SOL_SOCKET:
		return "SOL_SOCKET";
	}
	return StringFromFormat("SOL_%08x", level);
}

int convertSockoptNamePSP2Host(int optname, int level) {
	if (level == PSP_NET_INET_IPPROTO_TCP) {
		switch (optname) {
		case PSP_NET_INET_TCP_NODELAY:
			return TCP_NODELAY;
		case PSP_NET_INET_TCP_MAXSEG:
			return TCP_MAXSEG;
		}
	} else if (level == PSP_NET_INET_IPPROTO_IP) {
		switch (optname) {
		case PSP_NET_INET_IP_OPTIONS:
			return IP_OPTIONS;
		case PSP_NET_INET_IP_HDRINCL:
			return IP_HDRINCL;
		case PSP_NET_INET_IP_TOS:
			return IP_TOS;
		case PSP_NET_INET_IP_TTL:
			return IP_TTL;
#if defined(IP_RECVOPTS)
		case PSP_NET_INET_IP_RECVOPTS:
			return IP_RECVOPTS;
#endif
#if defined(IP_RECVRETOPTS)
		case PSP_NET_INET_IP_RECVRETOPTS:
			return IP_RECVRETOPTS;
#endif
#if defined(IP_RECVDSTADDR)
		case PSP_NET_INET_IP_RECVDSTADDR:
			return IP_RECVDSTADDR;
#endif
#if defined(IP_RETOPTS)
		case PSP_NET_INET_IP_RETOPTS:
			return IP_RETOPTS;
#endif
		case PSP_NET_INET_IP_MULTICAST_IF:
			return IP_MULTICAST_IF;
		case PSP_NET_INET_IP_MULTICAST_TTL:
			return IP_MULTICAST_TTL;
		case PSP_NET_INET_IP_MULTICAST_LOOP:
			return IP_MULTICAST_LOOP;
		case PSP_NET_INET_IP_ADD_MEMBERSHIP:
			return IP_ADD_MEMBERSHIP;
		case PSP_NET_INET_IP_DROP_MEMBERSHIP:
			return IP_DROP_MEMBERSHIP;
#if defined(IP_PORTRANGE)
		case PSP_NET_INET_IP_PORTRANGE:
			return IP_PORTRANGE;
#endif
#if defined(IP_RECVIF)
		case PSP_NET_INET_IP_RECVIF:
			return IP_RECVIF;
#endif
#if defined(IP_ERRORMTU)
		case PSP_NET_INET_IP_ERRORMTU:
			return IP_ERRORMTU;
#endif
#if defined(IP_IPSEC_POLICY)
		case PSP_NET_INET_IP_IPSEC_POLICY:
			return IP_IPSEC_POLICY;
#endif
		}
	} else if (level == PSP_NET_INET_SOL_SOCKET) {
		switch (optname) {
		case PSP_NET_INET_SO_DEBUG:
			return SO_DEBUG;
		case PSP_NET_INET_SO_ACCEPTCONN:
			return SO_ACCEPTCONN;
		case PSP_NET_INET_SO_REUSEADDR:
			return SO_REUSEADDR;
		case PSP_NET_INET_SO_KEEPALIVE:
			return SO_KEEPALIVE;
		case PSP_NET_INET_SO_DONTROUTE:
			return SO_DONTROUTE;
		case PSP_NET_INET_SO_BROADCAST:
			return SO_BROADCAST;
#if defined(SO_USELOOPBACK)
		case PSP_NET_INET_SO_USELOOPBACK:
			return SO_USELOOPBACK;
#endif
		case PSP_NET_INET_SO_LINGER:
			return SO_LINGER;
		case PSP_NET_INET_SO_OOBINLINE:
			return SO_OOBINLINE;
#if defined(SO_REUSEPORT)
		case PSP_NET_INET_SO_REUSEPORT:
			return SO_REUSEPORT;
#endif
#if defined(SO_TIMESTAMP)
		case PSP_NET_INET_SO_TIMESTAMP:
			return SO_TIMESTAMP;
#endif
#if defined(SO_ONESBCAST)
		case PSP_NET_INET_SO_ONESBCAST:
			return SO_ONESBCAST;
#endif
		case PSP_NET_INET_SO_SNDBUF:
			return SO_SNDBUF;
		case PSP_NET_INET_SO_RCVBUF:
			return SO_RCVBUF;
		case PSP_NET_INET_SO_SNDLOWAT:
			return SO_SNDLOWAT;
		case PSP_NET_INET_SO_RCVLOWAT:
			return SO_RCVLOWAT;
		case PSP_NET_INET_SO_SNDTIMEO:
			return SO_SNDTIMEO;
		case PSP_NET_INET_SO_RCVTIMEO:
			return SO_RCVTIMEO;
		case PSP_NET_INET_SO_ERROR:
			return SO_ERROR;
		case PSP_NET_INET_SO_TYPE:
			return SO_TYPE;
#if defined(SO_NBIO)
		case PSP_NET_INET_SO_NBIO:
			return SO_NBIO;
#endif
#if defined(SO_BIO)
		case PSP_NET_INET_SO_BIO:
			return SO_BIO;
#endif
		}
	}
	return hleLogError(Log::sceNet, optname, "Unknown or unsupported PSP's SockOpt Name (Level = %08x)", level);
}

int convertSockoptNameHost2PSP(int optname, int level) {
	if (level == IPPROTO_TCP) {
		switch (optname) {
		case TCP_NODELAY:
			return PSP_NET_INET_TCP_NODELAY;
		case TCP_MAXSEG:
			return PSP_NET_INET_TCP_MAXSEG;
		}
	} else if (level == IPPROTO_IP) {
		switch (optname) {
		case IP_OPTIONS:
			return PSP_NET_INET_IP_OPTIONS;
		case IP_HDRINCL:
			return PSP_NET_INET_IP_HDRINCL;
		case IP_TOS:
			return PSP_NET_INET_IP_TOS;
		case IP_TTL:
			return PSP_NET_INET_IP_TTL;
#if defined(IP_RECVOPTS)
		case IP_RECVOPTS:
			return PSP_NET_INET_IP_RECVOPTS;
#endif
#if defined(IP_RECVRETOPTS) && (IP_RECVRETOPTS != IP_RETOPTS)
		case IP_RECVRETOPTS:
			return PSP_NET_INET_IP_RECVRETOPTS;
#endif
#if defined(IP_RECVDSTADDR)
		case IP_RECVDSTADDR:
			return PSP_NET_INET_IP_RECVDSTADDR;
#endif
#if defined(IP_RETOPTS)
		case IP_RETOPTS:
			return PSP_NET_INET_IP_RETOPTS;
#endif
		case IP_MULTICAST_IF:
			return PSP_NET_INET_IP_MULTICAST_IF;
		case IP_MULTICAST_TTL:
			return PSP_NET_INET_IP_MULTICAST_TTL;
		case IP_MULTICAST_LOOP:
			return PSP_NET_INET_IP_MULTICAST_LOOP;
		case IP_ADD_MEMBERSHIP:
			return PSP_NET_INET_IP_ADD_MEMBERSHIP;
		case IP_DROP_MEMBERSHIP:
			return PSP_NET_INET_IP_DROP_MEMBERSHIP;
#if defined(IP_PORTRANGE)
		case IP_PORTRANGE:
			return PSP_NET_INET_IP_PORTRANGE;
#endif
#if defined(IP_RECVIF)
		case PSP_NET_INET_IP_RECVIF:
			return IP_RECVIF;
#endif
#if defined(IP_ERRORMTU)
		case IP_ERRORMTU:
			return PSP_NET_INET_IP_ERRORMTU;
#endif
#if defined(IP_IPSEC_POLICY)
		case IP_IPSEC_POLICY:
			return PSP_NET_INET_IP_IPSEC_POLICY;
#endif
		}
	} else if (level == SOL_SOCKET) {
		switch (optname) {
		case SO_DEBUG:
			return PSP_NET_INET_SO_DEBUG;
		case SO_ACCEPTCONN:
			return PSP_NET_INET_SO_ACCEPTCONN;
		case SO_REUSEADDR:
			return PSP_NET_INET_SO_REUSEADDR;
		case SO_KEEPALIVE:
			return PSP_NET_INET_SO_KEEPALIVE;
		case SO_DONTROUTE:
			return PSP_NET_INET_SO_DONTROUTE;
		case SO_BROADCAST:
			return PSP_NET_INET_SO_BROADCAST;
#if defined(SO_USELOOPBACK)
		case SO_USELOOPBACK:
			return PSP_NET_INET_SO_USELOOPBACK;
#endif
		case SO_LINGER:
			return PSP_NET_INET_SO_LINGER;
		case SO_OOBINLINE:
			return PSP_NET_INET_SO_OOBINLINE;
#if defined(SO_REUSEPORT)
		case SO_REUSEPORT:
			return PSP_NET_INET_SO_REUSEPORT;
#endif
#if defined(SO_TIMESTAMP)
		case SO_TIMESTAMP:
			return PSP_NET_INET_SO_TIMESTAMP;
#endif
#if defined(SO_ONESBCAST)
		case SO_ONESBCAST:
			return PSP_NET_INET_SO_ONESBCAST;
#endif
		case SO_SNDBUF:
			return PSP_NET_INET_SO_SNDBUF;
		case SO_RCVBUF:
			return PSP_NET_INET_SO_RCVBUF;
		case SO_SNDLOWAT:
			return PSP_NET_INET_SO_SNDLOWAT;
		case SO_RCVLOWAT:
			return PSP_NET_INET_SO_RCVLOWAT;
		case SO_SNDTIMEO:
			return PSP_NET_INET_SO_SNDTIMEO;
		case SO_RCVTIMEO:
			return PSP_NET_INET_SO_RCVTIMEO;
		case SO_ERROR:
			return PSP_NET_INET_SO_ERROR;
		case SO_TYPE:
			return PSP_NET_INET_SO_TYPE;
#if defined(SO_NBIO)
		case SO_NBIO:
			return PSP_NET_INET_SO_NBIO;
#endif
#if defined(SO_BIO)
		case SO_BIO:
			return PSP_NET_INET_SO_BIO;
#endif
		}
	}
	return hleLogError(Log::sceNet, optname, "Unknown Host's SockOpt Name (Level = %08x)", level);
}

std::string inetSockoptName2str(int optname, int level) {
	if (level == PSP_NET_INET_IPPROTO_TCP) {
		switch (optname) {
		case PSP_NET_INET_TCP_NODELAY:
			return "TCP_NODELAY";
		case PSP_NET_INET_TCP_MAXSEG:
			return "TCP_MAXSEG";
		}
	} else if (level == PSP_NET_INET_IPPROTO_IP) {
		switch (optname) {
		case PSP_NET_INET_IP_OPTIONS:
			return "IP_OPTIONS";
		case PSP_NET_INET_IP_HDRINCL:
			return "IP_HDRINCL";
		case PSP_NET_INET_IP_TOS:
			return "IP_TOS";
		case PSP_NET_INET_IP_TTL:
			return "IP_TTL";
		case PSP_NET_INET_IP_RECVOPTS:
			return "IP_RECVOPTS";
		case PSP_NET_INET_IP_RECVRETOPTS:
			return "IP_RECVRETOPTS";
		case PSP_NET_INET_IP_RECVDSTADDR:
			return "IP_RECVDSTADDR";
		case PSP_NET_INET_IP_RETOPTS:
			return "IP_RETOPTS";
		case PSP_NET_INET_IP_MULTICAST_IF:
			return "IP_MULTICAST_IF";
		case PSP_NET_INET_IP_MULTICAST_TTL:
			return "IP_MULTICAST_TTL";
		case PSP_NET_INET_IP_MULTICAST_LOOP:
			return "IP_MULTICAST_LOOP";
		case PSP_NET_INET_IP_ADD_MEMBERSHIP:
			return "IP_ADD_MEMBERSHIP";
		case PSP_NET_INET_IP_DROP_MEMBERSHIP:
			return "IP_DROP_MEMBERSHIP";
		case PSP_NET_INET_IP_PORTRANGE:
			return "IP_PORTRANGE";
		case PSP_NET_INET_IP_RECVIF:
			return "IP_RECVIF";
		case PSP_NET_INET_IP_ERRORMTU:
			return "IP_ERRORMTU";
		case PSP_NET_INET_IP_IPSEC_POLICY:
			return "IP_IPSEC_POLICY";
		}
	} else if (level == PSP_NET_INET_SOL_SOCKET) {
		switch (optname) {
		case PSP_NET_INET_SO_DEBUG:
			return "SO_DEBUG";
		case PSP_NET_INET_SO_ACCEPTCONN:
			return "SO_ACCEPTCONN";
		case PSP_NET_INET_SO_REUSEADDR:
			return "SO_REUSEADDR";
		case PSP_NET_INET_SO_KEEPALIVE:
			return "SO_KEEPALIVE";
		case PSP_NET_INET_SO_DONTROUTE:
			return "SO_DONTROUTE";
		case PSP_NET_INET_SO_BROADCAST:
			return "SO_BROADCAST";
		case PSP_NET_INET_SO_USELOOPBACK:
			return "SO_USELOOPBACK";
		case PSP_NET_INET_SO_LINGER:
			return "SO_LINGER";
		case PSP_NET_INET_SO_OOBINLINE:
			return "SO_OOBINLINE";
		case PSP_NET_INET_SO_REUSEPORT:
			return "SO_REUSEPORT";
		case PSP_NET_INET_SO_TIMESTAMP:
			return "SO_TIMESTAMP";
		case PSP_NET_INET_SO_ONESBCAST:
			return "SO_ONESBCAST";
		case PSP_NET_INET_SO_SNDBUF:
			return "SO_SNDBUF";
		case PSP_NET_INET_SO_RCVBUF:
			return "SO_RCVBUF";
		case PSP_NET_INET_SO_SNDLOWAT:
			return "SO_SNDLOWAT";
		case PSP_NET_INET_SO_RCVLOWAT:
			return "SO_RCVLOWAT";
		case PSP_NET_INET_SO_SNDTIMEO:
			return "SO_SNDTIMEO";
		case PSP_NET_INET_SO_RCVTIMEO:
			return "SO_RCVTIMEO";
		case PSP_NET_INET_SO_ERROR:
			return "SO_ERROR";
		case PSP_NET_INET_SO_TYPE:
			return "SO_TYPE";
		case PSP_NET_INET_SO_NBIO:
			return "SO_NBIO"; // SO_NONBLOCK
		case PSP_NET_INET_SO_BIO:
			return "SO_BIO";
		}
	}
	return StringFromFormat("SO_%08x (Level = %08x)", optname, level);
}

int convertInetErrnoHost2PSP(int error) {
	if (error == 0) {
		return 0;
	}
	switch (error) {
	case EINTR:
		return ERROR_INET_EINTR;
	case EBADF:
		return ERROR_INET_EBADF;
	case EACCES:
		return ERROR_INET_EACCES;
	case EFAULT:
		return ERROR_INET_EFAULT;
	case EINVAL:
		return ERROR_INET_EINVAL;
	case ENOSPC:
		return ERROR_INET_ENOSPC;
	case EPIPE:
		return ERROR_INET_EPIPE;
	case ENOMSG:
		return ERROR_INET_ENOMSG;
	case ENOLINK:
		return ERROR_INET_ENOLINK;
	case EPROTO:
		return ERROR_INET_EPROTO;
	case EBADMSG:
		return ERROR_INET_EBADMSG;
	case EOPNOTSUPP:
		return ERROR_INET_EOPNOTSUPP;
	case EPFNOSUPPORT:
		return ERROR_INET_EPFNOSUPPORT;
	case ECONNRESET:
		return ERROR_INET_ECONNRESET;
	case ENOBUFS:
		return ERROR_INET_ENOBUFS;
	case EAFNOSUPPORT:
		return ERROR_INET_EAFNOSUPPORT;
	case EPROTOTYPE:
		return ERROR_INET_EPROTOTYPE;
	case ENOTSOCK:
		return ERROR_INET_ENOTSOCK;
	case ENOPROTOOPT:
		return ERROR_INET_ENOPROTOOPT;
	case ESHUTDOWN:
		return ERROR_INET_ESHUTDOWN;
	case ECONNREFUSED:
		return ERROR_INET_ECONNREFUSED;
	case EADDRINUSE:
		return ERROR_INET_EADDRINUSE;
	case ECONNABORTED:
		return ERROR_INET_ECONNABORTED;
	case ENETUNREACH:
		return ERROR_INET_ENETUNREACH;
	case ENETDOWN:
		return ERROR_INET_ENETDOWN;
	case ETIMEDOUT:
		return ERROR_INET_ETIMEDOUT;
	case EHOSTDOWN:
		return ERROR_INET_EHOSTDOWN;
	case EHOSTUNREACH:
		return ERROR_INET_EHOSTUNREACH;
	case EALREADY:
		return ERROR_INET_EALREADY;
	case EMSGSIZE:
		return ERROR_INET_EMSGSIZE;
	case EPROTONOSUPPORT:
		return ERROR_INET_EPROTONOSUPPORT;
	case ESOCKTNOSUPPORT:
		return ERROR_INET_ESOCKTNOSUPPORT;
	case EADDRNOTAVAIL:
		return ERROR_INET_EADDRNOTAVAIL;
	case ENETRESET:
		return ERROR_INET_ENETRESET;
	case EISCONN:
		return ERROR_INET_EISCONN;
	case ENOTCONN:
		return ERROR_INET_ENOTCONN;
	case EAGAIN:
		return ERROR_INET_EAGAIN;
#if !defined(_WIN32)
	case EINPROGRESS:
		return ERROR_INET_EINPROGRESS;
#endif
	}
	if (error != 0)
		return hleLogError(Log::sceNet, error, "Unknown Host Error Number (%d)", error);
	return error;
}

const char *convertInetErrno2str(int error) {
	switch (error) {
	case 0: return "(0=no error)";
	case ERROR_INET_EINTR: return "EINTR";
	case ERROR_INET_EBADF: return "EBADF";
	case ERROR_INET_EACCES: return "EACCES";
	case ERROR_INET_EFAULT: return "EFAULT";
	case ERROR_INET_EINVAL: return "EINVAL";
	case ERROR_INET_ENOSPC: return "ENOSPC";
	case ERROR_INET_EPIPE: return "EPIPE";
	case ERROR_INET_ENOMSG: return "ENOMSG";
	case ERROR_INET_ENOLINK: return "ENOLINK";
	case ERROR_INET_EPROTO: return "EPROTO";
	case ERROR_INET_EBADMSG: return "EBADMSG";
	case ERROR_INET_EOPNOTSUPP: return "EOPNOTSUPP";
	case ERROR_INET_EPFNOSUPPORT: return "EPFNOSUPPORT";
	case ERROR_INET_ECONNRESET: return "ECONNRESET";
	case ERROR_INET_ENOBUFS: return "ENOBUFS";
	case ERROR_INET_EAFNOSUPPORT: return "EAFNOSUPPORT";
	case ERROR_INET_EPROTOTYPE: return "EPROTOTYPE";
	case ERROR_INET_ENOTSOCK: return "ENOTSOCK";
	case ERROR_INET_ENOPROTOOPT: return "ENOPROTOOPT";
	case ERROR_INET_ESHUTDOWN: return "ESHUTDOWN";
	case ERROR_INET_ECONNREFUSED: return "ECONNREFUSED";
	case ERROR_INET_EADDRINUSE: return "EADDRINUSE";
	case ERROR_INET_ECONNABORTED: return "ECONNABORTED";
	case ERROR_INET_ENETUNREACH: return "ENETUNREACH";
	case ERROR_INET_ENETDOWN: return "ENETDOWN";
	case ERROR_INET_ETIMEDOUT: return "ETIMEDOUT";
	case ERROR_INET_EHOSTDOWN: return "EHOSTDOWN";
	case ERROR_INET_EHOSTUNREACH: return "EHOSTUNREACH";
	case ERROR_INET_EALREADY: return "EALREADY";
	case ERROR_INET_EMSGSIZE: return "EMSGSIZE";
	case ERROR_INET_EPROTONOSUPPORT: return "EPROTONOSUPPORT";
	case ERROR_INET_ESOCKTNOSUPPORT: return "ESOCKTNOSUPPORT";
	case ERROR_INET_EADDRNOTAVAIL: return "EADDRNOTAVAIL";
	case ERROR_INET_ENETRESET: return "ENETRESET";
	case ERROR_INET_EISCONN: return "EISCONN";
	case ERROR_INET_ENOTCONN: return "ENOTCONN";
	case ERROR_INET_EAGAIN: return "EAGAIN";
	default: return "(unknown!)";
	}
}

// FIXME: Some of this might be wrong
int convertInetErrno2PSPError(int error) {
	switch (error) {
	case ERROR_INET_EINTR:
		return SCE_KERNEL_ERROR_ERRNO_DEVICE_BUSY;
	case ERROR_INET_EACCES:
		return SCE_KERNEL_ERROR_ERRNO_READ_ONLY;
	case ERROR_INET_EFAULT:
		return SCE_KERNEL_ERROR_ERRNO_ADDR_OUT_OF_MAIN_MEM;
	case ERROR_INET_EINVAL:
		return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
	case ERROR_INET_ENOSPC:
		return SCE_KERNEL_ERROR_ERRNO_NO_MEMORY;
	case ERROR_INET_EPIPE:
		return SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
	case ERROR_INET_ENOMSG:
		return SCE_KERNEL_ERROR_ERRNO_NO_MEDIA;
	case ERROR_INET_ENOLINK:
		return SCE_KERNEL_ERROR_ERRNO_DEVICE_NOT_FOUND;
	case ERROR_INET_EPROTO:
		return SCE_KERNEL_ERROR_ERRNO_FILE_PROTOCOL;
	case ERROR_INET_EBADMSG:
		return SCE_KERNEL_ERROR_ERRNO_INVALID_MEDIUM;
	case ERROR_INET_EOPNOTSUPP:
		return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;
	case ERROR_INET_EPFNOSUPPORT:
		return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;
	case ERROR_INET_ECONNRESET:
		return SCE_KERNEL_ERROR_ERRNO_CONNECTION_RESET;
	case ERROR_INET_ENOBUFS:
		return SCE_KERNEL_ERROR_ERRNO_NO_FREE_BUF_SPACE;
	case ERROR_INET_EAFNOSUPPORT:
		return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;
	case ERROR_INET_EPROTOTYPE:
		return SCE_KERNEL_ERROR_ERRNO_FILE_PROTOCOL;
	case ERROR_INET_ENOTSOCK:
		return SCE_KERNEL_ERROR_ERRNO_INVALID_FILE_DESCRIPTOR;
	case ERROR_INET_ENOPROTOOPT:
		return SCE_KERNEL_ERROR_ERRNO_FILE_PROTOCOL;
	case ERROR_INET_ESHUTDOWN:
		return SCE_KERNEL_ERROR_ERRNO_CLOSED;
	case ERROR_INET_ECONNREFUSED:
		return SCE_KERNEL_ERROR_ERRNO_FILE_ALREADY_EXISTS;
	case ERROR_INET_EADDRINUSE:
		return SCE_KERNEL_ERROR_ERRNO_FILE_ADDR_IN_USE;
	case ERROR_INET_ECONNABORTED:
		return SCE_KERNEL_ERROR_ERRNO_CONNECTION_ABORTED;
	case ERROR_INET_ENETUNREACH:
		return SCE_KERNEL_ERROR_ERRNO_DEVICE_NOT_FOUND;
	case ERROR_INET_ENETDOWN:
		return SCE_KERNEL_ERROR_ERRNO_CLOSED;
	case ERROR_INET_ETIMEDOUT:
		return SCE_KERNEL_ERROR_ERRNO_FILE_TIMEOUT;
	case ERROR_INET_EHOSTDOWN:
		return SCE_KERNEL_ERROR_ERRNO_CLOSED;
	case ERROR_INET_EHOSTUNREACH:
		return SCE_KERNEL_ERROR_ERRNO_DEVICE_NOT_FOUND;
	case ERROR_INET_EALREADY:
		return SCE_KERNEL_ERROR_ERRNO_ALREADY;
	case ERROR_INET_EMSGSIZE:
		return SCE_KERNEL_ERROR_ERRNO_FILE_IS_TOO_BIG;
	case ERROR_INET_EPROTONOSUPPORT:
		return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;
	case ERROR_INET_ESOCKTNOSUPPORT:
		return SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED;
	case ERROR_INET_EADDRNOTAVAIL:
		return SCE_KERNEL_ERROR_ERRNO_ADDRESS_NOT_AVAILABLE;
	case ERROR_INET_ENETRESET:
		return SCE_KERNEL_ERROR_ERRNO_CONNECTION_RESET;
	case ERROR_INET_EISCONN:
		return SCE_KERNEL_ERROR_ERRNO_ALREADY; // SCE_KERNEL_ERROR_ERRNO_IS_ALREADY_CONNECTED; // UNO only check for 0x80010077 and 0x80010078
	case ERROR_INET_ENOTCONN:
		return SCE_KERNEL_ERROR_ERRNO_NOT_CONNECTED;
	case ERROR_INET_EAGAIN:
		return SCE_KERNEL_ERROR_ERRNO_RESOURCE_UNAVAILABLE; // SCE_ERROR_ERRNO_EAGAIN;
#if !defined(_WIN32)
	case ERROR_INET_EINPROGRESS:
		return SCE_KERNEL_ERROR_ERRNO_IN_PROGRESS;
#endif
	}
	if (error != 0)
		return hleLogError(Log::sceNet, error, "Unknown PSP Error Number (%d)", error);
	return error;
}


template <typename I>
std::string num2hex(I w, size_t hex_len = sizeof(I) << 1) {
	static const char* digits = "0123456789ABCDEF";
	std::string rc(hex_len, '0');
	for (size_t i = 0, j = (hex_len - 1) * 4; i < hex_len; ++i, j -= 4)
		rc[i] = digits[(w >> j) & 0x0f];
	return rc;
}

// Unused, might remove in the future.
std::string convertNetError2str(uint32_t errorCode) {
	std::string str = "";
	if (((errorCode >> 31) & 1) != 0)
		str += "ERROR ";
	if (((errorCode >> 30) & 1) != 0)
		str += "CRITICAL ";
	switch ((errorCode >> 16) & 0xfff) {
	case 0x41:
		str += "NET ";
		break;
	default:
		str += "UNK" + num2hex(u16((errorCode >> 16) & 0xfff), 3) + " ";
	}
	switch ((errorCode >> 8) & 0xff) {
	case 0x00:
		str += "COMMON ";
		break;
	case 0x01:
		str += "CORE ";
		break;
	case 0x02:
		str += "INET ";
		break;
	case 0x03:
		str += "POECLIENT ";
		break;
	case 0x04:
		str += "RESOLVER ";
		break;
	case 0x05:
		str += "DHCP ";
		break;
	case 0x06:
		str += "ADHOC_AUTH ";
		break;
	case 0x07:
		str += "ADHOC ";
		break;
	case 0x08:
		str += "ADHOC_MATCHING ";
		break;
	case 0x09:
		str += "NETCNF ";
		break;
	case 0x0a:
		str += "APCTL ";
		break;
	case 0x0b:
		str += "ADHOCCTL ";
		break;
	case 0x0c:
		str += "UNKNOWN1 ";
		break;
	case 0x0d:
		str += "WLAN ";
		break;
	case 0x0e:
		str += "EAPOL ";
		break;
	case 0x0f:
		str += "8021x ";
		break;
	case 0x10:
		str += "WPA ";
		break;
	case 0x11:
		str += "UNKNOWN2 ";
		break;
	case 0x12:
		str += "TRANSFER ";
		break;
	case 0x13:
		str += "ADHOC_DISCOVER ";
		break;
	case 0x14:
		str += "ADHOC_DIALOG ";
		break;
	case 0x15:
		str += "WISPR ";
		break;
	default:
		str += "UNKNOWN" + num2hex(u8((errorCode >> 8) & 0xff)) + " ";
	}
	str += num2hex(u8(errorCode & 0xff));
	return str;
}
