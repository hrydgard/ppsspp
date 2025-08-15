// TODO: move apctl here

#include "sceNetApctl.h"

const char *ApctlStateToString(int apctlState) {
	switch (apctlState) {
	case PSP_NET_APCTL_STATE_DISCONNECTED: return "DISCONNECTED";
	case PSP_NET_APCTL_STATE_SCANNING: return "SCANNING";
	case PSP_NET_APCTL_STATE_JOINING: return "JOINING";
	case PSP_NET_APCTL_STATE_GETTING_IP: return "GETTING_IP";
	case PSP_NET_APCTL_STATE_GOT_IP: return "GOT_IP";
	case PSP_NET_APCTL_STATE_EAP_AUTH: return "EAP_AUTH";
	case PSP_NET_APCTL_STATE_KEY_EXCHANGE: return "KEY_EXCHANGE";
	default: return "N/A";
	}
}
