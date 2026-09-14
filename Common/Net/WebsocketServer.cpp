#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "Common/Net/SocketCompat.h"
#include "Common/Data/Encoding/Base64.h"
#include "Common/Net/HTTPServer.h"
#include "Common/Net/Sinks.h"
#include "Common/Net/WebsocketServer.h"

#include "Common/Crypto/sha1.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"

static const char *const WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// Sanity cap on a single message's total size, so a client can't crash us by simply
// claiming an enormous frame length before sending any actual data.
// TODO: Make configurable?
static constexpr uint64_t MAX_WS_MESSAGE_SIZE = 128 * 1024 * 1024;

namespace net {

enum class Opcode {
	CONTINUE = 0,
	TEXT = 1,
	BINARY = 2,

	CLOSE = 8,
	PING = 9,
	PONG = 10,

	PAYLOAD_MAX = 2,
	CONTROL_MIN = 8,
	CONTROL_MAX = 10,
};

static const size_t OUT_PRESSURE = 65536;
// How much already-sent data we let sit at the front of outBuf_ before reclaiming it.
static const size_t OUT_COMPACT_THRESHOLD = 1024 * 1024;

static inline std::string TrimString(const std::string &s) {
	auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c) {
		// isspace() expects 0 - 255, so convert any sign-extended value.
		return std::isspace(c & 0xFF);
	});
	auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](int c){
		return std::isspace(c & 0xFF);
	}).base();
	return wsback > wsfront ? std::string(wsfront, wsback) : std::string();
}

static bool ListContainsNoCase(const std::string &list, const std::string value) {
	std::vector<std::string> split;
	SplitString(list, ',', split);

	for (auto item : split) {
		std::transform(item.begin(), item.end(), item.begin(), tolower);
		if (TrimString(item) == value) {
			return true;
		}
	}

	return false;
}

WebSocketServer *WebSocketServer::CreateAsUpgrade(const http::ServerRequest &request, const std::string &protocol) {
	auto requireHeader = [&](const char *name, const char *expected) {
		std::string val;
		if (!request.GetHeader(name, &val)) {
			return false;
		}
		return strcasecmp(val.c_str(), expected) == 0;
	};
	auto requireHeaderContains = [&](const char *name, const char *expected) {
		std::string val;
		if (!request.GetHeader(name, &val)) {
			return false;
		}
		return ListContainsNoCase(val, expected);
	};

	if (!requireHeader("upgrade", "websocket") || !requireHeaderContains("connection", "upgrade")) {
		request.WriteHttpResponseHeader("1.1", 400, -1, "text/plain");
		request.Out()->Push("Must send a websocket request.");
		return nullptr;
	}
	if (!requireHeader("sec-websocket-version", "13")) {
		request.WriteHttpResponseHeader("1.1", 400, -1, "text/plain", "Sec-WebSocket-Version: 13\r\n");
		request.Out()->Push("Unsupported version.");
		return nullptr;
	}

	std::string requestedProtocols;
	std::string obtainedProtocolHeader;
	if (!protocol.empty() && request.GetHeader("sec-websocket-protocol", &requestedProtocols)) {
		if (ListContainsNoCase(requestedProtocols, protocol)) {
			obtainedProtocolHeader = "Sec-WebSocket-Protocol: " + protocol + "\r\n";
		}
	}

	std::string key;
	if (!request.GetHeader("sec-websocket-key", &key)) {
		request.WriteHttpResponseHeader("1.1", 400, -1, "text/plain");
		request.Out()->Push("Cannot accept without key.");
		return nullptr;
	}

	key += WEBSOCKET_GUID;
	unsigned char accept[20];
	sha1((unsigned char *)key.c_str(), (int)key.size(), accept);

	std::string acceptKey = Base64Encode(accept, 20);
	std::string otherHeaders = StringFromFormat("Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n%s", acceptKey.c_str(), obtainedProtocolHeader.c_str());

	// Okay, we're good to go then.
	request.WriteHttpResponseHeader("1.1", 101, -1, "websocket", otherHeaders.c_str());
	request.WritePartial();

	return new WebSocketServer(request.fd(), request.In(), request.Out());
}

