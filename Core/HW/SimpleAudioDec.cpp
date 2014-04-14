// Copyright (c) 2013- PPSSPP Project.

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

#include <algorithm>
#include "Core/Config.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HW/SimpleAudioDec.h"
#include "Core/HW/MediaEngine.h"
#include "Core/HW/BufferQueue.h"

#ifdef USE_FFMPEG

extern "C" {
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/samplefmt.h>
}

#endif  // USE_FFMPEG

bool SimpleAudio::GetAudioCodecID(int audioType){
#ifdef USE_FFMPEG

	switch (audioType)
	{
	case PSP_CODEC_AAC:
		audioCodecId = AV_CODEC_ID_AAC;
		break;
	case PSP_CODEC_AT3:
		audioCodecId = AV_CODEC_ID_ATRAC3;
		break;
	case PSP_CODEC_AT3PLUS:
		audioCodecId = AV_CODEC_ID_ATRAC3P;
		break;
	case PSP_CODEC_MP3:
		audioCodecId = AV_CODEC_ID_MP3;
		break;
	default:
		audioType = 0;
		break;
	}
	if (audioType != 0){
		return true;
	}
	return false;
#else
	return false;
#endif // USE_FFMPEG
}

SimpleAudio::SimpleAudio(int audioType)
: codec_(0), codecCtx_(0), swrCtx_(0), audioType(audioType), outSamples(0), wanted_resample_freq(44100){
#ifdef USE_FFMPEG
	avcodec_register_all();
	av_register_all();
	InitFFmpeg();

	frame_ = av_frame_alloc();

	// Get Audio Codec ID
	if (!GetAudioCodecID(audioType)){
		ERROR_LOG(ME, "This version of FFMPEG does not support Audio codec type: %08x. Update your submodule.", audioType);
		return;
	}
	// Find decoder
	codec_ = avcodec_find_decoder(audioCodecId);
	if (!codec_) {
		// Eh, we shouldn't even have managed to compile. But meh.
		ERROR_LOG(ME, "This version of FFMPEG does not support AV_CODEC_ID for audio (%s). Update your submodule.", GetCodecName(audioType));
		return;
	}
	// Allocate codec context
	codecCtx_ = avcodec_alloc_context3(codec_);
	if (!codecCtx_) {
		ERROR_LOG(ME, "Failed to allocate a codec context");
		return;
	}
	codecCtx_->channels = 2;
	codecCtx_->channel_layout = AV_CH_LAYOUT_STEREO;
	codecCtx_->sample_rate = 44100;
	// Open codec
	AVDictionary *opts = 0;
	if (avcodec_open2(codecCtx_, codec_, &opts) < 0) {
		ERROR_LOG(ME, "Failed to open codec");
		return;
	}

	av_dict_free(&opts);
#endif  // USE_FFMPEG
}


SimpleAudio::SimpleAudio(u32 ctxPtr, int audioType)
: codec_(0), codecCtx_(0), swrCtx_(0), ctxPtr(ctxPtr), audioType(audioType), outSamples(0), wanted_resample_freq(44100){
#ifdef USE_FFMPEG
	avcodec_register_all();
	av_register_all();
	InitFFmpeg();

	frame_ = av_frame_alloc();

	// Get Audio Codec ctx
	if (!GetAudioCodecID(audioType)){
		ERROR_LOG(ME, "This version of FFMPEG does not support Audio codec type: %08x. Update your submodule.", audioType);
		return;
	}
	// Find decoder
	codec_ = avcodec_find_decoder(audioCodecId);
	if (!codec_) {
		// Eh, we shouldn't even have managed to compile. But meh.
		ERROR_LOG(ME, "This version of FFMPEG does not support AV_CODEC_ctx for audio (%s). Update your submodule.", GetCodecName(audioType));
		return;
	}
	// Allocate codec context
	codecCtx_ = avcodec_alloc_context3(codec_);
	if (!codecCtx_) {
		ERROR_LOG(ME, "Failed to allocate a codec context");
		return;
	}
	codecCtx_->channels = 2;
	codecCtx_->channel_layout = AV_CH_LAYOUT_STEREO;
	codecCtx_->sample_rate = 44100;
	// Open codec
	AVDictionary *opts = 0;
	if (avcodec_open2(codecCtx_, codec_, &opts) < 0) {
		ERROR_LOG(ME, "Failed to open codec");
		av_dict_free(&opts);
		return;
	}

	av_dict_free(&opts);
#endif  // USE_FFMPEG
}

