# libswscale source files (all platforms)
set(LIBSWSCALE_SOURCE_FILES
	${LIBSWSCALE_SRC_DIR}/alphablend.c
	${LIBSWSCALE_SRC_DIR}/gamma.c
	${LIBSWSCALE_SRC_DIR}/hscale_fast_bilinear.c
	${LIBSWSCALE_SRC_DIR}/hscale.c
	${LIBSWSCALE_SRC_DIR}/input.c
	${LIBSWSCALE_SRC_DIR}/options.c
	${LIBSWSCALE_SRC_DIR}/output.c
	${LIBSWSCALE_SRC_DIR}/rgb2rgb.c
	${LIBSWSCALE_SRC_DIR}/slice.c
	${LIBSWSCALE_SRC_DIR}/swscale_unscaled.c
	${LIBSWSCALE_SRC_DIR}/swscale.c
	${LIBSWSCALE_SRC_DIR}/utils.c
	${LIBSWSCALE_SRC_DIR}/vscale.c
	${LIBSWSCALE_SRC_DIR}/yuv2rgb.c
)

set(LIBSWSCALE_HEADERS
	${LIBSWSCALE_SRC_DIR}/swscale.h
	${LIBSWSCALE_SRC_DIR}/version.h
)