void WebSocketServer::Send(const std::string &str) {
	_assert_(open_);
	if (SendingIsOver())
		return;
	_assert_(fragmentOpcode_ == -1);
	SendHeader(true, (int)Opcode::TEXT, str.size());
	SendBytes(str.c_str(), str.size());
}

void WebSocketServer::Send(const std::vector<uint8_t> &payload) {
	_assert_(open_);
	if (SendingIsOver())
		return;
	_assert_(fragmentOpcode_ == -1);
	SendHeader(true, (int)Opcode::BINARY, payload.size());
	SendBytes((const char *)payload.data(), payload.size());
}

void WebSocketServer::AddFragment(bool finish, const std::string &str) {
	_assert_(open_);
	if (SendingIsOver())
		return;
	if (fragmentOpcode_ == -1) {
		SendHeader(finish, (int)Opcode::TEXT, str.size());
		fragmentOpcode_ = (int)Opcode::TEXT;
	} else if (fragmentOpcode_ == (int)Opcode::TEXT) {
		SendHeader(finish, (int)Opcode::CONTINUE, str.size());
	} else {
		_assert_(fragmentOpcode_ == (int)Opcode::TEXT || fragmentOpcode_ == -1);
	}
	SendBytes(str.c_str(), str.size());
	if (finish) {
		fragmentOpcode_ = -1;
	}
}

void WebSocketServer::AddFragment(bool finish, const std::vector<uint8_t> &payload) {
	_assert_(open_);
	if (SendingIsOver())
		return;
	if (fragmentOpcode_ == -1) {
		SendHeader(finish, (int)Opcode::BINARY, payload.size());
		fragmentOpcode_ = (int)Opcode::BINARY;
	} else if (fragmentOpcode_ == (int)Opcode::BINARY) {
		SendHeader(finish, (int)Opcode::CONTINUE, payload.size());
	} else {
		_assert_(fragmentOpcode_ == (int)Opcode::BINARY || fragmentOpcode_ == -1);
	}
	SendBytes((const char *)payload.data(), payload.size());
	if (finish) {
		fragmentOpcode_ = -1;
	}
}

void WebSocketServer::Ping(const std::vector<uint8_t> &payload) {
	_assert_(open_);
	if (SendingIsOver())
		return;
	_assert_(payload.size() <= 125);
	SendHeader(true, (int)Opcode::PING, payload.size());
	SendBytes((const char *)payload.data(), payload.size());
}

void WebSocketServer::Pong(const std::vector<uint8_t> &payload) {
	_assert_(open_);
	if (SendingIsOver())
		return;
	_assert_(payload.size() <= 125);
	SendHeader(true, (int)Opcode::PONG, payload.size());
	SendBytes((const char *)payload.data(), payload.size());
}

void WebSocketServer::Close(WebSocketClose reason) {
	if (sentClose_) {
		// Already closing - a second close frame would just be more data we can't send.
		return;
	}
	closeReason_ = reason;
	if (reason == WebSocketClose::NO_STATUS) {
		// This means we received a CLOSE without a code.
		SendHeader(true, (int)Opcode::CLOSE, 0);
	} else {
		SendHeader(true, (int)Opcode::CLOSE, 2);

		uint16_t r = (uint16_t)reason;
		uint8_t reasonData[] = {
			(uint8_t)((r >> 8) & 0xFF),
			(uint8_t)((r >> 0) & 0xFF),
		};
		SendBytes((const char *)reasonData, sizeof(reasonData));
	}

	sentClose_ = true;
}

