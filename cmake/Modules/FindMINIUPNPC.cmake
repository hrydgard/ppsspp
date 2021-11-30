
# --------------------------------- FindMINIUPNPC Start ---------------------------------
# Locate miniupnp library
# This module defines
#  MINIUPNP_FOUND, if false, do not try to link to miniupnp
#  MINIUPNP_LIBRARY, the miniupnp variant
#  MINIUPNP_INCLUDE_DIR, where to find miniupnpc.h and family)
#  MINIUPNPC_VERSION_1_7_OR_HIGHER, set if we detect the version of miniupnpc is 1.7 or higher
#
# Note that the expected include convention is
#  #include "miniupnpc.h"
# and not
#  #include <miniupnpc/miniupnpc.h>
# This is because, the miniupnpc location is not standardized and may exist
# in locations other than miniupnpc/

if (MINIUPNP_INCLUDE_DIR AND MINIUPNP_LIBRARY)
	# Already in cache, be silent
	set(MINIUPNP_FIND_QUIETLY TRUE)
endif ()

find_path(MINIUPNP_INCLUDE_DIR miniupnpc.h
	HINTS $ENV{MINIUPNP_INCLUDE_DIR}
	PATH_SUFFIXES miniupnpc
)

find_library(MINIUPNP_LIBRARY miniupnpc
	HINTS $ENV{MINIUPNP_LIBRARY}
)

find_library(MINIUPNP_STATIC_LIBRARY libminiupnpc.a
	HINTS $ENV{MINIUPNP_STATIC_LIBRARY}
)

set(MINIUPNP_INCLUDE_DIRS ${MINIUPNP_INCLUDE_DIR})
set(MINIUPNP_LIBRARIES ${MINIUPNP_LIBRARY})
set(MINIUPNP_STATIC_LIBRARIES ${MINIUPNP_STATIC_LIBRARY})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
	MINIUPNPC DEFAULT_MSG
	MINIUPNP_INCLUDE_DIR
	MINIUPNP_LIBRARY
)

IF(MINIUPNPC_FOUND)
	file(STRINGS "${MINIUPNP_INCLUDE_DIR}/miniupnpc.h" MINIUPNPC_API_VERSION_STR REGEX "^#define[\t ]+MINIUPNPC_API_VERSION[\t ]+[0-9]+")
	if(MINIUPNPC_API_VERSION_STR MATCHES "^#define[\t ]+MINIUPNPC_API_VERSION[\t ]+([0-9]+)")
		set(MINIUPNPC_API_VERSION "${CMAKE_MATCH_1}")
	if (${MINIUPNPC_API_VERSION} GREATER "10" OR ${MINIUPNPC_API_VERSION} EQUAL "10")
		message(STATUS "Found miniupnpc API version " ${MINIUPNPC_API_VERSION})
		set(MINIUPNP_FOUND true)
		set(MINIUPNPC_VERSION_1_7_OR_HIGHER true)
	endif()
endif()

ENDIF()

mark_as_advanced(MINIUPNP_INCLUDE_DIR MINIUPNP_LIBRARY MINIUPNP_STATIC_LIBRARY)
# --------------------------------- FindMINIUPNPC End ---------------------------------
