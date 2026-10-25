if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if (JPEG_VERSION STREQUAL "6")
        message("Using Jpeg Lib 62")
        set(jpeg_flag "")
    elseif (JPEG_VERSION STREQUAL "7")
        message("Using Jpeg Lib 70")
        set(jpeg_flag "-DWITH_JPEG7=ON")
    else ()
        message("Using Jpeg Lib 80")
        set(jpeg_flag "-DWITH_JPEG8=ON")
    endif ()
endif()

# The SIMD code needs NASM. The build scripts drop Strawberry's c/bin (which ships nasm.exe) from PATH,
# so look for it explicitly; without it libjpeg-turbo still builds, only slower.
set(_jpeg_nasm "")
if (WIN32)
    find_program(DEP_NASM_EXECUTABLE nasm HINTS "$ENV{ProgramFiles}/NASM" "C:/Strawberry/c/bin")
    if (DEP_NASM_EXECUTABLE)
        set(_jpeg_nasm "-DCMAKE_ASM_NASM_COMPILER=${DEP_NASM_EXECUTABLE}")
    endif ()
endif ()

orcaslicer_add_cmake_project(JPEG
    URL https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/3.2.0.zip
    URL_HASH SHA256=1b4c2cb6cba1ca5e53b9e85eeeab02154cab4b70894c96e97a9c747a26d19d63
    DEPENDS ${ZLIB_PKG}
    CMAKE_ARGS
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
        -DWITH_TOOLS=OFF
        ${_jpeg_nasm}
        ${jpeg_flag}
)

if (MSVC)
    add_debug_dep(dep_JPEG)
endif()