// The connection is gone or unusable - there's nothing left to send and no point waiting for
// anything, so drop whatever is queued and let the caller's loop end.
void WebSocketServer::CloseAbnormally() {
	closeReason_ = WebSocketClose::ABNORMAL;
	open_ = false;
	out_->Discard();
	outBuf_.clear();
	outBufOffset_ = 0;
}

bool WebSocketServer::Process(float timeout) {
	if (!open_) {
		return false;
	}

	SendFlush();

	if (out_->HasError()) {
		// The socket is broken (peer gone, reset, ...), so out_ can never drain again. We must not
		// keep waiting for it to empty: select() reports an errored socket as ready every single
		// time and SendFlush() can't make progress, so we'd return true forever and the caller
		// would sit in a tight loop burning a core.
		CloseAbnormally();
		return false;
	}

	if (OutBufPending() == 0 && out_->Empty() && sentClose_) {
		// Okay, we've sent the close.  Don't wait for anything else (whether we got a close or not.)
		open_ = false;
		return false;
	}

	struct timeval tv;
	tv.tv_sec = floor(timeout);
	tv.tv_usec = (timeout - floor(timeout)) * 1000000.0;

	fd_set read;
	FD_ZERO(&read);
	// In case we closed due to protocol error, don't even try to read.
	if (!sentClose_) {
		FD_SET(fd_, &read);
	}

	fd_set write;
	FD_ZERO(&write);
	if (OutBufPending() != 0 || !out_->Empty()) {
		FD_SET(fd_, &write);
	}

	// First argument to select is the highest socket in the set + 1.
	int rval = select((int)fd_ + 1, &read, &write, nullptr, &tv);
	if (rval < 0) {
		const int err = socket_errno;
#if !PPSSPP_PLATFORM(WINDOWS)
		if (err == EINTR) {
			// Just a signal, the next lap will select() again - and it did wait, so no spinning.
			return true;
		}
#endif
		// Anything else isn't going to fix itself (a bad fd, say), and returning true on a call
		// that fails immediately means the caller busy-loops instead of being paced by the timeout.
		ERROR_LOG(Log::IO, "WebSocket select() failed: %d - closing connection", err);
		CloseAbnormally();
		return false;
	}

	if (rval == 0) {
		// Timed out.
		return true;
	}

	if (FD_ISSET(fd_, &write)) {
		SendFlush();
	}
	if (FD_ISSET(fd_, &read)) {
		// Fill even when the sink still holds bytes. Otherwise a disconnect goes unnoticed for as
		// long as there are leftovers, and if those leftovers are half a frame, ReadFrames() ends
		// up waiting inside TakeExact() for bytes that can never arrive.
		if (!in_->TryFill()) {
			// select() said readable and there's still nothing, so the peer is gone.
			CloseAbnormally();
			return false;
		}

		// Note this before draining - the peer can close with data still in flight, and we want to
		// hand over what it did send before acting on the disconnect.
		const bool atEnd = in_->AtEnd();

		while (ReadFrames() && !in_->Empty())
			continue;

		if (atEnd && in_->Empty()) {
			// Consumed everything they sent, and nothing more is coming.
			CloseAbnormally();
			return false;
		}
	}

	return true;
}

bool WebSocketServer::ReadFrames() {
	if (pendingLeft_ != 0) {
		return ReadPending();
	}

	return ReadFrame();
}

// A read came up short. TakeExact() only tells us it failed, not why - so ask the sink whether
// the peer is simply gone, rather than blaming it for a protocol violation it didn't commit.
void WebSocketServer::CloseForReadFailure() {
	if (in_->AtEnd() || in_->HasError()) {
		Close(WebSocketClose::ABNORMAL);
	} else {
		Close(WebSocketClose::POLICY_VIOLATION);
	}
}