bool SimpleAudio::ResetCodecCtx(int channels, int samplerate){
#ifdef USE_FFMPEG
	if (codecCtx_)
		avcodec_close(codecCtx_);

	// Find decoder
	codec_ = avcodec_find_decoder(audioCodecId);
	if (!codec_) {
		// Eh, we shouldn't even have managed to compile. But meh.
		ERROR_LOG(ME, "This version of FFMPEG does not support AV_CODEC_ctx for audio (%s). Update your submodule.", GetCodecName(audioType));
		return false;
	}

	codecCtx_->channels = channels;
	codecCtx_->channel_layout = channels==2?AV_CH_LAYOUT_STEREO:AV_CH_LAYOUT_MONO;
	codecCtx_->sample_rate = samplerate;
	// Open codec
	AVDictionary *opts = 0;
	if (avcodec_open2(codecCtx_, codec_, &opts) < 0) {
		ERROR_LOG(ME, "Failed to open codec");
		av_dict_free(&opts);
		return false;
	}
	av_dict_free(&opts);
	return true;
#endif
	return false;
}

SimpleAudio::~SimpleAudio() {
#ifdef USE_FFMPEG
	if (frame_)
		av_frame_free(&frame_);
	if (codecCtx_)
		avcodec_close(codecCtx_);
	frame_ = 0;
	codecCtx_ = 0;
	codec_ = 0;
#endif  // USE_FFMPEG
}

void SaveAudio(const char filename[], uint8_t *outbuf, int size){
	FILE * pf;
	pf = fopen(filename, "ab+");

	fwrite(outbuf, size, 1, pf);
	fclose(pf);
}

bool SimpleAudio::Decode(void* inbuf, int inbytes, uint8_t *outbuf, int *outbytes) {
#ifdef USE_FFMPEG
	AVPacket packet;
	av_init_packet(&packet);
	packet.data = static_cast<uint8_t *>(inbuf);
	packet.size = inbytes;

	int got_frame = 0;
	av_frame_unref(frame_);

	*outbytes = 0;
	srcPos = 0;
	int len = avcodec_decode_audio4(codecCtx_, frame_, &got_frame, &packet);
	if (len < 0) {
		ERROR_LOG(ME, "Error decoding Audio frame");
		// TODO: cleanup
		return false;
	}
	av_free_packet(&packet);
	
	// get bytes consumed in source
	srcPos = len;

	if (got_frame) {
		// Initializing the sample rate convert. We will use it to convert float output into int.
		int64_t wanted_channel_layout = AV_CH_LAYOUT_STEREO; // we want stereo output layout
		int64_t dec_channel_layout = frame_->channel_layout; // decoded channel layout

		swrCtx_ = swr_alloc_set_opts(
			swrCtx_,
			wanted_channel_layout,
			AV_SAMPLE_FMT_S16,
			wanted_resample_freq,
			dec_channel_layout,
			codecCtx_->sample_fmt,
			codecCtx_->sample_rate,
			0,
			NULL);

		if (!swrCtx_ || swr_init(swrCtx_) < 0) {
			ERROR_LOG(ME, "swr_init: Failed to initialize the resampling context");
			avcodec_close(codecCtx_);
			codec_ = 0;
			return false;
		}
		// convert audio to AV_SAMPLE_FMT_S16
		int swrRet = swr_convert(swrCtx_, &outbuf, frame_->nb_samples, (const u8 **)frame_->extended_data, frame_->nb_samples);
		if (swrRet < 0) {
			ERROR_LOG(ME, "swr_convert: Error while converting %d", swrRet);
			return false;
		}
		swr_free(&swrCtx_);
		// output samples per frame, we should *2 since we have two channels
		outSamples = swrRet * 2;

		// each sample occupies 2 bytes
		*outbytes = outSamples * 2;
		// We always convert to stereo.
		__AdjustBGMVolume((s16 *)outbuf, frame_->nb_samples * 2);

		// Save outbuf into pcm audio, you can uncomment this line to save and check the decoded audio into pcm file.
		// SaveAudio("dump.pcm", outbuf, *outbytes);
	}
	return true;
#else
	// Zero bytes output. No need to memset.
	*outbytes = 0;
	return true;
#endif  // USE_FFMPEG
}

int SimpleAudio::getOutSamples(){
	return outSamples;
}

int SimpleAudio::getSourcePos(){
	return srcPos;
}

