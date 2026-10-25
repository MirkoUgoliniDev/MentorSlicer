set(patch_command
  git init
  && ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-Static-library-named-zlib-on-Windows.patch
)
if (NOT WIN32)
  list(APPEND patch_command
    && ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0002-Namespace-internal-error-table.patch
  )
endif()

orcaslicer_add_cmake_project(ZLIB
  # GIT_REPOSITORY https://github.com/madler/zlib.git
  # GIT_TAG v1.2.11
  #URL https://github.com/madler/zlib/archive/refs/tags/v1.2.11.zip
  URL https://github.com/madler/zlib/archive/refs/tags/v1.3.2.zip
  #URL_HASH SHA256=f5cc4ab910db99b2bdbba39ebbdc225ffc2aa04b4057bc2817f1b94b6978cfc3
  URL_HASH SHA256=31fd9fee98812abcf147d0e103bc4d2f983c35a8d7a807a328a299f3a74e0050
  PATCH_COMMAND ${patch_command}
  CMAKE_ARGS
    -DZLIB_BUILD_SHARED=OFF          # static zlib.lib only (see patch 0001)
    -DZLIB_BUILD_STATIC=ON
    -DZLIB_BUILD_TESTING=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)
