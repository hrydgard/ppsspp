set(CMAKE_REQUIRED_LIBRARIES iconv)
check_symbol_exists(iconv "iconv.h" HAVE_ICONV_LIB)

add_library(utf8 STATIC
    charset.c
    iconvert.c
    utf8.c)

target_link_libraries(utf8 PUBLIC grabbag $<$<BOOL:${HAVE_ICONV_LIB}>:iconv>)