bool WebSocketServer::ReadFrame() {
	_assert_(pendingLeft_ == 0);

	// TODO: For now blocking on header trickle, shouldn't be common.
	auto readExact = [&](void *p, size_t sz) {
		if (!in_->TakeExact((char *)p, sz)) {
			// TODO: Failing on too slow trickle timeout for now.
			CloseForReadFailure();
			return false;
		}
		return true;
	};

	// Client frames are always between 6 and 14 bytes.  We start with 6.
	uint8_t header[14];
	if (!readExact(header, 6))
		return false;

	// Don't allow reserved bits to be set, require masking.
	if ((header[0] & 0x70) != 0 || (header[1] & 0x80) == 0) {
		Close(WebSocketClose::PROTOCOL_ERROR);
		return false;
	}

	const bool fin = (header[0] & 0x80) != 0;
	const int opcode = header[0] & 0x0F;
	uint64_t sz = header[1] & 0x7F;
	const uint8_t *mask = &header[2];

	if (opcode >= (int)Opcode::CONTROL_MIN && (sz > 125 || !fin)) {
		// Control frames must be <= 125 bytes.
		Close(WebSocketClose::PROTOCOL_ERROR);
		return false;
	}

	if (opcode > (int)Opcode::CONTROL_MAX || (opcode > (int)Opcode::PAYLOAD_MAX && opcode < (int)Opcode::CONTROL_MIN)) {
		// Undefined opcode.
		Close(WebSocketClose::PROTOCOL_ERROR);
		return false;
	}

	if (!pendingFin_ && opcode == (int)Opcode::CONTINUE) {
		// Can't continue what you haven't started.
		Close(WebSocketClose::PROTOCOL_ERROR);
		return false;
	}
	if (pendingFin_ && opcode != (int)Opcode::CONTINUE && opcode < (int)Opcode::CONTROL_MIN) {
		// Can't start something else until you finish your thought.
		Close(WebSocketClose::PROTOCOL_ERROR);
		return false;
	}

	if (sz == 126) {
		// Read the rest of the mask.
		if (!readExact((char *)&header[6], 2))
			return false;

		mask = &header[4];
		sz = (header[2] << 8) | (header[3] << 0);
	} else if (sz == 127) {
		// We only have half the size so far - read the rest, and the mask.
		if (!readExact((char *)&header[6], 8))
			return false;

		mask = &header[10];
		// Read from big endian. Cast first: these promote to int, so a byte >= 0x80 in the top
		// position would shift into the sign bit and then sign-extend into the u64.
		uint64_t high = ((uint64_t)header[2] << 24) | ((uint64_t)header[3] << 16) | ((uint64_t)header[4] << 8) | (uint64_t)header[5];
		uint64_t low = ((uint64_t)header[6] << 24) | ((uint64_t)header[7] << 16) | ((uint64_t)header[8] << 8) | (uint64_t)header[9];
		sz = (high << 32) | low;

		if ((sz & 0x8000000000000000ULL) != 0) {
			Close(WebSocketClose::PROTOCOL_ERROR);
			return false;
		}
	}

	// Reject before ReadPending() turns this into a resize() - a client can claim any
	// length up front, long before actually sending that much data.
	if (sz > MAX_WS_MESSAGE_SIZE || pendingBuf_.size() + sz > MAX_WS_MESSAGE_SIZE) {
		Close(WebSocketClose::MESSAGE_TOO_LONG);
		return false;
	}

	if (opcode >= (int)Opcode::CONTROL_MIN) {
		// It's safe to overwrite this since we can be between fragmented frames, but not inside a frame.
		memcpy(pendingMask_, mask, sizeof(pendingMask_));
		return ReadControlFrame(opcode, sz);
	}

	// The data could be split among many TCP packets, so read it as it comes.
	if (!pendingFin_)
		pendingOpcode_ = opcode;
	pendingFin_ = !fin;
	pendingLeft_ = sz;
	memcpy(pendingMask_, mask, sizeof(pendingMask_));

	// Payload data is actually read in ReadPending().
	return true;
}

