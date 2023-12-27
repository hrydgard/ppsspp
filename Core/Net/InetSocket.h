#pragma once

// TODO: document
class InetSocket {
public:
	InetSocket(int sceSocketId, int nativeSocketId, int protocol, bool nonBlocking) :
		mInetSocketId(sceSocketId),
		mNativeSocketId(nativeSocketId),
		mProtocol(protocol),
		mNonBlocking(nonBlocking) {}

	int GetInetSocketId() const {
		return mInetSocketId;
	}

	int GetNativeSocketId() const {
		return mNativeSocketId;
	}

	bool IsNonBlocking() const {
		return mNonBlocking;
	}

	void SetNonBlocking(const bool nonBlocking) {
		mNonBlocking = nonBlocking;
	}

	// TODO: document that this is the native protocol
	int GetProtocol() const {
		return mProtocol;
	}
private:
	int mInetSocketId;
	int mNativeSocketId;
	int mProtocol;
	bool mNonBlocking = false;
};
