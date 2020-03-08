set(SRCS
  ui.cpp
  ui_context.cpp
  view.cpp
  viewgroup.cpp
  screen.cpp
  root.cpp
)

set(SRCS ${SRCS})

add_library(ui STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)

