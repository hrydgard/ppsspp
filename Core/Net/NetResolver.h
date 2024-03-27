#pragma once

#include "CommonTypes.h"

class NetResolver {
public:
    NetResolver(const NetResolver& other) = default;

    NetResolver() :
        mId(0),
        mIsRunning(false),
        mBufferAddr(0),
        mBufferLen(0) {}

    NetResolver(const int id, const u32 bufferAddr, const int bufferLen) :
        mId(id),
        mIsRunning(false),
        mBufferAddr(bufferAddr),
        mBufferLen(bufferLen) {}

    int GetId() const { return mId; }

    bool GetIsRunning() const { return mIsRunning; }

    void SetIsRunning(const bool isRunning) { this->mIsRunning = isRunning; }

private:
    int mId;
    bool mIsRunning;
    u32 mBufferAddr;
    u32 mBufferLen;
};
