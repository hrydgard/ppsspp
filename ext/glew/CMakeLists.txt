#find_package(GLEW)
if(NOT GLEW_FOUND)
  find_package(OpenGL REQUIRED)
  add_library(glew STATIC
    GL/glew.h
    GL/glxew.h
    GL/wglew.h
    glew.c
  )
  target_link_libraries(glew PUBLIC ${OPENGL_LIBRARIES})
  target_compile_definitions(glew PUBLIC GLEW_STATIC)
  target_include_directories(glew PUBLIC . ${OPENGL_INCLUDE_DIR})
  set_target_properties(glew PROPERTIES
    EXCLUDE_FROM_ALL ON
  )
endif()
