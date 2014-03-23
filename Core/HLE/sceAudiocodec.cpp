// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceAudiocodec.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/HW/SimpleAudioDec.h"
#include "Common/ChunkFile.h"

// Following kaien_fr's sample code https://github.com/hrydgard/ppsspp/issues/5620#issuecomment-37086024
// Should probably store the EDRAM get/release status somewhere within here, etc.
struct AudioCodecContext {
	u32_le unknown[6];
	u32_le inDataPtr;   // 6
	u32_le inDataSize;  // 7
	u32_le outDataPtr;  // 8
	u32_le audioSamplesPerFrame;  // 9
	u32_le inDataSizeAgain;  // 10  ??
}; 

struct AudioInfo{
	SimpleAudio * decoder; // pointer to audio decoder
	u32 ctxPtr;
	int codec;
	AudioInfo(){
		decoder = NULL;
	};
	AudioInfo(u32 ctxPtr, int codec):ctxPtr(ctxPtr),codec(codec){
		decoder = AudioCreate(codec);
	};
	~AudioInfo(){
		if (decoder)
			AudioClose(&decoder);
	}

};
struct AudioCell{
	AudioCell* prevcell;
	AudioInfo* currval;
	AudioCell* nextcell;
	AudioCell(){
		prevcell = NULL;
		currval = NULL;
		nextcell = NULL;
	};
};

class AudioList {
public:
	AudioCell* head;
	AudioCell* current;
	AudioCell* tail;
	int count;

	// for savestate
	int* codec_;
	u32* ctxPtr_;

	AudioList(){
		head = new AudioCell;
		tail = new AudioCell;
		current = head;
		head->nextcell = tail;
		count = 0;
	}

	~AudioList(){
		clear();
	}

	void push(AudioInfo* info){
		current = new AudioCell;
		current->currval = info;
		current->nextcell = head;
		head->prevcell = current;
		head = current;
		count++;
		INFO_LOG(ME, "New audio %08x start, we are playing %d audios in the same time", info->ctxPtr, count);
	};

	AudioInfo* pop_front(){
		current = head;
		head = head->nextcell;
		head->prevcell = NULL;
		count--;
		return current->currval;
	};

	AudioInfo* pop(u32 ctxPtr){
		current = head;
		for (int i = 0; i < count; i++)
		{
			if (current->currval->ctxPtr == ctxPtr){
				if (current->prevcell == NULL){
					//head
					head = current->nextcell;
					head->prevcell = NULL;
				}
				else{
					// pop this cell
					current->prevcell->nextcell = current->nextcell;
					current->nextcell->prevcell = current->prevcell;
				}
				count--;
				return current->currval;
			}
			current = current->nextcell;
		}
		return NULL;
	};

	AudioInfo* find(u32 ctxPtr){
		current = head;
		for (int i = 0; i < count; i++)
		{
			if (current->currval->ctxPtr == ctxPtr){
				return current->currval;
			}
			current = current->nextcell;
		}
		ERROR_LOG(ME, "Cannot find audio context %08x in AudioList", ctxPtr);
		return NULL;
	};

	
	void clear(){
		for (int i = 0; i < count; i++)
		{
			current = head;
			head = head->nextcell;
			delete(current);
		}
	};

	void DoState(PointerWrap &p){
		auto s = p.Section("AudioList", 0, 1);
		if (!s)
			return;

		p.Do(count);
		if (p.mode == p.MODE_WRITE && count > 0){
			// write savestate
			current = head;
			codec_ = new int[count];
			ctxPtr_ = new u32[count];
			for (int i = 0; i < count; i++){
				codec_[i] = current->currval->codec;
				ctxPtr_[i] = current->currval->ctxPtr;
				current = current->nextcell;
			}
			p.DoArray(codec_, ARRAY_SIZE(codec_));
			p.DoArray(ctxPtr_, ARRAY_SIZE(ctxPtr_));
		}
		if (p.mode == p.MODE_READ){
			// read savestate
			if (count > 0){
				// AudioList is nonempty
				auto c = count;
				clear(); // clear and prepare to read variables
				AudioList(); // reinitialization
				codec_ = new int[count];
				ctxPtr_ = new u32[count];
				p.DoArray(codec_, ARRAY_SIZE(codec_));
				p.DoArray(ctxPtr_, ARRAY_SIZE(ctxPtr_));
				for (int i = 0; i < c; i++){
					auto newaudio = new AudioInfo(ctxPtr_[i], codec_[i]);
					push(newaudio);
				}
			}
		}
	};
};