void SimpleAudio::setResampleFrequency(int freq){
	wanted_resample_freq = freq;
}

void AudioClose(SimpleAudio **ctx) {
#ifdef USE_FFMPEG
	delete *ctx;
	*ctx = 0;
#endif  // USE_FFMPEG
}

bool isValidCodec(int codec){
	if (codec >= PSP_CODEC_AT3PLUS && codec <= PSP_CODEC_AAC) {
		return true;
	}
	return false;
}


// sceAu module starts from here

// return output pcm size, <0 error
u32 AuCtx::AuDecode(u32 pcmAddr)
{
	if (!Memory::IsValidAddress(pcmAddr)){
		ERROR_LOG(ME, "%s: output bufferAddress %08x is invalctx", __FUNCTION__, pcmAddr);
		return -1;
	}

	auto outbuf = Memory::GetPointer(PCMBuf);
	memset(outbuf, 0, PCMBufSize); // important! empty outbuf to avoid noise
	u32 outpcmbufsize = 0;

	int repeat = 1;
	if (g_Config.bSoundSpeedHack){
		repeat = 2;
	}
	int i = 0;
	// decode frames in sourcebuff and output into PCMBuf (each time, we decode one or two frames)
	// some games as Miku like one frame each time, some games like DOA like two frames each time
	while (sourcebuff.size() > 0 && outpcmbufsize < PCMBufSize && i < repeat){
		i++;
		int pcmframesize;
		// decode
		decoder->Decode((void*)sourcebuff.c_str(), (int)sourcebuff.size(), outbuf, &pcmframesize);
		if (pcmframesize == 0){
			// no output pcm, we are at the end of the stream
			AuBufAvailable = 0;
			sourcebuff.clear();
			if (LoopNum != 0){
				// if we loop, reset readPos
				readPos = startPos;
			}
			break;
		}
		// count total output pcm size 
		outpcmbufsize += pcmframesize;
		// count total output samples
		SumDecodedSamples += decoder->getOutSamples();
		// get consumed source length
		int srcPos = decoder->getSourcePos();
		// remove the consumed source
		sourcebuff.erase(0, srcPos);
		// reduce the available Aubuff size
		// (the available buff size is now used to know if we can read again from file and how many to read)
		AuBufAvailable -= srcPos;
		// move outbuff position to the current end of output 
		outbuf += pcmframesize;
		// increase FrameNum count
		FrameNum++;
	}
	Memory::Write_U32(PCMBuf, pcmAddr);
	return outpcmbufsize;
}

u32 AuCtx::AuGetLoopNum()
{
	return LoopNum;
}

u32 AuCtx::AuSetLoopNum(int loop)
{
	LoopNum = loop;
	return 0;
}

// return 1 to read more data stream, 0 don't read
int AuCtx::AuCheckStreamDataNeeded()
{
	// if we have no available Au buffer, and the current read position in source file is not the end of stream, then we can read
	if (AuBufAvailable < (int)AuBufSize && readPos < (int)endPos){
		return 1;
	}
	return 0;
}

// check how many bytes we have read from source file
u32 AuCtx::AuNotifyAddStreamData(int size)
{
	realReadSize = size;
	int diffszie = realReadSize - askedReadSize;
	// Notify the real read size
	if (diffszie != 0){
		readPos += diffszie;
		AuBufAvailable += diffszie;
	}

	// append AuBuf into sourcebuff
	sourcebuff.append((const char*)Memory::GetPointer(AuBuf), size);

	if (readPos >= endPos && LoopNum != 0){
		// if we need loop, reset readPos
		readPos = startPos;
		// reset LoopNum
		if (LoopNum > 0){
			LoopNum--;
		}
	}

	return 0;
}

// read from stream position srcPos of size bytes into buff
// buff, size and srcPos are all pointers
u32 AuCtx::AuGetInfoToAddStreamData(u32 buff, u32 size, u32 srcPos)
{
	// you can not read beyond file size and the buffersize
	int readsize = std::min((int)AuBufSize - AuBufAvailable, (int)endPos - readPos);

	// we can recharge AuBuf from its begining
	if (Memory::IsValidAddress(buff))
		Memory::Write_U32(AuBuf, buff);
	if (Memory::IsValidAddress(size))
		Memory::Write_U32(readsize, size);
	if (Memory::IsValidAddress(srcPos))
		Memory::Write_U32(readPos, srcPos);

	// preset the readPos and available size, they will be notified later in NotifyAddStreamData.
	askedReadSize = readsize;
	readPos += askedReadSize;
	AuBufAvailable += askedReadSize;

	return 0;
}

