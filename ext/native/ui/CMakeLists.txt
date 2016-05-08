set(SRCS
  ui.cpp
  ui_context.cpp
	view.cpp
	viewgroup.cpp
  screen.cpp
  virtual_input.cpp
)

set(SRCS ${SRCS})

add_library(ui STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)

