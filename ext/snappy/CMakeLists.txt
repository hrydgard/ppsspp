find_package(Snappy)

if(NOT SNAPPY_FOUND)
  add_library(snappy STATIC
    snappy-c.cpp
    snappy-c.h
    snappy-internal.h
    snappy-sinksource.h
    snappy-stubs-internal.h
    snappy-stubs-public.h
    snappy.cpp
    snappy.h
  )
  target_include_directories(snappy PUBLIC .)

  if(NOT MSVC)
    target_compile_options(snappy PRIVATE "-O3")
  endif()

  add_library(Snappy::Snappy ALIAS snappy)
endif()
