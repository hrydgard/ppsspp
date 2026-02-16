#include <algorithm>
#include <cstring>

#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Core/EmuLinkServer.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/MemMap.h"
#include "Core/System.h"

static constexpr int EMULINK_PORT = 55355;
static constexpr u32 MAX_PAYLOAD = 1024;

// Disc ID written here for EmuLnk game detection (end of scratchpad RAM).
static constexpr u32 EMULINK_ID_ADDR = 0x00013FF0;

EmuLinkServer &EmuLinkServer::Instance() {
	static EmuLinkServer instance;
	return instance;
}

EmuLinkServer::~EmuLinkServer() {
	Stop();
}

bool EmuLinkServer::Start() {
	if (m_running)
		return true;

	m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (m_socket == INVALID_SOCKET) {
		ERROR_LOG(Log::System, "EmuLinkServer: Failed to create socket");
		return false;
	}

	int on = 1;
	setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));

#if PPSSPP_PLATFORM(WINDOWS)
	unsigned long nonblocking = 1;
	ioctlsocket(m_socket, FIONBIO, &nonblocking);
#else
	fcntl(m_socket, F_SETFL, O_NONBLOCK);
#endif

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(EMULINK_PORT);
	addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(m_socket, (sockaddr *)&addr, sizeof(addr)) < 0) {
		ERROR_LOG(Log::System, "EmuLinkServer: Failed to bind to port %d", EMULINK_PORT);
		closesocket(m_socket);
		m_socket = INVALID_SOCKET;
		return false;
	}

	{
		auto lock = Memory::Lock();
		std::string discID = g_paramSFO.GetDiscID();
		if (!discID.empty() && Memory::IsValidRange(EMULINK_ID_ADDR, 16)) {
			u8 idBuf[16] = {};
			size_t len = std::min(discID.size(), (size_t)15);
			std::memcpy(idBuf, discID.c_str(), len);
			Memory::MemcpyUnchecked(EMULINK_ID_ADDR, idBuf, 16);
			INFO_LOG(Log::System, "EmuLinkServer: Wrote disc ID '%s' at 0x%08X", discID.c_str(), EMULINK_ID_ADDR);
		}
	}

	m_running = true;
	m_thread = std::thread(&EmuLinkServer::ServerLoop, this);
	INFO_LOG(Log::System, "EmuLinkServer: Started on port %d", EMULINK_PORT);
	return true;
}

void EmuLinkServer::Stop() {
	if (!m_running)
		return;

	m_running = false;

	if (m_socket != INVALID_SOCKET) {
		closesocket(m_socket);
		m_socket = INVALID_SOCKET;
	}

	if (m_thread.joinable())
		m_thread.join();

	INFO_LOG(Log::System, "EmuLinkServer: Stopped");
}

void EmuLinkServer::ServerLoop() {
	u8 packet_buffer[8 + MAX_PAYLOAD];
	u8 memory_buffer[MAX_PAYLOAD];

	while (m_running) {
		sockaddr_in sender{};
		socklen_t senderLen = sizeof(sender);
		int received = recvfrom(m_socket, (char *)packet_buffer, sizeof(packet_buffer),
		                        0, (sockaddr *)&sender, &senderLen);

		if (received >= 8) {
			u32 address, size;
			std::memcpy(&address, packet_buffer, 4);
			std::memcpy(&size, packet_buffer + 4, 4);

			if (received == 8 && size > 0 && size <= MAX_PAYLOAD) {
				// READ request
				auto lock = Memory::Lock();
				if (Memory::IsValidRange(address, size)) {
					Memory::MemcpyUnchecked(memory_buffer, address, size);
					sendto(m_socket, (const char *)memory_buffer, size, 0,
					       (sockaddr *)&sender, senderLen);
				}
			} else if (received > 8) {
				// WRITE request
				u32 data_len = (u32)(received - 8);
				u32 write_size = std::min(size, data_len);
				if (write_size > MAX_PAYLOAD)
					write_size = MAX_PAYLOAD;
				auto lock = Memory::Lock();
				if (write_size > 0 && Memory::IsValidRange(address, write_size)) {
					Memory::MemcpyUnchecked(address, packet_buffer + 8, write_size);
				}
			}
		} else {
			sleep_ms(1, "emulink-poll");
		}
	}
}
