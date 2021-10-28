# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying file COPYING-CMAKE-SCRIPTS or
# https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindZSTD
--------

Find the ZSTD library

Zstandard C/C++ library is built with CMake. So this find module
should be removed when ZStandard library export cmake config files
as distribution. Unfortunately ZStandard does not export it,
we need to prepare find module.

IMPORTED targets
^^^^^^^^^^^^^^^^

This module defines the following :prop_tgt:`IMPORTED` target: ``ZSTD::zstd``

Result variables
^^^^^^^^^^^^^^^^

This module will set the following variables if found:

``ZSTD_INCLUDE_DIRS`` - where to find zstd.h, etc.
``ZSTD_LIBRARIES`` - the libraries to link against to use ZSTD.
``ZSTD_VERSION`` - version of the ZSTD library found
``ZSTD_FOUND`` - TRUE if found

::

  ``ZSTD_VERSION_MAJOR``  - The major version of zstd
  ``ZSTD_VERSION_MINOR``  - The minor version of zstd
  ``ZSTD_VERSION_RELEASE``  - The release version of zstd

#]=======================================================================]

find_package(PkgConfig)
pkg_check_modules(PC_ZSTD QUIET libzstd)

find_path(
  ZSTD_INCLUDE_DIR
  NAMES zstd.h
  PATHS ${PC_ZSTD_INCLUDE_DIRS}
)
find_library(
  ZSTD_LIBRARY
  NAMES zstd
  PATHS ${PC_ZSTD_LIBRARY_DIRS}
)

# Extract version information from the header file
if(EXISTS "${ZSTD_INCLUDE_DIR}/zstd.h")
  file(STRINGS "${ZSTD_INCLUDE_DIR}/zstd.h"
       _ZSTD_VERSION_MAJOR REGEX "^#define ZSTD_VERSION_MAJOR")
  string(REGEX MATCH "[0-9]+" ZSTD_VERSION_MAJOR ${_ZSTD_VERSION_MAJOR})
  file(STRINGS "${ZSTD_INCLUDE_DIR}/zstd.h"
       _ZSTD_VERSION_MINOR REGEX "^#define ZSTD_VERSION_MINOR")
  string(REGEX MATCH "[0-9]+" ZSTD_VERSION_MINOR ${_ZSTD_VERSION_MINOR} )
  file(STRINGS "${ZSTD_INCLUDE_DIR}/zstd.h"
       _ZSTD_VERSION_RELEASE REGEX "^#define ZSTD_VERSION_RELEASE")
  string(REGEX MATCH "[0-9]+" ZSTD_VERSION_RELEASE ${_ZSTD_VERSION_RELEASE} )
  set(ZSTD_VERSION ${ZSTD_VERSION_MAJOR}.${ZSTD_VERSION_MINOR}.${ZSTD_VERSION_RELEASE})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  ZSTD
  FOUND_VAR ZSTD_FOUND
  REQUIRED_VARS ZSTD_LIBRARY ZSTD_INCLUDE_DIR
  VERSION_VAR ZSTD_VERSION
  HANDLE_COMPONENTS)
mark_as_advanced(ZSTD_INCLUDE_DIR ZSTD_LIBRARY)

include(FeatureSummary)
set_package_properties(
  ZSTD PROPERTIES
  DESCRIPTION "Zstandard - Fast real-time compression algorithm"
  URL "https://github.com/facebook/zstd")

if(ZSTD_FOUND)
  set(ZSTD_INCLUDE_DIRS ${ZSTD_INCLUDE_DIR})
  set(ZSTD_LIBRARIES ${ZSTD_LIBRARY})
  set(ZSTD_DEFINITIONS ${PC_ZSTD_CFLAGS_OTHER})
  if(NOT TARGET ZSTD::zstd)
    add_library(ZSTD::zstd UNKNOWN IMPORTED)
    set_target_properties(ZSTD::zstd PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${ZSTD_INCLUDE_DIR})
    if(EXISTS "${ZSTD_LIBRARY}")
      set_target_properties(ZSTD::zstd PROPERTIES IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
        IMPORTED_LOCATION "${ZSTD_LIBRARY}")
    endif()
  endif()
endif()
