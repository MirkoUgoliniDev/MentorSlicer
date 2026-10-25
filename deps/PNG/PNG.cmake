if (APPLE)
    # Only disable NEON extension for Apple ARM builds, leave it enabled for Raspberry PI.
    set(_disable_neon_extension "-DPNG_ARM_NEON=off")
else ()
    set(_disable_neon_extension "")
endif ()

if(APPLE AND IS_CROSS_COMPILE)
# TODO: check if it doesn't create problem when compiling from arm to x86_64
    orcaslicer_add_cmake_project(PNG 
        GIT_REPOSITORY https://github.com/glennrp/libpng.git 
        GIT_TAG v1.6.35
        DEPENDS ${ZLIB_PKG}
        PATCH_COMMAND       ${GIT_EXECUTABLE} checkout -f -- . && git clean -df &&
                            ${GIT_EXECUTABLE} apply --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/macos-arm64.patch
        CMAKE_ARGS
            -DPNG_SHARED=OFF
            -DPNG_STATIC=ON
            -DPNG_PREFIX=prusaslicer_
            -DPNG_TESTS=OFF
            -DDISABLE_DEPENDENCY_TRACKING=OFF
            ${_disable_neon_extension}
    )
else ()
set(_patch_step "")
    if (APPLE)
        set(_patch_step PATCH_COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/PNG.patch)
    endif ()
    orcaslicer_add_cmake_project(PNG 
        # GIT_REPOSITORY https://github.com/glennrp/libpng.git 
        # GIT_TAG v1.6.35
        URL https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.59.zip
        URL_HASH SHA256=6fd97a1e5e2ac55cec0a538a9800a351eb3844086736291844ebd12db72ba433
        DEPENDS ${ZLIB_PKG}
        "${_patch_step}"
        CMAKE_ARGS
            -DPNG_SHARED=OFF
            -DPNG_STATIC=ON
            -DPNG_PREFIX=prusaslicer_
            -DPNG_TESTS=OFF
            -DPNG_TOOLS=OFF
            -DDISABLE_DEPENDENCY_TRACKING=OFF
            ${_disable_neon_extension}
)
endif()

if (MSVC)
    add_debug_dep(dep_PNG)
endif ()
