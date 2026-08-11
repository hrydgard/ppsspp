# libavcodec source files (all platforms)
set(LIBAVCODEC_SOURCE_FILES
	${LIBAVCODEC_SRC_DIR}/aac_ac3_parser.c
	${LIBAVCODEC_SRC_DIR}/aac_parser.c
	${LIBAVCODEC_SRC_DIR}/aacadtsdec.c
	${LIBAVCODEC_SRC_DIR}/aacdec.c
	${LIBAVCODEC_SRC_DIR}/aacps_float.c
	${LIBAVCODEC_SRC_DIR}/aacpsdsp_float.c
	${LIBAVCODEC_SRC_DIR}/aacsbr.c
	${LIBAVCODEC_SRC_DIR}/aactab.c
	${LIBAVCODEC_SRC_DIR}/aandcttab.c
	${LIBAVCODEC_SRC_DIR}/allcodecs.c
	${LIBAVCODEC_SRC_DIR}/audioconvert.c
	${LIBAVCODEC_SRC_DIR}/avdct.c
	${LIBAVCODEC_SRC_DIR}/avfft.c
	${LIBAVCODEC_SRC_DIR}/avpacket.c
	${LIBAVCODEC_SRC_DIR}/avpicture.c
	${LIBAVCODEC_SRC_DIR}/bitstream_filter.c
	${LIBAVCODEC_SRC_DIR}/bitstream.c
	${LIBAVCODEC_SRC_DIR}/blockdsp.c
	${LIBAVCODEC_SRC_DIR}/bswapdsp.c
	${LIBAVCODEC_SRC_DIR}/cabac.c
	${LIBAVCODEC_SRC_DIR}/codec_desc.c
	${LIBAVCODEC_SRC_DIR}/d3d11va.c
	${LIBAVCODEC_SRC_DIR}/dct.c
	${LIBAVCODEC_SRC_DIR}/dct32_fixed.c
	${LIBAVCODEC_SRC_DIR}/dct32_float.c
	${LIBAVCODEC_SRC_DIR}/dirac.c
	${LIBAVCODEC_SRC_DIR}/dv_profile.c
	${LIBAVCODEC_SRC_DIR}/error_resilience.c
	${LIBAVCODEC_SRC_DIR}/exif.c
	${LIBAVCODEC_SRC_DIR}/faandct.c
	${LIBAVCODEC_SRC_DIR}/faanidct.c
	${LIBAVCODEC_SRC_DIR}/fdctdsp.c
	${LIBAVCODEC_SRC_DIR}/fft_fixed_32.c
	${LIBAVCODEC_SRC_DIR}/fft_fixed.c
	${LIBAVCODEC_SRC_DIR}/fft_float.c
	${LIBAVCODEC_SRC_DIR}/fft_init_table.c
	${LIBAVCODEC_SRC_DIR}/ffv1.c
	${LIBAVCODEC_SRC_DIR}/ffv1enc.c
	${LIBAVCODEC_SRC_DIR}/flvdec.c
	${LIBAVCODEC_SRC_DIR}/flvenc.c
	${LIBAVCODEC_SRC_DIR}/frame_thread_encoder.c
	${LIBAVCODEC_SRC_DIR}/golomb.c
	${LIBAVCODEC_SRC_DIR}/h263_parser.c
	${LIBAVCODEC_SRC_DIR}/h263.c
	${LIBAVCODEC_SRC_DIR}/h263data.c
	${LIBAVCODEC_SRC_DIR}/h263dec.c
	${LIBAVCODEC_SRC_DIR}/h263dsp.c
	${LIBAVCODEC_SRC_DIR}/h264_cabac.c
	${LIBAVCODEC_SRC_DIR}/h264_cavlc.c
	${LIBAVCODEC_SRC_DIR}/h264_direct.c
	${LIBAVCODEC_SRC_DIR}/h264_loopfilter.c
	${LIBAVCODEC_SRC_DIR}/h264_mb.c
	${LIBAVCODEC_SRC_DIR}/h264_parser.c
	${LIBAVCODEC_SRC_DIR}/h264_picture.c
	${LIBAVCODEC_SRC_DIR}/h264_ps.c
	${LIBAVCODEC_SRC_DIR}/h264_refs.c
	${LIBAVCODEC_SRC_DIR}/h264_sei.c
	${LIBAVCODEC_SRC_DIR}/h264_slice.c
	${LIBAVCODEC_SRC_DIR}/h264.c
	${LIBAVCODEC_SRC_DIR}/h264chroma.c
	${LIBAVCODEC_SRC_DIR}/h264dsp.c
	${LIBAVCODEC_SRC_DIR}/h264idct.c
	${LIBAVCODEC_SRC_DIR}/h264pred.c
	${LIBAVCODEC_SRC_DIR}/h264qpel.c
	${LIBAVCODEC_SRC_DIR}/hpeldsp.c
	${LIBAVCODEC_SRC_DIR}/huffman.c
	${LIBAVCODEC_SRC_DIR}/huffyuv.c
	${LIBAVCODEC_SRC_DIR}/huffyuvenc.c
	${LIBAVCODEC_SRC_DIR}/huffyuvencdsp.c
	${LIBAVCODEC_SRC_DIR}/idctdsp.c
	${LIBAVCODEC_SRC_DIR}/imdct15.c
	${LIBAVCODEC_SRC_DIR}/imgconvert.c
	${LIBAVCODEC_SRC_DIR}/intelh263dec.c
	${LIBAVCODEC_SRC_DIR}/ituh263dec.c
	${LIBAVCODEC_SRC_DIR}/ituh263enc.c
	${LIBAVCODEC_SRC_DIR}/jfdctfst.c
	${LIBAVCODEC_SRC_DIR}/jfdctint.c
	${LIBAVCODEC_SRC_DIR}/jpegtables.c
	${LIBAVCODEC_SRC_DIR}/jrevdct.c
	${LIBAVCODEC_SRC_DIR}/kbdwin.c
	${LIBAVCODEC_SRC_DIR}/latm_parser.c
	${LIBAVCODEC_SRC_DIR}/lossless_videodsp.c
	${LIBAVCODEC_SRC_DIR}/mathtables.c
	${LIBAVCODEC_SRC_DIR}/mdct_fixed_32.c
	${LIBAVCODEC_SRC_DIR}/mdct_fixed.c
	${LIBAVCODEC_SRC_DIR}/mdct_float.c
	${LIBAVCODEC_SRC_DIR}/me_cmp.c
	${LIBAVCODEC_SRC_DIR}/mjpegbdec.c
	${LIBAVCODEC_SRC_DIR}/mjpegdec.c
	${LIBAVCODEC_SRC_DIR}/motion_est.c
	${LIBAVCODEC_SRC_DIR}/mpeg_er.c
	${LIBAVCODEC_SRC_DIR}/mpeg12.c
	${LIBAVCODEC_SRC_DIR}/mpeg12data.c
	${LIBAVCODEC_SRC_DIR}/mpeg12dec.c
	${LIBAVCODEC_SRC_DIR}/mpeg4audio.c
	${LIBAVCODEC_SRC_DIR}/mpeg4video_parser.c
	${LIBAVCODEC_SRC_DIR}/mpeg4video.c
	${LIBAVCODEC_SRC_DIR}/mpeg4videodec.c
	${LIBAVCODEC_SRC_DIR}/mpeg4videoenc.c
	${LIBAVCODEC_SRC_DIR}/mpegaudio_parser.c
	${LIBAVCODEC_SRC_DIR}/mpegaudio.c
	${LIBAVCODEC_SRC_DIR}/mpegaudiodata.c
	${LIBAVCODEC_SRC_DIR}/mpegaudiodec_fixed.c
	${LIBAVCODEC_SRC_DIR}/mpegaudiodecheader.c
	${LIBAVCODEC_SRC_DIR}/mpegaudiodsp_data.c
	${LIBAVCODEC_SRC_DIR}/mpegaudiodsp_fixed.c
	${LIBAVCODEC_SRC_DIR}/mpegaudiodsp_float.c
	${LIBAVCODEC_SRC_DIR}/mpegaudiodsp.c
	${LIBAVCODEC_SRC_DIR}/mpegpicture.c
	${LIBAVCODEC_SRC_DIR}/mpegutils.c
	${LIBAVCODEC_SRC_DIR}/mpegvideo_enc.c
	${LIBAVCODEC_SRC_DIR}/mpegvideo_motion.c
	${LIBAVCODEC_SRC_DIR}/mpegvideo_parser.c
	${LIBAVCODEC_SRC_DIR}/mpegvideo.c
	${LIBAVCODEC_SRC_DIR}/mpegvideodata.c
	${LIBAVCODEC_SRC_DIR}/mpegvideodsp.c
	${LIBAVCODEC_SRC_DIR}/mpegvideoencdsp.c
	${LIBAVCODEC_SRC_DIR}/options.c
	${LIBAVCODEC_SRC_DIR}/parser.c
	${LIBAVCODEC_SRC_DIR}/pcm.c
	${LIBAVCODEC_SRC_DIR}/pixblockdsp.c
	${LIBAVCODEC_SRC_DIR}/profiles.c
	${LIBAVCODEC_SRC_DIR}/qpeldsp.c
	${LIBAVCODEC_SRC_DIR}/qsv_api.c
	${LIBAVCODEC_SRC_DIR}/rangecoder.c
	${LIBAVCODEC_SRC_DIR}/ratecontrol.c
	${LIBAVCODEC_SRC_DIR}/raw.c
	${LIBAVCODEC_SRC_DIR}/rdft.c
	${LIBAVCODEC_SRC_DIR}/resample.c
	${LIBAVCODEC_SRC_DIR}/resample2.c
	${LIBAVCODEC_SRC_DIR}/rl.c
	${LIBAVCODEC_SRC_DIR}/sbrdsp.c
	${LIBAVCODEC_SRC_DIR}/simple_idct.c
	${LIBAVCODEC_SRC_DIR}/sinewin_fixed.c
	${LIBAVCODEC_SRC_DIR}/sinewin.c
	${LIBAVCODEC_SRC_DIR}/startcode.c
	${LIBAVCODEC_SRC_DIR}/tiff_common.c
	${LIBAVCODEC_SRC_DIR}/utils.c	
	${LIBAVCODEC_SRC_DIR}/videodsp.c
	${LIBAVCODEC_SRC_DIR}/vorbis_parser.c
	${LIBAVCODEC_SRC_DIR}/xiph.c
	${LIBAVCODEC_SRC_DIR}/xvididct.c
)