bool WebSocketServer::ReadPending() {
	size_t pos = pendingBuf_.size();
	pendingBuf_.resize(pendingBuf_.size() + pendingLeft_);

	// Read what we can.
	size_t readBytes = in_->TakeAtMost((char *)&pendingBuf_[pos], pendingLeft_);
	for (size_t i = 0; i < readBytes; ++i) {
		pendingBuf_[pos + i] ^= pendingMask_[i & 3];
	}
	pendingLeft_ -= readBytes;

	if (pendingLeft_ != 0) {
		// Still more to read.  Careful: we might need to rotate the mask.
		// Example: if we read only 3 bytes, next read should start at fourth byte in mask.
		int offset = readBytes & 3;
		if (offset) {
			uint8_t orig[4];
			memcpy(orig, pendingMask_, sizeof(orig));
			for (size_t i = 0; i < sizeof(orig); ++i) {
				pendingMask_[i] = orig[(offset + i) & 3];
			}
		}

		// Truncate out the unread bytes for next time.
		pendingBuf_.resize(pos + readBytes);

		if (in_->AtEnd() || in_->HasError()) {
			// TakeAtMost() returns 0 both for "nothing right now" and "nothing ever again", so we
			// have to ask the sink which it was. The rest of this message is never arriving.
			return false;
		}
		return true;
	}

	// We're done, but were we waiting for a FIN packet?
	if (pendingFin_)
		return true;

	if (pendingOpcode_ == (int)Opcode::TEXT) {
		if (text_) {
			text_(std::string(pendingBuf_.begin(), pendingBuf_.end()));
		}
	} else if (pendingOpcode_ == (int)Opcode::BINARY) {
		if (binary_) {
			binary_(pendingBuf_);
		}
	} else {
		_assert_(false);
	}

	// All done, clear it out.
	pendingBuf_.clear();
	pendingOpcode_ = -1;

	return true;
}

// Which close codes we're allowed to put on the wire, per RFC 6455 7.4.1. 1004, 1005, 1006 and
// 1015 are reserved for local use only, and anything below 1000 is undefined.
static bool IsCloseCodeSendable(uint16_t code) {
	if (code >= 3000 && code <= 4999) {
		// Registered and private-use ranges.
		return true;
	}
	switch ((WebSocketClose)code) {
	case WebSocketClose::NORMAL:
	case WebSocketClose::GOING_AWAY:
	case WebSocketClose::PROTOCOL_ERROR:
	case WebSocketClose::UNSUPPORTED_DATA:
	case WebSocketClose::INVALID_DATA:
	case WebSocketClose::POLICY_VIOLATION:
	case WebSocketClose::MESSAGE_TOO_LONG:
	case WebSocketClose::MISSING_EXTENSION:
	case WebSocketClose::INTERNAL_ERROR:
	case WebSocketClose::SERVICE_RESTART:
	case WebSocketClose::TRY_AGAIN_LATER:
	case WebSocketClose::BAD_GATEWAY:
		return true;
	default:
		return false;
	}
}

bool WebSocketServer::ReadControlFrame(int opcode, size_t sz) {
	std::vector<uint8_t> payload;
	payload.resize(sz);
	// Just block here to read the payload.
	if (!in_->TakeExact((char *)payload.data(), sz)) {
		// TODO: Failing on too slow trickle timeout for now.
		CloseForReadFailure();
		return false;
	}

	for (size_t i = 0; i < sz; ++i) {
		payload[i] ^= pendingMask_[i & 3];
	}

	if (opcode == (int)Opcode::PING) {
		Pong(payload);
		// Try to send immediately if possible, but don't block.
		SendFlush();

		if (ping_) {
			ping_(payload);
		}
	} else if (opcode == (int)Opcode::PONG) {
		if (pong_) {
			pong_(payload);
		}
	} else if (opcode == (int)Opcode::CLOSE) {
		if (payload.size() >= 2) {
			uint16_t reason = (payload[0] << 8) | payload[1];
			// Send back a close right away - but not their code verbatim. NO_STATUS, ABNORMAL and
			// friends describe how a connection ended locally and RFC 6455 7.4.1 says they must
			// never go on the wire, so echoing one back would be our protocol violation, not theirs.
			Close(IsCloseCodeSendable(reason) ? WebSocketClose(reason) : WebSocketClose::PROTOCOL_ERROR);
		} else {
			Close(WebSocketClose::NO_STATUS);
		}
		// Don't read anything more.
		return false;
	} else {
		_assert_(false);
	}

	return true;
}

