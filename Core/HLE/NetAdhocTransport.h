// Copyright (c) 2012- PPSSPP Project.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include "ext/aemu_postoffice/client/postoffice_client.h"
#include "ext/aemu_postoffice/adhocctl/client/adhocctl.h"

// The relay interface can also be provided by a frontend. Install before boot,
// and restore only after PSP_Shutdown has joined all networking threads.
struct NetAdhocTransport {
	decltype(&::init_aemu_postoffice) init = ::init_aemu_postoffice;
	decltype(&::pump_enet_aemu_postoffice) pump = ::pump_enet_aemu_postoffice;
	decltype(&::deinit_aemu_postoffice) deinit = ::deinit_aemu_postoffice;
	decltype(&::pdp_create_v4) pdpCreate = ::pdp_create_v4;
	decltype(&::pdp_delete) pdpDelete = ::pdp_delete;
	decltype(&::pdp_send) pdpSend = ::pdp_send;
	decltype(&::pdp_recv) pdpRecv = ::pdp_recv;
	decltype(&::pdp_peek_next_size) pdpPeek = ::pdp_peek_next_size;
	decltype(&::pdp_buffered_data_size) pdpBuffered = ::pdp_buffered_data_size;
	decltype(&::pdp_send_buf_not_full) pdpWritable = ::pdp_send_buf_not_full;
	decltype(&::pdp_is_dead) pdpDead = ::pdp_is_dead;
	decltype(&::ptp_listen_v4) ptpListen = ::ptp_listen_v4;
	decltype(&::ptp_accept) ptpAccept = ::ptp_accept;
	decltype(&::ptp_connect_v4) ptpConnect = ::ptp_connect_v4;
	decltype(&::ptp_send) ptpSend = ::ptp_send;
	decltype(&::ptp_recv) ptpRecv = ::ptp_recv;
	decltype(&::ptp_close) ptpClose = ::ptp_close;
	decltype(&::ptp_listen_close) ptpListenClose = ::ptp_listen_close;
	decltype(&::ptp_peek_next_size) ptpPeek = ::ptp_peek_next_size;
	decltype(&::ptp_send_buf_not_full) ptpWritable = ::ptp_send_buf_not_full;
	decltype(&::ptp_listen_has_request) ptpPending = ::ptp_listen_has_request;
	decltype(&::create_adhocctl_v1_session_v4) ctlCreate = ::create_adhocctl_v1_session_v4;
	decltype(&::destroy_adhocctl_session) ctlDestroy = ::destroy_adhocctl_session;
	decltype(&::adhocctl_get_event) ctlEvent = ::adhocctl_get_event;
	decltype(&::adhocctl_ping) ctlPing = ::adhocctl_ping;
	decltype(&::adhocctl_connect) ctlConnect = ::adhocctl_connect;
	decltype(&::adhocctl_disconnect) ctlDisconnect = ::adhocctl_disconnect;
	decltype(&::adhocctl_scan) ctlScan = ::adhocctl_scan;
	decltype(&::adhocctl_chat) ctlChat = ::adhocctl_chat;
	decltype(&::ptp_is_dead) ptpDead = ::ptp_is_dead;
	void (*getLocalMac)(uint8_t *mac) = nullptr;
	// A frontend transport that isn't connected yet: the login waits for it instead of failing.
	bool (*ready)() = nullptr;
};

extern NetAdhocTransport g_netAdhocTransport;
