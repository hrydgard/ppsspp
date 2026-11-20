# The AArch64 (NEON) sources PPSSPP builds in addition to sources.mk, relative to ext/ffmpeg, for the
# components it enables. config.h turns them on for GCC and Clang on Linux and Android; every build
# system that includes this has to match that.
FFMPEG_SOURCES_AARCH64 := \
	libavcodec/aarch64/fft_init_aarch64.c \
	libavcodec/aarch64/fft_neon.S \
	libavcodec/aarch64/h264chroma_init_aarch64.c \
	libavcodec/aarch64/h264cmc_neon.S \
	libavcodec/aarch64/h264dsp_init_aarch64.c \
	libavcodec/aarch64/h264dsp_neon.S \
	libavcodec/aarch64/h264idct_neon.S \
	libavcodec/aarch64/h264pred_init.c \
	libavcodec/aarch64/h264pred_neon.S \
	libavcodec/aarch64/h264qpel_init_aarch64.c \
	libavcodec/aarch64/h264qpel_neon.S \
	libavcodec/aarch64/hpeldsp_neon.S \
	libavcodec/aarch64/imdct15_init.c \
	libavcodec/aarch64/imdct15_neon.S \
	libavcodec/aarch64/mdct_neon.S \
	libavcodec/aarch64/mpegaudiodsp_init.c \
	libavcodec/aarch64/mpegaudiodsp_neon.S \
	libavcodec/aarch64/videodsp.S \
	libavcodec/aarch64/videodsp_init.c \
	libavutil/aarch64/cpu.c \
	libavutil/aarch64/float_dsp_init.c \
	libavutil/aarch64/float_dsp_neon.S
