add_subdirectory(cmake/armips)
if(NOT USING_GLES2)
  add_subdirectory(glew)
endif()

add_subdirectory(snappy)
add_subdirectory(udis86)