# includes to ship with libavcodec
set(LIBAVCODEC_HEADERS
	${LIBAVCODEC_SRC_DIR}/avcodec.h
	${LIBAVCODEC_SRC_DIR}/avdct.h
	${LIBAVCODEC_SRC_DIR}/avfft.h
	${LIBAVCODEC_SRC_DIR}/dv_profile.h
	${LIBAVCODEC_SRC_DIR}/d3d11va.h
	${LIBAVCODEC_SRC_DIR}/dirac.h
	${LIBAVCODEC_SRC_DIR}/dxva2.h
	${LIBAVCODEC_SRC_DIR}/qsv.h
	${LIBAVCODEC_SRC_DIR}/vaapi.h
	${LIBAVCODEC_SRC_DIR}/vda.h
	${LIBAVCODEC_SRC_DIR}/vdpau.h
	${LIBAVCODEC_SRC_DIR}/version.h
	${LIBAVCODEC_SRC_DIR}/videotoolbox.h
	${LIBAVCODEC_SRC_DIR}/vorbis_parser.h
	${LIBAVCODEC_SRC_DIR}/xvmc.h
)

# OS and configure option/check specific additional sources
if (WIN32)
	list(APPEND LIBAVCODEC_SOURCE_FILES ${LIBAVCODEC_SRC_DIR}/file_open.c)
	if (CONFIG_DXVA2 AND HAVE_DXVA2_LIB)
		list(APPEND LIBAVCODEC_SOURCE_FILES ${LIBAVCODEC_SRC_DIR}/dxva2.c)
	endif()
endif()

if (Threads_FOUND)
	list(APPEND LIBAVCODEC_SOURCE_FILES ${LIBAVCODEC_SRC_DIR}/pthread.c)
	list(APPEND LIBAVCODEC_SOURCE_FILES ${LIBAVCODEC_SRC_DIR}/pthread_slice.c)
	list(APPEND LIBAVCODEC_SOURCE_FILES ${LIBAVCODEC_SRC_DIR}/pthread_frame.c)
endif()

# Experimental addition to troubleshoot stubborn builds that inject ARCH_X86
if (ARCH_X86)
	list(APPEND LIBAVCODEC_SOURCE_FILES ${LIBAVCODEC_SRC_DIR}/x86/lossless_videodsp_init.c)
endif()

