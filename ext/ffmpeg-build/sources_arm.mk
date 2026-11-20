# The 32-bit ARM sources (ARMv7, NEON detected at run time) PPSSPP builds in addition to sources.mk,
# relative to ext/ffmpeg, for the components it enables. config.h turns them on for GCC and Clang
# on Linux and Android; every build system that includes this has to match that.
FFMPEG_SOURCES_ARM := \
	libavcodec/arm/aacpsdsp_init_arm.c \
	libavcodec/arm/aacpsdsp_neon.S \
	libavcodec/arm/fft_init_arm.c \
	libavcodec/arm/fft_neon.S \
	libavcodec/arm/fft_vfp.S \
	libavcodec/arm/h264chroma_init_arm.c \
	libavcodec/arm/h264cmc_neon.S \
	libavcodec/arm/h264dsp_init_arm.c \
	libavcodec/arm/h264dsp_neon.S \
	libavcodec/arm/h264idct_neon.S \
	libavcodec/arm/h264pred_init_arm.c \
	libavcodec/arm/h264pred_neon.S \
	libavcodec/arm/h264qpel_init_arm.c \
	libavcodec/arm/h264qpel_neon.S \
	libavcodec/arm/hpeldsp_neon.S \
	libavcodec/arm/mdct_neon.S \
	libavcodec/arm/mdct_vfp.S \
	libavcodec/arm/me_cmp_armv6.S \
	libavcodec/arm/me_cmp_init_arm.c \
	libavcodec/arm/mpegaudiodsp_fixed_armv6.S \
	libavcodec/arm/mpegaudiodsp_init_arm.c \
	libavcodec/arm/rdft_neon.S \
	libavcodec/arm/sbrdsp_init_arm.c \
	libavcodec/arm/sbrdsp_neon.S \
	libavcodec/arm/startcode_armv6.S \
	libavcodec/arm/videodsp_armv5te.S \
	libavcodec/arm/videodsp_init_arm.c \
	libavcodec/arm/videodsp_init_armv5te.c \
	libavutil/arm/cpu.c \
	libavutil/arm/float_dsp_init_arm.c \
	libavutil/arm/float_dsp_init_neon.c \
	libavutil/arm/float_dsp_init_vfp.c \
	libavutil/arm/float_dsp_neon.S \
	libavutil/arm/float_dsp_vfp.S \
	libswscale/arm/swscale_unscaled.c \
	libswscale/arm/yuv2rgb_neon.S
