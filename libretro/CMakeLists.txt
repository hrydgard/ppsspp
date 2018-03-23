set(LIBRETRO_SRCS
  libretro.cpp
  libretro_osm.cpp
)

add_library(ppsspp_libretro SHARED ${LIBRETRO_SRCS})
set_target_properties(ppsspp_libretro PROPERTIES PREFIX "")
if(ANDROID)
	set_target_properties(ppsspp_libretro PROPERTIES SUFFIX "_android.so")
endif()

target_link_libraries(ppsspp_libretro "-Wl,-Bsymbolic")
target_link_libraries(ppsspp_libretro ${LinkCommon})
