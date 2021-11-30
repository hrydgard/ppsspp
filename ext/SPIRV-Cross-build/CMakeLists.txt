# Copyright 2016 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.0)
project(SPIRV-Cross)
enable_testing()

option(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS "Instead of throwing exceptions assert" ON)

set(SRCDIR "${CMAKE_CURRENT_SOURCE_DIR}/../SPIRV-Cross")

add_library(spirv-cross-core STATIC
		${SRCDIR}/GLSL.std.450.h
		${SRCDIR}/spirv_common.hpp
		${SRCDIR}/spirv.hpp
		${SRCDIR}/spirv_cross.hpp
		${SRCDIR}/spirv_cross.cpp
		${SRCDIR}/spirv_cross_containers.hpp
		${SRCDIR}/spirv_cross_error_handling.hpp
		${SRCDIR}/spirv_cross_util.hpp
		${SRCDIR}/spirv_cross_util.cpp
		${SRCDIR}/spirv_cfg.hpp
		${SRCDIR}/spirv_cfg.cpp
		${SRCDIR}/spirv_cross_parsed_ir.hpp
		${SRCDIR}/spirv_cross_parsed_ir.cpp
		${SRCDIR}/spirv_parser.hpp
		${SRCDIR}/spirv_parser.cpp)

add_library(spirv-cross-glsl STATIC
		${SRCDIR}/spirv_glsl.cpp
		${SRCDIR}/spirv_glsl.hpp)

add_library(spirv-cross-cpp STATIC
		${SRCDIR}/spirv_cpp.hpp
		${SRCDIR}/spirv_cpp.cpp)

add_library(spirv-cross-msl STATIC
		${SRCDIR}/spirv_msl.hpp
		${SRCDIR}/spirv_msl.cpp)

add_library(spirv-cross-hlsl STATIC
		${SRCDIR}/spirv_hlsl.hpp
		${SRCDIR}/spirv_hlsl.cpp)

	#add_executable(spirv-cross main.cpp)
	#target_link_libraries(spirv-cross spirv-cross-glsl spirv-cross-cpp spirv-cross-msl spirv-cross-hlsl spirv-cross-core)
target_link_libraries(spirv-cross-glsl spirv-cross-core)
target_link_libraries(spirv-cross-msl spirv-cross-glsl)
target_link_libraries(spirv-cross-cpp spirv-cross-glsl)
target_link_libraries(spirv-cross-hlsl spirv-cross-glsl)
target_include_directories(spirv-cross-core PUBLIC ${SRCDIR})

set(spirv-compiler-options "")
set(spirv-compiler-defines "")

if(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS)
  set(spirv-compiler-defines ${spirv-compiler-defines} SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS)
endif()

# To specify special debug or optimization options, use
# -DCMAKE_CXX_COMPILE_FLAGS
# However, we require the C++11 dialect.
if (NOT "${MSVC}")
  set(spirv-compiler-options ${spirv-compiler-options})
  set(spirv-compiler-defines ${spirv-compiler-defines} __STDC_LIMIT_MACROS)

  if(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS)
    set(spirv-compiler-options ${spirv-compiler-options} -fno-exceptions)
  endif()
endif()

target_compile_options(spirv-cross-core PRIVATE ${spirv-compiler-options})
target_compile_options(spirv-cross-glsl PRIVATE ${spirv-compiler-options})
target_compile_options(spirv-cross-msl PRIVATE ${spirv-compiler-options})
target_compile_options(spirv-cross-cpp PRIVATE ${spirv-compiler-options})
target_compile_options(spirv-cross-hlsl PRIVATE ${spirv-compiler-options})
target_compile_definitions(spirv-cross-core PRIVATE ${spirv-compiler-defines})
target_compile_definitions(spirv-cross-glsl PRIVATE ${spirv-compiler-defines})
target_compile_definitions(spirv-cross-msl PRIVATE ${spirv-compiler-defines})
target_compile_definitions(spirv-cross-cpp PRIVATE ${spirv-compiler-defines})
target_compile_definitions(spirv-cross-hlsl PRIVATE ${spirv-compiler-defines})
