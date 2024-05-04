cmake_minimum_required (VERSION 3.2.0)
project (libzstd_static)

set(SRC_DIR ../zstd/lib)
message(${SRC_DIR})

set(ALL_SOURCE_FILES
	${SRC_DIR}/common/debug.c
	${SRC_DIR}/common/entropy_common.c
	${SRC_DIR}/common/error_private.c
	${SRC_DIR}/common/fse_decompress.c
	${SRC_DIR}/common/pool.c
	${SRC_DIR}/common/threading.c
	${SRC_DIR}/common/xxhash.c
	${SRC_DIR}/common/zstd_common.c
	${SRC_DIR}/compress/fse_compress.c
	${SRC_DIR}/compress/hist.c
	${SRC_DIR}/compress/huf_compress.c
	${SRC_DIR}/compress/zstd_compress.c
	${SRC_DIR}/compress/zstd_compress_literals.c
	${SRC_DIR}/compress/zstd_compress_sequences.c
	${SRC_DIR}/compress/zstd_compress_superblock.c
	${SRC_DIR}/compress/zstd_double_fast.c
	${SRC_DIR}/compress/zstd_fast.c
	${SRC_DIR}/compress/zstd_lazy.c
	${SRC_DIR}/compress/zstd_ldm.c
	${SRC_DIR}/compress/zstd_opt.c
	${SRC_DIR}/compress/zstdmt_compress.c
	${SRC_DIR}/decompress/huf_decompress.c
	${SRC_DIR}/decompress/zstd_ddict.c
	${SRC_DIR}/decompress/zstd_decompress.c
	${SRC_DIR}/decompress/zstd_decompress_block.c
	${SRC_DIR}/dictBuilder/cover.c
	${SRC_DIR}/dictBuilder/divsufsort.c
	${SRC_DIR}/dictBuilder/fastcover.c
	${SRC_DIR}/dictBuilder/zdict.c
)

if (MSVC)
    add_compile_options(-DZSTD_DISABLE_ASM)
else ()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "amd64.*|AMD64.*|x86_64.*|X86_64.*")
        set(ALL_SOURCE_FILES ${ALL_SOURCE_FILES} ${SRC_DIR}/decompress/huf_decompress_amd64.S)
    else()
        add_compile_options(-DZSTD_DISABLE_ASM)
    endif()
endif ()

add_library(libzstd_static STATIC ${ALL_SOURCE_FILES})

target_include_directories(libzstd_static PUBLIC ../zstd/lib)
