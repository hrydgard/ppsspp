
set(LIBRETRO_SRCS
	libretro.cpp
	LibretroGraphicsContext.cpp
	LibretroGLContext.cpp
	LibretroGLCoreContext.cpp
	LibretroVulkanContext.cpp
	libretro_vulkan.cpp)

if(WIN32)
	set(LIBRETRO_SRCS ${LIBRETRO_SRCS}
		LibretroD3D11Context.cpp)
endif()

include_directories(libretro)

add_library(ppsspp_libretro SHARED ${LIBRETRO_SRCS})
set(LIBRARY_OUTPUT_PATH "${CMAKE_BINARY_DIR}")
set_target_properties(ppsspp_libretro PROPERTIES PREFIX "")
if(ANDROID)
	set_target_properties(ppsspp_libretro PROPERTIES SUFFIX "_android.so")
endif()

if(NOT MSVC)
   if (APPLE OR IOS)
   	target_link_libraries(ppsspp_libretro objc "-Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/libretro.osx.def")
   else()
   	target_link_libraries(ppsspp_libretro "-Wl,-Bsymbolic")
   endif()
endif()
target_link_libraries(ppsspp_libretro ${LinkCommon})
