
set(_context_abi_line "")
set(_context_arch_line "")
set(_context_asm_flags "")
set(_boost_zstd_args "")
set(_boost_win_args "")
if (MSVC)
    # Boost.Locale uses the WinAPI backend on Windows and the application uses no bzip2/lzma/zstd filter.
    # Without these switches Boost picks up iconv/bzip2/lzma from whatever happens to be on the system
    # (e.g. Strawberry Perl) and its installed CMake configs then require them.
    set(_boost_win_args
        -DBOOST_LOCALE_ENABLE_ICONV:BOOL=OFF
        -DBOOST_IOSTREAMS_ENABLE_BZIP2:BOOL=OFF
        -DBOOST_IOSTREAMS_ENABLE_LZMA:BOOL=OFF
        -DBOOST_IOSTREAMS_ENABLE_ZSTD:BOOL=OFF)
endif ()
if (APPLE)
    # Boost 1.84 hard-codes zstd::libzstd_shared in Boost.Iostreams.  Our
    # macOS dependency bundle intentionally builds zstd as static-only, so
    # patch that target before configuring Boost and enforce the build order.
    set(_boost_zstd_args
        PATCH_COMMAND ${CMAKE_COMMAND}
            -DBOOST_SOURCE_DIR=<SOURCE_DIR>
            -P ${CMAKE_CURRENT_LIST_DIR}/apply-static-zstd-patch.cmake
        DEPENDS dep_zstd
    )
endif ()

if (APPLE AND CMAKE_OSX_ARCHITECTURES)
    if (CMAKE_OSX_ARCHITECTURES MATCHES "x86")
        set(_context_abi_line "-DBOOST_CONTEXT_ABI:STRING=sysv")
    elseif (CMAKE_OSX_ARCHITECTURES MATCHES "arm")
        set (_context_abi_line "-DBOOST_CONTEXT_ABI:STRING=aapcs")
    endif ()
    set(_context_arch_line "-DBOOST_CONTEXT_ARCHITECTURE:STRING=${CMAKE_OSX_ARCHITECTURES}")
endif ()

if (CMAKE_SYSTEM_PROCESSOR MATCHES "loongarch" AND
    CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Boost.Context's LoongArch assembly uses GNU assembler syntax such as
    # %plt(...), which Clang's integrated assembler does not accept.
    set(_context_asm_flags "-DCMAKE_ASM_FLAGS:STRING=-fno-integrated-as")
endif ()

orcaslicer_add_cmake_project(Boost
    URL "https://github.com/boostorg/boost/releases/download/boost-1.84.0/boost-1.84.0.tar.gz"
    URL_HASH SHA256=4d27e9efed0f6f152dc28db6430b9d3dfb40c0345da7342eaa5a987dde57bd95
    ${_boost_zstd_args}
    LIST_SEPARATOR |
    CMAKE_ARGS
        -DBOOST_EXCLUDE_LIBRARIES:STRING=contract|fiber|numpy|stacktrace|wave|test
        -DBOOST_LOCALE_ENABLE_ICU:BOOL=OFF # do not link to libicu, breaks compatibility between distros
        -DBUILD_TESTING:BOOL=OFF
        ${_boost_win_args}
        "${_context_abi_line}"
        "${_context_arch_line}"
		"${_context_asm_flags}")

if (MSVC)
    add_debug_dep(dep_Boost)
endif()
