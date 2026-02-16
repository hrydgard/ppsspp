#pragma once

#include <atomic>
#include <thread>
#include "Common/Net/SocketCompat.h"

class EmuLinkServer {
public:
	static EmuLinkServer &Instance();
	bool Start();
	void Stop();

private:
	EmuLinkServer() = default;
	~EmuLinkServer();
	void ServerLoop();

	std::atomic<bool> m_running{false};
	std::thread m_thread;
	SOCKET m_socket = INVALID_SOCKET;
};
