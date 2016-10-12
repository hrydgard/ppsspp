# Using ccache greatly improves the speed of our CI builds, let's enable for all.
# Without this, our CI can't use ccache for clang, for some reason.
find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ${CCACHE_FOUND})
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ${CCACHE_FOUND})

    # ccache uses -I when compiling without preprocessor, which makes clang complain.
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Qunused-arguments -fcolor-diagnostics")
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Qunused-arguments -fcolor-diagnostics")
    endif()
endif(CCACHE_FOUND)
