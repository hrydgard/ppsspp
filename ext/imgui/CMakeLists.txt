set(IMGUI_FILES
	imgui.cpp
	imgui_demo.cpp
	imgui_draw.cpp
	imgui_tables.cpp
	imgui_widgets.cpp
	imgui_impl_vulkan.cpp
)

add_library(imgui ${IMGUI_FILES})
target_include_directories(imgui PRIVATE ../..)
