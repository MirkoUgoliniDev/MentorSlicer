# Mesa llvmpipe software OpenGL renderer for Windows x64 (MSVC release build by pal1000/mesa-dist-win).
# Only opengl32.dll and libgallium_wgl.dll are needed: the launcher loads <exe_dir>/mesa/opengl32.dll
# when the host OpenGL driver is unavailable or --sw-renderer is passed.
ExternalProject_Add(
    dep_Mesa
    URL https://github.com/pal1000/mesa-dist-win/releases/download/26.2.4/mesa3d-26.2.4-release-msvc.7z
    URL_HASH SHA256=351fc8c8b695878ffb3eaa044b3ead08672a48b1a045e3c3e3975811df0f6695
    DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/Mesa
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
)
ExternalProject_Get_Property(dep_Mesa SOURCE_DIR)
add_custom_command(TARGET dep_Mesa POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${DESTDIR}/bin/mesa
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DESTDIR}/bin/mesa
    COMMAND ${CMAKE_COMMAND} -E copy ${SOURCE_DIR}/x64/opengl32.dll ${SOURCE_DIR}/x64/libgallium_wgl.dll ${DESTDIR}/bin/mesa
)
