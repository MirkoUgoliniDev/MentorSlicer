if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
set(_conf_cmd ./configure)
set(_make_cmd make -j${NPROC})
set(_install_cmd make -j${NPROC} install)
ExternalProject_Add(dep_FFmpeg
    #EXCLUDE_FROM_ALL ON
    URL "https://ffmpeg.org/releases/ffmpeg-4.2.tar.bz2"
    URL_HASH SHA256=306bde5f411e9ee04352d1d3de41bd3de986e42e2af2a4c44052dce1ada26fb8
    DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/FFmpeg
	CONFIGURE_COMMAND ${_conf_cmd}
        "--prefix=${DESTDIR}"
        "--enable-gpl"
        "--enable-libx264"
        "--enable-static"
        "--disable-x86asm"
	    "--disable-doc"
        "--disable-inline-asm"
    BUILD_IN_SOURCE ON
    BUILD_COMMAND ${_make_cmd}
    INSTALL_COMMAND ${_install_cmd}
)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    ExternalProject_Add(dep_x264
        URL "https://code.videolan.org/videolan/x264/-/archive/master/x264-master.tar.bz2"
        DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/x264
        CONFIGURE_COMMAND ./configure --prefix=${DESTDIR} --enable-static --disable-opencl
        BUILD_COMMAND make -j${NPROC}
        INSTALL_COMMAND make install
        BUILD_IN_SOURCE ON
    )
    set(_conf_cmd ./configure)
    set(_make_cmd make -j${NPROC})
    set(_install_cmd make -j${NPROC} install)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
        set(TARGET_ARCH "x86_64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
        set(TARGET_ARCH "arm64")
    elseif (${CMAKE_SYSTEM_PROCESSOR} MATCHES "loongarch|loongarch64")
        set(TARGET_ARCH "loongarch")
    endif()
    list(APPEND _ffmpeg_flags
        "--prefix=${DESTDIR}"
        "--libdir=${DESTDIR}/lib"
        "--enable-gpl"
        "--enable-libx264"
        "--enable-static"
        "--disable-doc"
        "--extra-cflags=-I${DESTDIR}/include"
        "--extra-ldflags=-L${DESTDIR}/lib"
        "--extra-libs=-lx264"
        "--pkg-config-flags=--static"
        
    )
    if(TARGET_ARCH STREQUAL "x86_64")
        list(APPEND _ffmpeg_flags "--enable-x86asm")  # 启用x86汇编
    elseif(TARGET_ARCH STREQUAL "arm64")
        list(APPEND _ffmpeg_flags "--enable-neon")    # 启用ARM NEON优化
    elseif(TARGET_ARCH STREQUAL "loongarch")
        list(APPEND _ffmpeg_flags "--disable-doc") 
    endif()
    ExternalProject_Add(dep_FFmpeg
        URL "https://ffmpeg.org/releases/ffmpeg-4.2.tar.bz2"
        URL_HASH SHA256=306bde5f411e9ee04352d1d3de41bd3de986e42e2af2a4c44052dce1ada26fb8
        DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/FFmpeg
        CONFIGURE_COMMAND ${_conf_cmd}
            ${_ffmpeg_flags}
        BUILD_IN_SOURCE ON
        BUILD_COMMAND ${_make_cmd}
        INSTALL_COMMAND ${_install_cmd}
        DEPENDS dep_x264
    )
endif()
if(WIN32)
# LGPL shared build of FFmpeg 8.1 by BtbN/FFmpeg-Builds. Use the last autobuild of a month: BtbN keeps those for
# two years, while other daily builds are deleted after 14 days. All codec libraries are linked into the DLLs.
set(DOWNLOAD_URL "https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-09-30-13-08/ffmpeg-n8.1.3-9-g29e619e767-win64-lgpl-shared-8.1.zip")
# Only the DLLs the application loads: MentorSlicer_Slicer.dll imports avformat, avcodec and avutil;
# avcodec imports swresample.
set(_ffmpeg_dlls avutil-60.dll avcodec-62.dll avformat-62.dll swresample-6.dll)
ExternalProject_Add(
    dep_FFmpeg
    URL ${DOWNLOAD_URL}
    URL_HASH SHA256=3e47bda1607740550141e37c0e49d1e5182b34699f15adfd137ee266d346811a
    DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/FFmpeg
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
)
ExternalProject_Get_Property(dep_FFmpeg SOURCE_DIR)
set(_ffmpeg_bin_files "")
foreach(_dll ${_ffmpeg_dlls})
    list(APPEND _ffmpeg_bin_files ${SOURCE_DIR}/bin/${_dll})
endforeach()
add_custom_command(TARGET dep_FFmpeg POST_BUILD
    # Drop the headers and DLLs of the previous FFmpeg package before installing the new ones.
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${DESTDIR}/include/libavdevice ${DESTDIR}/include/libavfilter ${DESTDIR}/include/libavformat
            ${DESTDIR}/include/libavutil ${DESTDIR}/include/libswresample ${DESTDIR}/include/libswscale
            ${DESTDIR}/include/libavcodec ${DESTDIR}/bin/ffmpeg
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SOURCE_DIR}/include ${DESTDIR}/include
    COMMAND ${CMAKE_COMMAND} -E copy ${SOURCE_DIR}/lib/avcodec.lib ${SOURCE_DIR}/lib/avdevice.lib ${SOURCE_DIR}/lib/avfilter.lib
            ${SOURCE_DIR}/lib/avformat.lib ${SOURCE_DIR}/lib/avutil.lib ${SOURCE_DIR}/lib/swresample.lib
            ${SOURCE_DIR}/lib/swscale.lib ${DESTDIR}/lib
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DESTDIR}/bin/ffmpeg
    COMMAND ${CMAKE_COMMAND} -E copy ${_ffmpeg_bin_files} ${DESTDIR}/bin/ffmpeg
)
endif()