#pragma once

#include <functional>
#include <string>

#include "Common/Net/HTTPServer.h"
#include "Common/Net/Sinks.h"

namespace net {

enum class WebSocketClose : uint16_t {
	NORMAL = 1000,
	GOING_AWAY = 1001,
	PROTOCOL_ERROR = 1002,
	UNSUPPORTED_DATA = 1003,
	INVALID_DATA = 1007,
	POLICY_VIOLATION = 1008,
	MESSAGE_TOO_LONG = 1009,
	MISSING_EXTENSION = 1010,
	INTERNAL_ERROR = 1011,
	SERVICE_RESTART = 1012,
	TRY_AGAIN_LATER = 1013,
	BAD_GATEWAY = 1014,

	NO_STATUS = 1005,
	ABNORMAL = 1006,
};

// RFC 6455
class WebSocketServer {
public:
	static WebSocketServer *CreateAsUpgrade(const http::ServerRequest &request, const std::string &protocol = "");

	void Send(const std::string &str);
	void Send(const std::vector<uint8_t> &payload);

	// Call with finish = false to start and continue, then finally with finish = true to complete.
	// Note: Fragmented data cannot be interleaved, per protocol.
	void AddFragment(bool finish, const std::string &str);
	void AddFragment(bool finish, const std::vector<uint8_t> &payload);

	void Ping(const std::vector<uint8_t> &payload = {});
	void Pong(const std::vector<uint8_t> &payload = {});
	void Close(WebSocketClose reason = WebSocketClose::GOING_AWAY);

	// Note: may interrupt early.  Call in a loop.
	bool Process(float timeout = -1.0f);

	void SetTextHandler(std::function<void(const std::string &)> func) {
		text_ = func;
	}
	void SetBinaryHandler(std::function<void(const std::vector<uint8_t> &)> func) {
		binary_ = func;
	}
	// Doesn't need to send a Pong.
	void SetPingHandler(std::function<void(const std::vector<uint8_t> &)> func) {
		ping_ = func;
	}
	void SetPongHandler(std::function<void(const std::vector<uint8_t> &)> func) {
		pong_ = func;
	}

	bool IsOpen() {
		return open_;
	}
	WebSocketClose CloseReason() {
		return closeReason_;
	}

protected:
	WebSocketServer(size_t fd, InputSink *in, OutputSink *out) : fd_(fd), in_(in), out_(out) {
	}

	void SendHeader(bool fin, int opcode, size_t sz);
	void SendBytes(const void *p, size_t sz);
	void SendFlush();
	bool ReadFrames();
	bool ReadFrame();
	bool ReadPending();
	bool ReadControlFrame(int opcode, size_t sz);

	bool open_ = true;
	bool sentClose_ = false;
	int fragmentOpcode_ = -1;
	size_t fd_ = 0;
	InputSink *in_ = nullptr;
	OutputSink *out_ = nullptr;
	WebSocketClose closeReason_ = WebSocketClose::NO_STATUS;
	std::vector<uint8_t> outBuf_;
	size_t lastPressure_ = 0;

	std::vector<uint8_t> pendingBuf_;
	uint8_t pendingMask_[4]{};
	// Bytes left to read in the frame (in case of a partial frame read.)
	uint64_t pendingLeft_ = 0;
	int pendingOpcode_ = -1;
	// Waiting for a frame with FIN.
	bool pendingFin_ = false;

	std::function<void(const std::string &)> text_;
	std::function<void(const std::vector<uint8_t> &)> binary_;
	std::function<void(const std::vector<uint8_t> &)> ping_;
	std::function<void(const std::vector<uint8_t> &)> pong_;
};

};
