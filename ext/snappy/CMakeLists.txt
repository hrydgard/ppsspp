find_package(Snappy)

if(SNAPPY_FOUND AND USE_SYSTEM_SNAPPY)
  add_library(system_snappy INTERFACE)
  add_library(Ext::Snappy ALIAS system_snappy)
  target_link_libraries(system_snappy INTERFACE Snappy::Snappy)
else()
  add_library(snappy STATIC
    snappy-c.cpp
    snappy-c.h
    snappy-internal.h
    snappy-sinksource.cpp
    snappy-sinksource.h
    snappy-stubs-internal.cpp
    snappy-stubs-internal.h
    snappy-stubs-public.h
    snappy.cpp
    snappy.h
  )
  target_include_directories(snappy PUBLIC .)

  if(NOT MSVC)
    target_compile_options(snappy PRIVATE "-O3")
  endif()

  add_library(Ext::Snappy ALIAS snappy)
endif()