void WebSocketServer::SendHeader(bool fin, int opcode, size_t sz) {
	_assert_((opcode & 0x0F) == opcode);
	uint8_t frameHeader = (fin ? 0x80 : 0x00) | opcode;
	SendBytes(&frameHeader, 1);

	// We never mask from the server.
	if (sz <= 125) {
		uint8_t frameSize = (int8_t)sz;
		SendBytes(&frameSize, 1);
	} else if (sz <= 0xFFFF) {
		uint8_t frameSize[] = {
			126,
			(uint8_t)((sz >> 8) & 0xFF),
			(uint8_t)((sz >> 0) & 0xFF),
		};
		SendBytes(frameSize, sizeof(frameSize));
	} else {
		uint64_t sz64 = sz;
		_assert_((sz64 & 0x8000000000000000ULL) == 0);
		uint8_t frameSize[] = {
			127,
			(uint8_t)((sz64 >> 56) & 0xFF),
			(uint8_t)((sz64 >> 48) & 0xFF),
			(uint8_t)((sz64 >> 40) & 0xFF),
			(uint8_t)((sz64 >> 32) & 0xFF),
			(uint8_t)((sz64 >> 24) & 0xFF),
			(uint8_t)((sz64 >> 16) & 0xFF),
			(uint8_t)((sz64 >> 8) & 0xFF),
			(uint8_t)((sz64 >> 0) & 0xFF),
		};
		SendBytes(frameSize, sizeof(frameSize));
	}
}

void WebSocketServer::SendBytes(const void *p, size_t sz) {
	const char *data = (const char *)p;
	if (OutBufPending() == 0) {
		size_t pushed = out_->PushAtMost(data, sz);
		data += pushed;
		sz -= pushed;
	}

	if (sz != 0) {
		size_t pos = outBuf_.size();
		outBuf_.resize(pos + sz);
		memcpy(&outBuf_[pos], data, sz);

		if (OutBufPending() > lastPressure_ + OUT_PRESSURE) {
			size_t pushed = out_->PushAtMost((const char *)&outBuf_[outBufOffset_], OutBufPending());
			outBufOffset_ += pushed;
			CompactOutBuf();
			lastPressure_ = OutBufPending();
		}
	}
}

void WebSocketServer::SendFlush() {
	out_->Flush(false);

	// Drain out as much of our buffer as possible.
	while (OutBufPending() != 0) {
		size_t pushed = out_->PushAtMost((const char *)&outBuf_[outBufOffset_], OutBufPending());
		if (pushed == 0)
			break;

		outBufOffset_ += pushed;
		out_->Flush(false);
	}

	CompactOutBuf();
	lastPressure_ = OutBufPending();
}

// outBufOffset_ marks how much of outBuf_ has already gone out. We don't erase it every time we
// drain some: when a client falls behind and the backlog grows, erasing from the front on every
// lap memmoves the whole backlog each time, which is quadratic and can eat a core by itself.
void WebSocketServer::CompactOutBuf() {
	if (outBufOffset_ == outBuf_.size()) {
		outBuf_.clear();
		outBufOffset_ = 0;
	} else if (outBufOffset_ >= OUT_COMPACT_THRESHOLD) {
		outBuf_.erase(outBuf_.begin(), outBuf_.begin() + outBufOffset_);
		outBufOffset_ = 0;
	}
}

};