// AudioList is a queue to store current playing audios.
AudioList audioQueue;

int sceAudiocodecInit(u32 ctxPtr, int codec) {
	if (isValidCodec(codec)){
		// Create audio decoder for given audio codec and put it into AudioList
		auto newaudio = new AudioInfo (ctxPtr, codec);
		audioQueue.push(newaudio);		
		INFO_LOG(ME, "sceAudiocodecInit(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
		return 0;
	}
	ERROR_LOG_REPORT(ME, "sceAudiocodecInit(%08x, %i (%s)): Unknown audio codec %i", ctxPtr, codec, GetCodecName(codec), codec);
	return 0;
}

int sceAudiocodecDecode(u32 ctxPtr, int codec) {
	if (!ctxPtr){
		ERROR_LOG_REPORT(ME, "sceAudiocodecDecode(%08x, %i (%s)) got NULL pointer", ctxPtr, codec, GetCodecName(codec));
		return -1;
	}
	if (isValidCodec(codec)){
		// Use SimpleAudioDec to decode audio
		// Get AudioCodecContext
		auto ctx = new AudioCodecContext;
		Memory::ReadStruct(ctxPtr, ctx);
		int outbytes = 0;
		// search decoder in AudioList
		auto audiodecoder = audioQueue.find(ctxPtr)->decoder;
		if (audiodecoder != NULL){
			// Decode audio
			AudioDecode(audiodecoder, Memory::GetPointer(ctx->inDataPtr), ctx->inDataSize, &outbytes, Memory::GetPointer(ctx->outDataPtr));
			DEBUG_LOG(ME, "sceAudiocodecDec(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
		}
		// Delete AudioCodecContext
		delete(ctx);
		return 0;
	}
	ERROR_LOG_REPORT(ME, "UNIMPL sceAudiocodecDecode(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
	return 0;
}

int sceAudiocodecGetInfo(u32 ctxPtr, int codec) {
	ERROR_LOG_REPORT(ME, "UNIMPL sceAudiocodecGetInfo(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
	return 0;
}

int sceAudiocodecCheckNeedMem(u32 ctxPtr, int codec) {
	WARN_LOG(ME, "UNIMPL sceAudiocodecCheckNeedMem(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
	return 0;
}

int sceAudiocodecGetEDRAM(u32 ctxPtr, int codec) {
	WARN_LOG(ME, "UNIMPL sceAudiocodecGetEDRAM(%08x, %i (%s))", ctxPtr, codec, GetCodecName(codec));
	return 0;
}

int sceAudiocodecReleaseEDRAM(u32 ctxPtr, int id) {
	//id is not always a codec, so what is should be? 
	auto info = audioQueue.pop(ctxPtr);
	if (info != NULL){ 
		info->~AudioInfo();
		delete info;
		INFO_LOG(ME, "sceAudiocodecReleaseEDRAM(%08x, %i)", ctxPtr, id);
		return 0;
	}
	WARN_LOG(ME, "UNIMPL sceAudiocodecReleaseEDRAM(%08x, %i)", ctxPtr, id);
	return 0;
}

const HLEFunction sceAudiocodec[] = {
	{0x70A703F8, WrapI_UI<sceAudiocodecDecode>, "sceAudiocodecDecode"},
	{0x5B37EB1D, WrapI_UI<sceAudiocodecInit>, "sceAudiocodecInit"},
	{0x8ACA11D5, WrapI_UI<sceAudiocodecGetInfo>, "sceAudiocodecGetInfo"},
	{0x3A20A200, WrapI_UI<sceAudiocodecGetEDRAM>, "sceAudiocodecGetEDRAM" },
	{0x29681260, WrapI_UI<sceAudiocodecReleaseEDRAM>, "sceAudiocodecReleaseEDRAM" },
	{0x9D3F790C, WrapI_UI<sceAudiocodecCheckNeedMem>, "sceAudiocodecCheckNeedMem" },
	{0x59176a0f, 0, "sceAudiocodec_59176A0F"},
};

void Register_sceAudiocodec()
{
	RegisterModule("sceAudiocodec", ARRAY_SIZE(sceAudiocodec), sceAudiocodec);
}

void __sceAudiocodecDoState(PointerWrap &p){
	auto s = p.Section("AudioList", 0, 1);
	if (!s)
		return;

	p.Do(audioQueue);
}
