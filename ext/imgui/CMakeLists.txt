set(IMGUI_FILES
	imgui.cpp
	imgui_demo.cpp
	imgui_draw.cpp
	imgui_extras.cpp
	imgui_tables.cpp
	imgui_widgets.cpp
	imgui_impl_thin3d.cpp
	imgui_impl_platform.cpp
)

add_library(imgui ${IMGUI_FILES})
target_include_directories(imgui PRIVATE ../..)
