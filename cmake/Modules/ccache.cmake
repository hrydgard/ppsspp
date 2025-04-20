# Using ccache greatly improves the speed of our CI builds, let's enable for all.
# Without this, our CI can't use ccache for clang, for some reason.
find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_FOUND})
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_FOUND})

    # ccache uses -I when compiling without preprocessor, which makes clang complain.
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        add_compile_options(-Qunused-arguments -fcolor-diagnostics)
    endif()
endif(CCACHE_FOUND)
