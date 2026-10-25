include(${CMAKE_SOURCE_DIR}/../cmake/modules/CheckUos.cmake)
if(IS_UOS)
    set(patch_command git init && ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-Rename-bundled-miniz-symbols.patch && ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0002-clang-nontrivial-memcall.patch && ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-uos.patch)
else()
    set(patch_command git init && ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-Rename-bundled-miniz-symbols.patch && ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0002-clang-nontrivial-memcall.patch)
endif()
orcaslicer_add_cmake_project(Assimp
  URL                 https://github.com/assimp/assimp/archive/refs/tags/v6.0.5.zip
  URL_HASH            SHA256=89e477e89df739ecc7dd21874897ac2e70c8944eb1074fbdbbdbf4dd2de6a403
  PATCH_COMMAND ${patch_command}
  DEPENDS ${ZLIB_PKG}
  CMAKE_ARGS
        -DASSIMP_WARNINGS_AS_ERRORS=OFF
        # Use the zlib of the deps prefix instead of the copy bundled with Assimp (1.2.13).
        -DASSIMP_BUILD_ZLIB=OFF
        -DASSIMP_BUILD_TESTS=OFF
        -DASSIMP_BUILD_ASSIMP_TOOLS=OFF
)

if (MSVC)
    add_debug_dep(dep_Assimp)
endif ()
