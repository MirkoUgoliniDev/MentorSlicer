# LERC library integration for UOS systems
# LERC (Limited Error Raster Compression) is a library for efficient raster data compression

# Include UOS detection module
include(${CMAKE_SOURCE_DIR}/../cmake/modules/CheckUos.cmake)

# Build LERC on UOS or LoongArch systems
set(_build_lerc FALSE)
if (IS_UOS)
    set(_build_lerc TRUE)
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "loongarch")
    set(_build_lerc TRUE)
endif()

if(_build_lerc)
    message(STATUS "Building LERC for UOS/LoongArch system")

    orcaslicer_add_cmake_project(
        Lerc
        URL https://github.com/Esri/lerc/archive/refs/tags/v4.0.0.tar.gz
        URL_HASH SHA256=91431c2b16d0e3de6cbaea188603359f87caed08259a645fd5a3805784ee30a0
        CMAKE_ARGS
            -DBUILD_SHARED_LIBS=OFF
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DCMAKE_BUILD_TYPE=Release
            -DBUILD_TESTING=OFF
    )

    set(LERC_FOUND TRUE CACHE BOOL "LERC library found")
    set(LERC_LIBRARIES Lerc CACHE STRING "LERC library name")
else()
    message(STATUS "LERC is only built on UOS/LoongArch systems, skipping...")
    set(LERC_FOUND FALSE CACHE BOOL "LERC library not available")
endif()