u32 AuCtx::AuGetMaxOutputSample()
{
	return MaxOutputSample;
}

u32 AuCtx::AuGetSumDecodedSample()
{
	return SumDecodedSamples;
}

u32 AuCtx::AuResetPlayPosition()
{
	readPos = startPos;
	return 0;
}

int AuCtx::AuGetChannelNum(){
	return Channels;
}

int AuCtx::AuGetBitRate(){
	return BitRate;
}

int AuCtx::AuGetSamplingRate(){
	return SamplingRate;
}

u32 AuCtx::AuResetPlayPositionByFrame(int position){
	readPos = position;
	return 0;
}

int AuCtx::AuGetVersion(){
	return Version;
}

int AuCtx::AuGetFrameNum(){
	return FrameNum;
}

static int _Readbuffer(void *opaque, uint8_t *buf, int buf_size) {
	auto ctx = (AuCtx *)opaque;
	int toread = std::min((int)ctx->AuBufSize, buf_size);
	memcpy(buf, Memory::GetPointer(ctx->AuBuf), toread);
	return toread;
}

static void closeAvioCtxandFormatCtx(AVIOContext* pAVIOCtx, AVFormatContext* pFormatCtx){
	if (pAVIOCtx && pAVIOCtx->buffer)
		av_free(pAVIOCtx->buffer);
	if (pAVIOCtx)
		av_free(pAVIOCtx);
	if (pFormatCtx)
		avformat_close_input(&pFormatCtx);
}

// you need at least have initialized AuBuf, AuBufSize and decoder
bool AuCtx::AuCreateCodecContextFromSource(){
	u8* tempbuf = (u8*)av_malloc(AuBufSize);

	auto pFormatCtx = avformat_alloc_context();
	auto pAVIOCtx = avio_alloc_context(tempbuf, AuBufSize, 0, (void*)this, _Readbuffer, NULL, NULL);
	pFormatCtx->pb = pAVIOCtx;

	int ret;
	// Load audio buffer
	if ((ret = avformat_open_input((AVFormatContext**)&pFormatCtx, NULL, NULL, NULL)) != 0) {
		ERROR_LOG(ME, "avformat_open_input: Cannot open input %d", ret);
		closeAvioCtxandFormatCtx(pAVIOCtx,pFormatCtx);
		return false;
	}

	if ((ret = avformat_find_stream_info(pFormatCtx, NULL)) < 0) {
		ERROR_LOG(ME, "avformat_find_stream_info: Cannot find stream information %d", ret);
		closeAvioCtxandFormatCtx(pAVIOCtx, pFormatCtx);
		return false;
	}
	// reset decoder context
	if (decoder->codecCtx_){
		avcodec_close(decoder->codecCtx_);
		av_free(decoder->codecCtx_);
	}
	decoder->codecCtx_ = pFormatCtx->streams[ret]->codec;

	if (decoder->codec_){
		decoder->codec_ = 0;
	}
	// select the audio stream
	ret = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder->codec_, 0);
	if (ret < 0) {
		if (ret == AVERROR_DECODER_NOT_FOUND) {
			ERROR_LOG(HLE, "av_find_best_stream: No appropriate decoder found");
		}
		else {
			ERROR_LOG(HLE, "av_find_best_stream: Cannot find an audio stream in the input file %d", ret);
		}
		closeAvioCtxandFormatCtx(pAVIOCtx, pFormatCtx);
		return false;
	}

	// close and free AVIO and AVFormat
	// closeAvioCtxandFormatCtx(pAVIOCtx, pFormatCtx);

	// open codec
	if ((ret = avcodec_open2(decoder->codecCtx_, decoder->codec_, NULL)) < 0) {
		avcodec_close(decoder->codecCtx_);
		av_free(decoder->codecCtx_);
		decoder->codecCtx_ = 0;
		decoder->codec_ = 0;
		ERROR_LOG(ME, "avcodec_open2: Cannot open audio decoder %d", ret);
		return false;
	}

	// set audio informations
	SamplingRate = decoder->codecCtx_->sample_rate;
	Channels = decoder->codecCtx_->channels;
	BitRate = decoder->codecCtx_->bit_rate/1000;
	freq = SamplingRate;

	return true;
}
